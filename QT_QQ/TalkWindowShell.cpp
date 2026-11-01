#include "stdafx.h"
#include <QDir>
#include <QCoreApplication>
#include <QStandardPaths>
#include "TalkWindowShell.h"
#include "TalkWindow.h"
#include "TalkWindowItem.h"
#include "CustomProxyStyle.h"
#include <qdebug.h>
#include <qfile.h>
#include <qmessagebox.h>
#include "UseMySQL.h"
#include <qtextstream.h>
#include <qjsonobject.h>
#include <qjsondocument.h>
#include <qbytearray.h>
#include "WindowManager.h"
#include <qjsonarray.h>
#include "RecvFile.h"
#include <QTimer>
#include "AppConfig.h"

extern QString LoginUserAccount;      // 定义与 CCMainWindow.cpp（当前登录账号）

// ============================================================================
// 构造函数：聊天主框架壳——承载"左侧会话列表 + 右侧聊天页 + 表情面板"，
// 是整个聊天界面的生命周期中枢（不是主窗口，主窗口是 CCMainWindow）。
// WA_DeleteOnClose：关掉最后一个聊天窗时壳自毁（见 addTalkWindow 的 lambda），
//   WindowManager 那边连着 destroyed 把指针清掉，下次进聊天再重建。
// ============================================================================

/**
 * @brief 构造函数：聊天主框架壳——承载会话列表 + 聊天页 + 表情面板。
 *
 * WA_DeleteOnClose：关掉最后一个聊天窗时壳自毁（见 addTalkWindow 的 lambda），
 * WindowManager 那边连着 destroyed 把指针清掉，下次进聊天再重建。
 * @param parent 父控件，默认 Q_NULLPTR
 */
TalkWindowShell::TalkWindowShell(QWidget *parent)
	: BasicWindow(parent), emotionWindow(nullptr)   // 基类构造 + 表情面板先置空
{
	ui.setupUi(this);                    // 生成 ui 控件并绑定到本窗

	setAttribute(Qt::WA_DeleteOnClose);  // 关闭即销毁，壳不常驻
	init();                              // 样式→列表信号→表情面板→生成 JS→连网
}

/**
 * @brief 析构：表情面板是无 parent 的顶层弹窗，对象树带不走它，必须手动 deleteLater。
 *
 * 不这么做的话壳销毁后面板还悬在屏幕上。
 */
TalkWindowShell::~TalkWindowShell()
{
	if (emotionWindow) emotionWindow->deleteLater();   // 手动延迟释放无父弹窗
}

/**
 * @brief 初始化顺序有意：先样式 → 再接列表点击 → 再建表情面板 → 生成 JS → 最后连网。
 *
 * JS 必须在连网前就绪：TCP 一连上就可能收到历史/离线消息，页面资源
 * （msghandling.js）没生成好的话消息渲染不出来。
 */
void TalkWindowShell::init()
{
	initStyle();                         // QSS + 列表代理 + 分栏比例
	connect(ui.listWidget, &QListWidget::itemClicked, this, &TalkWindowShell::onListItemClicked);   // 列表点击翻页
	initEmotionWindow();                 // 建唯一表情面板（常驻隐藏）
	initJsFile();                        // qrc 资源落盘 + 生成 msghandling.js
	initTcpSocket();                     // NDJSON 连接 + hello + 心跳重连
}

/**
 * @brief 样式三件套：QSS、列表的自绘代理（去掉 hover 时的默认高亮框）、分栏初始比例。
 */
void TalkWindowShell::initStyle()
{
	loadStyleSheet("TalkWindow");        // 加载 TalkWindow.qss
	ui.listWidget->setStyle(new CustomProxyStyle());   // 自绘代理，去掉默认高亮框
	ui.splitter->setSizes(QList<int>{154, width() - 154});   // 左列表 154px，右侧吃剩余宽度
}

/**
 * @brief 表情面板：整壳只建一个、常驻隐藏，点表情按钮切换显隐。
 *
 * 不做成每次 new：面板里 168 个表情反复创建太浪费，而且用户会连续多次发图。
 */
void TalkWindowShell::initEmotionWindow()
{
	emotionWindow = new EmotionWindow;   // 无 parent 顶层弹窗，析构时手动 deleteLater
	emotionWindow->hide();               // 默认隐藏，按钮切换显隐

	connect(emotionWindow, &EmotionWindow::addEmotion, this, &TalkWindowShell::onAddEmotion);   // 点选 → 插入当前输入框
}

