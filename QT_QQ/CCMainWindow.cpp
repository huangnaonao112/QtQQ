#include "stdafx.h"
#include "CCMainWindow.h"
#include <qpainter.h>
#include <qdebug.h>
#include <qtimer.h>
#include <QCoreApplication>
#include "UseMySQL.h"
#include "CommonUtils.h"
#include "SkinWindow.h"
#include "NotifyManager.h"
#include <QTreeWidgetItem>
#include "RootContactItem.h"
#include "SysTrayIcon.h"
#include "AutoTest.h"

//全局变量：当前登录账号。
//MsgPage / TalkWindow / TalkWindowShell 等模块 extern 引用它（发消息时填"发送者"），
//定义（分配存储）只能有一处，就在这里；登录成功后由构造函数赋值
QString LoginUserAccount;   // 全项目 extern 引用的当前登录账号，分配存储在此
#include "ContactItem.h"
//#include "SysTrayIcon.h"
#include "WindowManager.h"
#include "TalkWindow.h"
#include "CustomProxyStyle.h"
// 注：CustomProxyStyle 类已抽到独立头文件 CustomProxyStyle.h，
// 这里不再内联定义，避免和头文件里的类声明重复（之前报 C2011 重定义就是这个原因）

// ============================================================================
// 主界面构造：与 UserLogin 完全同构的三步走
//   (1) setupUi(this)   → 由 CCMainWindow.ui 生成的界面先落地（确定窗口大小）
//   (2) init()          → 加载样式、接线 ui 自带的系统按钮（依赖窗口已存在）
//   (3) account 已由构造参数保存 → 后续查数据库、显示头像都用它
// 顺序不能颠倒的原因和 UserLogin 一样：setupUi 决定窗口尺寸，
// 标题栏要按这个宽度铺满。
// ============================================================================
/**
 * @brief 主界面构造：与 UserLogin 完全同构的三步走——setupUi 落地界面（确定窗口大小）→
 *        init() 加载样式/接线/建联系人树 → 账号存成员变量供后续查库。
 *        顺序不能颠倒：setupUi 决定窗口尺寸，标题栏要按这个宽度铺满。
 * @param account 登录成功的账号字符串（工号），由 UserLogin 在登录成功时传入
 * @param parent 父窗口指针，一般传 nullptr（主界面是顶层窗口）
 */
CCMainWindow::CCMainWindow(const QString& account, QWidget* parent)
	: BasicWindow(parent),account(account)   // 成员初始化列表：把登录窗传进来的账号存到成员变量
{
	LoginUserAccount = account;   // 登录即记录，供全项目 extern 访问
	ui.setupUi(this);   // 根据 CCMainWindow.ui 创建主界面所有控件（标题栏、搜索框、联系人树等）
	init();             // 加载样式表、初始化按钮、联系人树、信号连接
	setWindowFlags(windowFlags() | Qt::Tool | Qt::WindowStaysOnTopHint);  //不显示任务栏图标，窗口始终显示在最前面
}

/**
 * @brief 析构：界面控件由 ui.setupUi 挂到对象树，随窗口自动析构
 */
CCMainWindow::~CCMainWindow()
{}

// ============================================================================
// init：初始化主界面
//
// 为什么这里【不调用】BasicWindow::initTitleBar（与 UserLogin 的关键区别）：
//   UserLogin.ui 的 titleWidget 是"空容器"，专门留给 initTitleBar 注入
//   TitleBar 控件；而 CCMainWindow.ui 的 titleWidget 里已经自带了整套
//   标题栏内容（左：titleIcon 图标；右：sysmin/sysclose 按钮）。
//   如果再调 initTitleBar 会 new 出第二个标题栏叠在 (0,0) 上，
//   造成图标和系统按钮重影（之前截图的 bug 就是这么来的）。
//   所以主界面的做法是：直接使用 ui 里自带的按钮，把它们连到
//   BasicWindow 提供的窗口控制槽上即可。
// ============================================================================
/**
 * @brief 初始化主界面：开三个容器的 WA_StyledBackground 画 QSS 背景，加载 CCMainWindow.css，
 *        再依次 initButton/setUserName/setUserHeadPixmap/initContact/initConnect/initSysTray。
 *        注意这里不调 initTitleBar（.ui 里已自带标题栏，再调会重影）。
 *        带 --autotest 时额外挂 scheduleAutoTest 钩子。
 */
