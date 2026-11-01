#include "stdafx.h"
#include "TalkWindow.h"
#include "WindowManager.h"
#include <qtooltip.h>
#include <qjsonarray.h>
#include <qtextcursor.h>
#include <qtextobject.h>
#include <qjsonobject.h>
#include <qdebug.h>
#include "UseMySQL.h"
#include <qmessagebox.h>
#include "RootContactItem.h"
#include "ContactItem.h"
#include "CommonUtils.h"
#include "WindowManager.h"
#include "SendFile.h"

extern QString LoginUserAccount;      // 定义与 CCMainWindow.cpp（当前登录账号）

// ============================================================================
// 构造函数：创建一个会话聊天窗并立刻注册进 WindowManager
// 两个时序点必须记住：
//   1) 先 addWindowMap 再 init——注册早于任何信号接线，外部拿到本指针时
//      它已是"可被 findWindowMap 找到"的合法状态；
//   2) parent 是 TalkWindowShell：析构时由对象树带走，同时本类析构里
//      注销 map，注册/注销成对，壳关窗不会留悬空指针。
// talkId 是 const 成员：一个窗一辈子只服务一个会话，构造后不可换。
// ============================================================================

/**
 * @brief 构造函数：创建一个会话聊天窗并立刻注册进 WindowManager。
 *
 * 两个时序点必须记住：
 *   1) 先 addWindowMap 再 init——注册早于任何信号接线，外部拿到本指针时
 *      它已是"可被 findWindowMap 找到"的合法状态；
 *   2) parent 是 TalkWindowShell：析构时由对象树带走，同时本类析构里
 *      注销 map，注册/注销成对，壳关窗不会留悬空指针。
 * talkId 是 const 成员：一个窗一辈子只服务一个会话，构造后不可换。
 * @param id 群号（4 位）或对方账号
 * @param parent 必须是 TalkWindowShell（构造时传入的约定，见 WindowManager::addNewTalkWindow）
 */
TalkWindow::TalkWindow(const QString& id, QWidget *parent)
	: QWidget(parent), talkId(id)        // 基类构造 + const 成员 talkId 就地初始化
{
	ui.setupUi(this);                    // 生成 ui 控件并绑定到本窗

	WindowManager::instance()->addWindowMap(talkId, this);   // 先注册，保证外部随时 findWindowMap 可查
	init();                              // 再初始化样式/接线/分栏内容
}

/**
 * @brief 析构：把自己从注册表摘掉。
 *
 * shell 关窗会批量 delete 所有 TalkWindow，不注销的话 findWindowMap
 * 还能查到已死的指针，下次切换就用野指针了。
 */
TalkWindow::~TalkWindow()
{
	WindowManager::instance()->removeWindowMap(talkId);   // 注销，与构造里的注册成对
}

/**
 * @brief 三步初始化的总入口，顺序有意：样式 → 信号 → 内容。
 *
 * 先摆样式（分栏/焦点），再接信号（接线时控件必须已就位），
 * 最后按群/单聊填充内容（依赖前面两步的布局）。
 */
void TalkWindow::init()
{
	initStyle();                         // 分栏比例 + 焦点
	initConnect();                       // 按钮/信号接线
	initTalkWindow();                    // 群/单聊分流填充
}

/*
void TalkWindow::initTalkType()
{
	if (QString::number(talkId).length() <= GROUP_ID_LENGTH)     //群聊
		isGroupTalk = true;
	else
		isGroupTalk = false;
}
*/

// 上面这段 initTalkType 是废弃的老实现：判断群/单聊的逻辑已并入
// initTalkWindow（直接按 talkId 长度分流），留着只为考古，不要恢复。

/**
 * @brief 初始样式：输入区(600) : 消息区(138) 的分栏比例，光标直接落在输入框。
 *
 * 打开聊天窗第一件事就是打字，省一次点击。
 */
void TalkWindow::initStyle()
{
	ui.bodySplitter->setSizes(QList<int>{600, 138});   // 分栏左 600 右 138
	ui.textEdit->setFocus();             // 设置焦点到输入框
}

