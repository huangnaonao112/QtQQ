#include "ChatTransport.h"    // 通信层声明
#include "BotConfig.h"        // botId/trigger/groupIds/tcpPort
#include <QJsonDocument>      // JSON 序列化/反序列化
#include <QJsonObject>        // 单条消息对象
#include <QJsonArray>         // msg 内嵌块数组

// 构造只拷贝配置，不碰网络——所有连接动作推迟到 start()
//（main 的装配顺序：全部组件建好、接完线，最后才 start）
/**
 * @brief 构造：仅拷贝 cfg，不发起任何网络动作
 * @param cfg 配置（按值）
 * @param parent Qt 父对象
 */
ChatTransport::ChatTransport(const BotConfig& cfg, QObject* parent)
    : QObject(parent), m_cfg(cfg)                  // 拷贝配置并挂父对象
{
}

// QTcpSocket 是成员对象，随本对象析构自动断开并释放
/**
 * @brief 析构：无手动资源；成员 QTcpSocket 随析构断开
 */
ChatTransport::~ChatTransport()
{
}

// start — 把收发链路一次拉起来：信号接线 → hello 自报账号 → 首次连接
// → 每 5 秒心跳重连。
//
// 为什么必须有重连心跳：机器人平时不主动发言，TCP 断了（服务端重启、
// 网络闪断）没有任何事件会触发重试——不加定时器的话机器人会永久失联，
// 而群里没人知道它已经聋了（见函数体内定时器注释）
/**
 * @brief 接线 readyRead/stateChanged 与 hello；首连 + 每 5s 检查未连接则重连
 */
void ChatTransport::start()
{
    // 收与发共用同一条 TCP 长连接：服务端按行拆包，再逐客户端单播回来
    connect(&m_tcp, &QTcpSocket::readyRead,
            this, &ChatTransport::onTcpReadyRead);  // 收包 → 拆行路由
    connect(&m_tcp, &QTcpSocket::stateChanged,
            this, &ChatTransport::onTcpStateChanged);  // 状态日志

    // 连上先自报账号：服务端按 hello 登记"id → 连接"，
    // 发给本账号的私聊消息才能点对点送到这条连接上。
    // role=bot 让服务端跳过"离线私聊补发"——那是给终端用户的功能，
    // 否则机器人每次重连都会被重新投喂一堆旧问题。
    connect(&m_tcp, &QTcpSocket::connected, this, [this]() {
        QJsonObject hello;                          // 自报对象
        hello.insert("what", "hello");              // 协议类型 = hello
        hello.insert("id", m_cfg.botId);            // 本机器人账号
        hello.insert("role", "bot");                // 标记机器人：跳过补发
        m_tcp.write(QJsonDocument(hello).toJson(QJsonDocument::Compact));  // 紧凑 JSON
        m_tcp.write(QByteArray(1, '\n'));           // NDJSON 尾 LF
        m_tcp.flush();                              // 立刻推送到网卡
    });

    ensureTcpConnected();                           // 立即尝试首连

    // 心跳重连：机器人不主动发言时没有重连时机，一旦 TCP 断开
    // （服务端重启等）将永远收不到消息——每 5 秒检查一次自动恢复
    m_reconnectTimer = new QTimer(this);            // 挂 this 释放
    m_reconnectTimer->setInterval(5000);            // 5s 心跳间隔
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (m_tcp.state() == QAbstractSocket::UnconnectedState)
        {
            qInfo() << "[transport] 重连服务端...";  // 仅断开时重连
            m_tcp.connectToHost("127.0.0.1", static_cast<quint16>(m_cfg.tcpPort));  // 服务端仅本机
        }
    });
    m_reconnectTimer->start();                      // 启动心跳

    qInfo() << "[transport] 就绪 tcp=" << m_cfg.tcpPort;  // 启动完成日志
}

// 发送前自愈：服务端重启的窗口期里连接是断的，这里补一次连接再发，
// 机器人不需要人工重启。已连接时直接返回，不打断现有连接。
/**
 * @brief 发送前自愈：已连接直接返回；否则 abort 后重连并最多等 2s
 */
void ChatTransport::ensureTcpConnected()
{
    if (m_tcp.state() == QAbstractSocket::ConnectedState)
        return;                                     // 已连上：不打断

    m_tcp.abort();                                  // 清掉半开/残状态
    m_tcp.connectToHost("127.0.0.1", static_cast<quint16>(m_cfg.tcpPort));  // 重连本机服务端
    if (!m_tcp.waitForConnected(2000))
        qWarning() << "[transport] TCP 连接失败（服务端未启动？）";  // 同步等 2s 失败则告警
}