void CCMainWindow::init()
{
	//与 UserLogin 相同：普通 QWidget 容器要画样式表背景必须开 WA_StyledBackground。
	//注意主界面的 ui 布局里没有 bodyWidget（那是登录窗的命名），
	//中间区域叫 widget，底部叫 bottomWidget，都要单独设置
	ui.titleWidget->setAttribute(Qt::WA_StyledBackground, true);   // 标题栏容器允许画 QSS 背景
	ui.widget->setAttribute(Qt::WA_StyledBackground, true);        // 中间内容区容器允许画背景
	ui.bottomWidget->setAttribute(Qt::WA_StyledBackground, true);  // 底部状态栏容器允许画背景

	loadStyleSheet("CCMainWindow");   // QSS 文件：Resources/QSS/CCMainWindow.css，加载时自动叠加当前皮肤色


	initButton();       // 初始化状态按钮、应用按钮、等级按钮
	setUserName();      // 从数据库查当前账号姓名，显示到 nameLabel
	setUserHeadPixmap();// 从数据库查当前账号头像，圆角裁剪后贴到 headLabel
	initContact();      // 构建联系人树（根节点 + 公司群/部门群子项）
	initConnect();      // 连接信号槽（换肤通知、系统按钮）
	initSysTray();      // 系统托盘图标（最小化到托盘、右键菜单）

	//==== autotest hook：打开指定聊天窗并发一条消息（实现见 AutoTest.cpp）====
	if (QCoreApplication::arguments().contains("--autotest"))   // 命令行带 --autotest 才挂钩子
		scheduleAutoTest(this);   // 挂无人值守冒烟测试钩子

	ui.searchLineEdit->installEventFilter(this);   // 给搜索框安装事件过滤器，监听焦点进出事件
}

/**
 * @brief 初始化左侧/底部的一排状态小图标按钮：先在 statusBtn 上画在线状态图标，
 *        再调 initAppButton 初始化应用小按钮、initLevelButton 初始化等级按钮
 */
void CCMainWindow::initButton()
{
	//状态按钮：在 ui.statusBtn 上绘制"在线状态"图标（绿色对勾小图标）
	drawButtonIcon(ui.statusBtn, ":/Resources/MainWindow/StatusSucceeded.png");   // 画在线状态图标
	initAppButton();    // 初始化中间那排"应用"小按钮（朋友圈、游戏等图标）
	initLevelButton();  // 初始化右下角等级按钮（带定时器动态加等级）
}

// ============================================================================
// initContact：初始化联系人树
//
// 结构：树只有一个根项（"苏州科技大学"，学校名，RootContactItem 带展开箭头动画），
// 根项下的群组子项由 addUserGroup 按数据库查询结果动态挂载。
// UserRole 约定：0=根，1=群组；UserRole+1 存群号（见 addUserGroup）。
// ============================================================================
/**
 * @brief 初始化联系人树：装 CustomProxyStyle 去掉焦点虚线框，建根节点"苏州科技大学"（RootContactItem 带展开箭头动画），
 *        再调 addUserGroup 挂公司群/部门群子项，最后接 itemClicked/DoubleClicked/Expanded/Collapsed 四个信号。
 *        UserRole 约定：0=根，1=群组；UserRole+1 存群号。
 */