// ============================================================================
// initConnect：把本窗的按钮接到"壳"和"自己"的槽上
// 为什么最小化/关闭接 parent()（即 TalkWindowShell）而不是本窗：
//   关掉单个聊天窗不该退出程序、也不该只藏自己——壳才是窗口生命周期的
//   拥有者，最小化/关闭都得以壳为单位（parent() 为 TalkWindowShell，
//   这是构造时传进来的约定，见 WindowManager::addNewTalkWindow）。
// 表情按钮同样接壳：表情面板是壳级的弹出层，所有会话共用一个。
// 但发送走带 talkId 的重载：见下方 lambda 的注释。
// ============================================================================

/**
 * @brief 把本窗的按钮接到"壳"和"自己"的槽上。
 *
 * 最小化/关闭/表情按钮接 parent()（壳）——壳才是窗口生命周期的拥有者，
 * 关单个聊天窗不该退出程序也不该只藏自己；表情面板是壳级弹出层，所有
 * 会话共用。但发送走带 talkId 的重载，避免多开会话时发错人。
 */
void TalkWindow::initConnect()
{
	//TalkWindow构造时，parent()为TalkWindowShell
	connect(ui.sysmin, SIGNAL(clicked()), parent(), SLOT(onMin()));        // 最小化 → 壳的 onMin
	connect(ui.sysclose, SIGNAL(clicked()), parent(), SLOT(onClose()));    // 标题栏关闭 → 壳的 onClose
	connect(ui.closeBtn, SIGNAL(clicked()), parent(), SLOT(onClose()));    // 关闭按钮 → 壳的 onClose

	connect(ui.faceBtn, SIGNAL(clicked()), parent(), SLOT(onEmotionBtnClicked()));   // 表情按钮 → 壳切换表情面板显隐
	connect(ui.sendBtn, &QToolButton::clicked, this, &TalkWindow::onSendBtnClicked); // 发送按钮 → 本窗发送链路

	// 自己发的消息：带上本窗口的 talkId 交给 shell 发送。
	// 不能让 shell 去猜"当前窗口是谁"——多开几个会话时会发错人。
	connect(ui.msgWidget, &MsgWebView::signalSendMsg, this, [this](QJsonValue data) {   // 按值捕 this（成员访问）
		TalkWindowShell* shell = WindowManager::instance()->getTalkWindowShell();      // 拿聊天主框架
		if (shell)                      // 壳存在才发（关窗竞态下可能为空）
			shell->sendMsgToServer(talkId, data);   // 带 talkId 的重载，路由不会错人
	});

	connect(ui.treeWidget, &QTreeWidget::itemDoubleClicked, this, &TalkWindow::onTreeItemDoubleClicked);   // 双击成员树
	connect(ui.fileopenBtn, &QPushButton::clicked, this, &TalkWindow::onFileOpenBtnClicked);   // 文件按钮
}

/**
 * @brief 按 id 长度分流群/单聊。
 *
 * 群号固定 4 位 ≤ GROUP_ID_LENGTH，单聊是完整账号；和
 * WindowManager::addNewTalkWindow 用的是同一套判定，两处必须保持一致。
 */
void TalkWindow::initTalkWindow()
{
	if (talkId.length() <= GROUP_ID_LENGTH)     // 群聊（4 位群号）
		initGroupTalk();
	else                                                         // 单聊（对方账号）
		initSingleTalk();
}

// ============================================================================
// initGroupTalk：搭右侧成员树——根项 = "群名-人数"，子项 = 每个成员
// 根/子项用 Qt::UserRole 数据 0/1 区分（onTreeItemDoubleClicked 靠它判断
// 双击的是不是成员：只有成员才开单聊，双击根项不动作）。
// 查库失败直接弹窗 return：成员树空了顶多难看，硬着头皮往下跑
//   （addTopLevelItem 之后 infoList 为空也只是没有子项，但错误必须暴露出来）。
// ============================================================================

/**
 * @brief 搭右侧成员树——根项 = "群名-成员数"，子项 = 每个成员（头像/姓名/签名现场查库）。
 *
 * 根/子项用 Qt::UserRole 数据 0/1 区分（onTreeItemDoubleClicked 靠它判断
 * 双击的是不是成员：只有成员才开单聊，双击根项不动作）。
 * 查库失败直接弹窗 return：错误必须暴露出来，不硬着头皮往下跑。
 */
