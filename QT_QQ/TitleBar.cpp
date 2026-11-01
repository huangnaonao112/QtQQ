#include "stdafx.h"
#include "TitleBar.h"
#include <Qpixmap.h>
#include "CommonUtils.h"
#include <QMouseEvent>
#include <QDebug>


// ============================================================================
// 构造函数：创建标题栏控件
// 为什么在构造函数中清空文字：
//   .ui 文件中按钮和标签有占位文字（如 "min"、"icon"），
//   实际运行时这些文字由 CSS 中的 border-image 替代（按钮）或由代码设置（图标/标题），
//   清空避免占位文字短暂闪现
// ============================================================================

/**
 * @brief 构造函数：创建标题栏控件，清空占位文字后走 init()。
 *
 * 为什么在构造函数中清空文字：
 *   .ui 文件中按钮和标签有占位文字（如 "min"、"icon"），
 *   实际运行时这些文字由 CSS 中的 border-image 替代（按钮）或由代码设置（图标/标题），
 *   清空避免占位文字短暂闪现。
 * @param type 按钮组合类型（MIN_CLOSE_BUTTON / MIN_MAX_CLOSE_BUTTON / CLOSE_BUTTON）
 * @param parent 父窗口指针
 */
TitleBar::TitleBar(ButtonType type, QWidget *parent)
	: btnType(type), QWidget(parent)     // 初始化列表：先存按钮类型，再转交基类
{
	ui.setupUi(this);                    // 生成 ui 控件并绑定到本控件
	ui.iconLable->clear();               // 清占位文字，防闪现
	ui.titleContent->clear();            // 清标题占位
	ui.buttonMin->setText("");           // 按钮文字清空（图标走 QSS）
	ui.buttonMax->setText("");           // 同上
	ui.buttonClose->setText("");         // 同上
	ui.buttonRestore->setText("");       // 同上

	init();                              // 样式 → 按钮配置 → 接线
}

/**
 * @brief 析构函数：无手动释放的资源，控件随 Qt 对象树自动析构。
 */
TitleBar::~TitleBar()
{}
// ============================================================================
// setTitleIcon：设置标题栏左侧图标
// 为什么 setFixedSize(pix.size())：
//   让 QLabel 的尺寸与图片实际尺寸一致，避免图片被拉伸或留白。
//   QLabel 默认会根据内容自适应大小，但用 setPixmap 后并不会自动调整，
//   所以需要手动设置。
// ============================================================================

/**
 * @brief 设置标题栏左侧图标。
 *
 * setFixedSize(pix.size())：让 QLabel 的尺寸与图片实际尺寸一致，避免
 * 图片被拉伸或留白；setPixmap 后 QLabel 并不会自动调整，需要手动设置。
 * @param filepath 图标的 QRC 路径，如 ":/Resources/MainWindow/qqlogoclassic.png"
 */
void TitleBar::setTitleIcon(const QString& filepath)
{
	QPixmap pix(filepath);               // 加载图标

	ui.iconLable->setFixedSize(pix.size());   // 按图片实际尺寸锁死标签
	ui.iconLable->setPixmap(pix);             // 贴图
}

/**
 * @brief 设置标题文字（纯 setter，样式由构造时的 QSS 定）。
 * @param titleContent 要显示的标题文字，如 "QQ"、"登录"
 */
void TitleBar::setTitleContent(const QString& titleContent)
{
	ui.titleContent->setText(titleContent);   // 刷新标题文本
}

/**
 * @brief 锁定标题栏宽度（配合无边框窗口手动布局用）。
 * @param width 宽度像素值
 */
void TitleBar::setTitleWidth(int width)
{
	this->setFixedWidth(width);          // 锁定宽度
}

// ============================================================================
// saveRestoInfo / getRestoInfo：保存和读取窗口最大化前的位置和大小
// 为什么需要这两个方法：
//   用户点击最大化按钮时，需要记住当前窗口位置和尺寸；
//   点击还原按钮时，用这些信息把窗口恢复到原来的位置和大小。
//   数据存储在 TitleBar 中是因为按钮点击事件在这里处理，
//   而实际的窗口 resize/move 由父窗口通过信号回调完成。
// ============================================================================

/**
 * @brief 赋值函数：最大化时记录窗口位置和大小，供后续还原使用。
 * @param point 当前窗口左上角坐标
 * @param size 当前窗口尺寸
 */
void TitleBar::saveRestoInfo(const QPoint& point, const QSize& size)
{
	restorePos = point;                  // 存位置
	restoreSize = size;                  // 存尺寸
}