void CCMainWindow::initContact()
{
	//改变样式：去掉 QTreeWidget 项获得焦点时的虚线框（PE_FrameFocusRect）
	ui.treeWidget->setStyle(new CustomProxyStyle());   // 代理样式：只影响这棵树，不污染全局

	//根项：QTreeWidgetItem 是"树节点数据载体"，本身不画 UI
	QTreeWidgetItem* rootItem = new QTreeWidgetItem;   // 新建根节点数据载体
	rootItem->setData(0, Qt::UserRole, 0);           //根项 UserRole 角色下存 0，标记"这是根节点"

	//RootContactItem 是贴在根节点上的自定义控件（带展开箭头动画）
	RootContactItem* rootWidget = new RootContactItem(true, ui.treeWidget);   // 带展开箭头动画的根控件
	rootWidget->setText(QStringLiteral("苏州科技大学"));   // 设置根节点标题文字

	//把 rootItem 作为顶层节点插入树
	ui.treeWidget->addTopLevelItem(rootItem);   // 插入顶层节点
	//把 rootWidget 贴到 rootItem 的第 0 列位置（一个节点可以贴一个自定义 widget）
	ui.treeWidget->setItemWidget(rootItem, 0, rootWidget);   // 把自定义控件贴到根节点上

	//子项：往根节点下挂公司群和部门群
	addUserGroup(rootItem);   // 按数据库查询结果挂群组子项

	//连接信号与槽
	connect(ui.treeWidget, &QTreeWidget::itemClicked, this, &CCMainWindow::onTreeItemClicked);                //项被点击：根节点点击时切换展开/折叠
	connect(ui.treeWidget, &QTreeWidget::itemDoubleClicked, this, &CCMainWindow::onTreeItemDoubleClicked);    //项被双击：打开对应聊天窗口
	connect(ui.treeWidget, &QTreeWidget::itemExpanded, this, &CCMainWindow::onTreeItemExpanded);              //项被展开：根节点展开时箭头旋转到 90°
	connect(ui.treeWidget, &QTreeWidget::itemCollapsed, this, &CCMainWindow::onTreeItemCollapsed);            //项被折叠：根节点折叠时箭头旋转回 0°
}

// ============================================================================
// addUserGroup：往联系人树的根项下挂"群组"子项（公司群 + 部门群）
//
// 数据流：getUserGroup 查出当前账号所在的群 id 列表
//   → 每个群 new 一个 QTreeWidgetItem（树节点）+ 一个 ContactItem（树节点上
//   显示的自定义控件，通过 setItemWidget 挂上去）
//   → 头像/群名分别来自 tab_department 表。
//
// UserRole 的分工（Qt 树控件的惯例）：
//   UserRole    存"节点类型"：0=根(公司)、1=群组，点树时用它区分；
//   UserRole+1  存业务数据（群号），双击打开聊天窗口时取出来用。
// ============================================================================
/**
 * @brief 往联系人树根项下挂"公司群+部门群"子节点：每查到一个群 new 一个树节点（UserRole=1、UserRole+1 存群号）
 *        + 一个 ContactItem 卡片贴上去，头像用 CommonUtils::getRoundedPixmap 圆角裁剪。
 * @param rootItem 根节点 QTreeWidgetItem*，子节点挂到它下面
 */
void CCMainWindow::addUserGroup(QTreeWidgetItem * rootItem)
{
	//获取群号   公司群+部门群：从数据库查当前账号属于哪两个群
	QStringList group = UseMySQL::instance()->getUserGroup(account);   // 查当前账号属于哪些群

	//遍历每个群 id，建一个树子节点 + 一个联系人卡片
	for (const QString& id : group)
	{
		QTreeWidgetItem* childItem = new QTreeWidgetItem;   // 新建子节点数据载体
		childItem->setData(0, Qt::UserRole, 1);           //子项 UserRole=1，标记"这是群组节点"
		childItem->setData(0, Qt::UserRole + 1, id);      //UserRole+1 位置存群号 id，双击时取出来用

		ContactItem* childWidget = new ContactItem(ui.treeWidget);   // 群组卡片控件（头像+群名）

		//设置群组头像（圆角裁剪后贴到 ContactItem 的头像槽）
		QString headPath = UseMySQL::instance()->getGroupHeadPixmap(id);   // 从数据库查群头像图片路径
		QPixmap head(headPath);                                             // 加载原图
		QPixmap head_mask(":/Resources/MainWindow/head_mask.png");          // 圆角遮罩
		// CommonUtils::getRoundedPixmap：用 mask 把 head 裁成圆形，尺寸匹配卡片头像 label
		childWidget->setHeadPixmap(CommonUtils::getRoundedPixmap(head, head_mask, childWidget->getHeadLabelSize()));   // 贴圆角群头像

		//设置群组名称：从数据库查群名，写到卡片上
		QString groupName = UseMySQL::instance()->getGroupName(id);   // 从数据库查群名
		childWidget->setUserName(groupName);   // 写到卡片名称 label

		//将子项添加到根项下，并把卡片贴上去
		rootItem->addChild(childItem);   // 挂到根节点下
		ui.treeWidget->setItemWidget(childItem, 0, childWidget);   // 把卡片贴到子节点上
	}
}