void TalkWindow::initGroupTalk()
{
	//根项
	QTreeWidgetItem* rootItem = new QTreeWidgetItem;
	rootItem->setData(0, Qt::UserRole, 0);         // 根项 Qt::UserRole 角色下数据为 0

	//获取群成员信息（同一个账号+头像列表，MsgWebView 注册通道对象也用它）
	QList<QPair<QString, QString>> infoList;
	if (!UseMySQL::instance()->getRegisterObjectInfo(talkId, infoList))   // 查库取成员列表
	{
		QMessageBox::information(nullptr, "error", "getRegisterObjectInfo Error.");   // 查库失败弹窗
		return;                          // 成员树空了顶多难看，但错误必须暴露
	}

	RootContactItem* rootWidget = new RootContactItem(false, ui.treeWidget);   // 群根项无箭头，parent 为树控件
	// 根项文案带成员数，用户一眼能看出群规模，也方便核对查库结果
	rootWidget->setText(QStringLiteral("%1 成员-%2").arg(UseMySQL::instance()->getGroupName(talkId)).arg(infoList.count()));   // 群名-人数

	ui.treeWidget->addTopLevelItem(rootItem);        // 把根项挂到树顶层
	ui.treeWidget->setItemWidget(rootItem, 0, rootWidget);   // 第 0 列用自定义根控件显示

	rootItem->setExpanded(true);            // 展开子项
	for (auto& info : infoList)             // 遍历每个成员
	{
		addMember(rootItem, info);          // 逐个挂到根项下
	}
}

/**
 * @brief 单聊：右侧贴一张固定装饰图，尺寸按图片原尺寸锁死避免拉伸变形。
 */
void TalkWindow::initSingleTalk()
{
	QPixmap pixmap(":/Resources/MainWindow/skin.png");   // 加载装饰图

	ui.widget->setFixedSize(pixmap.size());   // 按原图尺寸锁死，防拉伸变形
	QLabel* label = new QLabel(ui.widget);   // 占位画板
	label->setPixmap(pixmap);                // 贴图
	label->setFixedSize(ui.widget->size());  // 标签与容器等大
}

// ============================================================================
// parseDocumentToJson：输入框的 QTextDocument → 协议 JSON（发送格式的唯一来源）
// 形状约定（MsgWebView::parseMsgFromJson 按同样的形状还原，两边不能改一边）：
//   [ [ {TXT:"你好"}, {IMG:"smile"} ], [ {TXT:"第二行"} ] ]
//   外层一条 = 一个换行块，内层一块 = 块内的文本/表情片段序列。
// 图片只留表情名：把 "qrc:/.../xx.png" 两头的 IMG_QRC/IMG_PNG 剥掉，
//   协议里既不传路径也不传二进制——服务端只存名字，各端 qrc 里有同一套图。
// 文本必须转义 & / <：用户输入 "<b>" 这种，不转义对端渲染时会被当 HTML
//   标签吃掉；软换行符(U+2028)换成 <br>，否则页面上一行会糊到底。
// ============================================================================

/**
 * @brief 把输入框的 QTextDocument 序列化成协议 JSON（发送格式的唯一来源）。
 *
 * 形状约定（MsgWebView::parseMsgFromJson 按同样的形状还原，两边不能改一边）：
 *   [ [ {TXT:"你好"}, {IMG:"smile"} ], [ {TXT:"第二行"} ] ]
 *   外层一条 = 一个换行块，内层一块 = 块内的文本/表情片段序列。
 * 图片只留表情名：把 "qrc:/.../xx.png" 两头的 IMG_QRC/IMG_PNG 剥掉，
 *   协议里既不传路径也不传二进制——服务端只存名字，各端 qrc 里有同一套图。
 * 文本必须转义 & / <：用户输入 "<b>" 这种，不转义对端渲染时会被当 HTML
 *   标签吃掉；软换行符(U+2028)换成 <br>，否则页面上一行会糊到底。
 * @param document 输入框对应的富文本文档
 * @return 协议 JSON 数组
 */