// ============================================================================
// initJsFile：把编译在 qrc 里的消息页资源"落盘"到临时目录，并现场生成
// msghandling.js——每个员工账号一个 recvHtml_<账号> 函数 + 一个 external_<账号>
// 通道对象引用（模板替换见 replaceJsTxt）。
// 为什么必须落磁盘、不能直接加载 qrc：见体内注释——相对引用、工作目录、
//   exe 路径里的 "c++"、WebEngine 缓存四个坑都在里面。
// 顺序：读模板 → 替换生成 JS → 建目录 → 拷 html/css/js → 写 msghandling.js，
//   任何一步失败都弹窗并放弃（返回），页面最多空白，绝不半成品静默继续。
// ============================================================================

/**
 * @brief 把 qrc 里的消息页资源"落盘"到临时目录，并现场生成 msghandling.js。
 *
 * 为什么必须落磁盘、不能直接加载 qrc：相对引用 "./msghandling.js" 要命中
 * 磁盘上动态生成的脚本；工作目录随启动方式变化没有 Resources 目录；
 * exe 路径含 "c++"（两个加号）会让 WebEngine 的 file URL 兼容性出问题；
 * qrc 路径还会触发 WebEngine 缓存导致新 JS 永远不生效。
 * 任何一步失败都弹窗并放弃（返回），页面最多空白，绝不半成品静默继续。
 */
void TalkWindowShell::initJsFile()
{
	//读取msgPageTmp.txt
	QFile tmpFile(":/Resources/MainWindow/MsgHtml/msgPageTmp.txt");   // qrc 里的 JS 模板
	QString tmpFileContent;
	if (tmpFile.open(QFile::ReadOnly))  // 读取模板
	{
		tmpFileContent = tmpFile.readAll();   // 全文读入内存
		tmpFile.close();                      // 及时关闭
	}
	else
	{
		QMessageBox::information(nullptr, "error", "Read msgPageTmp.txt Error.");   // 读失败弹窗
		return;                          // 放弃生成，页面最多空白
	}

	if (!replaceJsTxt(tmpFileContent)) return;   // 按员工列表展开模板，失败已弹窗

	//生成的 js 不能只留在 qrc 里：页面要从磁盘加载，相对引用的 "./msghandling.js"
	//才会命中这份动态生成的脚本。也不能写到工作目录 "./"——工作目录随启动方式变化
	//（Qt Creator 里是项目根、双击 exe 时是 Debug），那里没有 Resources 目录，
	//QFile 不会自动建目录，写入会直接失败报 "Write msghandling.js Error"
	//用临时目录而不是 exe 目录：exe 路径含 "c++"（两个加号），
	//file URL 对这类路径的处理在 WebEngine 里有兼容性问题（页面停在 about:blank）
	QString htmlDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/QtQQ_MsgHtml";   // 系统临时目录子文件夹
	if (!QDir().mkpath(htmlDir))       // 建目录（含父目录）
	{
		QMessageBox::information(nullptr, "error", "Create MsgHtml dir Error.");   // 建目录失败弹窗
		return;                          // 放弃生成
	}

	//msgPage.html 也复制一份到同一目录：页面里的相对引用（msghandling.js / ui.css）
	//才会解析到磁盘上这份动态生成的脚本与样式，而不是 qrc 里的旧内容
	const QStringList resList = {
		"jquery-3.3.1.min.js", "qwebchannel.js", "ui.css", "msgPage.html"   // 需落盘的四个静态资源
	};
	for (const QString& res : resList)  // 逐个从 qrc 拷到临时目录
	{
		QFile src(":/Resources/MainWindow/MsgHtml/" + res);   // qrc 源
		QFile dst(htmlDir + "/" + res);                        // 磁盘目标
		if (src.open(QFile::ReadOnly) && dst.open(QFile::WriteOnly | QFile::Truncate))   // 两边都打开成功才拷
		{
			QByteArray content = src.readAll();   // 读源内容
			if (res == "msgPage.html")   //html 中的 qrc 引用改成同目录相对引用
			{
				content.replace("qrc:/Resources/MainWindow/MsgHtml/", "");   // qrc 路径改相对引用
				//防缓存：给本目录脚本引用加时间戳参数，否则 WebEngine 可能返回
				//缓存的旧 msghandling.js，导致新的流式逻辑永远不生效
				const QString stamp = QString::number(QDateTime::currentMSecsSinceEpoch());   // 当前毫秒时间戳
				content.replace("msghandling.js", "msghandling.js?v=" + stamp.toUtf8());   // 拼防缓存参数
			}
			dst.write(content);          // 写入磁盘
			dst.close();                 // 关闭目标
		}
		src.close();                     // 关闭源
	}

	QFile js(htmlDir + "/msghandling.js");   // 动态生成的 JS
	if (js.open(QFile::WriteOnly | QFile::Truncate))   // 打开写入
	{
		QTextStream stream(&js);
		stream << tmpFileContent;        // 写入展开后的 JS
		js.close();                      // 关闭文件
		return;                          // 生成成功
	}
	else
	{
		QMessageBox::information(nullptr, "error", "Write msghandling.js Error.");   // 写失败弹窗
		return;                          // 放弃，页面最多空白
	}
}

