#include "stdafx.h"
#include "SysTrayIcon.h"
#include <qwidget.h>
#include "CustomMenu.h"

/**
 * @brief 构造函数：保存宿主主窗口指针，并调用 init() 完成托盘初始化。
 * @param parent 宿主主窗口指针（CCMainWindow），显示/退出动作都经它执行
 */
SysTrayIcon::SysTrayIcon(QWidget *parent)
	: QSystemTrayIcon(parent),parent(parent)   // 基类用 parent 建托盘，成员再存一份供菜单动作使用
{
	init();   // 设置托盘图标、提示文字、连接点击信号
}

/**
 * @brief 析构：托盘图标归 QSystemTrayIcon 自身释放，菜单每次弹完就 deleteLater 了。
 */
SysTrayIcon::~SysTrayIcon()
{}

/**
 * @brief 初始化：配置托盘图标外观 + 接线"激活"信号。
 */
void SysTrayIcon::init()
{
	setToolTip("QtQQ");          // 设置系统托盘图标的提示信息（鼠标悬停时显示）
	setIcon(QIcon(":/Resources/MainWindow/app/logo.ico"));          // 设置系统托盘图标（QQ logo）

	connect(this, &QSystemTrayIcon::activated, this, &SysTrayIcon::onIconActivated);   // 托盘被点击 → 触发 onIconActivated
}

/**
 * @brief 弹出右键菜单：用 CustomMenu 创建 QQ 风格菜单，点"显示"显示主窗口，点"退出"结束程序。
 */
void SysTrayIcon::popupMenu()
{
	// 初始化菜单：parent 传主窗口，菜单随主窗口析构
	CustomMenu* menu = new CustomMenu(parent);   // QQ 风格自定义菜单，挂在主窗口下
	menu->addMenuAction("show", ":/Resources/MainWindow/app/logo.ico", QStringLiteral("显示"));   // 添加"显示"项
	menu->addMenuAction("quit", ":/Resources/MainWindow/app/page_close_btn_hover.png", QStringLiteral("退出"));   // 添加"退出"项

	// 连接信号与槽："显示" → 主窗口 onShow（显示并激活）；"退出" → 主窗口 onQuit（结束事件循环）
	connect(menu->getMenuAction("show"), SIGNAL(triggered()), parent, SLOT(onShow()));     // 显示菜单项 → 主窗 onShow
	connect(menu->getMenuAction("quit"), SIGNAL(triggered()), parent, SLOT(onQuit()));     // 退出菜单项 → 主窗 onQuit

	menu->exec(QCursor::pos());    // 在鼠标当前位置弹出菜单（阻塞，直到菜单被关闭）
	menu->deleteLater();           // 菜单关闭后延迟释放内存（exec 返回后安全删除）
}

/**
 * @brief 托盘图标点击响应：根据激活原因区分左键 / 右键。
 *
 * 左键（Trigger）→ 显示主窗口并拉到前台；右键（Context）→ 弹出右键菜单。
 * @param reason 激活原因：Trigger=左键单击；Context=右键单击
 */
void SysTrayIcon::onIconActivated(QSystemTrayIcon::ActivationReason reason)
{
	if (reason == QSystemTrayIcon::Trigger)      // 左键单击：显示主窗口并拉到前台
	{
		parent->show();                          // 重新显示主窗口
		parent->activateWindow();                // 把窗口拉到前台获得焦点
	}
	else if (reason == QSystemTrayIcon::Context)       // 右键单击：弹出右键菜单
	{
		popupMenu();                             // 在鼠标处弹"显示/退出"菜单
	}
}
