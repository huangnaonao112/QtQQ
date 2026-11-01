#include "LlmClient.h"        // 客户端声明
#include <QNetworkAccessManager>  // HTTP 发送
#include <QNetworkRequest>     // 请求头
#include <QNetworkReply>       // 在途 reply
#include <QTimer>              // 超时保护
#include <QJsonDocument>       // body 序列化 / 响应解析
#include <QJsonObject>         // body / delta 对象

// 构造：QNetworkAccessManager 挂在自己名下，随对象析构统一回收所有 reply
/**
 * @brief 构造：创建并父子化 QNetworkAccessManager
 * @param parent Qt 父对象
 */
LlmClient::LlmClient(QObject* parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))  // nam 子对象统一回收
{
}

// setApi — 接口三元组（地址/key/模型），启动前调用一次；
// 对在途请求不生效——main 的装配顺序保证先 setApi 再发请求
/**
 * @brief 设置 url/key/model；仅对后续请求生效
 * @param url 接口地址
 * @param key API key
 * @param model 模型名
 */
void LlmClient::setApi(const QString& url, const QString& key, const QString& model)
{
    m_url = url;                                    // 接口地址
    m_key = key;                                    // Bearer 用 key
    m_model = model;                                // 请求体 model 字段
}

// setLimits — 生成上限与超时（都可配在 bot.ini）。
// timeoutMs<=0 时保留当前值（默认 60 秒）：不接受"关掉超时"——
// 没有超时兜底的在途请求会把 AgentService 的 m_busy 永久卡死，
// 整个机器人从此不再应答
/**
 * @brief 设置 maxTokens 与 timeoutMs；timeoutMs<=0 不允许关闭超时
 * @param maxTokens 生成上限（<=0 不限制）
 * @param timeoutMs 超时毫秒；<=0 保留旧值
 */
void LlmClient::setLimits(int maxTokens, int timeoutMs)
{
    m_maxTokens = maxTokens;                        // 生成上限原样保存
    if (timeoutMs > 0)
        m_timeoutMs = timeoutMs;                    // 非法/关超时值不采纳
}

// chat — 发起一次请求，接管它的整个生命周期；无论成败都恰好回调一次 onFinished
//
// 两个前置校验（已有在途请求 / 流式缺 onDelta）也走 onFinished 同步报错，
// 调用方不必区分"立刻失败"和"网络失败"，AgentService 的收尾只写一处。
// 超时语义（关键，别写反）：
//   流式    = 空闲超时：每次 readyRead 把计时器推后，只要还在出字就不判死，
//             长回答不会被总时长掐断；
//   非流式  = 总时长：没有增量可等，从发出到 finished 整体计时（默认 60 秒）。
// 具体实现与历史教训见函数体内超时段注释
/**
 * @brief 发起一次 chat 请求；双前置校验失败也同步 onFinished；超时见上方语义
 * @param messages OpenAI 消息数组
 * @param tools 工具 schema 数组（可空）
 * @param stream 是否 SSE 流式
 * @param onDelta 流式增量（stream 时必给）
 * @param onFinished 结束回调（恰好一次）
 */