// ============================================================================
// replaceJsTxt：拿两个模板对员工列表做展开，拼出完整的 msghandling.js 内容
// 三处替换互相对应（以账号为后缀）：
//   var external      → var external_<账号>        （每个会话一个通道变量）
//   external = ...    → external_<账号> = ...      （通道对象赋值）
//   recvHtml          → recvHtml_<账号>            （每个发言者一个渲染函数）
// 名字必须和 MsgWebView::registerChannelObject 注册的完全一致，JS 那边
//   按 external_<账号> 取对象、C++ 按 recvHtml_<账号> 分发消息，对不上就是
//   ReferenceError 整条消息丢掉（renderMsg 里另有兜底）。
// 读模板失败返回 false，调用方直接放弃生成——宁缺毋滥。
// ============================================================================

/**
 * @brief 拿两个模板对员工列表做展开，拼出完整的 msghandling.js 内容。
 *
 * 三处替换互相对应（以账号为后缀）：
 *   var external → var external_<账号>；external = ... → external_<账号> = ...；
 *   recvHtml → recvHtml_<账号>。
 * 名字必须和 MsgWebView::registerChannelObject 注册的完全一致，对不上就是
 * ReferenceError 整条消息丢掉。读模板失败返回 false，宁缺毋滥。
 * @param fileContent 模板文本（就地替换为展开后的内容）
 * @return 展开成功 true；模板读取失败 false
 */
bool TalkWindowShell::replaceJsTxt(QString & fileContent)
{
	//需要替换的文本
	QString replaceVar = "var external = null;";                       // 变量定义模板
	QString replaceObj = "external = channel.objects.external;";       // 共享对象赋值模板
	QString replaceRecv;
	QFile recvFile(":/Resources/MainWindow/MsgHtml/recvHtmlTmp.txt");  // 渲染函数模板
	if (recvFile.open(QFile::ReadOnly))    // 读渲染函数模板
	{
		replaceRecv = recvFile.readAll();   // 全文读入
		recvFile.close();                   // 关闭
	}
	else
	{
		QMessageBox::information(nullptr, "error", "Read recvHtmlTmp.txt Error.");   // 读失败弹窗
		return false;                      // 模板缺了就不生成
	}

	//获取用户列表（QString：账号可能超过 int 上限，见 WindowManager.h）
	QStringList userList = UseMySQL::instance()->getUserList();   // 查库取全部员工账号

	//替换文本内容
	QString var, obj, recv;                 // 三段展开结果的累积器
	for (const QString& idStr : userList)      // 每轮一个账号
	{
		QString tmpVar = replaceVar;       // 复制变量模板
		tmpVar.replace("external", QString("external_%1").arg(idStr));   // 后缀成账号专属变量
		var += (tmpVar + "\n");            // 累加一行

		QString tmpObj = replaceObj;       // 复制赋值模板
		tmpObj.replace("external", QString("external_%1").arg(idStr));   // 后缀成账号专属赋值
		obj += (tmpObj + "\n");            // 累加一行

		QString tmpRecv = replaceRecv;     // 复制渲染函数模板
		tmpRecv.replace("recvHtml", QString("recvHtml_%1").arg(idStr));  // 函数名带账号
		tmpRecv.replace("external", QString("external_%1").arg(idStr));  // 内部引用也带账号
		recv += (tmpRecv + "\n");          // 累加
	}

	fileContent.replace(replaceVar, var);  // 替换变量定义段
	fileContent.replace(replaceObj, obj);  // 替换对象赋值段
	fileContent.replace(replaceRecv, recv);   // 替换渲染函数段

	return true;                           // 展开成功
}

