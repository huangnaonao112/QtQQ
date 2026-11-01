#include "stdafx.h"
#include "MsgPage.h"
#include <qfile.h>
#include "define.h"
#include <qmessagebox.h>
#include "UseMySQL.h"
#include <qwebchannel.h>
#include <QWebEngineSettings>
#include <QStandardPaths>
#include <QCoreApplication>
#include "WindowManager.h"
#include <qjsonarray.h>
#include <qjsonobject.h>
#include <qjsondocument.h>

extern QString LoginUserAccount;      //定义与CCMainWindow.cpp

// ============================================================================
// MsgHtmlObj 构造：一个注册给 WebChannel 的"消息通道对象"
// 拿着己方登录账号（右气泡头像）和对方头像路径，把 userLeft/userRight
// 两套 HTML 模板里 %1 占位的头像 URL 替换成真实路径。
// parent 必须是 MsgWebView——通道对象的生命周期跟页面走，页面销毁它必须跟着死，
// 否则 QWebChannel 里留个悬空注册项。
// ============================================================================
/**
 * @brief 消息通道对象构造：把左右气泡模板从 HTML_PATH 加载进来，并把 %1 占位替换成真实头像路径。
 *        parent 必须是 MsgWebView——通道对象生命周期跟页面走，否则 QWebChannel 里留悬空注册项。
 * @param parent 父对象，必须传 MsgWebView
 * @param headPath 对方头像路径（左气泡用）；传空则只填右气泡
 */
MsgHtmlObj::MsgHtmlObj(QObject *parent, QString headPath)
	: QObject(parent), leftHeadPath(headPath)
{
	initMsgHtml();   // 立即加载模板并填头像
}

/**
 * @brief 析构：模板字符串是成员，随对象释放，无需手动清理
 */
MsgHtmlObj::~MsgHtmlObj()
{
}

/**
 * @brief 加载左右两套气泡模板并填头像：右边是自己（登录账号查库），左边是对方（构造时传入的 headPath）
 */
void MsgHtmlObj::initMsgHtml()
{
	//右边显示发送消息
	msgRight = getMsgHtml("userRight");   // 加载右气泡模板
	msgRight.replace("%1", UseMySQL::instance()->getUserHeadPixmap(LoginUserAccount));   // %1 换成自己的头像路径

	//左边显示接收消息
	msgLeft = getMsgHtml("userLeft");     // 加载左气泡模板
	msgLeft.replace("%1", leftHeadPath);  // %1 换成对方头像路径
}

/**
 * @brief 从资源/磁盘读一个 HTML 模板的全文；读失败弹窗报错并返回空串——
 *        空模板会让页面渲染出空白气泡，但比静默失败好排查
 * @param fileName 模板名（不含路径后缀），如 "userLeft"、"userRight"
 * @return QString 模板全文；读失败返回空串
 */
QString MsgHtmlObj::getMsgHtml(const QString & fileName)
{
	QFile file(HTML_PATH(fileName));   // 拼 :/Resources/MainWindow/MsgHtml/<fileName>.html
	QString fileContent;               // 暂存读到的内容
	if (file.open(QFile::ReadOnly))    // 打开成功
	{
		fileContent = file.readAll();  // 读取全部内容
		file.close();                  // 关闭文件句柄
	}
	else                               // 打开失败（文件不存在等）
	{
		QMessageBox::information(nullptr, "error", QString("Read %1.html Error.").arg(fileName));   // 弹窗告知排查
	}

	return fileContent;                // 返回（失败时为空串）
}

