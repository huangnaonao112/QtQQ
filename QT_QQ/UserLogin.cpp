#include "stdafx.h"
#include "UserLogin.h"
#include "CommonUtils.h"
#include <qmessagebox.h>
#include <QTimer>
#include <QCoreApplication>
#include "ConfigIni.h"
#include "UseMySQL.h"

// ============================================================================
// 构造函数：初始化 UI 和标题栏
// 执行顺序：
//   1. BasicWindow() → 设置无边框标志
//   2. ui.setupUi(this) → 创建登录界面的控件（输入框、按钮等）
//   3. init() → 创建标题栏
// 为什么 setupUi 必须放在 initTitleBar 之前：
//   setupUi 会设置对话框的初始大小（600x400），
//   initTitleBar 需要知道父窗口宽度来设置标题栏宽度。
//   如果顺序颠倒，标题栏的初始宽度可能是错误的。
// ============================================================================

/**
 * @brief 构造函数：初始化 UI 和标题栏，并提前触发数据库单例连接。
 *
 * setupUi 必须先于 initTitleBar：setupUi 会设置对话框初始大小（600x400），
 * initTitleBar 需要知道父窗口宽度来设置标题栏宽度；顺序颠倒标题栏宽度会错。
 * @param parent 父窗口指针（一般 nullptr，登录窗是顶层窗口）
 */
UserLogin::UserLogin(QWidget *parent)
	: BasicWindow(parent)                // 转交父类构造（设置无边框标志）
{
	ui.setupUi(this);  // 先初始化登录界面控件（确定窗口大小）
	init();             // 再初始化标题栏（依赖窗口宽度）
	UseMySQL::instance();   // 提前触发单例构造 → 连接数据库（懒加载第一次真正执行）
}

/**
 * @brief 析构函数：界面控件由 Qt 对象树统一管理，无需手动释放。
 */
UserLogin::~UserLogin()
{}

// ============================================================================
// init：初始化标题栏
//
// 为什么用 MIN_CLOSE_BUTTON：
//   登录窗口不支持最大化（无边框异形窗最大化会破坏整体视觉设计），
//   所以标题栏只给最小化 + 关闭两个按钮，
//   与 UserLogin.h 里"登录窗不需要最大化"的说明保持一致。
//
// 为什么用 QQ logo 作为图标：
//   增强品牌识别度，模拟 QQ 客户端的标题栏样式。
//   图标文件通过 QRC 资源系统嵌入，路径前缀 ":" 表示从 QRC 根目录读取。
// ============================================================================

/**
 * @brief 初始化标题栏、皮肤背景属性、样式表、autotest 钩子、logo 与记住密码回填。
 *
 * 为什么用 MIN_CLOSE_BUTTON：登录窗不支持最大化（无边框异形窗最大化会
 * 破坏整体视觉设计），只给最小化 + 关闭两个按钮。
 * WA_StyledBackground：普通 QWidget 的 paintEvent 是空函数，不设的话
 * 样式表里的 background-color 画不出来。
 */
void UserLogin::init()
{
	BasicWindow::initTitleBar(MIN_CLOSE_BUTTON,
		":/Resources/MainWindow/qqlogoclassic.png");   // 创建标题栏：只有最小化+关闭按钮，图标是 QQ logo
	// 让 titleWidget / bodyWidget 这两个普通 QWidget 容器能绘制样式表的背景色。
	// 为什么必须设置 WA_StyledBackground：
	//   普通 QWidget 的 paintEvent 是空函数，不会画样式表里的 background-color；
	//   设了该属性后，Qt 重绘时会按样式表/调色板填充背景。
	//   否则 loadStyleSheet 里 [titleskin=true] / [bottomskin=true] 的皮肤色画不出来。
	ui.titleWidget->setAttribute(Qt::WA_StyledBackground, true);   // 允许画皮肤背景
	ui.bodyWidget->setAttribute(Qt::WA_StyledBackground, true);    // 同上
	loadStyleSheet("UserLogin");   // 加载 UserLogin.css，自动叠加当前皮肤色

	//==== autotest hook: --autotest --account X --password Y auto-login ====
	const QStringList args = QCoreApplication::arguments();   // 读启动参数
	if (args.contains("--autotest"))     // 带 autotest 标志才走自动登录
	{
		const int ai = args.indexOf("--account");   // 定位账号参数下标
		const int pi = args.indexOf("--password");  // 定位密码参数下标
		if (ai >= 0 && pi >= 0 && ai + 1 < args.size() && pi + 1 < args.size())   // 参数齐全
		{
			//注意：必须在定时器里填（而不是立即填），
			//因为后面 initUser() 会用 ini 里记住的账号覆盖掉预填值
			QTimer::singleShot(2000, this, [this, ai, pi, args]() {   // 2 秒后执行
				ui.editUserAccount->setText(args.at(ai + 1));   // 填账号
				ui.editPassword->setText(args.at(pi + 1));      // 填密码
				on_loginBtn_clicked();                          // 模拟点登录
			});
		}
	}	initPicture();                 // 在登录窗中间画一个圆角头像 logo
	initUser();  // 读取 ini 里记住的账号密码回填输入框（之前漏了调用，导致"记住密码"看似不生效）
}