// ============================================================================
// initTcpSocket：建立与 QtQQ_Server 的唯一消息通道（NDJSON over TCP）
// 三件事，细节和为什么都在体内注释：
//   1) connected → 先发 hello 自报账号（服务端靠它点对点投递），再补发
//      排队的历史请求——顺序反了 history 查不到私聊；
//   2) connectToHost 用 AppConfig 的地址端口（client.ini，没有就生成默认）；
//   3) 5 秒心跳重连：兜住服务端重启后"只发不收"的半死状态。
// ============================================================================

/**
 * @brief 建立与 QtQQ_Server 的唯一消息通道（NDJSON over TCP）。
 *
 * 三件事：
 *   1) connected → 先发 hello 自报账号（服务端靠它点对点投递），再补发
 *      排队的历史请求——顺序反了 history 查不到私聊；
 *   2) connectToHost 用 AppConfig 的地址端口（client.ini，没有就生成默认）；
 *   3) 5 秒心跳重连：兜住服务端重启后"只发不收"的半死状态。
 */
void TalkWindowShell::initTcpSocket()
{
	tcp = new QTcpSocket(this);          // 建 socket，parent 为 this 随窗销毁
	// 收发共用这一条 TCP 连接：服务端把每条消息逐客户端单播回来，
	// 客户端按 NDJSON 拆行后逐条路由（早期 UDP 广播已被服务端移除）
	connect(tcp, &QTcpSocket::readyRead, this, &TalkWindowShell::onTcpReadyRead);   // 可读 → 拆包处理

	// 连上就先自报账号：服务端靠它把私聊点对点投递（不然只能全体广播）。
	// 每次重连都会再发一次；发完 hello 再把攒下的历史请求补发出去
	// （history 要靠 hello 登记的身份才能查私聊，顺序不能反）。
	connect(tcp, &QTcpSocket::connected, this, [this]() {   // 连接建立回调
		QJsonObject hello;
		hello.insert("what", "hello");    // 标记消息类型
		hello.insert("id", LoginUserAccount);   // 自报账号
		tcp->write(QJsonDocument(hello).toJson(QJsonDocument::Compact));   // 写紧凑 JSON
		tcp->write(QByteArray(1, '\n'));  // NDJSON 行终止符
		tcp->flush();                     // 立即推送，不等事件循环

		const QStringList pending = m_pendingHistory;   // 拷贝排队的历史请求
		m_pendingHistory.clear();         // 清空队列
		for (const QString& with : pending)   // 逐个补发
			requestHistory(with);         // 此时已连接，直接发出
	});

	const AppConfig& cfg = AppConfig::instance();   // 读取配置（client.ini）
	const QString host = cfg.serverHost;   // 服务器地址
	const int port = cfg.serverPort;       // 服务器端口
	tcp->connectToHost(host, port);      // 发起连接（异步）

	// 心跳重连：只在发送时重连的话，服务端一重启客户端就"只发不收"，
	// 必须重开窗口才能恢复。每 5 秒检查一次（机器人那边同一套做法）
	m_reconnectTimer = new QTimer(this);   // 定时器随窗销毁
	m_reconnectTimer->setInterval(5000);   // 5 秒一次
	connect(m_reconnectTimer, &QTimer::timeout, this, [this, host, port]() {   // 按值拷贝地址端口
		if (tcp->state() == QAbstractSocket::UnconnectedState)   // 断线才重连
		{
			qInfo() << "[client] 重连消息服务" << host << port;   // 日志记录重连
			tcp->connectToHost(host, port);   // 发起重连
		}
	});
	m_reconnectTimer->start();           // 启动心跳重连
}

// ============================================================================
// requestHistory：向服务端要某个会话的最近记录（打开聊天窗时调）
// 没连上就先排队（m_pendingHistory 去重），connected 回调里原样补发——
//   这同时保证了 hello 必然排在 history 前面，服务端登记完身份才查私聊。
// limit=50：首屏只要最近 50 条，再多会把聊天窗开场堵住，也够回溯用。
// ============================================================================

/**
 * @brief 向服务端要某个会话的最近记录（打开聊天窗时调）。
 *
 * 没连上就先排队（m_pendingHistory 去重），connected 回调里原样补发——
 * 这同时保证了 hello 必然排在 history 前面，服务端登记完身份才查私聊。
 * limit=50：首屏只要最近 50 条，再多会把聊天窗开场堵住，也够回溯用。
 * @param with 对端标识（群号或对方账号）
 */
