#include "TcpServer.h"
#include "UseMySQL.h"    // 落库 / 取离线消息 / 查历史
#include <QTcpSocket>    // accept 得到的连接
#include <QJsonArray>    // 拆 msg 块数组
#include <QJsonDocument> // 编解码 JSON
#include <QJsonObject>   // 单个 JSON 对象
#include <QDebug>        // 调试日志

/**
 * @brief 构造只记端口，监听动作留给显式调用的 run()
 *
 * 构造函数没法返回错误，监听成败必须由 run 的返回值上报。
 * @param tcpPort 监听端口，来自 server.ini 的 AppConfig
 * @param parent 父对象指针，缺省 nullptr
 */
TcpServer::TcpServer(int tcpPort, QObject *parent)
	: QTcpServer(parent), tcpPort(tcpPort)  // 把端口存进成员，run() 里用
{
}

/**
 * @brief 析构：无手动释放（在线 socket 的释放挂在各自的 disconnected → deleteLater 上）
 */
TcpServer::~TcpServer()
{
}

/**
 * @brief 启动服务器：开始监听 AnyIPv4:tcpPort
 *
 * AnyIPv4 = 绑定本机所有 IPv4 网卡（0.0.0.0），局域网里其他机器才连得进来；
 * 只绑 127.0.0.1 的话别的机器连不上。listen 是非阻塞的：让内核开始监听后立即返回。
 * @return bool 监听成功返回 true，失败（端口被占用等）返回 false
 */
bool TcpServer::run()
{
	if (this->listen(QHostAddress::AnyIPv4, tcpPort))             // 绑所有 IPv4 网卡，局域网可达
	{
		qDebug() << QStringLiteral("服务端监听端口 %1 成功！").arg(tcpPort); // 成功日志
		return true;                                               // 告诉调用方可以开始 accept
	}
	else
	{
		qDebug() << QStringLiteral("服务端监听端口 %1 失败！").arg(tcpPort); // 失败日志（常见原因：端口被占）
		return false;                                              // 告诉调用方监听没起来
	}
}

/**
 * @brief 有新客户端连接时被 Qt 自动调用：包装 socket、挂信号槽、登记在线表
 *
 * 断开时同步从 m_clients/m_ids/m_buf 摘掉——deleteLater 只是把释放排进事件队列，
 * 本 lambda 在同一次断开信号里同步跑完，保证对象真正析构前先从各表里摘掉，
 * 否则 broadcast 可能写到已释放的 socket。
 * @param socketDescriptor 内核 accept 得到的原生 socket 句柄
 */
void TcpServer::incomingConnection(qintptr socketDescriptor)
{
	QTcpSocket* socket = new QTcpSocket;                          // 用原生句柄构造 Qt 的 socket 对象
	socket->setSocketDescriptor(socketDescriptor);                // 绑到 accept 出来的 fd 上

	connect(socket, &QTcpSocket::readyRead, this, &TcpServer::readSocket); // 可读时拆帧
	connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater); // 断开后排进事件队列释放

	// 注册到在线表；断开时同时移除连接和它的账号登记
	// （deleteLater 只是把释放排进事件队列，本 lambda 在同一次断开信号里同步跑完，
	//  保证对象真正析构前先从各表里摘掉，否则 broadcast 可能写到已释放的 socket）
	m_clients.append(socket);                                      // 先登记在线，hello 之后 m_ids 才有账号
	connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
		m_clients.removeOne(socket);                               // 从在线列表摘掉
		m_ids.remove(socket);                                      // 从账号登记摘掉
		m_buf.remove(socket);                                      // 清掉半包缓冲
		qDebug() << QStringLiteral("连接断开，剩余在线 %1").arg(m_clients.size()); // 剩余在线数
	});

	qDebug() << QStringLiteral("新的连接：%1，在线 %2").arg(socketDescriptor).arg(m_clients.size()); // 新连接日志
}

/**
 * @brief 向单个连接写一行，带背压保护
 *
 * 连接为空或已断开直接丢弃；积压超过 1MB 跳过本条（聊天消息可以丢，
 * 服务端不能被拖垮）——某个客户端收得慢时数据会堆在服务端内存里。
 * @param c 目标连接（可为 nullptr）
 * @param packet 完整报文（调用方负责带换行）
 */
