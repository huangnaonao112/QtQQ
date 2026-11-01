#pragma once

#include <QTcpServer>     // 基类：非阻塞 accept
#include <QTcpSocket>     // 每个客户端连接对应一个 QTcpSocket
#include <QList>          // 在线连接列表
#include <QHash>          // 连接→账号 / 连接→半包缓冲
#include <QString>        // 账号字符串
#include <QJsonObject>    // 解析后的 JSON 报文对象

// ============================================================================
// TcpServer — 服务端的连接管理与消息投递
//
// 职责：
//   1) 监听端口，接受客户端连接（每个连接一个 QTcpSocket）；
//   2) 按 NDJSON 拆行，处理三种报文：
//        {"what":"hello","id":"10001"}        连接登记：这条 socket 是谁
//        {"what":"history","with":"10002"}    查某会话最近历史，只回请求方
//        {"what":"msg"/"file"/"msg_stream",...} 业务消息
//   3) 投递：
//        群聊(type=0)  → 广播给所有在线连接（客户端自己过滤掉自己发的）
//        私聊(type=1)  → 只发给 recv 账号名下的所有连接（同一账号可能双开）
//        收件人不在线    → 不投递：消息已落库，等他上线由 deliverPending 补发
//
// 为什么需要 hello：服务端原本是"收一行广播给所有人"，私聊也广播出去，
// 任何抓包的人都能看到全部私聊。要知道"哪个 socket 是谁"，客户端连上后
// 必须先自报账号，服务端才能点对点投递。
//
// 为什么继承 QTcpServer 而不是自己 while(accept())：
//   QTcpServer 是事件驱动的非阻塞模型，一个线程就能服务成百上千连接，
//   没有 accept 阻塞、也没有每连接一线程的开销。
// ============================================================================

/**
 * @brief 服务端 TCP 连接管理与消息投递：hello 登记、history 回历史、业务消息先落库再按 type 投递
 *
 * 为什么需要 hello：服务端原本"收一行广播给所有人"，私聊也广播出去，
 * 任何抓包的人都能看到全部私聊。要知道"哪个 socket 是谁"，客户端连上后
 * 必须先自报账号，服务端才能点对点投递。
 * 为什么继承 QTcpServer 而不是自己 while(accept())：QTcpServer 是事件驱动的
 * 非阻塞模型，一个线程就能服务成百上千连接，没有 accept 阻塞、也没有每连接一线程的开销。
 */
class TcpServer : public QTcpServer
{
	Q_OBJECT

public:
	/**
	 * @brief 构造：只记端口，不监听（监听留给 run()——构造没法返回错误）
	 * @param tcpPort 监听端口，来自 server.ini 的 AppConfig
	 * @param parent 父对象指针，缺省 nullptr
	 */
	TcpServer(int tcpPort, QObject *parent = nullptr);
	/**
	 * @brief 析构：无手动释放（在线 socket 的释放挂在各自的 disconnected → deleteLater 上）
	 */
	~TcpServer();

