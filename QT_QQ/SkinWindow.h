#pragma once

#include "BasicWindow.h"
#include "ui_SkinWindow.h"
#include "QClickLabel.h"

// ============================================================================
// 类：SkinWindow — 皮肤设置窗口（12 宫格颜色选择）
//
// 工作流程：
//   initControl 把 12 个颜色块（QClickLabel）摆进 gridLayout；
//   用户点某块 → initLabel 里的 lambda 发 NotifyManager::notifySkinChanged；
//   所有窗口各自 connect skinChanged 信号刷新自己（发布/订阅，见 NotifyManager.h）。
// ============================================================================

/**
 * @class SkinWindow
 * @brief 换肤面板：3x4 宫格颜色块，点击后经 NotifyManager 广播换肤通知。
 *
 * 一次性临时窗（WA_DeleteOnClose），每次点工具栏按钮重开；色块用
 * QClickLabel 数据驱动生成，点击 lambda 按值捕捉颜色并发换肤通知。
 */
class SkinWindow : public BasicWindow
{
	Q_OBJECT

public:
	/**
	 * @brief 构造函数：一次性换肤面板（WA_DeleteOnClose，关闭即 delete 不常驻）。
	 *
	 * 颜色块不在构造里摆，留给 initControl 数据驱动生成。
	 * @param parent 父窗口指针（一般 nullptr）
	 */
	SkinWindow(QWidget *parent = nullptr);

	/**
	 * @brief 析构函数：WA_DeleteOnClose 已保证关闭时自动释放本窗口，此处无额外清理。
	 */
	~SkinWindow();

private:
	/**
	 * @brief 生成 12 宫格颜色块（3 行 x 4 列）摆进 gridLayout，并接线标题栏最小化/关闭按钮。
	 *
	 * 用代码生成而非画在 .ui：12 块布局相同只有颜色不同，数据驱动
	 * （双重循环+颜色数组）比 Designer 手摆省事，加减颜色只改数组。
	 * 用 QClickLabel 而非 QPushButton：按钮自带边框/按下效果还需清样式，
	 * QLabel 填 palette 最干净，"点击"能力由 QClickLabel 补上。
	 */
	void initControl();

	/**
	 * @brief 工厂方法：创建一个带点击信号的颜色块 QClickLabel。
	 *
	 * 用 setPalette 而非样式表：palette 不经 QSS 引擎，色块不参与换肤，
	 * 用最底层方式上色最稳。lambda 必须按值捕捉 color——形参是栈变量，
	 * 引用捕捉到点击触发时早已悬空（所有"捕捉局部变量且信号延迟触发"
	 * 的 lambda 都要遵守这条）。
	 * @param color 这个色块要显示的 QColor
	 * @return 建好的 QClickLabel*，由 initControl 加进 gridLayout
	 */
	QClickLabel* initLabel(const QColor& color);

private:
	Ui::SkinWindow ui;  // SkinWindow.ui 生成的布局（含 gridLayout、sysmin/sysclose 按钮）
};