void LlmClient::chat(const QJsonArray& messages,
                     const QJsonArray& tools,
                     bool stream,
                     DeltaFn onDelta,
                     FinishedFn onFinished)
{
    if (m_reply)     // 上一个请求未结束（理论上有 m_busy 挡住，双保险）
    {
        onFinished(QString(), QJsonArray(), "上一个 LLM 请求尚未完成");  // 同步报错
        return;
    }
    if (stream && !onDelta)     // 流式必须有增量回调，否则收不到任何内容
    {
        onFinished(QString(), QJsonArray(), "流式请求缺少增量回调");  // 同步报错
        return;
    }

    m_deltaFn = onDelta;                            // 存增量回调
    m_finishedFn = onFinished;                      // 存结束回调
    m_isStream = stream;                            // 记形态（收尾分支用）
    m_sseBuf.clear();                               // 清 SSE 缓冲
    m_streamContent.clear();                        // 清累计文本
    m_streamDoneSent = false;                       // 允许首次收尾

    QJsonObject body;                               // 请求 JSON 体
    body.insert("model", m_model);                  // 模型名
    body.insert("messages", messages);              // 对话消息
    if (!tools.isEmpty())
        body.insert("tools", tools);                // 有工具才带 tools
    body.insert("stream", stream);                  // 是否流式
    if (m_maxTokens > 0)                       // 限制生成长度：既省钱，也给响应时间兜底
        body.insert("max_tokens", m_maxTokens);     // 生成 token 上限

    QNetworkRequest req{ QUrl(m_url) };             // 请求对象
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");  // JSON content-type
    req.setRawHeader("Authorization", "Bearer " + m_key.toUtf8());  // OpenAI 兼容接口要求 Bearer 前缀

    qInfo() << "[llm] 请求" << m_url << "stream=" << stream  // 观察入口参数
            << "messages=" << messages.size()
            << "tools=" << tools.size()
            << "bytes=" << QJsonDocument(body).toJson(QJsonDocument::Compact).size();  // 便于排查超大 body

    QNetworkReply* reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));  // POST 出站
    m_reply = reply;                                // 登记在途请求

    // 超时保护：Qt 5.14 的 QNetworkReply 没有内建超时，请求挂起会让整个
    // 智能体流程卡死。两种形态判死方式不同：
    //   - 非流式（工具轮）：没有增量可等，按请求总时长判死，窗口放宽到
    //     m_timeoutMs（默认 60 秒）——长回答（比如解一道 LeetCode）要跑
    //     三五十秒很常见，早前 30 秒的绝对超时正是不够用；
    //   - 流式：模型边生成边发，只要还有增量进来就不该被判死，所以每次
    //     readyRead 都把计时器推后（空闲超时）。否则一个 60 秒的长回答会被"总时长"
    //     掐断——这正是 30 秒绝对超时的毛病。
    const int timeoutMs = m_timeoutMs;              // 快照本次窗口（lambda 按值捕）
    m_elapsed.start();                              // 起表，结束时打耗时

    QTimer* timer = new QTimer(reply);              // 子到 reply，reply 删则删
    timer->setSingleShot(true);                     // 只触发一次
    timer->start(timeoutMs);                        // 起动超时窗
    connect(timer, &QTimer::timeout, this, [reply, timeoutMs]() {
        if (reply->isRunning())
        {
            qWarning() << "[llm] 连续" << (timeoutMs / 1000) << "秒没有新数据，中止";  // 记超时原因
            reply->abort();                         // 中止 → 走 finished 分支报错
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_reply = nullptr;                          // 解除在途占用
        reply->deleteLater();                       // 延迟释放 reply

        qInfo() << "[llm] finished err=" << reply->error() << reply->errorString().left(60)  // 结束诊断
                << "耗时" << m_elapsed.elapsed() << "ms";

        // 调用前先拷贝一份回调：回调里很可能会立刻发起下一次 chat()（工具轮 → 回答轮），
        // 那会把 m_finishedFn 重新赋值。若直接调 m_finishedFn，
        // 正在执行的那个 std::function 会被析构，它的捕获（比如 messages）随之失效，
        // 后续再读就是悬空内存——自检脚本当年就是崩在这里。
        const FinishedFn done = m_finishedFn;       // 先拷贝再调用（防自毁）

        if (reply->error() != QNetworkReply::NoError)
        {
            // 超时给用户的话要说人话："Operation canceled" 这种内部术语没必要外露
            QString msg = reply->errorString();     // 默认用 Qt 原文
            if (reply->error() == QNetworkReply::OperationCanceledError
                || reply->error() == QNetworkReply::TimeoutError)
            {
                msg = QStringLiteral("大模型响应超时（%1 秒没等到新内容），请稍后重试")
                          .arg(qMax(1, m_timeoutMs / 1000));   // 1ms 也不该显示成 0 秒
            }
            done(QString(), QJsonArray(), msg);     // 统一失败回调
            return;
        }

        if (m_isStream)     // 流式：内容已经在增量回调里发过了
        {
            finishStream();                         // 只交全文给 onFinished
            return;
        }

        // 非流式：一次性解析 content / tool_calls
        const QJsonObject msg = QJsonDocument::fromJson(reply->readAll())
                                    .object().value("choices").toArray()
                                    .at(0).toObject().value("message").toObject();  // choices[0].message
        if (msg.isEmpty())     // 200 但不是预期结构（例如网关返回的错误 JSON）
        {
            done(QString(), QJsonArray(), "响应格式异常（缺少 choices.message）");  // 结构校验失败
            return;
        }
        done(msg.value("content").toString(),
             msg.value("tool_calls").toArray(),
             QString());                            // 正常结束：content + tool_calls
    });

    if (stream)
    {
        // 每收到一段就把计时器推后：流式回答以"空闲多久"判断超时，
        // 长回答不会被总时长掐断
        connect(reply, &QNetworkReply::readyRead, this, [this, reply, timer, timeoutMs]() {
            timer->start(timeoutMs);                // 每次有数据就重置空闲窗
            onStreamData(reply->readAll());         // 解析本批 SSE
        });
    }
}