// onTcpStateChanged — 只打连接状态日志；恢复由 m_reconnectTimer 兜底，
// 这里不做任何业务动作（避免在信号回调里同步重入 connectToHost）
/**
 * @brief 状态变化只记日志；重连决策完全交给心跳定时器
 * @param state 新状态
 */
void ChatTransport::onTcpStateChanged(QAbstractSocket::SocketState state)
{
    if (state == QAbstractSocket::ConnectedState)
        qInfo() << "[transport] TCP 已连接";        // 连上日志
    else if (state == QAbstractSocket::UnconnectedState)
        qInfo() << "[transport] TCP 已断开";        // 断开日志（等心跳恢复）
}

// ----------------------------------------------------------------------------
// 收包解析。协议字段与客户端一致：
//   {"what":"msg","send":"10001","recv":"2000","type":0,
//    "msg":[[{"txt":"..."},...],[...]]}   ← 内层数组为一条消息的文本/表情块
//
// 分流规则：
//   type=0 且 recv 在监听群列表里 → 群聊消息，回应目标是那个群
//   type=1 且 recv=botId          → 私聊消息，回应目标是发送者本人
// 其他（别的群、别人的单聊）直接忽略。表情块拼接为 [表情] 占位不丢信息。
// ----------------------------------------------------------------------------
/**
 * @brief 路由一条 msg：过滤非本机消息；群聊须在 groupIds，私聊须 recv=botId
 * @param line 一行 JSON
 */
void ChatTransport::processMessage(const QByteArray& line)
{
    const QJsonObject root = QJsonDocument::fromJson(line).object();  // 解析一行 NDJSON
    if (root.value("what").toString() != "msg")     // 本机器人只处理普通消息
        return;

    //type 在协议里是数字（0=群聊 1=单聊），必须用 toInt()；
    //toString() 对数字型 JSON 值返回空串，会让所有消息被误判
    const int typeNum    = root.value("type").toInt();  // 0=群 1=私（必须 toInt）
    const QString recv   = root.value("recv").toString();  // 群号或收件人
    const QString sender = root.value("send").toString();  // 发送者账号
    if (sender == m_cfg.botId)                      // 防自激：忽略自己
        return;

    // 提取纯文本
    QString text;                                   // 累加纯文本
    for (const auto& blockArr : root.value("msg").toArray())  // 外层：消息分段
    {
        for (const auto& block : blockArr.toArray())  // 内层：一段里的块
        {
            const QJsonObject o = block.toObject(); // 单块对象
            if (o.contains("txt"))
                text += o.value("txt").toString();  // 文本块直接拼
            else if (o.contains("img"))
                text += "[表情]";                   // 图片/表情折成占位
        }
    }

    if (text.isEmpty())
        return;                                     // 空文本丢弃

    if (typeNum == 0 && m_cfg.groupIds.contains(recv))
    {
        qDebug() << "[transport] 群消息，来自" << sender;  // 群消息日志
        emit messageReceived(ReplyTarget(recv, /*isGroup*/ true), sender, text);  // 回复目标=群号
    }
    else if (typeNum == 1 && recv == m_cfg.botId)
    {
        qDebug() << "[transport] 私聊消息，来自" << sender;   // 只打来源，不打内容
        emit messageReceived(ReplyTarget(sender, /*isGroup*/ false), sender, text);  // 回复目标=发送者
    }
}

// onTcpReadyRead — NDJSON（每行一条 JSON）缓冲拆行后逐条路由。
// TCP 是字节流没有消息边界，一次 readyRead 可能拿到半行或多行，必须自己缓冲。
/**
 * @brief 读尽 TCP 字节 → 缓冲到 m_tcpBuf → 按 LF 拆行逐条 processMessage
 */
void ChatTransport::onTcpReadyRead()
{
    const QByteArray arrived = m_tcp.readAll();     // 一次读尽当前缓冲
    // 只打字节数：内容里可能有别人的私聊，控制台/投屏不该泄露
    qDebug() << "[transport] TCP 收到" << arrived.size() << "字节";  // 隐私：不打内容
    m_tcpBuf += arrived;                            // 拼进行缓冲
    int nl = -1;                                    // 最近 LF 下标
    while ((nl = m_tcpBuf.indexOf(char(0x0A))) != -1)
    {
        const QByteArray line = m_tcpBuf.left(nl);  // 取出一行（不含 LF）
        m_tcpBuf.remove(0, nl + 1);                 // 消费掉该行及 LF
        if (!line.trimmed().isEmpty())
            processMessage(line.trimmed());         // 非空行才路由
    }
}

