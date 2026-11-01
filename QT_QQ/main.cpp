#include "stdafx.h"
#include "UserLogin.h"
#include <QtWidgets/QApplication>
#include "TitleBar.h"
#include "ConfigIni.h"

// ============================================================================
// 程序入口
// 为什么先显示登录窗口而非主窗口：
//   QQ 客户端的启动流程是：登录 → 认证 → 进入主界面，
//   登录窗口是用户看到的第一个界面。
//   后续会在 UserLogin 的"登录"按钮点击事件中创建并显示主窗口。
// ============================================================================
/**
 * @brief 程序入口：创建 QApplication（必须先于任何 QWidget），设 setQuitOnLastWindowClosed(false)
 *        保证托盘常驻，然后显示登录窗口并进入事件循环。
 *        退出入口有两处：主界面 X 和托盘右键"退出"，都走 CCMainWindow::onQuit。
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组（含 --autotest 等测试开关）
 * @return int 事件循环退出码
 */
int main(int argc, char *argv[])
{
	QApplication app(argc, argv);   // QApplication 实例必须先于任何 QWidget 创建——Qt 的硬性要求
	//（QApplication 实例必须先于任何 QWidget 创建——Qt 的硬性要求）
	//关键：app.setQuitOnLastWindowClosed(false)，关闭最后一个窗口时不自动退出。
	//没有它，登录窗验证通过后先 close() 再创建主窗口的间隙、
	//或子窗口关到一个不剩时，Qt 会直接结束进程，托盘图标随之消失。
	//退出入口有两处：主界面 X 和托盘右键"退出"，都走 CCMainWindow::onQuit。
	app.setQuitOnLastWindowClosed(false);   // 本句开关：窗口全关程序也活着，托盘得以常驻
	UserLogin window;               // 创建登录窗口
	window.show();                  // 显示窗口

	return app.exec();              // 进入事件循环（阻塞，直到窗口关闭）
}