	/**
	 * @brief 启动服务器：开始监听 AnyIPv4:tcpPort
	 *
	 * AnyIPv4 = 绑定本机所有 IPv4 网卡（0.0.0.0），局域网里其他机器才连得进来；
	 * 只绑 127.0.0.1 的话别的机器连不上。listen 是非阻塞的：让内核开始监听后立即返回。
	 * @return bool 监听成功返回 true，失败（端口被占用等）返回 false
	 */
	bool run();

private slots:
	/**
	 * @brief [槽] 某个客户端的 socket 变为可读：按行拆开逐条处理
	 *
	 * 必须自己缓冲：TCP 是字节流没有消息边界，一条大消息（比如几 MB 的 base64
	 * 文件）一定会被拆成多个报文。原来直接 split('\n') 处理"本次读到的东西"，
	 * 分片会被当成好几条独立消息转发出去，接收端全是坏 JSON。
	 */
	void readSocket();

protected:
	/**
	 * @brief 有新客户端连接时被 Qt 自动调用：包装 socket、挂信号槽、登记在线表
	 *
	 * 断开时同步从 m_clients/m_ids/m_buf 摘掉（deleteLater 只是把释放排进事件队列，
	 * 本 lambda 在同一次断开信号里同步跑完，保证对象真正析构前先从各表里摘掉，
	 * 否则 broadcast 可能写到已释放的 socket）。
	 * @param socketDescriptor 内核 accept 得到的原生 socket 句柄
	 */
	void incomingConnection(qintptr socketDescriptor) override;

private:
	/**
	 * @brief 处理一条完整报文：hello 登记，history 查历史，其余落库 + 投递
	 *
	 * hello：登记"这条连接是哪个账号"，顺手补发离线私聊（bot 角色除外），不转发。
	 * history：查会话历史，只回给请求方。
	 * 其他：先落库，再按 type 投递（群聊广播 / 私聊点对点）。
	 * @param from 报文来源连接（hello 用它登记归属）
	 * @param line 一条完整 NDJSON 报文（不含换行）
	 */
	void handleLine(QTcpSocket* from, const QByteArray& line);
	/**
	 * @brief 业务消息落库（msg / file / msg_stream 的最终全文）
	 *
	 * msg：拼接文本块（txt 原样，img 记成 [表情]）；
	 * file：只记文件名，不存内容（内容可能是几 MB 的 base64）；
	 * msg_stream：只在 done 时记一条（优先 full 全文，没有则退回本次 delta）。
	 * @param obj 解析后的 JSON 对象（含 what/type/send/recv/msg/file 等字段）
	 */
	void persist(const QJsonObject& obj);
	/**
	 * @brief 给刚上线的账号补发他不在线期间收到的私聊
	 *
	 * 从 pendingPrivateMessages 取最多 100 条，逐条合成 msg 报文发给 to，
	 * 最后 markPrivateDelivered 把 delivered 全量置 1。
	 * @param to 上线者当前的 socket 连接
	 * @param account 上线者的账号
	 */
	void deliverPending(QTcpSocket* to, const QString& account);
	/**
	 * @brief 应答客户端的 history 请求：只回请求方，不落库不转发
	 *
	 * limit 不传默认 50，qBound 夹到 [1, 200]（防一次拉爆内存）；
	 * isGroup 与客户端一致：with 长度 ≤ 4 视为群号。
	 * @param to 请求方连接
	 * @param req 含 with（会话对象/群号）和可选 limit 的 JSON 请求
	 */
	void sendHistory(QTcpSocket* to, const QJsonObject& req);
	/**
	 * @brief 向单个连接发送一行（带背压保护）
	 *
	 * 连接为空或已断开直接丢弃；积压超过 1MB 跳过本条（聊天消息可以丢，
	 * 服务端不能被拖垮）。
	 * @param c 目标连接（可为 nullptr）
	 * @param packet 完整报文（调用方负责带换行）
	 */
	void sendTo(QTcpSocket* c, const QByteArray& packet);
	/**
	 * @brief 广播给所有在线连接（含发送者自己，客户端自行过滤）
	 *
	 * 每条连接的积压保护由 sendTo 单独把关，慢客户端不会拖垮广播。
	 * @param packet 完整报文（调用方负责带换行）
	 */
	void broadcast(const QByteArray& packet);
	/**
	 * @brief 按账号反查它名下的全部连接（同一账号可双开）
	 *
	 * 只有 hello 登记过的连接才查得到：连上还没自报账号、或 id 拼错，
	 * 都返回空列表——调用方（私聊投递）据此走"不在线，等补发"分支。
	 * @param account 要找的账号
	 * @return QList<QTcpSocket*> 该账号名下所有连接；未登记返回空列表
	 */
	QList<QTcpSocket*> socketsOf(const QString& account) const;

	int tcpPort;                    // 监听端口（来自 server.ini）
	QList<QTcpSocket*> m_clients;   // 在线连接
	QHash<QTcpSocket*, QString> m_ids;   // 连接 → 账号（hello 之后填）
	QHash<QTcpSocket*, QByteArray> m_buf;// 连接 → 半包缓冲（TCP 分片用）
};
