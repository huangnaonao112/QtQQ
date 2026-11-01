#pragma once

#include <QObject>
#include <qwebenginepage.h>
#include <qwebengineview.h>
#include <qjsonvalue.h>

/**
 * @brief 注册给 WebChannel 的"消息通道对象"：拿着己方登录账号（右气泡头像）和对方头像路径，
 *        把 userLeft/userRight 两套 HTML 模板里 %1 占位的头像 URL 替换成真实路径。
 *        parent 必须是 MsgWebView——通道对象的生命周期跟页面走，页面销毁它必须跟着死，
 *        否则 QWebChannel 里留个悬空注册项。
 */
class MsgHtmlObj : public QObject
{
	Q_OBJECT

	Q_PROPERTY(QString msgRHtmlTmpl MEMBER msgRight NOTIFY msgHtmlChanged)   // 右气泡模板（自己发的），暴露给 JS
	Q_PROPERTY(QString msgLHtmlTmpl MEMBER msgLeft NOTIFY msgHtmlChanged)    // 左气泡模板（对方发的），暴露给 JS

public:
	/**
	 * @brief 构造：把左右气泡模板从 HTML_PATH 加载进来，并把 %1 占位替换成真实头像路径。
	 *        parent 必须是 MsgWebView（生命周期跟页面走）。
	 * @param parent 父对象，必须传 MsgWebView
	 * @param headPath 对方头像路径（左气泡用）；传空则只填右气泡
	 */
	MsgHtmlObj(QObject *parent, QString headPath = QString());
	/**
	 * @brief 析构：模板字符串是成员，随对象释放，无需手动清理
	 */
	~MsgHtmlObj();

signals:
	/**
	 * @brief 模板变化通知：Q_PROPERTY 要求 NOTIFY 信号存在，这里只是抑制 moc 警告，实际很少 emit
	 * @param msgHtml 变化的模板内容
	 */
	void msgHtmlChanged(const QString& msgHtml);      //抑制警告

private:
	/**
	 * @brief 加载左右两套气泡模板并把 %1 换成头像路径：右边是自己（登录账号查库），左边是对方
	 */
	void initMsgHtml();
	/**
	 * @brief 从资源/磁盘读一个 HTML 模板的全文；读失败弹窗报错并返回空串——
	 *        空模板会让页面渲染出空白气泡，但比静默失败好排查
	 * @param fileName 模板名（不含路径后缀），如 "userLeft"、"userRight"
	 * @return QString 模板全文；读失败返回空串
	 */
	QString getMsgHtml(const QString& fileName);

private:
	QString msgRight;   // 右气泡模板（自己发的），%1 已换成自己的头像路径
	QString msgLeft;    // 左气泡模板（对方发的），%1 已换成对方头像路径
	QString leftHeadPath;         //左边用户头像（接收消息的头像）
};

/**
 * @brief 消息渲染页的 QWebEnginePage 子类：只重写 acceptNavigationRequest，
 *        放行 qrc/file/about:blank，其余 scheme 拒绝，防止聊天区被当作浏览器跳转
 */
class MsgWebPage : public QWebEnginePage
{
	Q_OBJECT

public:
	/**
	 * @brief 构造：空实现，只是个放行 qrc/file 导航的 QWebEnginePage
	 * @param parent 父对象（一般传 MsgWebView）
	 */
	MsgWebPage(QObject* parent) : QWebEnginePage(parent)
	{
	}

protected:
	/**
	 * @brief 重写导航请求：允许 qrc（课程原始方案）与 file（现在页面从磁盘加载）以及 about:blank，
	 *        其余 scheme 拒绝，防止聊天区被当作浏览器跳转到外部页面
	 * @param url 即将导航到的 URL
	 * @param type 导航类型（链接点击/表单提交等，这里不区分）
	 * @param isMainFrame 是否是主框架导航
	 * @return true=允许导航；false=拒绝
	 */
	bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override
	{
		if (url.scheme() == "qrc" || url.scheme() == "file" || url == QUrl("about:blank"))
			//允许 qrc（课程原始方案）与 file（现在页面从磁盘加载），其余 scheme 拒绝，防止聊天区被当作浏览器跳转
		{
			return true;          //允许导航
		}
		return false;             // 拒绝其余 scheme
	}

};

