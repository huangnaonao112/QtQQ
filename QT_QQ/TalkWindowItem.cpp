#include "stdafx.h"
#include "TalkWindowItem.h"
#include "CommonUtils.h"

/**
 * @brief 构造函数：搭出侧边栏一条会话项（头像 + 名字 + 关闭按钮）。
 *
 * parent 传的是对应 TalkWindow 本体（见 WindowManager::addNewTalkWindow），
 * 这样聊天窗销毁时会话项跟着一起被对象树带走，不用手动删。
 * @param parent 对应的 TalkWindow 本体
 */
TalkWindowItem::TalkWindowItem(QWidget *parent)
	: QWidget(parent)                    // 转交父类构造
{
	ui.setupUi(this);                    // 生成 ui 控件并绑定到本项
	init();                              // 关闭按钮隐藏 + 信号转发
}

/**
 * @brief 析构：控件归对象树管，无手动释放。
 */
TalkWindowItem::~TalkWindowItem()
{
}

/**
 * @brief 初始化：关闭按钮默认藏起来（鼠标悬停才显示），并把它的点击
 * 直接转发成自己的 signalCloseBtnClicked——信号接信号，本类不参与逻辑，
 * 关不关窗口由 TalkWindowShell::addTalkWindow 里接线的 lambda 决定。
 */
void TalkWindowItem::init()
{
	ui.tclosebtn->setVisible(false);     // 关闭按钮默认隐藏，悬停才出现
	connect(ui.tclosebtn, &QPushButton::clicked, this, &TalkWindowItem::signalCloseBtnClicked);         // 信号连接信号，只转发不处理
}

/**
 * @brief 设置头像：原图先用 mask 圆角裁剪再上，直贴原图会是个方块，和整体风格不搭。
 * @param headPath 头像图片资源路径
 */
void TalkWindowItem::setHeadPixmap(const QString & headPath)
{
	QPixmap head(headPath);     // 原图
	QPixmap mask(":/Resources/MainWindow/head_mask.png");         // 底片（圆角蒙版）
	ui.headLabel->setPixmap(CommonUtils::getRoundedPixmap(head, mask, ui.headLabel->size()));   // 裁剪成圆角后贴到头像标签
}

/**
 * @brief 设置会话项上的名字文本（群名 或 对方姓名）。
 * @param msg 名字文本
 */
void TalkWindowItem::setMsgText(const QString & msg)
{
	ui.msgLabel->setText(msg);           // 刷新名字标签
}

/**
 * @brief 鼠标移入：显示关闭按钮（QQ 老版交互——不悬停不给关，避免误点），再转交基类。
 * @param event 进入事件
 */
void TalkWindowItem::enterEvent(QEvent * event)
{
	ui.tclosebtn->setVisible(true);      // 悬停显示关闭按钮

	return QWidget::enterEvent(event);   // 基类默认处理
}

/**
 * @brief 鼠标移出：收起关闭按钮，恢复干净的列表样式。
 * @param event 离开事件
 */
void TalkWindowItem::leaveEvent(QEvent * event)
{
	ui.tclosebtn->setVisible(false);     // 移出隐藏关闭按钮

	return QWidget::leaveEvent(event);   // 基类默认处理
}