// ============================================================================
// initSysTray：创建系统托盘图标
//
// 为什么 new 出来不保存成员指针也不设 parent 之外的引用：
//   QSystemTrayIcon 以 CCMainWindow 为 parent，窗口析构时托盘随之销毁；
//   本函数只负责"创建"，后续交互全部由 SysTrayIcon 内部的信号槽处理，
//   主界面不需要再持有它，避免多一个无意义的成员变量。
// ============================================================================
/**
 * @brief 创建系统托盘图标：以 this 为 parent，窗口析构时托盘随之销毁；
 *        本函数只负责"创建"，后续交互全由 SysTrayIcon 内部信号槽处理，主界面不再持有指针
 */
void CCMainWindow::initSysTray()
{
	SysTrayIcon* icon = new SysTrayIcon(this);   // 以 this 为 parent，随窗口析构
	icon->show();   // 显示托盘图标
}

// ============================================================================
// initConnect：主窗口的信号接线（换肤刷新 + 系统按钮）
// 为什么 sysclose 只接 onQuit 不接 BasicWindow::onClose：主窗点 X 是
//   "退出整个程序"，不是"藏起来"；两条都接会连触两个槽，行为靠顺序碰运气。
// ============================================================================
/**
 * @brief 主窗口信号接线：订阅 NotifyManager::skinChanged 刷新搜索框样式；
 *        sysmin 接基类 onMin，sysclose 只接本类 onQuit（主窗点 X 是退出整个程序，不是藏起来）。
 *        以前 sysclose 还额外连了 BasicWindow::onClose，点一次 X 触发两个槽靠顺序碰运气，现在只保留一条。
 */
void CCMainWindow::initConnect()
{
	/*
	qss层叠样式表优先级：
	1.内联样式（直接在部件上设置的样式）
	2.ID选择器（如#searchWidget）
	3.类选择器（如QWidget）
	4.属性选择器（如[titleskin=true]）
	使用组合选择器时，其优先级比单个选择器的更高（更具体）
*/
	// 换肤通知：SkinWindow 点了颜色后发 skinChanged 信号，这里收到后刷新搜索框样式
	connect(NotifyManager::instance(), &NotifyManager::skinChanged, [this]() { updateSearchStyle(false); });   // 换肤→搜索框刷回半透明
	// 系统按钮：最小化 → 基类 onMin；关闭 → 本类 onQuit（先关聊天窗口再退出）
	// 以前 sysclose 还额外连了 BasicWindow::onClose（只 close 不退出），
	// 等于点一次 X 触发两个槽，靠先后顺序碰巧能退出；现在只保留一条
	connect(ui.sysmin, &QPushButton::clicked, this, &BasicWindow::onMin);   // 最小化按钮 → 基类 onMin
	connect(ui.sysclose, &QPushButton::clicked, this, &CCMainWindow::onQuit);   // 关闭按钮 → 本类 onQuit（退出整个程序）
}

/**
 * @brief 初始化中间"应用"那一排小图标按钮（朋友圈、QQ游戏、皮肤等入口）：
 *        水平布局排列 appList 里的图标按钮到 appWidget，bottomList 里的小按钮到底部 bottomLayout_up，末尾都加弹簧垫片靠左对齐
 */
void CCMainWindow::initAppButton()
{
	//appWidget：中间那排横向排列的应用按钮容器
	QHBoxLayout* appLayout = new QHBoxLayout(ui.appWidget);   // 水平布局，parent 是 appWidget
	appLayout->setContentsMargins(0, 0, 0, 0);          //设置布局四周边距为 0，按钮贴紧边缘
	appLayout->setSpacing(2);                           //设置相邻按钮之间的间距为 2px
	// 要显示的图标列表：对应 QRC 资源里 app_7.png 等图片
	QStringList appList = { "app_7", "app_2", "app_3", "app_4", "app_5", "app_6", "app_skin" };   // 中间排应用图标名
	for (auto app : appList)   // 遍历每个图标名
	{
		// addOtherButton：创建一个带图标的按钮，APP_ICON 宏拼出完整 QRC 路径
		appLayout->addWidget(addOtherButton(APP_ICON(app), app));   // 建按钮并加进水平布局
	}
	appLayout->addStretch();       //在末尾添加一个弹簧垫片，把所有按钮顶到左边

	//bottomLayout：底部那排更小的按钮（设置、查找等）
	QStringList bottomList = { "app_10", "app_8", "app_11", "app_9" };   // 底部排小图标名
	for (auto app : bottomList)   // 遍历每个图标名
	{
		ui.bottomLayout_up->addWidget(addOtherButton(APP_ICON(app), app));   // 建按钮并加进底部布局
	}
	ui.bottomLayout_up->addStretch();   // 末尾加弹簧，按钮靠左对齐
}