// ============================================================================
// MsgWebView 构造：搭消息渲染页——整个聊天内容区其实是个 QWebEngineView
// 三个关键契约（细节见体内注释）：
//   1) 页面从临时目录 QtQQ_MsgHtml/msgPage.html 加载，必须开
//      LocalContentCanAccess* 两个开关，否则 jquery/webchannel 全被拦；
//   2) QWebChannel 注册 external0（自己）+ external_<账号>（每个会话对象），
//      JS 侧靠这些对象名把消息回传给 C++；
//   3) 页面 loadFinished 前到达的消息先攒在 m_pendingJs，加载完按序补执行
//      ——历史/离线消息经常比页面 ready 得早，直接 runJavaScript 会丢。
// 发送信号不在这连：必须带 talkId 才知道是哪个会话发的，由 TalkWindow
//   构造时用自己的 id 接线（见 TalkWindow::initConnect）。
// ============================================================================
/**
 * @brief 消息渲染页构造：搭消息渲染页——整个聊天内容区其实是个 QWebEngineView。
 *        三个关键契约：1) 页面从临时目录 QtQQ_MsgHtml/msgPage.html 加载，必须开
 *        LocalContentCanAccess* 两个开关；2) QWebChannel 注册 external0+external_<账号>；
 *        3) loadFinished 前到达的消息先攒在 m_pendingJs，加载完按序补执行。
 *        发送信号不在这连：必须带 talkId，由 TalkWindow 构造时接线。
 * @param parent 父窗口指针（一般传 TalkWindow.ui 里的提升控件容器）
 */
MsgWebView::MsgWebView(QWidget* parent)
	: QWebEngineView(parent), rightObj(nullptr)
{
	this->setContextMenuPolicy(Qt::NoContextMenu);        //禁止右键弹出菜单

	MsgWebPage* page = new MsgWebPage(this);        //初始化页面（重写 acceptNavigationRequest 放行 file://）
	this->setPage(page);   // 把自定义页面设给 WebEngineView

	channel = new QWebChannel(this);                //初始化通道
	this->page()->setWebChannel(channel);   // 把通道绑到页面
	//data: 页面（setHtml）默认禁止加载 file:// 资源，
	//不打开这两个开关，jquery/qwebchannel/msghandling 都会被安全策略拦截
	this->page()->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);   // 允许加载本地 file:// 资源
	this->page()->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);	//加载磁盘上动态生成的页面（initJsFile已写好）。
	//acceptNavigationRequest 已放行 file://；页面设置已开启本地文件访问
	QUrl pageUrl = QUrl::fromLocalFile(
		QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/QtQQ_MsgHtml/msgPage.html");   // 拼临时目录页面路径
	this->load(pageUrl);   // 加载磁盘页面

	// 页面加载完成：把攒下的脚本按顺序补执行（历史消息、离线消息都可能是
	// 窗口刚建好就到了，早于 loadFinished）
	connect(this, &QWebEngineView::loadFinished, this, [this](bool ok)
	{
		if (!ok)   // 页面加载失败
		{
			qWarning() << "[MsgWebView] 消息页面加载失败（检查临时目录 QtQQ_MsgHtml）";   // 日志提示排查点
			m_pendingJs.clear();   // 加载都失败了，攒的脚本也没意义
			return;                // 放弃补执行
		}

		m_loaded = true;                    // 标记页面已就绪，后续 runScript 直接执行
		const QStringList pending = m_pendingJs;   // 取出攒下的脚本
		m_pendingJs.clear();                // 清空，避免重复执行
		for (const QString& js : pending)   // 按序补执行
			this->page()->runJavaScript(js);   // 逐条执行之前攒的脚本
	});

	registerChannelObject();            //注册通道对象

	// 注意：这里不再直接连到 TalkWindowShell::sendMsgToServer。
	// 发送必须带上"是哪个会话发的"（talkId），否则多开窗口时会发错人；
	// 由 TalkWindow 在构造时把 talkId 一起接进去（见 TalkWindow::initConnect）。
}

/**
 * @brief 析构：rightObj/channel 都以本对象为 parent，随对象树自动释放
 */
MsgWebView::~MsgWebView()
{
}

/**
 * @brief 聊天中的一条新消息：渲染上屏 + 回传服务端（emitSend=true），统一入口是 renderMsg
 * @param jsonMsg 协议 JSON 数组（块数组的数组）
 * @param obj 对方账号；传 MYSELF 表示自己发的（右气泡）
 */