/**
 * @brief 读回最大化前的位置和大小（出参），交给父窗口 setGeometry 还原。
 * @param point [out] 带出最大化前的窗口位置
 * @param size [out] 带出最大化前的窗口尺寸
 */
void TitleBar::getRestoInfo(QPoint & point, QSize & size)
{
	point = restorePos;                  // 带出位置
	size = restoreSize;                  // 带出尺寸
}

// ============================================================================
// windowIsMax：判断"当前是不是最大化状态"
// 这里不问窗口系统，而是看 buttonMax 是否可见——onButtonMaxClicked /
// onButtonRestoreClicked 切换按钮时就等于登记了状态：
//   restore 可见 = 已最大化。max/restore 在 .ui 里占同一位置，互斥显示。
// 注意只对 MIN_MAX_CLOSE_BUTTON 判断：其他类型压根没有最大化概念，
// 永远返回 false（可拖动），否则弹窗会因为 max 按钮被隐藏而误判成最大化。
// ============================================================================

/**
 * @brief 判断"当前是不是最大化状态"。
 *
 * 不问窗口系统，而是看 buttonMax 是否可见（切换按钮时就等于登记了状态）。
 * 只对 MIN_MAX_CLOSE_BUTTON 判断：其他类型永远返回 false（可拖动），
 * 否则弹窗会因为 max 按钮被隐藏而误判成最大化。
 * @return true=已最大化（buttonMax 不可见）；false=正常
 */
bool TitleBar::windowIsMax()
{
	if(btnType == MIN_MAX_CLOSE_BUTTON)  // 只有支持最大化的类型才判断
	{
		if (!ui.buttonMax->isVisible())  // 最大化按钮不可见 → 已最大化
		{
			return true;      // 最大化按钮不可见，则为最大化状态
		}
	}
	return false;                        // 其余情况均视为非最大化
}

// ============================================================================
// init()：初始化标题栏
// 执行顺序：
//   1. loadStyleSheet → 加载 QSS 样式（按钮图标、字体等）
//   2. setTitleButton → 按 ButtonType 显示/隐藏按钮
//   3. 设置 moveAble 初始值（区分不支持最大化和支持最大化的窗口）
//   4. initConnect → 连接按钮信号
// ============================================================================

/**
 * @brief 初始化标题栏：样式 → 按钮可见性 → 移动状态 → 信号连接。
 */
void TitleBar::init()
{
	CommonUtils::loadStyleSheet(this, "Title");   // 加载 Title.qss（按钮图标、字体等）
	setTitleButton();                   // 按 ButtonType 显示/隐藏按钮

	// 初始化移动状态
	// 为什么区分按钮类型：
	//   MIN_CLOSE_BUTTON / CLOSE_BUTTON 不支持最大化，窗口永远不会处于最大化状态，
	//   所以始终可移动。
	//   MIN_MAX_CLOSE_BUTTON 支持最大化切换，移动状态取决于 buttonMax 是否可见——
	//   buttonMax 可见 → 窗口非最大化 → 可移动；
	//   buttonMax 隐藏（restore 可见）→ 窗口已最大化 → 不可移动（系统约定）。
	if (btnType == MIN_CLOSE_BUTTON || btnType == CLOSE_BUTTON) {
		moveAble = true;                 // 不支持最大化，始终可拖
	}
	else {
		moveAble = ui.buttonMax->isVisible();   // 由是否最大化决定可拖
	}

	initConnect();                      // 接线四个按钮
}

// ============================================================================
// setTitleButton：根据 ButtonType 配置按钮可见性
// buttonMax 和 buttonRestore 是互斥的（同一位置只显示一个），
// 其中 buttonRestore 在初始化时总是隐藏的，只有最大化后才会显示。
// 为什么各类型都要隐藏 buttonRestore：
//   窗口启动时总是非最大化状态，restore 按钮没有任何作用，统一隐藏。
// ============================================================================

/**
 * @brief 根据 ButtonType 配置按钮可见性。
 *
 * buttonMax 与 buttonRestore 互斥（同一位置只显示一个）；启动时总是
 * 非最大化状态，所以所有类型都先隐藏 buttonRestore。
 */