// ----------------------------------------------------------------------------
// SSE 解析。OpenAI 兼容格式：
//   data: {"choices":[{"delta":{"content":"增量文本"}}]}
//   data: [DONE]
// 注意：一次 readyRead 可能包含半行（TCP 分段），m_sseBuf 负责跨包缓冲。
// ----------------------------------------------------------------------------
/**
 * @brief 把新字节追加进 m_sseBuf，按 LF 拆出 data: 行解析 delta / [DONE]
 * @param data 本批网络字节
 */
void LlmClient::onStreamData(const QByteArray& data)
{
    m_sseBuf += data;                               // 跨包缓冲
    int nl = -1;                                    // 最近 LF
    while ((nl = m_sseBuf.indexOf('\n')) != -1)
    {
        const QByteArray lineBytes = m_sseBuf.left(nl);  // 取一行
        m_sseBuf.remove(0, nl + 1);                 // 消费该行

        const QString line = QString::fromUtf8(lineBytes).trimmed();  // UTF-8 解码
        if (line.isEmpty() || !line.startsWith("data:"))
            continue;                               // 忽略非 data 行

        const QString payload = line.mid(5).trimmed();  // 去掉 "data:" 前缀
        if (payload == "[DONE]")
        {
            finishStream();                         // 服务端流结束标记
            continue;
        }

        const QJsonObject delta = QJsonDocument::fromJson(payload.toUtf8())
                                      .object().value("choices").toArray()
                                      .at(0).toObject().value("delta").toObject();  // choices[0].delta

        const QString piece = delta.value("content").toString();  // 增量文本
        if (!piece.isEmpty())
        {
            m_streamContent += piece;               // 累计全文
            m_deltaFn(piece);                       // 回调增量
        }
    }
}

// finishStream — 流式收尾：把累计的全文一次性交给 onFinished。
// [DONE] 行与 reply->finished 两个入口都可能触发，m_streamDoneSent
// 保证只收尾一次（onFinished 的"恰好一次"契约）
/**
 * @brief 流式收尾：m_streamDoneSent 防重入；先拷贝回调再交全文
 */
void LlmClient::finishStream()
{
    if (m_streamDoneSent)     // [DONE] 与 finished 可能都到，只收尾一次
        return;
    m_streamDoneSent = true;                        // 置位防重入

    // 同 finished 分支：先拷贝再调用，回调里若立刻发起下一次请求也不会把它析构掉
    const FinishedFn done = m_finishedFn;           // 先拷贝再调用（防自毁）
    done(m_streamContent, QJsonArray(), QString()); // 全文 + 空 toolCalls + 无错
}