void MsgWebView::appendMsg(const QJsonArray & jsonMsg, QString obj)
{
	renderMsg(jsonMsg, obj, /*emitSend*/ true);   // emitSend=true：新消息要回发服务端落库
}

/**
 * @brief 历史消息：和 appendMsg 的区别只有两点——不把消息再发回服务端；
 *        自己发的那些直接走右气泡，对方发的走对应账号的定向气泡函数
 * @param jsonMsg 协议 JSON 数组
 * @param sender 发送者账号（isSelf=true 时忽略）
 * @param isSelf true=自己发的历史消息（右气泡）；false=对方发的
 */
void MsgWebView::appendHistoryMsg(const QJsonArray & jsonMsg, const QString & sender, bool isSelf)
{
	renderMsg(jsonMsg, isSelf ? QString(MYSELF) : sender, /*emitSend*/ false);   // emitSend=false：历史只是回放
}

// ============================================================================
// renderMsg：把 JSON 消息数组拼成 HTML 片段，再交给页面 JS 画气泡
// emitSend 是和 appendMsg/appendHistoryMsg 的分界：新聊天消息要回发服务端
//   落库，历史消息只是回放（再发一遍服务端就会重复存）。
// 分两个分支的原因：自己发的走 appendHtml0 固定右气泡；对方发的走
//   recvHtml_<账号> 定向函数（每个账号一个，头像各不相同），
//   账号去掉减号是因为 JS 标识符不容 '-'（历史上按 int 存过负数）。
// 兜底 recvHtmlGeneric 见体内注释——服务端新加员工时老客户端没有
//   对应函数，不兜底整条消息就 ReferenceError 丢了。
// ============================================================================
/**
 * @brief 把 JSON 消息数组拼成 HTML 片段，再交给页面 JS 画气泡。
 *        emitSend 是和 appendMsg/appendHistoryMsg 的分界：新聊天消息要回发服务端落库，
 *        历史消息只是回放（再发一遍服务端就会重复存）。
 *        自己发的走 appendHtml0 固定右气泡；对方发的走 recvHtml_<账号> 定向函数，
 *        账号去掉减号是因为 JS 标识符不容 '-'。recvHtml 不存在时兜底 recvHtmlGeneric。
 * @param jsonMsg 协议 JSON 数组
 * @param obj 对方账号；传 MYSELF 表示自己发的
 * @param emitSend true=渲染后还要回发服务端落库；false=只是回放历史
 */
void MsgWebView::renderMsg(const QJsonArray & jsonMsg, const QString & obj, bool emitSend)
{
	QString msg = parseMsgFromJson(jsonMsg);   // 把块数组拍平成 HTML 字符串

	QJsonObject msgObj;                        // 包一层 JSON 对象，key 为 "MSG"
	msgObj.insert("MSG", msg);                 // 塞进拼好的 HTML

	//将jsonObject转换为紧凑格式的json字符串
	const QString jsonString = QJsonDocument(msgObj).toJson(QJsonDocument::Compact);   // 紧凑 JSON 字符串，直接嵌 JS 调用
	if (obj == MYSELF)   // 自己发的：右气泡
	{
		runScript(QString("appendHtml0(%1)").arg(jsonString));   // 调页面 appendHtml0 画右气泡
		if (emitSend)   // 只有新聊天消息才回发服务端
			emit signalSendMsg(jsonMsg);   // 回传给 TalkWindow → TalkWindowShell → 服务器
	}
	else                // 对方发的：左气泡（定向函数）
	{
		// recvHtml 系列函数名里用的是账号（去减号：早期用 int 时大账号会变负）
		QString recvName = obj;                // 先拿完整账号
		recvName.remove('-');                  // 去掉减号，JS 标识符不容 '-'

		// 头像路径从数据库查（UseMySQL 已解析成 qrc/本机可用路径）
		QString head = UseMySQL::instance()->getUserHeadPixmap(obj);   // 查对方头像路径
		head.replace('\\', '/');               // 反斜杠统一成正斜杠，JS 字符串里好处理
		head.replace('\'', "\\'");             // 单引号转义，防破坏 JS 字符串字面量

		// 兜底：recvHtml_<账号> 不存在时退回通用气泡。
		// 什么时候会不存在？服务端新加的员工——JS 是客户端启动时按当时的
		// 员工表生成的，老客户端里没有这个函数，原来会 ReferenceError 整条丢失。
		// 这里用字符串拼接而不是 .arg()：消息内容里可能正好含 "%1" 这种字符。
		const QString call = QStringLiteral("recvHtml_") + recvName;   // 定向函数名
		const QString js = "if (typeof " + call + " === 'function') "
						   + call + "(" + jsonString + "); "
						   + "else recvHtmlGeneric('" + head + "', " + jsonString + ");";   // typeof 探测 + 兜底
		runScript(js);   // 交给 runScript（可能先攒着）
	}
}

