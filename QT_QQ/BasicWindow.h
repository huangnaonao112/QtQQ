#pragma once

#include <QDialog>
#include "TitleBar.h"

// ============================================================================
// 类：BasicWindow — 所有窗口的基类
//
// 为什么需要 BasicWindow 这一层抽象：
//   项目中所有窗口（登录、主窗口、设置等）都需要：
//   (1) 无边框外观（自定义标题栏 + 皮肤背景）
//   (2) 一个 TitleBar 作为子控件
//   把公用的初始化逻辑（去边框、创建标题栏）放在基类中，
//   子类只需调用 initTitleBar 即可，避免重复代码。
//
// 为什么继承 QDialog 而非 QWidget：
//   QDialog 提供了对话框级别的窗口管理（模态/非模态、默认按钮等），
//   且 setWindowFlags 对 QDialog 的行为更加可预测。
//   如果要支持更复杂的窗口（如 QMainWindow 的菜单栏/工具栏），
//   可以另写一个继承 QMainWindow 的基类。
//
// 为什么构造函数是 protected：
//   基类不应该被直接实例化，只有子类可以构造。
// ============================================================================
class BasicWindow : public QDialog
{
	Q_OBJECT

protected:
	/**
	 * @brief 无边框窗口基类构造：统一去掉系统标题栏（FramelessWindowHint），从 color.ini 读上次皮肤色，
	 *        并订阅 NotifyManager::skinChanged 以便运行时换肤。构造为 protected，基类不可直接实例化。
	 * @param parent 父窗口指针（一般传 nullptr，顶层窗口）
	 */
	BasicWindow(QWidget *parent);
	/**
	 * @brief 析构：无动态资源需手动释放，界面控件由 Qt 对象树自动析构
	 */
	virtual ~BasicWindow();

protected:
	/**
	 * @brief 初始化标题栏：创建 TitleBar 控件并手动 move 到 (0,0)（不进布局），把四个按钮信号接到基类通用窗口控制槽。
	 *        为什么用 init 模式而非在构造函数中完成：C++ 中基类构造函数先于子类执行，
	 *        此时子类 UI 尚未 setupUi，父窗口大小可能还是默认值；延迟到子类构造里调用可以拿到正确尺寸。
	 * @param buttonType 按钮组合类型：MIN_CLOSE_BUTTON（最小化+关闭，登录窗）、
	 *                    MIN_MAX_CLOSE_BUTTON（最小化+最大化+关闭，主窗）、CLOSE_BUTTON（只关闭，提示弹窗）
	 * @param iconFileName 标题栏左侧图标 QRC 路径，如 ":/Resources/MainWindow/qqlogoclassic.png"；传空 QString() 则不显示
	 * @param titleContent 标题栏中间文字；传空则不显示
	 */
	void initTitleBar(ButtonType buttonType = MIN_CLOSE_BUTTON,
		const QString& iconFileName = QString(),
		const QString& titleContent = QString());

	/**
	 * @brief 加载 <filename>.css 并叠加运行时皮肤色规则（titleWidget 实色、bottomWidget 半透明）。
	 *        为什么包一层放在 BasicWindow：CommonUtils 只做通用读+设，这里额外注入 skinColor，
	 *        属于窗口业务，统一在基类处理，子类直接调用即可。
	 * @param filename QSS 文件名（不含路径与后缀），如 "UserLogin"、"CCMainWindow"、"Title"；
	 *                 内部用 QRC_QSS 宏拼成 ":/Resources/QSS/<filename>.css"
	 */
	void loadStyleSheet(const QString& filename);

protected:
	/**
	 * @brief 重写 paintEvent：手动触发 QStyle::drawPrimitive(PE_Widget)，让 QSS 的 background-color
	 *        在无边框 QDialog 上真正画出来。有系统边框的普通控件不需要这么做。
	 * @param event Qt 传入的绘制事件对象，直接转发给父类即可
	 */
	void paintEvent(QPaintEvent* event) override;

	/**
	 * @brief 鼠标按下：左键按下时记录拖动起点；最大化时禁止拖动。bar 可能为 nullptr（如 CCMainWindow），必须先判空。
	 * @param event 鼠标事件，event->button() 判断左键，event->globalPos() 取屏幕坐标
	 */
	void mousePressEvent(QMouseEvent* event) override;
	/**
	 * @brief 鼠标松开：结束拖动状态（moveAble 置 false）
	 * @param event 鼠标事件，转发给父类
	 */
	void mouseReleaseEvent(QMouseEvent* event) override;
	/**
	 * @brief 鼠标移动：若正在拖动，按全局坐标增量计算窗口新位置并 move
	 * @param event 鼠标事件，event->globalPos() 取屏幕坐标做增量
	 */
	void mouseMoveEvent(QMouseEvent* event) override;

public slots:
	/**
	 * @brief 最小化：工具窗口（Qt::Tool）直接 hide()（不进任务栏），普通窗口 showMinimized() 最小化到任务栏
	 */
	void onMin();
	/**
	 * @brief 最大化：先让 TitleBar 保存当前位置/大小（供还原用），再把窗口铺满主屏可用工作区（去任务栏）
	 */
	void onMax();
	/**
	 * @brief 还原：从 TitleBar 取出之前保存的位置/大小，setGeometry 回去
	 */
	void onRestore();
	/**
	 * @brief 关闭窗口：登录窗（objectName=="UserLoginClass"）直接退出整个程序，其余窗口只 close 本窗
	 */
	void onClose();


	/**
	 * @brief 隐藏窗口（hide，不销毁；常用于托盘"隐藏"菜单）
	 */
	void onHide();
	/**
	 * @brief 显示窗口并 activateWindow() 拉到前台（常用于托盘"显示"菜单）
	 */
	void onShow();
	/**
	 * @brief 退出整个应用：调 QApplication::quit() 终止事件循环
	 */
	void onQuit();

	/**
	 * @brief 换肤响应：收到 NotifyManager::skinChanged 信号时更新 skinColor，并用 styleFileName 重新加载样式表
	 * @param color 新选中的皮肤颜色
	 */
	void onSkinChanged(const QColor& color);

protected:
	TitleBar* bar = nullptr;  // 标题栏控件指针，供子类访问（连接信号等）。
	//为什么必须初始化为 nullptr：并非所有窗口都调 initTitleBar
	//（如 CCMainWindow 的标题栏画在 .ui 里），mousePressEvent 要先判空，
	//否则 bar 是野指针，在窗口上按下鼠标即崩溃

	bool moveAble = false;  //窗口是否可移动（拖动中=true）
	QPoint startMovePos;   //窗口开始移动时的鼠标全局坐标（拖动增量计算用）

	QColor skinColor;      // 当前皮肤颜色（构造时从 color.ini 读取，供 loadStyleSheet 上色）
	QString styleFileName; // 当前加载的样式文件名（记录，供后续换肤时重新加载）
};