/**
 * @brief 初始化右下角"等级"按钮：先画 0 级，再挂一个 2 秒间隔的 QTimer 每次 +1（到 99 回绕），纯演示效果
 */
void CCMainWindow::initLevelButton()
{
	drawLevelButton(0);              //先画 0 级（lv 图标 + 数字 00）
	QTimer* timer = new QTimer(this);   // 定时器，parent=this，随窗口自动析构
	timer->setInterval(2000);        //间隔 2000ms（2 秒）触发一次 timeout
	connect(timer, &QTimer::timeout, [this]()
		{
			static int level = 0;    // static 局部变量：函数调用间保持值，只在第一次初始化为 0
			if (level == 99) level = 0;  //等级上限 99，到顶后回绕到 0
			drawLevelButton(++level);  // 等级+1，重绘数字
		});

	timer->start();         //启动定时器，开始周期性触发
}

/**
 * @brief 在按钮上居中绘制一张图标：创建一块和按钮一样大的透明画布，把 icon 原图画在正中央，设为按钮 icon
 * @param btn 要画图标的按钮引用（传引用是为了内部 setIcon）
 * @param icon 图标图片的 QRC 路径，如 ":/Resources/MainWindow/app/app_7.png"
 */
void CCMainWindow::drawButtonIcon(QPushButton*& btn, const QString& icon)
{
	QPixmap pixmap(btn->size());  //创建一块和按钮一样大的透明画布
	pixmap.fill(Qt::transparent); // 填充透明背景

	QPixmap iconPixmap(icon);     //加载图标原图

	QPainter p(&pixmap);         // 在画布上开画笔
	// 把 icon 原图画在画布正中央：x = (画布宽-图宽)/2，y = (画布高-图高)/2
	p.drawPixmap((pixmap.width() - iconPixmap.width()) / 2, (pixmap.height() - iconPixmap.height()) / 2, iconPixmap);   // 居中绘制

	btn->setIcon(pixmap);            // 把合成好的图设为按钮图标
	btn->setIconSize(btn->size());   // 图标大小 = 按钮大小，铺满
}

/**
 * @brief 画等级按钮：lv 底标 + 十位数字 + 个位数字（用 levelvalue.png sprite 表切片绘制，每个数字 6×7）
 * @param level 当前等级（0~99），十位个位从这个数拆
 */
void CCMainWindow::drawLevelButton(int level)
{
	QPixmap pixmap(ui.levelBtn->size());       // 创建和按钮一样大的透明画布
	pixmap.fill(Qt::transparent);              // 透明底

	QPainter p(&pixmap);   // 在画布上开画笔
	p.drawPixmap(0, 4, QPixmap(":/Resources/MainWindow/lv.png"));    //在(0, 4)处绘制"LV"底标

	int tenNum = level / 10;      //十位数字：整除 10 得到
	int unitNum = level % 10;     //个位数字：模 10 得到

	// levelvalue.png 是一张"数字 sprite 表"：0~9 十个数字横向排列，每个宽 6px、高 7px
	QPixmap levelValue(":/Resources/MainWindow/levelvalue.png");   // 加载数字 sprite 表

	//绘制十位：从 sprite 表的 (tenNum*6, 0) 位置切一块 6x7 的区域画到按钮上
	p.drawPixmap(10, 4, levelValue, tenNum * 6, 0, 6, 7);      //在(10, 4)位置绘制十位，从原图的(tenNum * 6, 0)位置开始，绘制宽6，高7
	//绘制个位：位置往右挪 6px，切 unitNum 对应的数字
	p.drawPixmap(16, 4, levelValue, unitNum * 6, 0, 6, 7);     //在(16, 4)位置绘制个位，从原图的(unitNum * 6, 0)位置开始，绘制宽6，高7

	ui.levelBtn->setIcon(pixmap);          // 把画好的等级图设为按钮图标
	ui.levelBtn->setIconSize(ui.levelBtn->size());   // 图标大小=按钮大小，铺满
}

