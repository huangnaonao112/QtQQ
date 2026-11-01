#include "stdafx.h"
#include "BasicWindow.h"
#include <QScreen>
#include <QPainter>
#include <QMouseEvent>
#include "ConfigIni.h"
#include "CommonUtils.h"
#include "NotifyManager.h"

// ============================================================================
// 构造函数：设置无边框窗口标志
// Qt::FramelessWindowHint 的作用：
//   去掉系统原生标题栏和边框，使窗口完全由自定义控件绘制。
//   代价：系统提供的窗口移动、缩放、标题栏按钮全部失效，
//   必须由 TitleBar 控件手动实现这些功能。
// 为什么在这里设置而非在子类：
//   所有子类都需要无边框，统一在基类设置避免遗漏。
// ============================================================================
/**
 * @brief 无边框窗口基类构造：统一去掉系统标题栏，从 color.ini 读上次皮肤色，并订阅全局换肤信号。
 *        无边框的代价是拖动/缩放/标题栏按钮全部失效，由 TitleBar 子控件手动实现。
 * @param parent 父窗口指针（一般传 nullptr，顶层窗口）
 */
BasicWindow::BasicWindow(QWidget *parent)
	: QDialog(parent)
{
	this->setWindowFlags(Qt::FramelessWindowHint);   // 无边框：去掉系统标题栏和边框
	// 读取用户上次保存的皮肤颜色，供 loadStyleSheet 上色。
	// 为什么在基类构造时读取：所有子窗口（登录、主窗口等）共享同一皮肤色，
	// 在基类统一读取一次，子类 loadStyleSheet 时直接使用，避免各自重复读。
	skinColor = ConfigIni::getColor();   // 从 ini 读上次选的皮肤色（首次运行写默认蓝）
	//改变皮肤颜色：订阅全局换肤通知，任何窗口选了新颜色都会收到
	connect(NotifyManager::instance(), &NotifyManager::skinChanged, this, &BasicWindow::onSkinChanged);   // 换肤广播→本窗刷新
}

/**
 * @brief 析构：无动态资源需手动释放，界面控件由 Qt 对象树自动析构
 */
BasicWindow::~BasicWindow()
{}

// ============================================================================
// initTitleBar：创建并配置标题栏
// 为什么 bar->move(0, 0) 而不使用布局：
//   标题栏需要固定在窗口顶部，且不参与窗口内容区域的布局计算。
//   把它放在布局中会导致内容控件和标题栏挤占同一空间。
//   手动定位在 (0,0) 保证标题栏始终在窗口最顶部，内容区域从标题栏下方开始。
//
// 为什么这里连的是基类通用槽：
//   initTitleBar 只把按钮接到 onClose/onMax/onMin 这些通用行为上；
//   具体业务差异留给子类或槽内部处理——登录窗点关闭 = 退出程序
//   （onClose 里对 objectName "UserLoginClass" 特判），主窗压根不用
//   initTitleBar，自己把 .ui 里的按钮接 onQuit。写死在基类会把
//   各窗口的行为绑在一起，加窗口时改不动。
// ============================================================================
/**
 * @brief 创建并配置 TitleBar：手动 move 到 (0,0)（不进布局，避免和内容区挤占空间），
 *        并把四个按钮信号接到基类通用窗口控制槽。具体业务差异留给子类在槽内部或另行接线。
 * @param buttonType 按钮组合类型（MIN_CLOSE_BUTTON / MIN_MAX_CLOSE_BUTTON / CLOSE_BUTTON）
 * @param iconFileName 标题栏左侧图标 QRC 路径；传空 QString() 则不显示图标
 * @param titleContent 标题栏中间文字；传空则不显示
 */
void BasicWindow::initTitleBar(ButtonType buttonType,
	const QString& iconFileName,
	const QString& titleContent)
{
	bar = new TitleBar(buttonType, this);          // 创建标题栏控件，parent 是本窗口
	bar->setTitleIcon(iconFileName);                // 设置左侧图标（如 QQ logo）
	bar->setTitleContent(titleContent);            // 设置中间标题文字
	bar->move(0, 0);  // 定位在父窗口左上角 (0, 0)，手动摆放不进布局

	// 把标题栏四个按钮的信号连到本类的窗口控制槽
	connect(bar,&TitleBar::buttonCloseClicked,this,&BasicWindow::onClose);     // 关闭
	connect(bar,&TitleBar::buttonMaxClicked,this,&BasicWindow::onMax);         // 最大化
	connect(bar,&TitleBar::buttonRestoreClicked,this,&BasicWindow::onRestore); // 还原
	connect(bar,&TitleBar::buttonMinClicked,this,&BasicWindow::onMin);         // 最小化
}

