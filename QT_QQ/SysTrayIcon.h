#pragma once

#include <QSystemTrayIcon>

// ============================================================================
// 类：SysTrayIcon — 系统托盘图标
//
// 为什么需要这个类：
//   程序不靠"最后一个窗口关闭"来退出（main.cpp 里
//   setQuitOnLastWindowClosed(false)，关窗口不退出程序），
//   托盘图标是常驻的恢复/退出入口，承担两个交互：
//     (1) 左键单击 → 重新显示主窗口
//     (2) 右键单击 → 弹出菜单（显示 / 退出）
//   本类把托盘的所有逻辑（创建图标、响应点击、弹出菜单）封装在一起，
//   CCMainWindow::initSysTray 只需 new 一个并 show()，无需关心内部实现。
// ============================================================================

/**
 * @class SysTrayIcon
 * @brief 系统托盘图标：左键显示主窗、右键弹"显示/退出"菜单，是关窗后不退出程序的恢复入口。
 *
 * 程序 setQuitOnLastWindowClosed(false) 后，托盘是常驻的恢复/退出入口；
 * 菜单动作最终转发给宿主主窗口的 onShow/onQuit 槽执行。
 */
class SysTrayIcon  : public QSystemTrayIcon
{
	Q_OBJECT

public:
	/**
	 * @brief 构造函数：保存宿主主窗口指针，并调用 init() 完成托盘初始化。
	 * @param parent 宿主主窗口指针；托盘要操作主窗口时（显示、退出）通过它调用对应槽
	 */
	SysTrayIcon(QWidget *parent);

	/**
	 * @brief 析构：托盘图标归 QSystemTrayIcon 自身释放，菜单每次弹完就 deleteLater 了。
	 */
	~SysTrayIcon();

private:
	/**
	 * @brief 初始化：设置托盘提示文字、托盘图标，并连接 activated 信号。
	 */
	void init();

	/**
	 * @brief 弹出右键菜单：创建"显示/退出"两个菜单项、连接信号，并在鼠标当前位置弹出。
	 */
	void popupMenu();

private slots:
	/**
	 * @brief 托盘图标被激活（点击）的响应槽：左键显示主窗，右键弹菜单。
	 * @param reason 激活原因——Trigger=左键单击；Context=右键单击
	 */
	void onIconActivated(QSystemTrayIcon::ActivationReason reason);

private:
	QWidget* parent;   // 宿主主窗口指针：托盘动作（显示窗口、退出程序）都通过它执行
};