/**
 * @brief 页面没加载完就先把脚本攒起来，loadFinished 后按顺序补执行；
 *        上限 500 条防止页面一直加载不出来时无限攒
 * @param js 要执行的 JavaScript 代码
 */
void MsgWebView::runScript(const QString& js)
{
	if (m_loaded)   // 页面已就绪
	{
		this->page()->runJavaScript(js);   // 直接执行
		return;   // 不必再攒
	}

	if (m_pendingJs.size() < 500)     // 页面一直加载不出来时别无限攒
		m_pendingJs.append(js);       // 攒起来，loadFinished 后补执行
}

// ============================================================================
// registerChannelObject：向 QWebChannel 注册 C++ 对象，供页面 JS 回调
// 注册两类：
//   external0 = 自己（右气泡模板）；
//   external_<账号> = 会话里每个可能的发言者（左气泡模板 + 各自头像）。
// 时序契约：getCreateID() 能拿到非空 id，靠的是本函数在 MsgWebView 构造里
//   执行，而 MsgWebView 是 TalkWindow.ui 的提升控件——整条链都发生在
//   WindowManager::addNewTalkWindow 的 createID 赋值窗口期（new 完就 clear）。
//   谁把调用挪到构造之后，这里就会拿到空串、注册错对象。
// 账号名去掉减号：JS 标识符容不下 '-'（当年按 int 存，大数溢出变负号）。
// ============================================================================
/**
 * @brief 向 QWebChannel 注册 C++ 对象：external0=自己（右气泡模板），external_<账号>=会话里每个可能的发言者
 *        （左气泡模板+各自头像）。时序契约：必须在构造里调用（靠 WindowManager::getCreateID() 拿到非空 talkId），
 *        挪到构造之后就会拿到空串、注册错对象。账号名去掉减号：JS 标识符容不下 '-'。
 */
void MsgWebView::registerChannelObject()
{
	//注册发送消息对象
	rightObj = new MsgHtmlObj(this);            // 右气泡模板（自己发的）
	channel->registerObject("external0", rightObj);   // 注册为 external0

	//注册接收消息对象
	// talkId 只在"正在创建本窗口"的窗口期内有效，见上方时序契约
	const QString talkId = WindowManager::instance()->getCreateID();   // 拿正在创建的窗口 id
	QList<QPair<QString, QString>> infoList;   // 存每个发言者的 (账号, 头像路径)
	if (!UseMySQL::instance()->getRegisterObjectInfo(talkId, infoList))   // 查库失败
	{
		QMessageBox::information(nullptr, "error", QString("getRegisterObjectInfo Error."));   // 弹窗告知
		return;   // 放弃注册，页面侧对应函数会走兜底
	}

	for (auto& info : infoList)   // 遍历每个发言者
	{
		QString account = info.first;    // 账号
		QString headPath = info.second;  // 头像路径

		MsgHtmlObj* leftObj = new MsgHtmlObj(this, headPath);   // 左气泡模板（对方发的），带各自头像
		QString objName = QString("external_%1").arg(account);  // 拼对象名 external_<账号>
		objName.remove('-');   // JS 标识符不能含减号（账号当年按 int 处理，大数溢出会变负号）
		channel->registerObject(objName, leftObj);   // 注册到通道
	}
}