// ============================================================================
// loadStyleSheet：加载子类对应的 QSS，并叠加运行时皮肤颜色
//
// 为什么这里要额外拼一段 qss，而不是把颜色写死在 .css 文件里：
//   皮肤颜色是运行时从 color.ini 读出来的（skinColor），
//   而 .css 是编译进程序的静态资源，无法在编译期知道用户选了哪个颜色。
//   所以把颜色值抽出来，用 .arg() 填进去，作为 otherStyle 追加到 .css 之后。
//
// 为什么用属性选择器 QWidget[titleskin="true"] 定位容器：
//   titleWidget / bodyWidget 是 .ui 里的普通 QWidget 容器，没有自己的类名，
//   无法用类型选择器区分，所以给它们各设一个自定义动态属性
//   （titleskin / bottomskin），再用属性选择器精确命中。
//
// 为什么 bodyWidget 用 rgba(...,50)、titleWidget 用 rgb(...)：
//   bodyWidget 是半透明（alpha=50）的底层，让皮肤色若隐若现；
//   titleWidget 用不透明纯色保证标题栏区域颜色一致。
//   注意半透明必须写 rgba，Qt 不认 rgb 的四参数写法。
//
// ============================================================================
/**
 * @brief 加载 <filename>.css 并叠加当前皮肤色规则：titleWidget 实色、bottomWidget 半透明。
 *        颜色运行时从 skinColor 取出拼进临时 QSS，再作为 otherStyle 传给 CommonUtils。
 * @param filename QSS 文件名（不含路径与后缀），如 "UserLogin"、"CCMainWindow"
 */
void BasicWindow::loadStyleSheet(const QString& filename)
{
	styleFileName = filename;  // 记录当前样式文件名，供后续换肤时重新加载

	int red = skinColor.red();    // 取皮肤色 R 分量
	int green = skinColor.green();// G
	int blue = skinColor.blue();  // B

	// 拼一段临时 QSS：titleWidget 实色、bottomWidget 半透明
	QString qss = QString("QWidget[titleskin=\"true\"]  \
                                  { background-color:rgb(%1,%2,%3);}  \
                           QWidget[bottomskin=\"true\"]  \
                                  { background-color:rgba(%1,%2,%3,50); }"
	).arg(red).arg(green).arg(blue);   // 把 R/G/B 三个分量填进 %1/%2/%3 占位

	// 把「.css 文件内容 + 皮肤色规则」一起应用到本窗口（qss 作为 otherStyle 传入）
	CommonUtils::loadStyleSheet(this, filename, qss);
}

/**
 * @brief 重写绘制事件：手动触发 QStyle::drawPrimitive(PE_Widget)，让样式表里设置的
 *        background-color 在无边框 QDialog 上真正画出来（普通带边框控件不需要这么做）。
 * @param event Qt 传入的绘制事件对象，直接转发给父类即可
 */
void BasicWindow::paintEvent(QPaintEvent* event)
{
	QDialog::paintEvent(event);//保留默认的绘制行为，QDialog的绘图事件函数为空函数

	QStyleOption opt;                              // 样式选项容器：携带 rect/state/调色板
	opt.initFrom(this);  //获取部件的当前样式状态（调色板、rect、state 等）

	QPainter p(this);   // 在本窗体上开画笔


	//手动调用样式表相关绘制方法，确保样式表设置生效
	this->style()->drawPrimitive(QStyle::PE_Widget, &opt, &p ,this);   // PE_Widget 让 QSS 背景生效
}

/**
 * @brief 鼠标按下：左键按下时记录窗口拖动起点；最大化状态下禁止拖动。
 *        bar 可能为 nullptr（没调 initTitleBar 的窗口如 CCMainWindow），必须先判空，
 *        否则在窗口上按下鼠标即崩溃。
 * @param event 鼠标事件，用 event->button() 判断左键、event->globalPos() 取屏幕坐标
 */
void BasicWindow::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)   //触发事件的按钮是鼠标左键
	{
		//bar 可能是 nullptr：没调 initTitleBar 的窗口（如 CCMainWindow）没有
		//注入式标题栏，自然也不存在"最大化状态"限制，直接允许拖动；
		//有标题栏的窗口则要问它当前是否处于最大化（最大化时禁止拖动）。
		//之前在这里直接解引用 bar 导致主界面一按鼠标就崩溃
		if (!bar || !bar->windowIsMax())
		{
			moveAble = true;                       // 允许拖动
			startMovePos = event->globalPos();     // 记录鼠标按下时的屏幕全局坐标
		}
	}
	QDialog::mousePressEvent(event);   // 转发给父类，保证默认行为
}

