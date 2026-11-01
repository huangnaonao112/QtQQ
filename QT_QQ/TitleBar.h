#pragma once

#include <QWidget>
#include "ui_TitleBar.h"
#include "define.h"

// ============================================================================
// 类：TitleBar — 自定义标题栏控件
//
// 为什么需要自定义标题栏：
//   整个窗口使用 Qt::FramelessWindowHint 去掉了系统原生标题栏，
//   这样才能实现 QQ 风格的自绘 UI（背景皮肤、圆角、自定义按钮图标等）。
//   但去掉系统标题栏后，窗口的拖拽移动、最大/最小/关闭都需要自己实现，
//   TitleBar 就是替代系统标题栏的自绘控件。
//
// 为什么继承 QWidget 而非 QFrame/QGroupBox：
//   TitleBar 的布局由 .ui 文件提供（图标、标题文字、按钮），
//   不需要 QFrame 的边框或 QGroupBox 的分组功能，QWidget 最轻量。
//
// 为什么鼠标事件重写在 TitleBar 而非父窗口：
//   只有点击标题栏区域才应该触发拖拽。如果把事件处理放在 BasicWindow，
//   需要额外判断点击位置是否在标题栏范围内。放在 TitleBar 中，
//   Qt 的事件系统自动保证只有点击到标题栏区域才会触发。
// ============================================================================

/**
 * @class TitleBar
 * @brief 自定义标题栏：无边框窗口的拖拽移动、双击最大化、最小化/最大化/关闭按钮。
 *
 * 替代系统标题栏（Qt::FramelessWindowHint 后自绘）；按钮点击只发信号，
 * 具体逻辑由父窗口（BasicWindow）决定；槽函数故意用驼峰命名打破
 * Qt 自动连接规则，避免重复连接导致还原失效。
 */
class TitleBar : public QWidget
{
	Q_OBJECT

public:
	/**
	 * @brief 构造函数：按按钮组合类型初始化标题栏，清空占位文字后走 init()。
	 *
	 * 清空文字的原因：.ui 里按钮/标签有占位文字（"min"、"icon" 等），
	 * 运行时由 CSS border-image 或代码设置替代，清空避免占位文字短暂闪现。
	 * @param type 按钮组合类型（MIN_CLOSE_BUTTON / MIN_MAX_CLOSE_BUTTON / CLOSE_BUTTON）
	 * @param parent 父窗口指针
	 */
	TitleBar(ButtonType type, QWidget* parent);

	/**
	 * @brief 析构函数：无手动释放的资源，控件随 Qt 对象树自动析构。
	 */
	~TitleBar();

	/**
	 * @brief 设置标题栏左侧图标。
	 *
	 * setFixedSize(pix.size())：让 QLabel 尺寸与图片实际尺寸一致，避免拉伸或留白；
	 * setPixmap 后 QLabel 不会自动调整尺寸，必须手动设置。
	 * @param filepath 图标的 QRC 路径，如 ":/Resources/MainWindow/qqlogoclassic.png"
	 */
	void setTitleIcon(const QString& filepath);

	/**
	 * @brief 设置标题栏中间文字（纯 setter，样式由构造时的 QSS 定）。
	 * @param titleContent 要显示的标题文字，如 "QQ"、"登录"；传空串则不显示
	 */
	void setTitleContent(const QString& titleContent);

	/**
	 * @brief 设置标题栏宽度（通常等于父窗口宽度，paintEvent 里也会自动同步）。
	 * @param width 宽度像素值
	 */
	void setTitleWidth(int width);

	/**
	 * @brief 最大化时调用：记录当前窗口位置和大小。
	 *
	 * 数据存 TitleBar 是因为按钮点击事件在这里处理，实际 resize/move
	 * 由父窗口通过信号回调完成。
	 * @param point 当前窗口左上角坐标（this->pos()）
	 * @param size 当前窗口尺寸（this->size()）
	 */
	void saveRestoInfo(const QPoint& point, const QSize& size);

	/**
	 * @brief 还原时调用：通过引用参数带出之前保存的位置和大小。
	 * @param point [out] 带出最大化前的窗口位置
	 * @param size [out] 带出最大化前的窗口尺寸
	 */
	void getRestoInfo(QPoint& point, QSize& size);

	/**
	 * @brief 查询当前是否处于最大化状态。
	 *
	 * 不问窗口系统，而是看 buttonMax 是否可见（onButtonMaxClicked /
	 * onButtonRestoreClicked 切换按钮时就等于登记了状态）。
	 * 只对 MIN_MAX_CLOSE_BUTTON 判断，其他类型永远返回 false。
	 * @return true=已最大化（buttonMax 不可见）；false=正常
	 */
	bool windowIsMax();  // 窗口是否为最大化

private:
	/**
	 * @brief 初始化：加载样式、配置按钮、建立信号连接。
	 */
	void init();

