#pragma once

#include <QWidget>
#include <QTreeWidgetItem>
#include "ui_CCMainWindow.h"
#include "BasicWindow.h"

class SkinWindow;   //前置声明：成员只用指针，无需完整头文件（缩短编译依赖）

// ============================================================================
// 类：CCMainWindow — 登录成功后的主界面（联系人/会话列表）
//
// 框架定位：
//   UserLogin 验证通过 → new CCMainWindow(account) → show()
//   登录窗口把账号传进来，主界面后续用它去数据库查自己的资料、好友列表。
//
// 为什么继承 BasicWindow：
//   和登录窗口同一套路 —— 复用无边框窗口 + 自绘标题栏(TitleBar)，
//   保证登录窗和主界面风格统一（这也是整个项目的窗口框架基础）。
//
// ui_CCMainWindow.h 从哪来：
//   Qt VS Tools 在编译时对 CCMainWindow.ui 跑 uic 工具自动生成，
//   所以项目目录里看不到这个 .h，但 include 和 Ui:: 成员必须写。
// ============================================================================
class CCMainWindow  : public BasicWindow
{
	Q_OBJECT

public:
	/**
	 * @brief 主界面构造：与 UserLogin 完全同构的三步走——setupUi 落地界面（确定窗口大小）→
	 *        init() 加载样式/接线/建联系人树 → 账号存成员变量供后续查库。
	 *        顺序不能颠倒：setupUi 决定窗口尺寸，标题栏要按这个宽度铺满。
	 * @param account 登录成功的账号字符串（工号），由 UserLogin 在登录成功时传入
	 * @param parent 父窗口指针，一般传 nullptr（主界面是顶层窗口）
	 */
	CCMainWindow(const QString& account,QWidget*parent = Q_NULLPTR);
	/**
	 * @brief 析构：界面控件由 ui.setupUi 挂到对象树，随窗口自动析构
	 */
	~CCMainWindow();
private:
	/**
	 * @brief 初始化：setupUi 之后调用，加载样式、初始化按钮/联系人树/托盘、连接信号。
	 *        注意这里不调 initTitleBar（.ui 里已自带标题栏，再调会重影）。
	 */
	void init();
	QString account;      // 当前登录账号（由登录窗口传入），后续查数据库都用它

	/**
	 * @brief 初始化左侧/底部的一排状态小图标按钮（在线状态、朋友圈图标等）
	 */
	void initButton();

	/**
	 * @brief 初始化联系人树：建根节点"苏州科技大学"，再挂公司群/部门群子项（见 addUserGroup）
	 */
	void initContact();

	/**
	 * @brief 连接信号槽：换肤通知刷新搜索框样式、系统按钮最小化/关闭
	 */
	void initConnect();
	/**
	 * @brief 系统托盘图标：new 一个 SysTrayIcon 以 this 为 parent，随窗口析构自动销毁，
	 *        后续交互全由其内部信号槽处理，主界面不再持有指针
	 */
	void initSysTray();

	/**
	 * @brief 初始化中间那排横向排列的应用图标按钮（朋友圈、游戏、皮肤入口等）
	 */
	void initAppButton();

	/**
	 * @brief 初始化右下角等级按钮，并用 QTimer 每 2 秒自动 +1（演示效果，到 99 回绕）
	 */
	void initLevelButton();

	/**
	 * @brief 在按钮上居中绘制一张图标：把 pixmap 透明背景底 + icon 原图居中合成，设为按钮 icon
	 * @param btn 要画图标的按钮引用（传引用是为了内部 setIcon）
	 * @param icon 图标图片的 QRC 路径，如 ":/Resources/MainWindow/app/app_7.png"
	 */
	void drawButtonIcon(QPushButton*& btn,const QString & icon);

	/**
	 * @brief 画等级按钮：lv 底标 + 十位数字 + 个位数字（数字用 levelvalue.png sprite 表切片绘制）
	 * @param level 当前等级（0~99），十位个位从这个数拆
	 */
	void drawLevelButton(int level);

	/**
	 * @brief 切换搜索框样式：有焦点=白底高亮，无焦点=半透明皮肤色
	 * @param hasFocus true=搜索框正在输入；false=失去焦点
	 */
	void updateSearchStyle(bool hasFocus);

	/**
	 * @brief 从数据库查当前账号姓名，用 fontMetrics().elidedText 按 label 宽度末尾省略号截断后显示到 nameLabel
	 */
	void setUserName();

	/**
	 * @brief 从数据库查当前账号头像路径，用 CommonUtils::getRoundedPixmap 圆角裁剪后贴到 headLabel
	 */
	void setUserHeadPixmap();

	/**
	 * @brief 往联系人树根项下挂"公司群+部门群"子节点：每查到一个群 new 一个树节点 + 一个 ContactItem 卡片贴上去
	 * @param rootItem 根节点 QTreeWidgetItem*，子节点挂到它下面
	 */
	void addUserGroup(QTreeWidgetItem* rootItem);

	/**
	 * @brief 工厂方法：创建一个小图标按钮，统一设置大小、对象名、动态属性、点击信号
	 * @param icon 图标 QRC 路径
	 * @param objName objectName，供 QSS 用 #objName 选择器定位
	 * @param size 按钮固定大小，默认 20x20
	 * @return QPushButton* 新建好的按钮，由调用方加进布局
	 */
	QPushButton* addOtherButton(const QString& icon, const QString& objName, QSize size = QSize(20, 20));

private:
	Ui::CCMainWindowClass ui;
	SkinWindow* m_skinWindow = nullptr;   //皮肤窗口指针：复用同一个实例，避免每次点击都 new 一个叠一摞  // 主界面布局（titleWidget、treeWidget、searchLineEdit 等控件都在 .ui 里）
protected:
	/**
	 * @brief 事件过滤器：监听 searchLineEdit 的焦点进出事件，切换搜索框样式
	 * @param watched 被监视的对象（这里是 ui.searchLineEdit）
	 * @param event 事件对象，用 event->type() 判断是 FocusIn 还是 FocusOut
	 * @return true=事件已处理不再传播；false=继续传播（这里 return false 让基类也能处理）
	 */
	bool eventFilter(QObject* watched, QEvent* event);

private slots:
	/**
	 * @brief 应用按钮点击槽：根据 sender()->objectName() 判断点了哪个，目前只处理"换肤"（app_skin）
	 */
	void onButtonClicked();

	/**
	 * @brief 树节点被点击：点根节点时切换展开/折叠（群组节点不做反应）
	 * @param item 被点的节点
	 */
	void onTreeItemClicked(QTreeWidgetItem* item);

	/**
	 * @brief 树节点被双击：群组节点打开（或激活）对应聊天窗口（根节点不响应）
	 * @param item 被双击的节点
	 */
	void onTreeItemDoubleClicked(QTreeWidgetItem* item);

	/**
	 * @brief 树节点被展开：根节点展开时让 RootContactItem 的箭头动画转到 90°
	 * @param item 被展开的节点
	 */
	void onTreeItemExpanded(QTreeWidgetItem* item);

	/**
	 * @brief 树节点被折叠：根节点折叠时箭头动画转回 0°
	 * @param item 被折叠的节点
	 */
	void onTreeItemCollapsed(QTreeWidgetItem* item);

	/**
	 * @brief 关闭按钮 / 托盘"退出"：先关掉聊天窗口框架（内部各会话窗口随之销毁），再退出事件循环。
	 *        顺序不能反：直接 quit 的话 TalkWindowShell 不会走正常的关闭清理
	 */
	void onQuit();

};
