#include "stdafx.h"
#include "ContactItem.h"

// ============================================================================
// 构造函数：把 .ui 文件里摆好的布局实例化到当前 QWidget 上
// ui.setupUi(this) 是 Qt uic 工具生成的代码：
//   它会 new 出 ui 文件里所有控件（头像 label、名称 label 等），
//   并把它们挂到 this 下，同时按 .ui 里设置的 geometry/layout 摆好位置。
// ============================================================================
/**
 * @brief 联系人卡片构造：调用 ui.setupUi(this) 把 Designer 里摆好的控件（头像 label、名称 label）实例化到本窗体。
 * @param parent 父窗口指针（一般传 ui.treeWidget）
 */
ContactItem::ContactItem(QWidget *parent)
	: QWidget(parent)
{
	ui.setupUi(this);   // 根据 ContactItem.ui 创建子控件
}

/**
 * @brief 析构：子控件由 ui.setupUi 挂到对象树，随 this 自动析构，无需手动释放
 */
ContactItem::~ContactItem()
{}

/**
 * @brief 设置群名称：直接转发到 ui.userName（.ui 里放名称的 QLabel）
 * @param userName 要显示的群名称
 */
void ContactItem::setUserName(const QString & userName)
{
	ui.userName->setText(userName);   // 写到名称 label
}

/**
 * @brief 设置签名/备注：群组卡片用不到；TalkWindow::addMember 建成员列表时会填它
 * @param signName 签名或备注文本
 */
void ContactItem::setSignName(const QString & signName)
{
	ui.signName->setText(signName);   // 写到签名 label
}

/**
 * @brief 设置群头像：pixmap 已经由外部用 CommonUtils::getRoundedPixmap 裁成圆角，这里只负责贴到头像 label 上。
 * @param pixmap 已裁好的圆角头像
 */
void ContactItem::setHeadPixmap(const QPixmap & pixmap)
{
	ui.label->setPixmap(pixmap);   // 贴到头像 label
}

/**
 * @brief 读回群名称：外部（例如双击节点打开聊天窗时）可能需要拿到群名
 * @return QString 当前显示在 ui.userName 上的文本
 */
QString ContactItem::getUserName()
{
	return ui.userName->text();   // 返回名称 label 当前文本
}

/**
 * @brief 读回头像 label 的尺寸：CCMainWindow 在做圆角裁剪时，需要知道目标 label 的宽高，
 *        才能把 mask 缩放到完全匹配，避免头像错位。
 * @return QSize 头像 label 的当前宽高
 */
QSize ContactItem::getHeadLabelSize()
{
	return ui.label->size();   // 返回头像 label 尺寸
}


