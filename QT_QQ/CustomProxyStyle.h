#pragma once

#include <QProxyStyle>

// ============================================================================
// 类：CustomProxyStyle — 代理样式，用来"局部改掉" Qt 默认绘制行为
//
// 为什么用 QProxyStyle 而不是全局 setStyle：
//   QProxyStyle 可以只挂在某一个控件上（CCMainWindow::initContact 里
//   ui.treeWidget->setStyle(new CustomProxyStyle())），
//   只影响这个 QTreeWidget，不污染整个应用的样式。
//
// 本类只做一件事：
//   重写 drawPrimitive()，当绘制原语是 PE_FrameFocusRect（焦点虚线框）时
//   直接 return、什么都不画 —— 效果就是树节点被选中/获得焦点时，
//   周围不再出现那一圈难看的点状虚线，视觉更干净。
//
// drawPrimitive 的参数含义：
//   element   : 当前要画的是哪一种 GUI 元素（焦点框/按钮面板/复选框指示器...）
//   option    : 该元素的绘制参数（状态、矩形、调色板等）
//   painter   : 画笔，真正画图用它
//   widget    : 正在被绘制的控件指针（一般用不到）
// ============================================================================
class CustomProxyStyle : public QProxyStyle
{
public:
	/**
	 * @brief 重写底层元素绘制函数：只拦截 PE_FrameFocusRect（焦点虚线框）并直接 return 不画，
	 *        去掉 QTreeWidget 项获得焦点时的点状虚线；其余元素走 Qt 默认绘制，不做任何改动。
	 *        为什么用 QProxyStyle 而非全局 setStyle：可只挂在单个控件上，不污染整个应用。
	 * @param element 当前要画的 GUI 元素类型（焦点框/按钮面板/复选框指示器...）
	 * @param option 该元素的绘制参数（状态、矩形、调色板等）
	 * @param painter 画笔，真正画图用它
	 * @param widget 正在被绘制的控件指针（一般用不到）
	 */
	virtual void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = Q_NULLPTR) const override
	{
		if (element == PE_FrameFocusRect)          //如果要画的元素是"焦点虚线框"
		{
			return;             //直接返回，不画 → 去掉 QTreeWidget 项获得焦点时的虚线框
		}
		else
		{
			//其他元素保持 Qt 默认绘制行为，不做任何改动
			QProxyStyle::drawPrimitive(element, option, painter, widget);   // 转发给基类默认实现
		}
	}
};