void TitleBar::setTitleButton()
{
	switch (btnType)                    // 按按钮组合类型分流
	{
	case MIN_CLOSE_BUTTON:
	{
		// 仅保留最小化和关闭 → 适用登录/设置等不需要最大化的对话框
		ui.buttonRestore->setVisible(false);   // 隐藏还原
		ui.buttonMax->setVisible(false);       // 隐藏最大化
		break;
	}
	case MIN_MAX_CLOSE_BUTTON:
	{
		// 保留全部三个按钮 → 适用主窗口，支持最大化切换
		ui.buttonRestore->setVisible(false);   // 启动时非最大化，先隐藏还原
		break;
	}
	case CLOSE_BUTTON:
	{
		// 仅保留关闭 → 适用纯提示弹窗
		ui.buttonRestore->setVisible(false);   // 隐藏还原
		ui.buttonMax->setVisible(false);       // 隐藏最大化
		ui.buttonMin->setVisible(false);       // 隐藏最小化
		break;
	}
	default:
		break;                         // 未知类型保持默认
	}
}

// ============================================================================
// initConnect：连接按钮点击信号
//
// ⚠️ 为什么全部手动 connect，不依赖 Qt 自动连接：
//   槽函数如果命名为 on_<控件名>_<信号名>()（如 on_buttonMax_clicked），
//   ui.setupUi(this) 会自动连接 —— 但这里再手动 connect 就会连两次。
//   之前曾因此导致 onMax 被调用两次，saveRestoInfo 存的正确值被第二次调用
//   用最大化后的 (0,0,2560x1380) 覆盖，还原按钮彻底失效。
//   所以现统一用驼峰命名（onButtonMaxClicked）打破自动连接规则，
//   全部四个按钮在 initConnect 中手动管理，一目了然不会重复。
//
// 连接对比：
//   QPushButton::clicked → onButtonMaxClicked()    → emit buttonMaxClicked    → 父窗口
//   QPushButton::clicked → onButtonRestoreClicked() → emit buttonRestoreClicked → 父窗口
//   QPushButton::clicked → buttonMinClicked         → 父窗口（直连）
//   QPushButton::clicked → buttonCloseClicked       → 父窗口（直连）
// ============================================================================

/**
 * @brief 连接按钮点击信号（全部手动 connect，不依赖 Qt 自动连接）。
 *
 * ⚠️ 槽函数若命名为 on_<控件名>_<信号名>() 会被 setupUi 自动连接一次，
 * 手动再连一次 → 一次点击触发两次 → 还原失效。所以用驼峰命名
 * （onButtonMaxClicked）打破自动连接规则，全部手动管理。
 */
void TitleBar::initConnect()
{
	connect(ui.buttonMax,               // 最大化按钮
		&QPushButton::clicked,          // 点击信号
		this,                           // 接收者本控件
		&TitleBar::onButtonMaxClicked); // 切换可见性 + 发信号

	connect(ui.buttonRestore,           // 还原按钮
		&QPushButton::clicked,          // 点击信号
		this,                           // 接收者
		&TitleBar::onButtonRestoreClicked);   // 切换可见性 + 发信号

	connect(ui.buttonMin,               // 最小化按钮
		&QPushButton::clicked,          // 点击信号
		this,                           // 接收者
		&TitleBar::buttonMinClicked);   // 直连父窗口槽

	connect(ui.buttonClose,             // 关闭按钮
		&QPushButton::clicked,          // 点击信号
		this,                           // 接收者
		&TitleBar::buttonCloseClicked); // 直连父窗口槽
}

// ============================================================================
// mousePressEvent：鼠标按下 → 开始拖拽
//
// 为什么用 event->accept() 而不是 QWidget::mousePressEvent(event)：
//   这里已经完整处理了"按下 → 开始拖拽"这件事，属于已处理的事件，
//   必须标记为已接受（accept）。它同时解决两个问题：
//   1. 阻止事件向父控件传播（解决拖动偏移的根因）
//      QWidget::mousePressEvent 的默认实现实质是 event->ignore()，
//      被 ignore 的鼠标事件会沿父级链向上传播，
//      → TitleBar::mousePressEvent 执行完后，事件又传给父控件
//        BasicWindow::mousePressEvent（它也实现了拖拽逻辑），
//      两处都 moveAble = true、都记录 startMovePos；
//      拖拽时 TitleBar 移动 parentWidget()、BasicWindow 又移动 this，
//      两次叠加就产生了移动偏移。
//      现在 event->accept() 后事件停在 TitleBar，不再向下传播，
//      BasicWindow 的拖拽逻辑不会触发。
//   2. 隐式抓取鼠标（implicit grab）
//      按下事件被 accept 后，Qt 会把 TitleBar 设为鼠标抓取者，
//      后续的 move / release 事件全部直接发给 TitleBar，
//      即使鼠标快速移出标题栏范围，拖拽也不会断，移动更跟手。
//
// 为什么最大化时不进该分支（不调用 accept）：
//   最大化窗口不允许拖拽（Windows 系统约定，没有"拖到哪里"的概念），
//   此时不处理该事件、不 accept，让它沿事件链继续传播；
//   BasicWindow::mousePressEvent 同样判断 !bar->windowIsMax()，
//   也不会处理，事件最终被忽略，不会产生任何拖动。
// ============================================================================

