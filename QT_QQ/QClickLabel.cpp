#include "stdafx.h"
#include "QClickLabel.h"

/**
 * @brief 构造函数：直接转发给 QLabel，本类不做任何额外初始化。
 * @param parent 父控件指针，交由 Qt 对象树管理生命周期
 */
QClickLabel::QClickLabel(QWidget *parent)
	: QLabel(parent)   // 初始化列表：把 parent 转交给基类 QLabel 的构造
{}

/**
 * @brief 析构函数：无特殊资源需要释放，交由 Qt 对象树处理。
 */
QClickLabel::~QClickLabel()
{}

/**
 * @brief 鼠标按下时触发：左键按下则发射 clicked() 信号，右键/中键忽略。
 *
 * 只响应 Qt::LeftButton；按下即 emit clicked()，后续动作交给外部槽
 * （SkinWindow 里是发换肤通知）。未调用 QLabel::mousePressEvent——
 * 事件已完整处理，基类默认行为（无）无需保留。
 * @param event 鼠标事件，含按键类型信息
 */
void QClickLabel::mousePressEvent(QMouseEvent * event)
{
	if (event->button() == Qt::LeftButton)   // 只响应左键按下
	{
		emit clicked();    // 发射自定义信号，通知外部"被点了"
	}
}