/**
 * @brief 切换搜索框样式：有焦点=白底+皮肤色下边框+搜索按钮图标换高亮态；无焦点=半透明皮肤色+普通搜索图标
 * @param hasFocus true=搜索框正在输入；false=失去焦点
 */
void CCMainWindow::updateSearchStyle(bool hasFocus)
{
	if (hasFocus)     //有焦点：用户正在输入
	{
		// 内联 QSS：白底 + 皮肤色下边框 + 搜索按钮图标换成"高亮"状态
		ui.searchWidget->setStyleSheet(QString("QWidget#searchWidget{ "
			"background-color: rgb(255, 255, 255);"
			"border-bottom: 1px solid rgba(%1, %2, %3, 100); }"
			"QPushButton#searchBtn{ border-image:url(:/Resources/MainWindow/search/main_search_deldown.png); }"
			"QPushButton#searchBtn:hover{ border-image:url(:/Resources/MainWindow/search/main_search_delhighlight.png); }"
			"QPushButton#searchBtn:pressed{ border-image:url(:/Resources/MainWindow/search/main_search_delhighdown.png); }"
		).arg(skinColor.red()).arg(skinColor.green()).arg(skinColor.blue()));   // 用当前皮肤色 RGB 填到 rgba
	}
	else              //没有焦点：搜索框回到半透明皮肤色
	{
		ui.searchWidget->setStyleSheet(QString("QWidget#searchWidget{"
			"background-color: rgba(%1, %2, %3, 50);"
			"border-bottom: 1px solid rgba(%1, %2, %3, 30); }"
			"QPushButton#searchBtn{ border-image:url(:/Resources/MainWindow/search/search_icon.png); }"
		).arg(skinColor.red()).arg(skinColor.green()).arg(skinColor.blue()));   // 半透明皮肤色
	}
}

/**
 * @brief 把当前登录账号的姓名显示到 nameLabel：从数据库查姓名，用 fontMetrics().elidedText
 *        按 label 宽度末尾省略号截断（Qt::ElideRight），再设白色粗体 17px 样式
 */
void CCMainWindow::setUserName()
{
	QString name = UseMySQL::instance()->getUserName(account);   // 从数据库查姓名
	//QString name = "zzzzzzzzzzzzzzzzzzzzzzzzzzzz";

	//==== 排错临时日志：定位姓名不显示的问题，查清后删除 ====
	qDebug() << "[setUserName] account =" << account
		<< "| getUserName 返回 =" << name
		<< "| nameLabel 宽度 =" << ui.nameLabel->width()
		<< "| 位置 =" << ui.nameLabel->geometry();

	//Qt::ElideRight：文本超出 label 宽度时，在末尾显示省略号"…"
	//fontMetrics() 拿到当前字体的度量信息，elidedText 按 label 宽度截断
	name = ui.nameLabel->fontMetrics().elidedText(name, Qt::ElideRight, ui.nameLabel->width());   // 按 label 宽度截断

	//font: 简写形式 bold 17px：白色粗体 17 号字
	ui.nameLabel->setStyleSheet("color: rgb(255, 255, 255); font: bold 17px");   // 设白色粗体样式
	ui.nameLabel->setText(name);   // 最终显示截断后的姓名
}

/**
 * @brief 加载并显示当前用户头像：从数据库取路径 → 加载原图 → 用 head_mask.png 圆角裁剪 → 贴到 headLabel
 */
void CCMainWindow::setUserHeadPixmap()
{
	QString headPath = UseMySQL::instance()->getUserHeadPixmap(account);   // 数据库里存的头像图片路径

	QPixmap head(headPath);     //原头像
	QPixmap mask(":/Resources/MainWindow/head_mask.png");      //圆形遮罩底片
	// getRoundedPixmap：把 head 按 mask 形状裁成圆形，尺寸匹配 headLabel
	ui.headLabel->setPixmap(CommonUtils::getRoundedPixmap(head, mask, ui.headLabel->size()));   // 贴圆角头像
}