void TalkWindowShell::requestHistory(const QString& with)
{
	if (tcp->state() != QAbstractSocket::ConnectedState)   // 还没连上
	{
		// 还没连上：先记着，connected 回调里补发（顺便保证 hello 在前面）
		if (!m_pendingHistory.contains(with))   // 去重，同一会话只排队一次
			m_pendingHistory.append(with);     // 入队
		return;                          // 等连上再补发
	}

	QJsonObject req;
	req.insert("what", "history");       // 消息类型：历史请求
	req.insert("with", with);            // 对端标识
	req.insert("limit", 50);             // 只要最近 50 条
	tcp->write(QJsonDocument(req).toJson(QJsonDocument::Compact));   // 写紧凑 JSON
	tcp->write(QByteArray(1, '\n'));     // NDJSON 行终止符
	tcp->flush();                        // 立即推送
}

// ============================================================================
// addTalkWindow：把新建的聊天窗 + 它的侧边栏项"成对"挂进壳
// 三块状态必须同步维护：rightStackedWidget（页面）、listWidget（列表项）、
//   map（列表项 ↔ 窗口，切换显示全靠它反查）。
// 关闭链路（下面这个 lambda）是全项目最脆的一段，顺序不能乱：
//   先 map.remove 再删列表项 → 再从堆栈摘窗口 → 双 deleteLater 错峰销毁；
//   全部关完（count==0）壳自己也 close——壳是 WA_DeleteOnClose，
//   销毁时 WindowManager 收到 destroyed 清指针，下次进来重建。
// 谁调我：WindowManager::addNewTalkWindow，只在窗口新建时走这里；
//   已存在的窗口走 updateCurrentWindow，不重复挂。
// ============================================================================

/**
 * @brief 把新建的聊天窗 + 它的侧边栏项"成对"挂进壳。
 *
 * 三块状态必须同步维护：rightStackedWidget（页面）、listWidget（列表项）、
 * map（列表项 ↔ 窗口，切换显示全靠它反查）。关闭链路顺序不能乱：先
 * map.remove 再删列表项 → 再从堆栈摘窗口 → 双 deleteLater 错峰销毁；
 * 全部关完（count==0）壳自己也 close。
 * 谁调我：WindowManager::addNewTalkWindow，只在窗口新建时走这里。
 * @param window 新建的聊天窗
 * @param windowItem 对应的侧边栏列表项
 */
void TalkWindowShell::addTalkWindow(TalkWindow * window, TalkWindowItem * windowItem)
{
	ui.rightStackedWidget->addWidget(window);             // 聊天窗口添加到堆栈部件
	ui.rightStackedWidget->setCurrentWidget(window);      // 新窗立刻显示

	QListWidgetItem* item = new QListWidgetItem(ui.listWidget);   // 建列表项
	ui.listWidget->addItem(item);        // 挂到列表
	ui.listWidget->setItemWidget(item, windowItem);       // 窗口项添加到列表部件
	item->setSelected(true);             // 默认选中新建项

	map.insert(item, window);       // 添加到映射（列表项 → 聊天窗，点击切换用）

	// 关闭一个聊天窗的全链路：列表项、侧边项、堆栈窗三处一起拆，
	// 少拆一处就会留下"点得到但没窗口"或"有窗口但列表没了"的残骸
	connect(windowItem, &TalkWindowItem::signalCloseBtnClicked, this, [window, windowItem, item, this]()   // 捕获成对指针
	{
		map.remove(item);              // 先从映射摘掉，防悬空查询

		//listWidget
		ui.listWidget->takeItem(ui.listWidget->row(item));   // 从列表摘下项
		delete item;                   // 立即删除列表项
		windowItem->setParent(nullptr);   // 先脱离原父窗（talkWindow）：两边都排了 deleteLater，
		                                  // 不摘干净父窗口销毁时会把它连带删掉 → 双重释放
		windowItem->deleteLater();     // 错峰延迟销毁侧边项

		//StackedWidget
		ui.rightStackedWidget->removeWidget(window);   // 从堆栈摘窗口
		window->setParent(nullptr);    // 脱离父窗，防被连带删除
		window->deleteLater();           // deleteLater 而非 delete：本 lambda 由它的信号触发

		// 最后一个聊天窗也关了，壳没有存在意义，自毁（WA_DeleteOnClose）
		if (ui.rightStackedWidget->count() == 0)
		{
			this->close();         // 关闭 talkWindowShell（触发自毁）
		}
	});
}