void TcpServer::sendTo(QTcpSocket* c, const QByteArray& packet)
{
	if (!c || c->state() != QAbstractSocket::ConnectedState)       // 空指针或已断开：直接丢弃
		return;

	// 某个客户端收得慢，数据会堆在服务端内存里：
	// 积压超过 1MB 就跳过这条（聊天消息可以丢，服务端不能被拖垮）
	if (c->bytesToWrite() > (1 << 20))                             // 积压超过 1MB
	{
		qWarning() << QStringLiteral("[relay] 客户端 %1 积压 %2 字节，跳过本条")
					  .arg(c->peerPort()).arg(c->bytesToWrite()); // 打日志方便定位慢客户端
		return;                                                     // 本条丢弃，不写
	}

	c->write(packet);                    // 一次写完，数据+换行同包避免粘连歧义
}

/**
 * @brief 广播给所有在线连接（含发送者自己，客户端自行过滤）
 *
 * 每条连接的积压保护由 sendTo 单独把关，慢客户端不会拖垮广播。
 * @param packet 完整报文（调用方负责带换行）
 */
void TcpServer::broadcast(const QByteArray& packet)
{
	for (QTcpSocket* c : m_clients)                                // 遍历所有在线连接
		sendTo(c, packet);                                         // 逐个发送，各自过背压保护
}

/**
 * @brief 按账号反查它名下的全部连接（同一账号可双开）
 *
 * 只有 hello 登记过的连接才查得到：连上还没自报账号、或 id 拼错，
 * 都返回空列表——调用方（私聊投递）据此走"不在线，等补发"分支。
 * @param account 要找的账号
 * @return QList<QTcpSocket*> 该账号名下所有连接；未登记返回空列表
 */
QList<QTcpSocket*> TcpServer::socketsOf(const QString& account) const
{
	QList<QTcpSocket*> out;                                        // 结果列表
	if (account.isEmpty())                                         // 空账号无意义，直接返回空
		return out;

	for (auto it = m_ids.constBegin(); it != m_ids.constEnd(); ++it) // 遍历连接→账号映射
	{
		if (it.value() == account)                                 // 账号命中
			out << it.key();                                       // 把对应连接加入结果
	}
	return out;                                                    // 可能为空（未登记/全断开）
}

/**
 * @brief 处理一条完整报文：hello 登记，history 查历史，其余落库 + 投递
 *
 * hello：登记"这条连接是哪个账号"，顺手补发离线私聊（bot 角色除外），不转发。
 * history：查会话历史，只回给请求方。
 * 其他：先落库，再按 type 投递（群聊广播 / 私聊点对点）。
 * @param from 报文来源连接（hello 用它登记归属）
 * @param line 一条完整 NDJSON 报文（不含换行）
 */
void TcpServer::handleLine(QTcpSocket* from, const QByteArray& line)
{
	const QJsonObject obj = QJsonDocument::fromJson(line).object(); // 把字节数组解析成 JSON 对象
	const QString what = obj.value("what").toString();             // 取报文类型字段

	if (what == QStringLiteral("hello"))                           // 握手：登记这条连接属于谁
	{
		const QString id = obj.value("id").toString();             // 取自报的账号
		if (id.isEmpty())                                          // 账号为空：不登记，直接丢弃
			return;

		m_ids[from] = id;                                          // 建立"连接 → 账号"映射
		qInfo() << QStringLiteral("[hello] %1 上线，当前在线 %2").arg(id).arg(m_clients.size()); // 上线日志

		// 机器人也会 hello，但"离线私聊补发"是给终端用户的；
		// 不区分的话机器人每次重连都会被重新投喂一堆旧问题
		if (obj.value("role").toString() != QStringLiteral("bot")) // 非 bot 才补发离线消息
			deliverPending(from, id);                              // 给刚上线的账号补发离线私聊
		return;     // 握手指令不投递给任何人
	}

	if (what == QStringLiteral("history"))                         // 查历史：只回请求方
	{
		sendHistory(from, obj);                                    // 用请求里的 with/limit 查并回包
		return;                                                     // 不转发不落库
	}

	// 业务消息：先落库（服务端重启/客户端关闭都不丢），再投递
	persist(obj);                                                  // 落库（失败只打日志，不阻断投递）

	const QByteArray packet = line + char(0x0A);   // NDJSON：数据和换行一次写完

	const int type = obj.value("type").toInt();                   // 0=群聊 1=私聊
	const QString recv = obj.value("recv").toString();             // 群号或收件人账号

	if (type == 1 && !recv.isEmpty())     // 私聊：只发给收件人
	{
		// 同一个账号可能开了多个连接（双开客户端、脚本 + 客户端同时在线），
		// 要全部投递，只发第一条的话另一头永远收不到
		const QList<QTcpSocket*> targets = socketsOf(recv);        // 找收件人名下全部连接
		if (!targets.isEmpty())                                    // 收件人在线
		{
			for (QTcpSocket* t : targets)                          // 逐个投递（双开场景全发）
				sendTo(t, packet);                                 // 各自过背压保护
			return;                                                 // 私聊已投递，不再广播
		}
		// 收件人不在线：消息已经落库，等他上线由 deliverPending 补发，
		// 这里不再广播（避免所有人看到别人的私聊）
		return;                                                     // 不广播，离线消息已入库
	}

	broadcast(packet);     // 群聊 / 文件 / 没写 type 的老客户端
}