	/**
	 * @brief 根据 ButtonType 显示/隐藏对应的系统按钮。
	 */
	void setTitleButton();

	/**
	 * @brief 连接按钮的 clicked 信号到对应槽函数（全部手动 connect）。
	 */
	void initConnect();

protected:
	// ========================================================================
	// 事件重写 — 实现无边框窗口的核心交互
	// 为什么需要重写这几个事件：
	//   Qt 原生只对系统标题栏响应窗口移动和双击最大化，
	//   去掉原生标题栏后，必须手动实现这些行为
	// ========================================================================

	/**
	 * @brief 拖拽三件套之按下：非最大化时开启拖拽并 accept（防父窗双重移动）。
	 *
	 * 用 event->accept() 而非调用基类：基类默认实质是 ignore，会把事件
	 * 传播给 BasicWindow 的同名处理，两处都算位移就产生拖动偏移；
	 * accept 还隐式抓取鼠标，快速移出标题栏拖拽也不断。
	 * 最大化时不 accept、不处理，让事件链继续传播（最大化窗口不许拖拽）。
	 * @param event 鼠标事件，用 globalPos() 取屏幕全局坐标
	 */
	void mousePressEvent(QMouseEvent* event) override;

	/**
	 * @brief 拖拽三件套之松开：停止拖拽并 accept（与 press/move 语义一致）。
	 * @param event 鼠标事件
	 */
	void mouseReleaseEvent(QMouseEvent* event) override;

	/**
	 * @brief 拖拽三件套之移动：非拖拽时忽略；拖拽中按 globalPos 增量移动父窗口。
	 *
	 * 用 globalPos 而非 pos：pos 是控件相对坐标，控件本身在动会累积误差；
	 * globalPos 是屏幕绝对坐标，计算精确。移动后 accept，防止事件传播给
	 * BasicWindow 造成双重移动。
	 * @param event 鼠标事件
	 */
	void mouseMoveEvent(QMouseEvent* event) override;

	/**
	 * @brief 双击标题栏 → 最大化/还原切换（Windows 标准行为）。
	 *
	 * 只对 MIN_MAX_CLOSE_BUTTON 响应；实现方式：直接调用对应按钮的
	 * 切换函数（模拟点击）。
	 * @param event 鼠标事件
	 */
	void mouseDoubleClickEvent(QMouseEvent* event) override;

	/**
	 * @brief 绘制事件：同步标题栏宽度与父窗口等宽。
	 *
	 * 放 paintEvent 而非 resizeEvent：paintEvent 是绘制周期入口，
	 * 窗口大小变化必定触发重绘，在此同步可保证始终等宽、无延迟。
	 * 不调用父类是因为 QWidget 的 paintEvent 是空函数。
	 * @param event 绘制事件
	 */
	void paintEvent(QPaintEvent* event) override;

signals:
	/**
	 * @brief 最小化按钮点击信号——由父窗口（BasicWindow）连接处理具体逻辑。
	 *
	 * 用信号而非直接调用父窗口方法：TitleBar 是通用控件，不应知道父窗口
	 * 的具体类型；通过信号-槽解耦，任何窗口都可自行决定按钮行为。
	 */
	void buttonMinClicked();

	/**
	 * @brief 最大化按钮点击信号——由父窗口连接执行真正的最大化。
	 */
	void buttonMaxClicked();

	/**
	 * @brief 关闭按钮点击信号——由父窗口连接执行关闭。
	 */
	void buttonCloseClicked();

	/**
	 * @brief 还原按钮点击信号——由父窗口连接执行窗口还原。
	 */
	void buttonRestoreClicked();

private slots:
	/**
	 * @brief 最大化按钮点击：隐藏 max、显示 restore，再发射 buttonMaxClicked。
	 *
	 * max 和 restore 互斥显示（同一位置），点击时立即切换可见性给视觉反馈。
	 * ⚠️ 命名故意去掉下划线（非 on_buttonMax_clicked）：Qt 会自动连接
	 * on_<控件名>_<信号名>() 槽，若被 setupUi 自动连一次、initConnect 再
	 * 手动连一次 → 一次点击触发两次 → 还原失效。
	 */
	void onButtonMaxClicked();

	/**
	 * @brief 还原按钮点击：隐藏 restore、显示 max，再发射 buttonRestoreClicked。
	 *
	 * 同上：驼峰命名打破 Qt 自动连接规则，全部由 initConnect 手动管理。
	 */
	void onButtonRestoreClicked();

private:
	Ui::TitleBarClass ui;   // 由 .ui 文件生成的界面类（布局 + 子控件）
	ButtonType btnType;     // 按钮组合类型（控制哪些系统按钮可见）

	bool    moveAble;       // 当前是否允许拖拽移动窗口
	QPoint  startMovePos;   // 拖拽开始时的鼠标全局坐标，用于计算位移增量

	QPoint  restorePos;     // 最大化前窗口位置
	QSize   restoreSize;    // 最大化前窗口大小
};