/**
 * @brief 切到已存在的聊天窗：从 map 反查列表项并选中，右边翻到对应页面。
 *
 * 查不到就什么都不做（widget 不在本壳里时是合法情况，不算错误）。
 * @param widget 要显示的聊天窗
 */
void TalkWindowShell::updateCurrentWindow(QWidget * widget)
{
	QListWidgetItem* item = map.key(widget, nullptr);   // 反查列表项
	if (item)                        // 找到才切换
	{
		item->setSelected(true);     // 选中列表项
		ui.rightStackedWidget->setCurrentWidget(widget);   // 翻到对应页
	}
}

/**
 * @brief 表情按钮：切换面板显隐（再点一下收起），并按壳当前位置重摆。
 *
 * 位置每次现算：面板是全局顶层窗，壳可能被用户拖过位置，
 * 按屏幕坐标重摆到壳左上角偏移 (170,220)（大致对准表情按钮下方），不然会飘到别处。
 */
void TalkWindowShell::onEmotionBtnClicked()
{
	//表情按钮点击之后，表情窗口显示/隐藏
	emotionWindow->setVisible(!emotionWindow->isVisible());   // 取反切换显隐

	//显示表情窗口
	QPoint pos = this->mapToGlobal(QPoint(0, 0));     // 将窗体的左上角坐标转为全局屏幕坐标
	QPoint movePos = pos + QPoint(170, 220);         // 壳左上角偏移，大致对准表情按钮下方
	emotionWindow->move(movePos);                    // 按屏幕坐标重摆面板
}

/**
 * @brief 表情面板点选落地：先收面板，再插到"当前显示"的聊天窗输入框。
 *
 * 用 dynamic_cast 而不是假设一定是 TalkWindow——堆栈里理论上可以放别的页，
 * cast 失败就静默不动，比强转崩溃强。
 * @param name 表情资源编号
 */
void TalkWindowShell::onAddEmotion(int name)
{
	emotionWindow->hide();           // 隐藏表情窗口
	//获取TalkWindow
	TalkWindow* window = dynamic_cast<TalkWindow*>(ui.rightStackedWidget->currentWidget());   // 当前页安全转型
	if (window)                      // 转型成功才插入
	{
		window->addEmotionImage(name);   // 插到当前输入框光标处
	}
}

/**
 * @brief 点侧边栏会话项 → 右侧翻到对应聊天窗；map 查不到说明项已失效，什么都不做。
 * @param item 被点击的列表项
 */
void TalkWindowShell::onListItemClicked(QListWidgetItem * item)
{
	/*
		如果存在，则返回窗口，否则返回nullptr
	*/
	QWidget* widget = map.value(item, nullptr);   // 反查聊天窗
	if (widget)                      // 存在才翻页
	{
		ui.rightStackedWidget->setCurrentWidget(widget);   // 翻到对应页
	}
}

/**
 * @brief 发送（不带 talkId 重载）：表单/文件对话框这类不知道自己是哪个会话的调用方走这里。
 *
 * 用"当前显示的窗口"兜底取 talkId。
 * @param data 消息 JSON
 */
void TalkWindowShell::sendMsgToServer(QJsonValue data)
{
	TalkWindow* window = dynamic_cast<TalkWindow*>(ui.rightStackedWidget->currentWidget());   // 当前页安全转型
	sendMsgToServer(window ? window->getTalkId() : QString(), data);   // 取不到就空串，带 talkId 版本会直接 return
}

/**
 * @brief 真正的发送：由发起消息的那个聊天窗把自己的 talkId 传进来。
 *
 * 之前统一用"当前窗口"，多开几个窗口时会把消息发到别的会话里
 * （比如在群里点发送，实际发给了最后一个打开的私聊）。
 * @param talkId 目标会话标识（群号或对方账号）
 * @param data 消息 JSON（Array=普通消息；Object=文件消息）
 */