QJsonArray TalkWindow::parseDocumentToJson(QTextDocument * document)
{
	//遍历文档以查找和检索内容
	QTextCursor cursor(document);
	cursor.movePosition(QTextCursor::Start);         // 将游标移动到文档开头

	QJsonArray msgArray;            // msg json 数组（外层=换行块）

	//检索文档内所有的文本块
	while (!cursor.isNull())        // 游标为空，退出循环
	{
		QTextBlock block = cursor.block();           // 当前文本块
		QTextBlock::Iterator it;                     // 文本块迭代器

		QJsonArray blockArray;      // 文本块 json 数组（msgArray 的元素）
		for (it = block.begin(); !(it.atEnd()); it++)   // 遍历块内每个片段
		{
			QTextFragment fragment = it.fragment();  // 取当前片段
			if (fragment.isValid())                  // 忽略空片段
			{
				if (fragment.charFormat().isImageFormat())        // 当前片段的文本格式是图像格式
				{
					QTextImageFormat imageFormat = fragment.charFormat().toImageFormat();                 // 获取图像格式信息
					// 剥掉 qrc 前缀和 .png 后缀，只把表情名进协议（见上方契约说明）
					QJsonValue value = imageFormat.name().replace(IMG_QRC, QString()).replace(IMG_PNG, QString());           // 获取图片名称

					blockArray.append(QJsonObject{ {IMG, value} });          // 将 image 对象插入文本块数组内
				}
				else        // 该片段是文本
				{
					QString text = fragment.text();         // 获取片段的纯文本
					// 转义顺序有讲究：先 & 后 < ——若先转 < 产生的 &lt 里的 & 会被后一步
					// 再转一遍变成 &amplt，双重转义在对端显示成字面 "&amplt"
					text.replace("&", "&amp");              // 先转义 &，防止二次污染
					text.replace("<", "&lt");               // 不把<>识别为 html 标签
					text.replace((QChar)8232, BR);          // 如果有软换行符转出的字符，则替换为 html 里面的换行符 <br>

					QJsonValue value = text;                // 包成 JSON 值
				
					blockArray.append(QJsonObject{ {TXT, value} });   // 插入 TXT 对象
				}
			}
		}

		msgArray.append(blockArray);     // 一个换行块作为外层一条

		if (!cursor.movePosition(QTextCursor::NextBlock))          // 移动到下一个文本块
		{
			break;                       // 没有下一块就结束
		}
	}

	//qDebug() << msgArray;

	return msgArray;                     // 交给调用方上屏/发送
}

// ============================================================================
// addMember：往群成员树挂一个成员节点
// UserRole 存 1（标记"这是子项"）、UserRole+1 存账号——双击事件靠这两段
//   数据区分根/成员并取到该跟谁单聊（见 onTreeItemDoubleClicked）。
// 姓名/签名现场查库：成员数据只在建树这一刻用一次，不值得再维护一份缓存。
// ============================================================================

/**
 * @brief 往群成员树挂一个成员节点（头像/姓名/签名都现场查库）。
 *
 * UserRole 存 1（标记"这是子项"）、UserRole+1 存账号——双击事件靠这两段
 * 数据区分根/成员并取到该跟谁单聊。姓名/签名现场查库：成员数据只在建树
 * 这一刻用一次，不值得再维护一份缓存。
 * @param rootItem 根节点（"群名-人数"公司项）
 * @param info 成员信息对：first=账号，second=头像路径
 */