// ============================================================================
// parseMsgFromJson：把"块数组的数组"拍平成一段可直接进 HTML 的字符串
// 协议形状（和 define.h 里的 TXT/IMG 宏配套）：
//   [ [ {TXT:"你好"}, {IMG:"smile"} ], [ {TXT:"第二行"} ] ]
//   外层循环 = 一条消息里的多个换行块，内层 = 每块里的文本/图片片段；
//   图片不存原始二进制，只存 qrc 里的表情名，拼成 <img src="qrc:...">；
//   每个块末尾补 <br>——JS 侧按 HTML 插入，不补换行用户消息会挤成一行。
// ============================================================================
/**
 * @brief 把"块数组的数组"拍平成一段可直接进 HTML 的字符串。
 *        协议形状：[ [ {TXT:"你好"}, {IMG:"smile"} ], [ {TXT:"第二行"} ] ]；
 *        外层循环=一条消息里的多个换行块，内层=每块里的文本/图片片段；
 *        图片不存原始二进制，只存 qrc 里的表情名；每块末尾补 <br> 防挤成一行。
 * @param arr 块数组的数组，每块是若干 {TXT:...} 或 {IMG:...} 对象
 * @return QString 拼好的 HTML 片段
 */
QString MsgWebView::parseMsgFromJson(const QJsonArray & arr)
{
	QString msg;          //消息字符串
	for (int i = 0; i < arr.count(); i++)   // 遍历每个换行块
	{
		QJsonArray blockArray = arr[i].toArray();         //通过下标获取文本块json数组
		for (int j = 0; j < blockArray.count(); j++)      //组装文本块
		{
			QJsonObject obj = blockArray[j].toObject();   //片段json对象
			QString key = obj.keys().at(0);               // 取第一个 key（TXT 或 IMG）
			if (key == TXT)   // 文本片段
			{
				msg += obj[key].toString();        //文本
			}
			else if (key == IMG)   // 图片/表情片段
			{
				QString value = QString(IMG_SRC).arg(IMG_QRC + obj[key].toString() + IMG_PNG);   // 拼 <img src="qrc:...">
				msg += value;   // 追加到消息
			}
		}
		msg += BR;          //换行
	}

	return msg;   // 返回拼好的 HTML
}

/**
 * @brief AI 流式回复：调页面里的 streamAppend 函数追加增量。
 *        delta 需要 JS 字符串转义（反斜杠/单引号/换行），否则会破坏 runJavaScript 的字符串字面量。
 * @param sender 发送者标识（JS 侧用于区分气泡归属）
 * @param delta 本次增量文本
 * @param done true=流结束
 */
void MsgWebView::onStreamDelta(const QString& sender, const QString& delta, bool done)
{
	auto escapeJs = [](const QString& in) {   // lambda：把文本转义成可安全嵌入 JS 字符串的形态
		QString r = in;                                     // 拷贝一份，不改原参数
		r.replace(QChar(92), QString::fromUtf8("\\\\"));  // backslash first
		r.replace(QChar(39), QString::fromUtf8("\\'"));   // single quote
		r.remove(QChar(13));                              // 去掉回车
		r.replace(QChar(10), QString::fromUtf8("\\n"));   // LF
		return r;                                         // 返回转义后的字符串
	};
	runScript(
		QString("streamAppend('%1', '%2', %3)")
			.arg(escapeJs(sender), escapeJs(delta))   // 转义后嵌入
			.arg(done ? "true" : "false"));           // done 转成 JS 布尔字面量
}