void TalkWindowShell::sendMsgToServer(const QString& talkId, QJsonValue data)
{
	if (talkId.isEmpty())             // 没有目标会话，直接放弃
		return;

	QJsonObject msgObj;            // 消息对象

	//聊天窗口类型
	if (talkId.length() <= GROUP_ID_LENGTH)       // 群聊（4 位群号）
		msgObj.insert(TYPE, TYPE_GROUP);
	else                                          // 单聊
		msgObj.insert(TYPE, TYPE_SINGLE); 

	//发送者/接收者（群号/员工ID）
	msgObj.insert(SEND, LoginUserAccount);   // 发送者 = 本机登录账号
	msgObj.insert(RECV, talkId);             // 接收者 = 群号或对方账号

	//消息内容
	switch (data.type())              // 按 JSON 值类型分流
	{
	case QJsonValue::Array:                // 普通消息（协议数组）
	{
		msgObj.insert(WHAT, WHAT_MSG);     // 标记普通消息
		msgObj.insert(WHAT_MSG, data);     // 塞入消息体
		break;
	}
	case QJsonValue::Object:               // 文件消息（SendFile 的 fileObj）
	{
		msgObj.insert(WHAT, WHAT_FILE);    // 标记文件消息
		msgObj.insert(WHAT_FILE, data);    // 塞入文件描述
		break;
	}
	default:
	{
		QMessageBox::information(nullptr, "error", "Other Msg Type.");   // 其他类型拒绝
		return;                          // 不发未知类型
	}
	}

	//发送消息给服务器
	QString msg = QJsonDocument(msgObj).toJson(QJsonDocument::Compact);   // 压成一行紧凑 JSON

	qDebug() << msg;                   // 调试日志

	//未连接时不再直接报错返回：自动重连一次再发。
	//服务端重启的窗口期里启动的客户端，若只在构造时连一次就永远断着，
	//这里做成"发送前自愈"，服务端短暂掉线客户端无需重开
	if (tcp->state() != QAbstractSocket::ConnectedState)   // 断线才自愈
	{
		const AppConfig& cfg = AppConfig::instance();   // 拿服务器地址配置
		tcp->abort();                                    // 丢弃半开的旧连接
		tcp->connectToHost(cfg.serverHost, cfg.serverPort);   // 立即重连
		if (!tcp->waitForConnected(2000))                // 最多等 2 秒
		{
			QMessageBox::information(nullptr, "error",
				QStringLiteral("无法连接消息服务(%1:%2)，请确认 QtQQ_Server 已启动")
					.arg(cfg.serverHost).arg(cfg.serverPort));   // 连不上弹窗
			return;                          // 放弃本条发送
		}
	}

	//NDJSON：每条消息后补一个换行，服务端按行拆分。
	//不补换行时连发两条会在服务端同一次 readAll 里粘成一行，JSON 解析失败被丢弃
	tcp->write(msg.toUtf8());          // 写消息体
	tcp->write(QByteArray(1, '\n'));   // NDJSON 行终止符（防粘包）
	tcp->flush();                      // 立即推送
}

// processDatagram — 单条消息 JSON 的路由处理
// 全项目只有一条接收通道（服务端逐客户端 TCP 单播），协议解析只写这一份。

/**
 * @brief 单条消息 JSON 的路由处理：历史回包/自己发的忽略/群聊/单聊/流式/文件全在这里分流。
 *
 * 全项目只有一条接收通道（服务端逐客户端 TCP 单播），协议解析只写这一份。
 * 历史回包没有 send 字段，必须在"过滤自己发的"之前处理；单聊的窗口 key
 * 取发送者而非 recv（recv 是本机账号，拿它找窗永远找不到）。
 * @param msg 单条完整 NDJSON 文本
 */