void TalkWindow::addMember(QTreeWidgetItem * rootItem, const QPair<QString, QString>& info)
{
	//子项
	QTreeWidgetItem* childItem = new QTreeWidgetItem;
	childItem->setData(0, Qt::UserRole, 1);                  // 子项 UserRole 角色数据为 1
	childItem->setData(0, Qt::UserRole + 1, info.first);     // 子项保存用户 id

	ContactItem* childWidget = new ContactItem(ui.treeWidget);   // 成员行自定义控件

	//头像：先用 mask 圆角裁剪再上（和侧边栏会话项同一套处理）
	QPixmap src(info.second);                 // 头像原图
	QPixmap mask(":/Resources/MainWindow/head_mask.png");   // 圆角蒙版
	childWidget->setHeadPixmap(CommonUtils::getRoundedPixmap(src, mask, childWidget->getHeadLabelSize()));   // 圆角后上头像

	//用户名
	childWidget->setUserName(UseMySQL::instance()->getUserName(info.first));   // 按账号查库取昵称
	//签名
	childWidget->setSignName(UseMySQL::instance()->getUserSign(info.first));   // 按账号查库取个性签名

	rootItem->addChild(childItem);           // 挂到根项下
	ui.treeWidget->setItemWidget(childItem, 0, childWidget);   // 第 0 列用成员控件显示
}

/**
 * @brief 顶部标题：每次调用都重设样式和对齐再放文本。
 *
 * setStyleSheet 是覆盖不是叠加，必须和 setText 一起走，别拆开调。
 * @param name 标题文本（群名 或 部门+姓名）
 */
void TalkWindow::setWindowName(const QString & name)
{
	ui.nameLabel->setStyleSheet("color: rgb(112, 114, 113); font: bold;");       // 设置字体颜色、加粗
	ui.nameLabel->setAlignment(Qt::AlignCenter);        // 设置字体垂直和水平居中
	ui.nameLabel->setText(name);                        // 刷新标题文本
}

/**
 * @brief 表情面板点选回调：先把焦点还给输入框再插图。
 *
 * 点表情时焦点在面板那边，不抢回来的话插图后面接着打字会打到别处，
 * 用户也看不到光标落在哪（插入位置就是当前光标处）。
 * @param name 表情资源编号
 */
void TalkWindow::addEmotionImage(int name)
{
	ui.textEdit->setFocus();               // 先抢回焦点，保证插入位置是可见光标处
	ui.textEdit->addEmotionHtml(name);     // 按编号插入表情 HTML
}

/**
 * @brief 一行 getter：本窗对应的群号/对方账号（shell 路由消息、关窗时都靠它）。
 * @return talkId 字符串
 */
QString TalkWindow::getTalkId()
{
	return talkId;                         // 返回 const 成员
}

/**
 * @brief 收到一条新消息（本窗对应会话）：obj 是发言者账号（MYSELF=自己），转给 WebView 渲染。
 *
 * appendMsg 内部 emitSend=true 意味着还会回发服务端落库，所以这条链路
 * 只用于"实时消息"，历史回放必须走下面的 appendHistoryMsg。
 * @param jsonMsg 消息 JSON 数组
 * @param obj 发言者账号
 */
void TalkWindow::appendObjectMsg(const QJsonArray & jsonMsg, QString obj)
{
	ui.msgWidget->appendMsg(jsonMsg, obj);  // 渲染上屏 + 回发服务端
}

/**
 * @brief 历史消息便捷入口：服务端下发的纯文本现场包成单块单片段 TXT 协议结构。
 *
 * sender==登录账号 决定右气泡还是左气泡；emitSend=false——历史再回发
 * 一遍就重复落库了。
 * @param sender 发送者账号
 * @param text 消息正文
 */
void TalkWindow::appendHistoryMsg(const QString & sender, const QString & text)
{
	QJsonArray msgArray;                   // 协议外层
	msgArray.append(QJsonArray{ QJsonObject{ {TXT, text} } });   // 单块单片段 TXT
	ui.msgWidget->appendHistoryMsg(msgArray, sender, sender == LoginUserAccount);   // 自己发的走右气泡
}

/**
 * @brief 成员树双击：只有成员子项（UserRole==1）才开单聊窗。
 *
 * 根项双击不响应，且不能跟自己聊（否则会给自己开个会话窗口）。
 * @param item 被双击的树节点
 */
void TalkWindow::onTreeItemDoubleClicked(QTreeWidgetItem * item)
{
	bool isChild = item->data(0, Qt::UserRole).toBool();   // 读 UserRole 判断是不是成员子项

	if (isChild)                          // 只有成员子项才开单聊
	{
		QString talkId = item->data(0, Qt::UserRole + 1).toString();   // UserRole+1 取账号
		if (talkId == LoginUserAccount) return;            // 不能与自己聊天

		WindowManager::instance()->addNewTalkWindow(talkId);   // 开/切到与该账号的单聊窗
	}
}