/**
 * @brief 工厂方法：创建一个小图标按钮，统一设置固定大小、objectName（供 QSS #objName 选择器）、
 *        动态属性 hasborder=true，并把 clicked 信号接到本类 onButtonClicked
 * @param icon 图标 QRC 路径
 * @param objName objectName，供 QSS 用 #objName 选择器定位
 * @param size 按钮固定大小，默认 20x20
 * @return QPushButton* 新建好的按钮，由调用方加进布局
 */
QPushButton* CCMainWindow::addOtherButton(const QString& icon, const QString& objName, QSize size)
{
	//新的按钮
	QPushButton* btn = new QPushButton(this);   // 新建按钮，parent 为本窗
	btn->setFixedSize(size);               //设置固定大小（默认 20x20）
	btn->setObjectName(objName);           //设置部件对象名，供 QSS 用 #objName 选择器定位
	btn->setProperty("hasborder", true);   //添加动态属性 hasborder=true，供 QSS 属性选择器用

	drawButtonIcon(btn, icon);             // 在按钮上居中绘制图标

	// 点击任意应用按钮都触发 onButtonClicked（里面根据 objectName 区分是哪个按钮）
	connect(btn, &QPushButton::clicked, this, &CCMainWindow::onButtonClicked);   // 点击→统一槽

	return btn;   // 返回创建好的按钮，由调用方加进布局
}

/**
 * @brief 事件过滤器：监听 searchLineEdit 的 FocusIn/FocusOut，切换搜索框高亮/半透明样式；
 *        其余事件交给基类 BasicWindow::eventFilter 处理
 * @param watched 被监视的对象（这里是 ui.searchLineEdit）
 * @param event 事件对象，用 event->type() 判断是 FocusIn 还是 FocusOut
 * @return true=事件已处理不再传播；false=继续传播（这里 return false 让基类也能处理）
 */
bool CCMainWindow::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == ui.searchLineEdit)   // 只关心搜索框这一个对象
	{
		if (event->type() == QEvent::FocusIn)          //获取焦点：用户点进搜索框
		{
			updateSearchStyle(true);    // 切到高亮样式
			return false;               // return false = 事件继续传播，不拦截
		}
		else if (event->type() == QEvent::FocusOut)     //失去焦点：用户点到别处
		{
			updateSearchStyle(false);   // 切回半透明样式
			return false;               // 事件继续传播
		}
	}

	// 其他事件交给基类处理
	return BasicWindow::eventFilter(watched, event);   // 转发给基类
}

/**
 * @brief 应用按钮点击槽：根据 sender()->objectName() 判断点了哪个，目前只处理"换肤"按钮（app_skin）。
 *        懒加载 SkinWindow 单例（WA_DeleteOnClose 关闭时自动 delete，destroyed 信号置空指针防悬空），
 *        并把它定位在主窗左侧、顶部对齐，再夹一下屏幕范围防止挤出屏幕外。
 */
void CCMainWindow::onButtonClicked()
{
	// sender() 是发出信号的对象，转成 QPushButton* 后看它的 objectName
	if (sender()->objectName() == "app_skin")   // 只处理换肤按钮
	{
		//已有皮肤窗时不再重复创建：点 N 次按钮也只有一个皮肤窗（懒加载单例风格）
		if (!m_skinWindow)   // 还没建过
		{
			m_skinWindow = new SkinWindow;   //WA_DeleteOnClose：关闭时自动 delete
			//关闭即置空指针，防止下次点击拿到"悬空指针"（use-after-free 崩溃）
			connect(m_skinWindow, &QObject::destroyed, this, [this]() { m_skinWindow = nullptr; });   // 销毁即置空
		}

		//定位：贴在主窗口左侧、顶部对齐
		//面板左上角 x = 主窗左边缘 - 面板宽度，正好让面板右边缘贴住主窗左边缘，
		//天然不会和主窗体重叠；再夹一下屏幕范围，防止把面板挤出屏幕外
		QScreen* screen = QGuiApplication::primaryScreen();   // 拿主屏
		QRect avail = screen->availableGeometry();            // 可用工作区（去任务栏）
		int x = this->x() - m_skinWindow->width();            // 贴在主窗左侧
		int y = this->y();                                    // 顶部对齐
		x = qBound(0, x, avail.right() - m_skinWindow->width());   // 夹在屏幕水平范围内
		y = qBound(0, y, avail.bottom() - m_skinWindow->height()); // 夹在屏幕垂直范围内
		m_skinWindow->move(x, y);   // 移到计算好的位置

		m_skinWindow->show();      // 显示皮肤窗
		m_skinWindow->raise();      //已在显示时至少把它提到最前，保证用户点按钮有反馈
		m_skinWindow->activateWindow();   // 激活到前台
	}
}

