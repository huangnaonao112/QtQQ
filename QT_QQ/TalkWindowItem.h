#pragma once

#include <QWidget>
#include "ui_TalkWindowItem.h"

/**
 * @class TalkWindowItem
 * @brief 侧边栏会话列表项：头像 + 名字 + 悬浮出现的关闭按钮。
 *
 * 关闭按钮默认隐藏，鼠标悬停才显示（QQ 老版交互防误点）；点击只把
 * 按钮 clicked 转发成 signalCloseBtnClicked，真正拆窗逻辑在
 * TalkWindowShell::addTalkWindow 接线的 lambda 里。
 */
class TalkWindowItem : public QWidget
{
	Q_OBJECT

public:
	/**
	 * @brief 构造函数：parent 传对应 TalkWindow，聊天窗销毁时会话项随对象树一起死。
	 * @param parent 对应的 TalkWindow 本体
	 */
	TalkWindowItem(QWidget *parent = Q_NULLPTR);

	/**
	 * @brief 析构函数：控件归对象树管，无手动释放。
	 */
	~TalkWindowItem();

private:
	/**
	 * @brief 关闭按钮默认隐藏，并把它的点击转发成 signalCloseBtnClicked（信号接信号）。
	 */
	void init();

public:
	/**
	 * @brief 设置头像：原图先用 mask 圆角裁剪再上，直贴原图会是个方块。
	 * @param headPath 头像图片资源路径
	 */
	void setHeadPixmap(const QString& headPath);

	/**
	 * @brief 设置侧边栏显示的群名/对方姓名。
	 * @param msg 名字文本
	 */
	void setMsgText(const QString& msg);

signals:
	/**
	 * @brief 关闭按钮被点；真正的拆窗逻辑在 TalkWindowShell::addTalkWindow 的 lambda 里。
	 */
	void signalCloseBtnClicked();

protected:
	/**
	 * @brief 鼠标进入：显示关闭按钮（不悬停不给关，避免误点），再转交基类。
	 * @param event 进入事件
	 */
	void enterEvent(QEvent *event) override;

	/**
	 * @brief 鼠标离开：收起关闭按钮，恢复干净的列表样式。
	 * @param event 离开事件
	 */
	void leaveEvent(QEvent *event) override;

private:
	Ui::TalkWindowItem ui;   // ui 指针持有本项全部控件（头像/名字/关闭按钮）
};