/**
 * @brief 在登录窗正中放置一个圆形 QQ logo（垂直偏上一点，给输入框留空间）。
 */
void UserLogin::initPicture()
{
	QLabel* head = new QLabel(this);              // 头像 label
	head->setFixedSize(68, 68);                   // 固定 68x68
	head->move(width() / 2 - 34, height() / 2 - 90);   // 水平居中、垂直偏上一点

	QPixmap src(":/Resources/MainWindow/app/logo.ico");        // 原图
	QPixmap mask(":/Resources/MainWindow/head_mask.png");       // 圆形遮罩
	head->setPixmap(CommonUtils::getRoundedPixmap(src, mask, head->size()));   // 圆角裁剪后贴上
}

/**
 * @brief 启动时回填"记住密码"保存的账号密码（数据来源：exe 目录 ini 里 ConfigIni 写入）。
 */
void UserLogin::initUser()
{
	//启动时回填"记住密码"保存的账号（数据来源：exe 目录 ini\uer.ini，
	//由 ConfigIni::saveUserProfile 在上次勾选"记住密码"登录时写入）
	QString account, password;
	if (ConfigIni::getUserProfile(account, password))   // 读 ini，没有就返回 false
	{
		ui.editUserAccount->setText(account);     // 回填账号
		ui.editPassword->setText(password);      // 回填密码
		ui.editPassword->setEchoMode(QLineEdit::Password);   // 密码框显示为圆点
		//setEchoMode(Password)：让密码框显示为圆点而不是明文。
		//为什么放这里而不是 Designer 里设：只在"确实有保存的密码"时才需要
		//切到密文模式，放代码里可以按条件生效，比写死在 ui 文件里灵活
	}
}

/**
 * @brief "登录"按钮点击槽：校验输入 → 查数据库验证 → 打开主窗口。
 *
 * 由 Qt 自动连接（on_<控件名>_<信号> 命名规则）；空输入/验证失败弹窗提示；
 * 勾选"记住密码"则写入 ini；成功后关登录窗并 show 主界面。
 */
void UserLogin::on_loginBtn_clicked()
{
	QString account = ui.editUserAccount->text();     // 取账号输入框内容
	QString password = ui.editPassword->text();       // 取密码输入框内容
	if (account.isEmpty() || password.isEmpty())      // 空输入校验
	{
		QMessageBox::information(this, "提示","请输入账号密码");   // 提示补全
		return;                            // 不继续验证
	}
//验证账号密码
	if (!UseMySQL::instance()->verifyLogin(account, password))   // 数据库查密码
	{
		QMessageBox::information(this, "提示", "账号或者密码错误");   // 验证失败提示
		return;                            // 不打开主窗
	}
	this->close();   // 登录成功，关闭登录窗
//记住密码
    if(ui.checkBox->isChecked())   // 勾选了"记住密码"
    {
		ConfigIni::saveUserProfile(account, password);   // 写入 ini 文件
    }
	//打开主窗口
	CCMainWindow* mainwindow= new CCMainWindow(account);   // 创建主界面，把账号传进去
	mainwindow->show();                                      // 显示主界面
}