/**
 * @brief 鼠标按下 → 开始拖拽（仅非最大化时）。
 *
 * event->accept()：阻止事件传播给父窗口（BasicWindow 也有拖拽逻辑，
 * 叠加会产生移动偏移）+ 隐式抓取鼠标（快速移出标题栏拖拽不断）。
 * 最大化时不 accept、不处理，让事件链继续传播。
 * @param event 鼠标事件，用 globalPos() 取屏幕全局坐标
 */
void TitleBar::mousePressEvent(QMouseEvent* event)
{
	//if (btnType == MIN_CLOSE_BUTTON || btnType == CLOSE_BUTTON) {
	//	// 不支持最大化/还原切换的窗口，始终可移动
	//	moveAble = true;
	//	startMovePos = event->globalPos();
	//}
	//else if (ui.buttonMax->isVisible()) {
	//	// 支持最大化的窗口：max按钮可见 → 窗口非最大化 → 可移动
	//	moveAble = true;
	//	startMovePos = event->globalPos();
	//}

	//QWidget::mousePressEvent(event);

	if (!windowIsMax())                // 最大化窗口不许拖拽
	{
		moveAble = true;               // 开启拖拽标志
		startMovePos = event->globalPos();   // 记录按下点作为位移基准

		event->accept();  // 标记已处理：阻止传播给父窗口 + 隐式抓取鼠标
	}
}

// ============================================================================
// mouseMoveEvent：鼠标移动 → 计算位移并移动父窗口
// 位移计算原理：
//  1. 当前鼠标全局坐标 - 上次记录的全局坐标 = 本次鼠标移动增量
//  2. 父窗口当前位置 + 增量 = 父窗口新位置
//  3. 更新 startMovePos 为当前坐标，供下一次移动使用
// 为什么用 globalPos 而非 pos：
// pos() 是相对于当前控件的坐标，控件本身在移动中，
// 用相对坐标会出现累积误差。globalPos() 是屏幕绝对坐标，计算精确。
//
// 为什么移动成功后要 event->accept()：
//   与 mousePressEvent 同理，标志该事件已被本控件处理。
//   若 move 事件被 ignore，同样会向上传播给 BasicWindow，
//   而 TitleBar 已经移动了 parentWidget()，BasicWindow 若再移动 this
//   就会双重移动 → 偏移。accept 之后传播被截断，只移动一次。
//
// 为什么 accept 放在 if (moveAble) 里面：
//   只有正在拖拽（moveAble == true）时，移动事件才由标题栏处理；
//   否则说明本次移动与拖拽无关（可能是系统或其他控件触发），
//   不拦截，交给事件链上的其他控件。
// ============================================================================

/**
 * @brief 鼠标移动 → 按 globalPos 增量移动父窗口（仅拖拽中）。
 *
 * 用 globalPos 而非 pos：pos 是控件相对坐标，控件本身在动会累积误差。
 * accept 放在 if (moveAble) 内：只有正在拖拽时才处理，无关移动不拦截。
 * @param event 鼠标事件
 */
void TitleBar::mouseMoveEvent(QMouseEvent * event)
{

	if (moveAble)                      // 正在拖拽才处理
	{
		QPoint movePoint = event->globalPos() - startMovePos;   // 鼠标位移增量
		QPoint windowPoint = parentWidget()->pos();              // 父窗口当前位置
		QPoint windowMovePoint = movePoint + windowPoint;        // 父窗口目标位置
		parentWidget()->move(windowMovePoint);                    // 移动父窗口

		startMovePos = event->globalPos();                       // 更新基准点

		event->accept();  // 已处理：阻止传播给父窗口，避免双重移动
	}

	//QWidget::mouseMoveEvent(event);
}

