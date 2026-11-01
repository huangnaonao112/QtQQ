#pragma once

#include <QtWidgets/QDialog>
#include "ui_UserLogin.h"
#include "BasicWindow.h"

// ============================================================================
// 类：UserLogin — 登录界面窗口
//
// 继承关系：UserLogin → BasicWindow → QDialog
// BasicWindow 提供无边框和标题栏，UserLogin 只关注登录业务 UI。
//
// 为什么登录界面也用无边框窗口：
//   QQ 风格的登录界面是异形窗口（圆角、阴影、自定义背景），
//   系统原生标题栏会破坏整体的视觉设计。
//   用 BasicWindow + TitleBar 可以实现统一的 UI 风格。
// ============================================================================

/**
 * @class UserLogin
 * @brief 登录界面：校验空输入 → 数据库验证 → 记住密码可选 → 打开主窗口。
 *
 * 无边框异形窗（BasicWindow 提供标题栏，只给最小化+关闭）；构造时提前
 * 触发 UseMySQL 单例连库（懒加载第一次真正执行）；支持 --autotest 自动登录。
 */
class UserLogin : public BasicWindow
{
	Q_OBJECT

public:
	/**
	 * @brief 构造函数：初始化 UI 和标题栏，并提前触发数据库单例连接。
	 * @param parent 父窗口指针（一般 nullptr，登录窗是顶层窗口）
	 */
	UserLogin(QWidget *parent = nullptr);

	/**
	 * @brief 析构函数：界面控件由 Qt 对象树统一管理，无需手动释放。
	 */
	~UserLogin();

private:
	/**
	 * @brief 初始化：创建标题栏、加载样式表、设置 WA_StyledBackground、autotest 钩子、logo。
	 */
	void init();

	/**
	 * @brief 在登录窗正中画一个圆形 QQ logo（圆角裁剪后贴上）。
	 */
	void initPicture();

	/**
	 * @brief 启动时读 ini 里保存的账号密码，回填到输入框（"记住密码"功能）。
	 */
	void initUser();

private slots:
	/**
	 * @brief "登录"按钮点击槽：校验空输入 → 查数据库验证 → 成功则关登录窗、弹主界面。
	 *
	 * 由 Qt 自动连接（on_<控件名>_<信号> 命名规则），对应 ui.loginBtn 的 clicked 信号。
	 */
	void on_loginBtn_clicked();

private:
	Ui::UserLogin ui;  // 登录界面的 UI 布局（账号/密码输入框、登录按钮、记住密码 checkbox 等）
};