/**
 * @brief 树节点点击：如果点的是根节点（UserRole=0），切换其展开/折叠状态；群组节点不响应
 * @param item 被点的节点
 */
void CCMainWindow::onTreeItemClicked(QTreeWidgetItem* item)
{
	// UserRole 存的是 0（根）或 1（群组）。toBool() 后 0=false、1=true
	bool isRoot = !(item->data(0, Qt::UserRole).toBool());   // 取反：0→true 表示根节点
	if (isRoot)
	{
		item->setExpanded(!item->isExpanded());   // 切换展开状态（展开↔折叠）
	}
}

/**
 * @brief 树节点双击：双击群组节点（UserRole=1）时，取出 UserRole+1 存的群号/员工 id，
 *        调 WindowManager::addNewTalkWindow 打开（或激活）对应聊天窗口
 * @param item 被双击的节点
 */
void CCMainWindow::onTreeItemDoubleClicked(QTreeWidgetItem * item)
{
	bool isChild = item->data(0, Qt::UserRole).toBool();   // 1=true 表示群组子节点
	if (isChild)
	{
		//按节点里存的群号/员工id打开（或激活）对应聊天窗口
		WindowManager::instance()->addNewTalkWindow(item->data(0, Qt::UserRole + 1).toString());   // 用存的 id 打开聊天窗
	}
}

/**
 * @brief 树节点展开：如果展开的是根节点，取回贴在节点上的 RootContactItem 并把箭头动画转到 90°（朝下）
 * @param item 被展开的节点
 */
void CCMainWindow::onTreeItemExpanded(QTreeWidgetItem * item)
{
	bool isRoot = !(item->data(0, Qt::UserRole).toBool());   // 判断是否根节点
	if (isRoot)
	{
		// itemWidget(item, 0) 拿到贴在节点上的自定义控件，dynamic_cast 成 RootContactItem*
		RootContactItem* rootWidget = dynamic_cast<RootContactItem*>(ui.treeWidget->itemWidget(item, 0));   // 取回根控件
		if (rootWidget)
		{
			rootWidget->setExpanded(true);   // 箭头动画转到 90°（朝下）
		}
	}
}

/**
 * @brief 树节点折叠：根节点折叠时，取回 RootContactItem 并把箭头动画转回 0°（朝右）
 * @param item 被折叠的节点
 */
void CCMainWindow::onTreeItemCollapsed(QTreeWidgetItem * item)
{
	bool isRoot = !(item->data(0, Qt::UserRole).toBool());   // 判断是否根节点
	if (isRoot)
	{
		RootContactItem* rootWidget = dynamic_cast<RootContactItem*>(ui.treeWidget->itemWidget(item, 0));   // 取回根控件
		if (rootWidget)
		{
			rootWidget->setExpanded(false);  // 箭头转回 0°（朝右）
		}
	}
}

/**
 * @brief 关闭按钮：退出整个应用——先关聊天窗口框架（内部各会话窗口随之销毁），再退出事件循环。
 *        顺序不能反：直接 quit 的话 TalkWindowShell 不会走正常的关闭清理
 */
void CCMainWindow::onQuit()
{
	//先关聊天窗口框架（内部各会话窗口随之销毁），再退出事件循环。
	//顺序不能反：直接 quit 的话 TalkWindowShell 不会走正常的关闭清理
	TalkWindowShell* shell = WindowManager::instance()->getTalkWindowShell();   // 拿聊天壳指针
	if (shell)   // 判空：可能还没开过聊天窗
	{
		shell->close();   // 先关聊天壳，触发其正常的关闭清理
	}

	QApplication::quit();   // 终止 Qt 事件循环，程序退出
}
