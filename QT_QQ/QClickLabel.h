#pragma once

#include <QLabel>

// ============================================================================
// 类：QClickLabel — "可点击的 QLabel"
//
// 为什么需要这个类：
//   QLabel 默认不响应鼠标点击（没有 clicked 信号）。SkinWindow 的 12 个
//   色块用 QLabel 填色最干净（QPushButton 自带边框/按下效果，还要额外清样式），
//   但 QLabel 又需要"被点击"的能力，于是继承 QLabel 重写 mousePressEvent，
//   在鼠标左键按下时 emit 一个自定义的 clicked() 信号。
//
// 使用方式（见 SkinWindow::initLabel）：
//   QClickLabel* label = new QClickLabel(this);
//   connect(label, &QClickLabel::clicked, [](){ ... });
// ============================================================================

/**
 * @class QClickLabel
 * @brief 可点击的 QLabel：重写鼠标左键按下事件，发射自定义 clicked() 信号。
 *
 * 专为 SkinWindow 的换肤色块设计——QLabel 无 clicked 信号，继承后补上，
 * 外部 connect clicked 即可响应点击，免去 QPushButton 的默认边框/按压样式负担。
 */
class QClickLabel  : public QLabel
{
	Q_OBJECT

public:
	/**
	 * @brief 构造函数：只把 parent 转发给 QLabel，不做任何额外初始化。
	 * @param parent 父控件，负责随 Qt 对象树一起销毁本对象
	 */
	QClickLabel(QWidget *parent);

	/**
	 * @brief 析构函数：无自定义资源，交给 Qt 对象树自动释放。
	 */
	~QClickLabel();

protected:
	/**
	 * @brief 重写鼠标按下事件：仅处理左键按下，按下即发射 clicked() 信号。
	 *
	 * 不调用基类 QLabel::mousePressEvent（事件已完整处理）；右键/中键直接
	 * 忽略、不转发，避免默认菜单/行为干扰。
	 * @param event 鼠标事件对象，用 event->button() 判断按键类型
	 */
	void mousePressEvent(QMouseEvent* event) override;

signals:
	/**
	 * @brief 自定义点击信号：鼠标左键按下时发射，外部 connect 它来响应色块被点。
	 */
	void clicked();
};