// 出站统一转义：聊天页是网页渲染，< > & 会破坏页面结构，甚至被当成标签执行。
// 换行不压平——气泡 CSS 是 white-space: pre-wrap，\n 本来就能正常换行。
/**
 * @brief 出站 HTML 转义（& 先换，再 < > "）；\n 保留
 * @param text 原文
 * @return 转义后可嵌网页的文本
 */
QString ChatTransport::escapeHtml(const QString& text)
{
    QString r = text;                               // 工作副本
    r.replace('&',  "&amp;");                       // & 必须最先替换
    r.replace('<',  "&lt;");                        // 防标签注入
    r.replace('>',  "&gt;");                        // 闭合标签破坏
    r.replace('\"', "&quot;");                      // 属性上下文安全
    return r;
}

// sendToServer — 所有出站消息的唯一出口：发送前自愈补连 → NDJSON 封帧 → 写入。
// 连不上只打警告不抛错：机器人丢一条消息可接受，处理流程不能崩——
// 5 秒后的重连心跳会把链路修好，下一条自然送达
/**
 * @brief 唯一 TCP 出口：自愈 → 未连接则丢弃并告警；否则 Compact JSON + LF 写出
 * @param msgObj 出站 JSON
 */
void ChatTransport::sendToServer(const QJsonObject& msgObj)
{
    ensureTcpConnected();                           // 发送前补连
    if (m_tcp.state() != QAbstractSocket::ConnectedState)
    {
        qWarning() << "[transport] 发送失败：TCP 未连接";  // 丢包告警，不崩
        return;
    }
    //NDJSON：JSON 后跟 LF 分隔符（服务端按行拆分；两条 write 之间可能被
    //TCP 分段，但服务端按 LF 切分所以顺序安全）
    const QByteArray out = QJsonDocument(msgObj).toJson(QJsonDocument::Compact);  // 紧凑无换行
    m_tcp.write(out);                               // 写 JSON 本体
    m_tcp.write(QByteArray(1, char(0x0A)));         // 行尾 LF
    m_tcp.flush();                                  // 立刻推送
}

// 完整消息（群/私通用）：协议 type 由 target.isGroup 决定，
// 不再从 recv 反推——回复目标只有一个来源。
/**
 * @brief 普通 msg：一块 HTML 转义后的 txt；type 按 isGroup 映射 0/1
 * @param target 回复地址
 * @param text 纯文本
 */
void ChatTransport::sendText(const ReplyTarget& target, const QString& text)
{
    QJsonArray blocks;                              // 一块 txt
    blocks.append(QJsonArray{ QJsonObject{ {"txt", escapeHtml(text)} } });  // 转义后入块

    QJsonObject msgObj;                             // 完整 msg 对象
    msgObj.insert("what", "msg");                   // 协议类型
    msgObj.insert("send", m_cfg.botId);             // 发送者=本机器人
    msgObj.insert("recv", target.recv);             // 群号或对方账号
    msgObj.insert("type", target.isGroup ? 0 : 1);  // 0=群 1=私
    msgObj.insert("msg", QJsonArray{ blocks });     // 消息分段结构
    sendToServer(msgObj);                           // 经统一出口发送
}

// 流式增量（打字机效果）。delta 同样要转义：客户端用 jQuery.append 渲染，
// 内容是按 HTML 解析的，模型输出里一个 "<" 就能把气泡结构弄坏。
//
// done 那一包额外带 full（本次回答的完整文本）：流式回答的正文分散在几十个
// 增量里，服务端要落库（历史消息）就得有一份完整的，否则只能存到最后一两个字的碎片。
/**
 * @brief msg_stream：delta 转义写入；done 可附 full 供服务端落库
 * @param target 回复地址
 * @param delta 本批增量
 * @param done 结束标记
 * @param full 可选：完整回答（仅 done 包建议携带）
 */
void ChatTransport::sendStreamDelta(const ReplyTarget& target, const QString& delta,
                                    bool done, const QString& full)
{
    QJsonObject streamObj;                          // msg_stream 对象
    streamObj.insert("what",  "msg_stream");        // 协议类型
    streamObj.insert("send",  m_cfg.botId);         // 发送者
    streamObj.insert("recv",  target.recv);         // 接收会话
    streamObj.insert("type",  target.isGroup ? 0 : 1);  // 0=群 1=私
    streamObj.insert("delta", escapeHtml(delta));   // 增量也要 HTML 转义
    streamObj.insert("done",  done);                // 是否结束
    if (!full.isEmpty())
        streamObj.insert("full", escapeHtml(full)); // 结束包附全文供落库
    sendToServer(streamObj);                        // 统一出口发送
}