/**
 * @brief 把消息写进 tab_messages（表结构与设计动机见 UseMySQL 侧章节注释）
 *
 * msg：拼接文本块（txt 原样，img 记成 [表情]）；
 * file：只记文件名，不存内容（内容可能是几 MB 的 base64）；
 * msg_stream：只在 done 时记一条（优先 full 全文，没有则退回本次 delta；
 *              中间增量不落库，否则一条回答几百行）。
 * @param obj 解析后的 JSON 对象（含 what/type/send/recv/msg/file 等字段）
 */
void TcpServer::persist(const QJsonObject& obj)
{
	const QString what = obj.value("what").toString();             // 报文类型
	const int type = obj.value("type").toInt();                   // 0=群聊 1=私聊
	const QString sender = obj.value("send").toString();           // 发送者账号
	const QString recv = obj.value("recv").toString();             // 群号或收件人

	if (what == QStringLiteral("msg"))                             // 普通聊天消息
	{
		QString text;                                              // 拼好的纯文本
		for (const auto& blockArr : obj.value("msg").toArray())    // 外层数组（行）
		{
			for (const auto& block : blockArr.toArray())           // 内层数组（块）
			{
				const QJsonObject o = block.toObject();            // 单个块对象
				if (o.contains("txt"))                             // 文本块
					text += o.value("txt").toString();             // 原样拼接
				else if (o.contains("img"))                        // 表情块
					text += "[表情]";                              // 只记占位，不存图片数据
			}
		}
		if (!text.isEmpty())                                       // 拼出内容才落库
			UseMySQL::instance()->saveMessage(type, sender, recv, text); // 交给 DB 层
	}
	else if (what == QStringLiteral("file"))                       // 文件消息
	{
		const QJsonObject f = obj.value("file").toObject();        // 取 file 子对象
		UseMySQL::instance()->saveMessage(type, sender, recv,
										  QStringLiteral("[文件] ") + f.value("name").toString()); // 只记文件名
	}
	else if (what == QStringLiteral("msg_stream") && obj.value("done").toBool()) // 流式回答的最终包
	{
		// 流式回答的正文散在增量里：done 包优先用 full（机器人给的全文），
		// 没有 full 时退回本次 delta（一次性写入的那种回答）
		QString text = obj.value("full").toString();               // 优先用全文
		if (text.isEmpty())                                        // 没有全文
			text = obj.value("delta").toString();                  // 退回本次增量
		if (!text.isEmpty())                                       // 有内容才落库
			UseMySQL::instance()->saveMessage(type, sender, recv, text); // 交给 DB 层
	}
}

/**
 * @brief 给刚上线的账号补发他不在线期间收到的私聊
 *
 * 从 pendingPrivateMessages 取最多 100 条，逐条合成 msg 报文（带 offline=true
 * 标记，客户端据此决定要不要为它开个窗口）发给 to，最后 markPrivateDelivered
 * 把该账号全部 delivered=0 置 1。
 * @param to 上线者当前的 socket 连接
 * @param account 上线者的账号
 */
