#pragma once

#include <QWidget>
#include "ui_EmotionWindow.h"
#include "EmotionLabel.h"

/**
 * @brief 无边框半透明的表情选择面板：rowCount×columnCount 网格铺满全部表情，
 *        点选后发出 addEmotion(int) 信号，由 TalkWindowShell::onAddEmotion 接走。
 *        整个聊天壳只建一个、常驻隐藏（见 TalkWindowShell）。
 */
class EmotionWindow : public QWidget
{
	Q_OBJECT

public:
	/**
	 * @brief 表情面板构造：无边框（FramelessWindowHint）+ 子窗口（SubWindow，不进任务栏）+
	 *        透明背景（配合 QSS 画圆角），然后铺满网格。整壳只建一个、常驻隐藏。
	 * @param parent 父窗口指针（一般传聊天壳）
	 */
	EmotionWindow(QWidget *parent = Q_NULLPTR);
	/**
	 * @brief 析构：表情 label 都是本窗的子对象，随对象树释放
	 */
	~EmotionWindow();

private:
	/**
	 * @brief 按 rowCount×columnCount 网格铺满全部表情，编号 = 行*列数+列（改布局必须同步改编号，否则点 A 出 B）
	 */
	void initControl();
	/**
	 * @brief 造一个表情格子并把它的点击接到自己的 addEmotion 信号（插入逻辑归 TalkWindowShell）
	 * @param name 表情编号（= 行*列数+列）
	 * @return EmotionLabel* 新建的表情格子
	 */
	EmotionLabel* initLabel(int name);

signals:
	/**
	 * @brief 表情被点选，name = 表情编号；由 TalkWindowShell::onAddEmotion 接走
	 * @param name 表情编号
	 */
	void addEmotion(int name);

protected:
	/**
	 * @brief 无边框窗必须手动触发 PE_Widget 才能画出 QSS 的 background（和 BasicWindow 同理）
	 * @param event Qt 传入的绘制事件对象，转发给父类
	 */
	void paintEvent(QPaintEvent* event) override;

private:
	Ui::EmotionWindow ui;
	const int rowCount = 12;      //行数
	const int columnCount = 14;   //列数
};