void TalkWindowShell::processDatagram(const QByteArray& msg)
{
	QJsonDocument jsonDoc = QJsonDocument::fromJson(msg);   // 解析 JSON
	if (jsonDoc.isNull() || !jsonDoc.isObject()) return;        // json 格式错误直接丢弃
	QJsonObject jsonObj = jsonDoc.object();            // 返回文档中包含的 QJsonObject

	/*   解析json对象   */
	QString sender = jsonObj[SEND].toString();         // 发送者

	//历史回包没有 send 字段，必须在"过滤自己发的"之前处理
	if (jsonObj.value(WHAT).toString() == "history")   // 历史回包
	{
		const QString with = jsonObj.value("with").toString();   // 对端标识
		TalkWindow* tw = dynamic_cast<TalkWindow*>(WindowManager::instance()->findWindowMap(with));   // 找对应聊天窗
		if (!tw) return;                // 窗口不在（没打开过）就丢弃
		for (const auto& v : jsonObj.value("items").toArray())   // 遍历历史条目
		{
			const QJsonObject item = v.toObject();   // 单条历史
			tw->appendHistoryMsg(item.value("send").toString(), item.value("text").toString());   // 只渲染不回发
		}
		return;                          // 历史处理完毕
	}

	if (sender == LoginUserAccount) return;            // 自己发送的消息不做处理

	//type 在协议里是数字（0=群聊 1=单聊），必须用 toInt() 取。
	//toString() 对数字型 JSON 值返回空串，会让所有单聊消息都落进群聊分支而被丢掉
	const int typeNum = jsonObj.value(TYPE).toInt();   // 取数字类型的会话类型
	const QString receiver = jsonObj[RECV].toString(); // 接收者（群号/员工ID）

	//目标聊天窗的 key：群聊=群号；单聊=对方账号（发送者）。
	//recv 在单聊里是本机账号，拿它去找窗口永远找不到，私聊消息就是全丢在这里
	QString talkId = receiver;         // 默认群聊用群号找窗
	if (typeNum == TYPE_SINGLE)        // 单聊分支
	{
		if (receiver != LoginUserAccount) return;      // 不是发给自己的单聊，忽略
		talkId = sender;                 // 单聊用发送者账号找窗
	}

	QWidget* widget = WindowManager::instance()->findWindowMap(talkId);   // 查聊天窗
	if (!widget)                       // 窗口还没开
	{
		// 离线私聊（服务端补发的）：窗口还没开，先建一个再渲染，
		// 否则消息等于没收到——用户不知道要手动点开哪个窗口
		if (typeNum == TYPE_SINGLE && jsonObj.value("offline").toBool())   // 离线私聊标记
		{
			WindowManager::instance()->addNewTalkWindow(sender);   // 自动开窗
			widget = WindowManager::instance()->findWindowMap(sender);   // 重新查窗
		}
		if (!widget)                   // 还是没有窗口就丢弃
		{
			qDebug() << "[client] 找不到对应聊天窗，丢弃消息: talkId=" << talkId
					 << "what=" << jsonObj.value(WHAT).toString();   // 日志留痕
			return;                    // 丢弃
		}
	}

	//AI 机器人流式回复（msg_stream）：转给对应聊天窗增量渲染
	if (jsonObj.value(WHAT).toString() == "msg_stream")   // 流式回复
	{
		TalkWindow* streamWindow = dynamic_cast<TalkWindow*>(widget);   // 安全转型
		if (streamWindow)              // 转型成功才转发
			streamWindow->onStreamDelta(sender,
				jsonObj.value("delta").toString(),   // 本次增量文本
				jsonObj.value("done").toBool());     // 是否结束
		return;                          // 流式处理完毕
	}

	this->updateCurrentWindow(widget);  // 实时消息切到对应窗

	QString what = jsonObj[WHAT].toString();         // 消息类型
	QJsonValue data = jsonObj[what];                 // 消息内容（键名与类型同名）

	if (what == WHAT_MSG)             // 普通消息
	{
		QJsonArray msgArray = data.toArray();   // 取消息数组
		dynamic_cast<TalkWindow*>(widget)->appendObjectMsg(msgArray, sender);   // 渲染 + 回发确认
	}
	else if (what == WHAT_FILE)       // 文件消息
	{
		QJsonObject fileObj = data.toObject();  // 取文件 JSON

		RecvFile* recvFile = new RecvFile(fileObj, this);   // 弹接收确认框
		QString msg = QStringLiteral("收到来自") +
			UseMySQL::instance()->getUserName(sender) + QStringLiteral("发来的文件，是否接收？");   // 查库拼提示
		recvFile->setLabelMsg(msg);      // 刷提示文案
		recvFile->show();                // 显示接收框
	}
}

/**
 * @brief TCP 接收：NDJSON（每行一条 JSON）缓冲拆行后走同一路由。
 */
void TalkWindowShell::onTcpReadyRead()
{
	m_tcpBuf += tcp->readAll();        // 追加本次可读数据到缓冲
	int nl = -1;                       // 换行符位置
	while ((nl = m_tcpBuf.indexOf('\n')) != -1)   // 还有完整行
	{
		QByteArray line = m_tcpBuf.left(nl);      // 取出一行
		m_tcpBuf.remove(0, nl + 1);      // 从缓冲里删掉已消费部分
		if (!line.trimmed().isEmpty())   // 跳过空行
			processDatagram(line.trimmed());   // 单条消息路由
	}
}