// ============================================================================
// mouseDoubleClickEvent：双击标题栏 → 最大化/还原切换
// 为什么只对 MIN_MAX_CLOSE_BUTTON 响应：
// 只有这种模式支持最大化，其他模式双击标题栏无效果。
// 实现方式：模拟点击对应的按钮 → 触发按钮的 clicked 信号 → 执行切换逻辑
// ============================================================================

/**
 * @brief 双击标题栏 → 最大化/还原切换（仅 MIN_MAX_CLOSE_BUTTON 响应）。
 *
 * 实现方式：按 buttonMax 可见性判断当前状态，直接调用对应切换函数
 * （模拟点击），最后仍转交基类处理。
 * @param event 鼠标事件
 */
void TitleBar::mouseDoubleClickEvent(QMouseEvent* event)
{
	if (btnType == MIN_MAX_CLOSE_BUTTON) {   // 只有支持最大化的类型才响应

		if (ui.buttonMax->isVisible())       // 非最大化状态
		{
			onButtonMaxClicked();     // 当前非最大化 → 最大化
		}
		else                            // 已最大化
		{
			onButtonRestoreClicked(); // 当前最大化 → 还原
		}
	}
	QWidget::mouseDoubleClickEvent(event);   // 转交基类默认处理
}


// ============================================================================
// mouseReleaseEvent：鼠标松开 → 结束拖拽
// 为什么用 event->accept()：
//   与 press / move 保持一致，标记松开事件已被本控件处理。
//   （按下时 accept 触发的隐式抓取，也保证松开事件一定发给 TitleBar，
//   即使鼠标此时不在标题栏上。）
//   若调用 QWidget::mouseReleaseEvent（默认实现是 ignore），事件会传播给
//   父控件 BasicWindow，它的 release 虽然只是把 moveAble 置 false、无害，
//   但为了让事件链干净、与 press / move 语义一致，统一改为 accept()。
// ============================================================================

/**
 * @brief 鼠标松开 → 结束拖拽并 accept（与 press/move 语义一致）。
 * @param event 鼠标事件
 */
void TitleBar::mouseReleaseEvent(QMouseEvent* event)
{
	moveAble = false;   // 鼠标松开 → 停止拖拽
	event->accept();    // 已处理：阻止传播给父窗口
	//QWidget::mouseReleaseEvent(event);
}

// ============================================================================
// paintEvent：绘制事件 → 同步标题栏宽度
// 为什么在 paintEvent 中同步宽度：
// 父窗口大小可能变化（比如用户拖拽边缘 resize），
// paintEvent 是 Qt 绘制周期的入口，大小变化必定触发重绘，
// 在此同步可以保证标题栏宽度始终与父窗口等宽。
// 不调用父类的 paintEvent 是因为 QWidget 的 paintEvent 是空函数。
// ============================================================================

/**
 * @brief 绘制事件 → 同步标题栏宽度与父窗口等宽。
 *
 * paintEvent 是绘制周期入口，大小变化必定触发重绘，在此同步可保证
 * 始终等宽、无延迟；不调用父类是因为 QWidget 的 paintEvent 是空函数。
 * @param event 绘制事件
 */
void TitleBar::paintEvent(QPaintEvent* event)
{
	if (this->width() != parentWidget()->width())   // 与父窗口宽度不一致时
	{
		this->setFixedWidth(parentWidget()->width());   // 同步为父窗口宽度
	}
}

// ============================================================================
// onButtonRestoreClicked：还原按钮点击
// 切换按钮可见性 + 发射信号
// 视觉上：restore 按钮消失，max 按钮重新出现
// ============================================================================

/**
 * @brief 还原按钮点击：切换按钮可见性 + 发射 buttonRestoreClicked 通知父窗口。
 */
void TitleBar::onButtonRestoreClicked()
{
	ui.buttonRestore->setVisible(false);   // 隐藏还原按钮
	ui.buttonMax->setVisible(true);        // 显示最大化按钮

	emit buttonRestoreClicked();  // 通知父窗口：执行窗口还原操作
}

// ============================================================================
// onButtonMaxClicked：最大化按钮点击
// 切换按钮可见性 + 发射信号
// 视觉上：max 按钮消失，restore 按钮出现
// ============================================================================

/**
 * @brief 最大化按钮点击：切换按钮可见性 + 发射 buttonMaxClicked 通知父窗口。
 */
void TitleBar::onButtonMaxClicked()
{
	ui.buttonMax->setVisible(false);       // 隐藏最大化按钮
	ui.buttonRestore->setVisible(true);    // 显示还原按钮

	emit buttonMaxClicked();  // 通知父窗口：执行窗口最大化操作
}