/**
 * @brief 消息渲染页（QWebEngineView）：整个聊天内容区其实是个 WebEngine 页面。
 *        三个关键契约：
 *        1) 页面从临时目录 QtQQ_MsgHtml/msgPage.html 加载，必须开 LocalContentCanAccess* 两个开关；
 *        2) QWebChannel 注册 external0（自己）+ external_<账号>（每个会话对象），JS 靠这些对象名回传；
 *        3) 页面 loadFinished 前到达的消息先攒在 m_pendingJs，加载完按序补执行。
 */
class MsgWebView : public QWebEngineView
{
	Q_OBJECT

public:
	/**
	 * @brief 消息渲染页构造：搭建页面（临时目录 msgPage.html）、开本地访问权限、注册 QWebChannel 通道对象、
	 *        loadFinished 后补执行攒下的脚本。发送信号不在这连（必须带 talkId），由 TalkWindow 构造时接线。
	 * @param parent 父窗口指针（一般传 TalkWindow.ui 里的提升控件容器）
	 */
	MsgWebView(QWidget* parent = nullptr);
	/**
	 * @brief 析构：rightObj/channel 都以本对象为 parent，随对象树自动释放
	 */
	~MsgWebView();

	/**
	 * @brief 聊天中的一条新消息：渲染上屏 + 回传服务端（emitSend=true），统一入口是 renderMsg
	 * @param jsonMsg 协议 JSON 数组（块数组的数组）
	 * @param obj 对方账号；传 MYSELF 表示自己发的（右气泡）
	 */
	void appendMsg(const QJsonArray& jsonMsg, QString obj);
	/**
	 * @brief 渲染一条历史消息：只画气泡不回发服务端（不然每次打开窗口都会把历史重发一遍）。
	 *        自己发的那些直接走右气泡，对方发的走对应账号的定向气泡函数。
	 * @param jsonMsg 协议 JSON 数组
	 * @param sender 发送者账号（isSelf=true 时忽略）
	 * @param isSelf true=自己发的历史消息（右气泡）；false=对方发的
	 */
	void appendHistoryMsg(const QJsonArray& jsonMsg, const QString& sender, bool isSelf);
	/**
	 * @brief AI 流式回复的一段增量：调页面里的 streamAppend 函数追加；delta 需要 JS 字符串转义。
	 * @param sender 发送者标识（JS 侧用于区分气泡归属）
	 * @param delta 本次增量文本
	 * @param done true=流结束
	 */
	void onStreamDelta(const QString& sender, const QString& delta, bool done);

signals:
	/**
	 * @brief 页面 JS 发消息的回传口；由 TalkWindow 接走并带上自己的 talkId，
	 *        千万别让 shell 按"当前窗口"猜路由（多开会发错人）
	 * @param data 页面传回的 JSON 数据
	 */
	void signalSendMsg(QJsonValue data);

private:
	/**
	 * @brief appendMsg/appendHistoryMsg 的共同实现：把 JSON 数组拼成 HTML 后交给页面 JS 画气泡
	 * @param jsonMsg 协议 JSON 数组
	 * @param obj 对方账号；传 MYSELF 表示自己发的
	 * @param emitSend true=渲染后还要回发服务端落库；false=只是回放历史
	 */
	void renderMsg(const QJsonArray& jsonMsg, const QString& obj, bool emitSend);
	/**
	 * @brief 页面还没加载完时先攒着：历史消息/离线消息常常在窗口刚建好就到，
	 *        这时候 runJavaScript 打在空文档上，消息就丢了；加载完按序补执行
	 * @param js 要执行的 JavaScript 代码
	 */
	void runScript(const QString& js);
	/**
	 * @brief 注册通道对象：external0=自己（右气泡），external_<账号>=会话里每个可能的发言者（左气泡+各自头像）。
	 *        时序契约：靠 WindowManager::getCreateID() 拿到非空 talkId，必须在构造里调用，
	 *        谁把调用挪到构造之后就会拿到空串、注册错对象。
	 */
	void registerChannelObject();
	/**
	 * @brief 协议 JSON → HTML 字符串（和 TalkWindow::parseDocumentToJson 互为逆运算）：
	 *        外层循环=一条消息里的多个换行块，内层=每块里的文本/图片片段；每块末尾补 <br>
	 * @param arr 块数组的数组，每块是若干 {TXT:...} 或 {IMG:...} 对象
	 * @return QString 拼好的 HTML 片段
	 */
	QString parseMsgFromJson(const QJsonArray& arr);

private:
	MsgHtmlObj* rightObj;          //发送消息的对象
	QWebChannel* channel;          //与网页进行通信的通道
	bool m_loaded = false;         //页面是否加载完成
	QStringList m_pendingJs;       //加载完成前攒下的脚本
};