/**
 * @brief 文件按钮：new 出来就不管了——SendFile 自带 WA_DeleteOnClose。
 *
 * 点发送/关闭自动销毁，这里绝不能持指针等它回来。
 */
void TalkWindow::onFileOpenBtnClicked()
{
	SendFile* sendFile = new SendFile(this);   // 弹发送文件窗，parent 仅用于定位
	sendFile->show();                      // 显示（关闭即销毁，无须 delete）
}

// ============================================================================
// onSendBtnClicked：发送主链路 = 两道校验 → 序列化 → 上屏 → 清输入框
// 两道校验（空消息、超 MSG_LENGTH）都用 QToolTip 原地提示，不弹模态框——
//   发错字是高频小失误，弹框打断感太强。
// 真正"发出去"不在这里：appendMsg 里 emitSend=true 会发 signalSendMsg，
//   由 initConnect 里带 talkId 的 lambda 交给 shell 发服务端。
//   上屏先于发送完成，服务端挂了消息也会显示——IM 常见的乐观更新。
// ============================================================================

/**
 * @brief 发送主链路 = 两道校验 → 序列化 → 上屏 → 清输入框。
 *
 * 两道校验（空消息、超 MSG_LENGTH）都用 QToolTip 原地提示，不弹模态框——
 * 发错字是高频小失误，弹框打断感太强。真正"发出去"不在这里：
 * appendMsg 里 emitSend=true 会发 signalSendMsg，由 initConnect 里带
 * talkId 的 lambda 交给 shell 发服务端；上屏先于发送完成，服务端挂了
 * 消息也会显示——IM 常见的乐观更新。
 */
void TalkWindow::onSendBtnClicked()
{
	//如果全都是空白字符则不发送
	QString str = ui.textEdit->toPlainText();   // 取输入框纯文本
	if (str.trimmed().isEmpty())            // 全空白/空消息
	{
		// 顺手清掉全空白的输入，免得用户下次还得手动删
		ui.textEdit->clear();               // 清空输入框
		QToolTip::showText(mapToGlobal(QPoint(600, 685)),
			QStringLiteral("发送的信息不能为空！"), this, QRect(0, 0, 120, 100), 2000);   // 原地提示，不弹模态框
		return;                             // 空消息不发送
	}

	//单次消息长度限制
	if (str.length() > MSG_LENGTH)         // 超过单次上限
	{
		QToolTip::showText(mapToGlobal(QPoint(600, 685)),
			QStringLiteral("发送的信息长度不能大于%1!").arg(MSG_LENGTH), this, QRect(0, 0, 120, 100), 2000);   // 原地提示上限
		return;   // 超长不清输入框：用户还要删减重发，清了反而抓狂
	}

	// 输入框 → 协议 JSON（含表情/换行/转义，见 parseDocumentToJson）
	QJsonArray msgArray = parseDocumentToJson(ui.textEdit->document());   // 序列化成协议结构

	//将json消息添加到html（MYSELF=右气泡；内部会回发服务端）
	ui.msgWidget->appendMsg(msgArray, MYSELF);    // 上屏 + 回发服务端
	ui.textEdit->clear();                 // 发送成功后清空输入框
}

/**
 * @brief autotest 入口：填入输入框后走正常发送链路（onSendBtnClicked）。
 * @param text 要发送的文本
 */
void TalkWindow::sendTestMsg(const QString& text)
{
	ui.textEdit->setPlainText(text);      // 灌入输入框
	onSendBtnClicked();                   // 复用正常发送链路（校验+序列化+上屏）
}

/**
 * @brief AI 流式回复：把增量文本转给消息渲染控件。
 * @param sender 发送者
 * @param delta 本次增量文本
 * @param done 是否已结束（结束时做收尾渲染）
 */
void TalkWindow::onStreamDelta(const QString& sender, const QString& delta, bool done)
{
	ui.msgWidget->onStreamDelta(sender, delta, done);   // 交给 WebView 流式渲染
}