void TcpServer::deliverPending(QTcpSocket* to, const QString& account)
{
	const auto pending = UseMySQL::instance()->pendingPrivateMessages(account); // 取离线私聊
	if (pending.isEmpty())                                         // 没有离线消息：直接返回
		return;

	qInfo() << QStringLiteral("[offline] 给 %1 补发 %2 条离线私聊").arg(account).arg(pending.size()); // 补发日志
	for (const auto& m : pending)                                  // 逐条补发
	{
		QJsonObject msgObj;                                        // 合成一条标准 msg 报文
		msgObj.insert("what", "msg");                              // 报文类型
		msgObj.insert("send", m.sender);                           // 原发送者
		msgObj.insert("recv", account);                            // 收件人 = 上线者
		msgObj.insert("type", 1);                                  // 私聊
		msgObj.insert("offline", true);      // 客户端据此决定"要不要为它开个窗口"
		msgObj.insert("msg", QJsonArray{ QJsonArray{ QJsonObject{ {"txt", m.text} } } }); // 与普通 msg 同结构
		sendTo(to, QJsonDocument(msgObj).toJson(QJsonDocument::Compact) + char(0x0A)); // 压缩 JSON + 换行
	}
	UseMySQL::instance()->markPrivateDelivered(account);           // 全量置 delivered=1，避免重发
}

/**
 * @brief 应答客户端的 history 请求：只回请求方，不落库不转发
 *
 * limit 不传默认 50，qBound 夹到 [1, 200]（防一次拉爆内存）；
 * isGroup 与客户端一致：with 长度 ≤ 4 视为群号。
 * @param to 请求方连接
 * @param req 含 with（会话对象/群号）和可选 limit 的 JSON 请求
 */
void TcpServer::sendHistory(QTcpSocket* to, const QJsonObject& req)
{
	const QString with = req.value("with").toString();             // 会话对象：群号或对方账号
	const int limit = qBound(1, req.value("limit").toInt(50), 200);   // 不传默认 50，上限 200（防一次拉爆内存）
	const bool isGroup = with.length() <= 4;      // 与客户端一致：群号 4 位
	const QString self = m_ids.value(to);                           // 请求方自己的账号（hello 时登记）

	const auto items = UseMySQL::instance()->history(self, with, isGroup, limit); // 查最近 N 条

	QJsonArray arr;                                                // 组装响应条目数组
	for (const auto& m : items)                                    // 逐条转成 JSON 对象
		arr.append(QJsonObject{ {"send", m.sender}, {"text", m.text} }); // 只带 send/text 两字段

	QJsonObject resp;                                              // 响应包
	resp.insert("what", "history");                                // 类型字段
	resp.insert("with", with);                                     // 会话对象回显
	resp.insert("items", arr);                                     // 历史条目数组
	sendTo(to, QJsonDocument(resp).toJson(QJsonDocument::Compact) + char(0x0A)); // 压缩 JSON + 换行
}

/**
 * @brief 某个连接可读：按行拆开逐条处理
 *
 * 必须自己缓冲：TCP 是字节流没有消息边界，一条大消息（比如几 MB 的 base64
 * 文件）一定会被拆成多个报文。原来直接 split('\n') 处理"本次读到的东西"，
 * 分片会被当成好几条独立消息转发出去，接收端全是坏 JSON。
 * 单行上限 8MB：客户端不发换行地猛灌时，别把服务端内存撑爆，超限直接 abort。
 */
void TcpServer::readSocket()
{
	QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());      // 找到是哪个连接可读
	if (!socket)                                                   // 理论上不会，防御性返回
		return;

	m_buf[socket] += socket->readAll();                            // 把本次读到的字节追加进半包缓冲

	// 单行上限：客户端不发换行地猛灌时，别把服务端内存撑爆
	if (m_buf[socket].size() > (8 << 20))                          // 超过 8MB
	{
		qWarning() << QStringLiteral("[relay] 单条消息超过 8MB，断开 %1").arg(socket->peerPort()); // 警告日志
		socket->abort();                                           // 直接掐断连接
		return;                                                     // 不再处理
	}

	QByteArray& buf = m_buf[socket];                               // 拿到缓冲引用，下面原地消费
	int nl = -1;                                                   // 换行位置
	while ((nl = buf.indexOf(char(0x0A))) != -1)                   // 循环直到没有完整行
	{
		const QByteArray line = buf.left(nl).trimmed();            // 取出一行并去空白
		buf.remove(0, nl + 1);                                     // 从缓冲里删掉已处理的行（含换行）
		if (!line.isEmpty())                                       // 空行直接跳过
			handleLine(socket, line);                              // 交给报文处理
	}
}
