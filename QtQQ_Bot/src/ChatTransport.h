#pragma once

#include <QObject>
#include <QTimer>
#include <QTcpSocket>
#include <QString>

#include "BotConfig.h"
#include "ReplyTarget.h"

// ============================================================================
// ChatTransport — 通信层：机器人与 IM 世界的全部网络交互
//
// 收：与服务端保持一条 TCP 长连接，服务端把每条消息逐连接单播回来（NDJSON，
//     一行一条 JSON）。解析后只发一个信号"我收到一条发给我的消息"，
//     并把回复地址（ReplyTarget）一并算好，协议细节不外泄。
// 发：同一条 TCP 连接上写 NDJSON：
//     sendText(target, text)              一次性完整消息（普通 msg 协议）
//     sendStreamDelta(target, delta, done) 流式增量（msg_stream 协议，打字机效果）
//
// 只保留 TCP 一条通道：早期的 UDP 广播已被服务端移除（同机多 socket 会互相
// 抢包丢消息），继续 bind 一个永远收不到数据的端口只会多一个启动失败点。
//
// HTML 转义统一在这里做（收发两端都是网页模板渲染），业务层只处理纯文本。
// ============================================================================
/**
 * @brief 通信层：一条 TCP 上收 NDJSON 消息、发 msg / msg_stream；统一 HTML 转义与重连
 */
class ChatTransport : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造：只拷贝配置、不碰网络（连接推迟到 start()）
     * @param cfg 配置（按值拷贝，本对象独立于配置生命周期）
     * @param parent Qt 父对象
     */
    explicit ChatTransport(const BotConfig& cfg, QObject* parent = nullptr);
    /**
     * @brief 析构：QTcpSocket 为成员对象，随析构自动断开
     */
    ~ChatTransport() override;

    /**
     * @brief 拉起收发链路：接线 → hello 自报 → 首连 → 5s 心跳重连
     */
    void start();                                     // 连接服务端并开始接收

signals:
    // 一条发给机器人的文本消息。target 已经算好，业务层不用再猜：
    //   群聊 → {群号, true}（已按配置的 groupIds 过滤，其他群不会冒出来）
    //   私聊 → {对方账号, false}（recv=botId 的消息，回应对象是发送者）
    /**
     * @brief 一条发给本机器人的文本消息（群聊/私聊；target 已按协议算好）
     * @param target 回复地址（recv + isGroup）
     * @param sender 发送者账号
     * @param text 纯文本（表情已折成占位）
     */
    void messageReceived(const ReplyTarget& target, const QString& sender, const QString& text);

public:
    /**
     * @brief 发送完整消息（协议 type 由 target.isGroup 决定）
     * @param target 回复地址
     * @param text 纯文本（内部做 HTML 转义）
     */
    // 完整消息（群/私通用，target.isGroup 决定协议 type）
    void sendText(const ReplyTarget& target, const QString& text);
    /**
     * @brief 发送流式增量；done=true 表示回答结束，可带 full 供服务端落库
     * @param target 回复地址
     * @param delta 本批增量（内部转义）
     * @param done 是否结束标记
     * @param full 结束包可选：本次回答完整文本
     */
    // 流式增量（群/私通用）；done=true 表示本次回答结束，
    // 此时可以带 full（完整文本），服务端据此落库一份完整回答
    void sendStreamDelta(const ReplyTarget& target, const QString& delta, bool done,
                         const QString& full = QString());

private slots:
    /**
     * @brief readyRead：NDJSON 缓冲拆行后逐条 processMessage
     */
    void onTcpReadyRead();                                  // NDJSON 缓冲拆行（见 .cpp）
    /**
     * @brief 仅打连接/断开日志；重连由 m_reconnectTimer 兜底
     * @param state 新的 socket 状态
     */
    void onTcpStateChanged(QAbstractSocket::SocketState state);   // 仅打状态日志

private:
    /**
     * @brief 一条 NDJSON 的路由：只认 what=msg；群聊须在监听列表，私聊须 recv=botId
     * @param line 一行 JSON 字节
     */
    void processMessage(const QByteArray& line);      // 一条 JSON 消息的路由
    /**
     * @brief 发送前自愈：未连接则 abort+connectToHost（2s 等待），已连接则不动
     */
    void ensureTcpConnected();                       // 发送前自愈：断线补连，已连接不打断
    /**
     * @brief 底层出口：自愈 → JSON 紧凑序列化 + LF 封帧写入 TCP
     * @param msgObj 要发送的 JSON 对象
     */
    void sendToServer(const QJsonObject& msgObj);     // 底层：JSON → TCP
    /**
     * @brief 出站 HTML 转义（& " < >）；换行保留（气泡 pre-wrap）
     * @param text 原文
     * @return 可安全嵌入网页模板的文本
     */
    static QString escapeHtml(const QString& text);   // 出站文本 HTML 转义

private:
    BotConfig    m_cfg;                    // 配置按值持有（不依赖外部对象寿命）
    QTcpSocket   m_tcp;                    // 与服务端的唯一 TCP 连接
    QByteArray   m_tcpBuf;         // TCP 接收缓冲（NDJSON 拆行用）
    QTimer*      m_reconnectTimer = nullptr;  // 心跳重连（服务端重启/闪断后自动恢复）
};
