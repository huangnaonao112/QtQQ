#pragma once

#include <QWidget>
#include "ui_ContactItem.h"

// ============================================================================
// 类：ContactItem — 联系人树里"群组节点"上挂的自定义控件
//
// 在整个界面里的位置：
//   CCMainWindow::addUserGroup 里，每查到一个群，就：
//     new 一个 QTreeWidgetItem（树节点本身，只存数据）
//     new 一个 ContactItem（真正显示在树节点上的小卡片：头像 + 群名）
//     再用 ui.treeWidget->setItemWidget(item, 0, contactItem) 把卡片贴到节点上
//   所以 ContactItem 本质是"用 Designer 摆好布局的 QWidget 卡片"，
//   内部通过 ui.xxx 访问头像 label 和名称 label。
//
// 为什么不直接用 QTreeWidgetItem::setText()：
//   QTreeWidgetItem 只能显示纯文字一行，做不出"左边圆形头像 + 右边群名"的
//   QQ 风格卡片。把卡片做成 QWidget 再 setItemWidget 贴上去，
//   布局、样式、圆角头像都能在 .ui 里自由拖。
// ============================================================================
class ContactItem : public QWidget
{
	Q_OBJECT

public:
	/**
	 * @brief 联系人卡片构造：内部只调 ui.setupUi(this) 把 Designer 里摆好的控件（头像 label、名称 label）实例化。
	 * @param parent 父窗口指针（一般传 ui.treeWidget）
	 */
	ContactItem(QWidget *parent = nullptr);
	/**
	 * @brief 析构：子控件由 ui.setupUi 挂到对象树，随 this 自动析构
	 */
	~ContactItem();

	// 对外暴露的 4 个 setter/getter，供 CCMainWindow 在挂节点时填数据：
	/**
	 * @brief 设置群名称：写到 ui.userName（.ui 里放名称的 QLabel）
	 * @param userName 要显示的群名称
	 */
	void setUserName(const QString& userName);
	/**
	 * @brief 设置签名/备注：群组卡片不用；TalkWindow::addMember 建成员列表时会填它
	 * @param signName 签名或备注文本
	 */
	void setSignName(const QString& signName);
	/**
	 * @brief 设置圆角群头像：pixmap 已由外部用 CommonUtils::getRoundedPixmap 裁好，这里只贴到 ui.label
	 * @param pixmap 已裁好的圆角头像
	 */
	void setHeadPixmap(const QPixmap& pixmap);
	/**
	 * @brief 读回群名称：外部（例如双击节点打开聊天窗时）可能需要拿到群名
	 * @return QString 当前显示在 ui.userName 上的文本
	 */
	QString getUserName();
	/**
	 * @brief 读回头像 label 的尺寸：外部做圆角裁剪时需要知道目标宽高，才能把 mask 缩到完全匹配
	 * @return QSize 头像 label 的当前宽高
	 */
	QSize getHeadLabelSize();

private:
	Ui::ContactItem ui;  // 类名由 .ui 文件的 <class> 标签决定（新 ui 里是 ContactItem，
	                     // 不是 ContactItemClass，两边必须一致，否则报"不是 Ui 的成员"）
};