/**
 * @brief 鼠标松开：结束拖动状态（moveAble 置 false，后续 mouseMove 不再移动窗口）
 * @param event 鼠标事件，转发给父类
 */
void BasicWindow::mouseReleaseEvent(QMouseEvent * event)
{
	moveAble = false;   // 停止拖动

	QDialog::mouseReleaseEvent(event);   // 转发给父类，保证默认行为
}

/**
 * @brief 鼠标移动：若正在拖动，按全局坐标增量计算窗口新位置并 move，再把基准点更新到当前鼠标位置。
 * @param event 鼠标事件，用 event->globalPos() 取屏幕坐标做增量
 */
void BasicWindow::mouseMoveEvent(QMouseEvent * event)
{
	if (moveAble)   // 只有左键按下后才移动
	{
		QPoint movePoint = event->globalPos() - startMovePos;   // 鼠标位移增量
		QPoint windowPoint = this->pos();                         // 窗口当前位置
		this->move(windowPoint + movePoint);                     // 窗口移到 新位置

		startMovePos = event->globalPos();   // 更新基准点为当前鼠标位置，下次移动再算增量
	}
	QDialog::mouseMoveEvent(event);   // 转发给父类
}

/**
 * @brief 最小化：工具窗口（Qt::Tool）直接 hide()（不进任务栏），普通窗口 showMinimized() 最小化到任务栏。
 */
void BasicWindow::onMin()
{
	/*
	    Qt::Tool = 11 1011
	    Qt::Window = 1 0001
		1011 & 0001 = 1
	*/
	// 位与判断窗口标志里是否包含 Qt::Tool（工具窗口，如弹窗）
	if (Qt::Tool == (windowFlags() & Qt::Tool))  //如果该窗口是工具窗口，则隐藏
	{
		hide();   // 工具窗口直接隐藏（不进任务栏）
	}
	else     //如果该窗口不是工具窗口，则最小化
	{
		showMinimized();   // 普通窗口最小化到任务栏
	}
}

/**
 * @brief 最大化：先让 TitleBar 保存当前位置/大小（供还原用），再把窗口铺满主屏可用工作区（去任务栏）。
 */
void BasicWindow::onMax()
{
	bar->saveRestoInfo(this->pos(), this->size());   // 记录最大化前的位置和大小，点还原时恢复
	QRect rect = QApplication::screens()[0]->availableGeometry();  //获取主屏可用工作区（去掉任务栏）
	setGeometry(rect);   // 窗口铺满整个工作区
}

/**
 * @brief 还原：从 TitleBar 取出之前保存的位置和大小，setGeometry 回去。
 */
void BasicWindow::onRestore()
{
	QPoint pos;   // 还原目标位置（传出参数）
	QSize size;   // 还原目标尺寸（传出参数）
	bar->getRestoInfo(pos, size);        // 取出最大化前保存的窗口位置和大小
	setGeometry(QRect(pos, size));       // 恢复到原来的位置和尺寸
}

/**
 * @brief 关闭窗口：登录窗（objectName=="UserLoginClass"）直接退出整个程序，其余窗口只 close 本窗。
 */
void BasicWindow::onClose()
{
	QObject* parent = sender()->parent();   // 发信号的是标题栏按钮，它的 parent 就是本窗体
	if (parent)                             // 判空防御：理论上总有 parent，但保持防御
	{
		if (parent->objectName() == "UserLoginClass")   // 登录窗点 X = 退出程序
		{
			QApplication::quit();   // 终止事件循环
			return;                 // 已退出，不再走下面的 close
		}
	}
	this->close();   // 关闭本窗口（触发 closed 信号，具体是否退出看应用逻辑）
}

/**
 * @brief 隐藏窗口（hide，不销毁；常用于托盘"隐藏"菜单）
 */
void BasicWindow::onHide()
{
	hide();   // 隐藏窗口，任务栏图标消失，但对象还在
}

/**
 * @brief 显示窗口并 activateWindow() 拉到前台（常用于托盘"显示"菜单）
 */
void BasicWindow::onShow()
{
	show();                 // 显示窗口
	//活动窗口
	activateWindow();   // 把窗口拉到最前并设为焦点窗口
}

/**
 * @brief 退出整个应用：调 QApplication::quit() 终止事件循环
 */
void BasicWindow::onQuit()
{
	QApplication::quit();   // 终止事件循环
}

/**
 * @brief 换肤响应：收到 NotifyManager::skinChanged 信号时更新 skinColor，并用 styleFileName 重新加载样式表。
 * @param color 新选中的皮肤颜色
 */
void BasicWindow::onSkinChanged(const QColor& color)
{
	skinColor = color;               // 更新当前皮肤色
	loadStyleSheet(styleFileName);   // 用新颜色重新拼 QSS 并重载样式
}
