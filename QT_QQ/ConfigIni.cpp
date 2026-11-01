#include "stdafx.h"
#include "ConfigIni.h"
#include <qsettings.h>
#include <qapplication.h>
#include <qfile.h>
#include "define.h"

// ============================================================================
// saveColor：把皮肤颜色保存到 ini 文件
//
// 为什么路径用 QApplication::applicationDirPath() + "/ini/color.ini"：
//   颜色配置需要运行时「可写」，所以放在「可执行文件所在目录/ini/」下，
//   而不是 QRC 资源里（QRC 编译进 exe，只读，无法写回）。
//   用 applicationDirPath() 定位，保证无论从哪个工作目录启动都能找到同一位置。
//
// 为什么用 QSettings(IniFormat) 而不是手写 fopen/文本解析：
//   QSettings 封装了 ini 的读写、转义与跨平台差异，代码更简洁可靠。
// ============================================================================
/**
 * @brief 把皮肤颜色的 R/G/B 三分量写入 exe 目录 / ini / color.ini 的 [COLOR] 节。
 *        路径用 applicationDirPath 定位，保证无论从哪个 cwd 启动都写到同一位置；
 *        用 QSettings(IniFormat) 而非手写解析，跨平台更可靠。
 * @param color 要保存的颜色（内部只取 R/G/B 三个分量存）
 */
void ConfigIni::saveColor(const QColor& color)
{
	const QString path = INI_PATH("color.ini");  //应用程序所在目录/ini/color.ini
	QSettings set(path, QSettings::IniFormat);   // 以 ini 格式打开文件

	//将设置储存在ini文件中：写到 [COLOR] 节下的 red/green/blue 三个键
	set.setValue("COLOR/red",color.red());     // 写红色分量
	set.setValue("COLOR/green",color.green()); // 写绿色分量
	set.setValue("COLOR/blue",color.blue());   // 写蓝色分量
}

// ============================================================================
// getColor：从 ini 文件读取皮肤颜色
//
// 为什么先判断文件是否存在：
//   首次运行（或用户删掉了 ini）时 color.ini 不存在，
//   此时用默认蓝色 (22,154,218) 先写一份，保证后续读到的是有效颜色；
//   否则 QSettings 对不存在的键返回 0，会得到 QColor(0,0,0) 的黑色背景。
// ============================================================================
/**
 * @brief 从 exe 目录 / ini / color.ini 读皮肤颜色；文件不存在则先用默认蓝 (22,154,218) 写一份再读。
 *        先写默认值的原因：QSettings 对不存在的键返回 0，直接读会得到 QColor(0,0,0) 的黑色背景。
 * @return QColor 读到的皮肤色；首次运行返回默认蓝 (22,154,218)
 */
QColor ConfigIni::getColor()
{
	const QString path = INI_PATH("color.ini");  //应用程序所在目录

	if (!QFile::exists(path))   // 首次运行，ini 还不存在
	{
		saveColor(QColor(22, 154, 218));   // 用默认蓝写一份
	}

	QSettings set(path, QSettings::IniFormat);   // 打开（此时文件一定存在）

	//从ini文件读取设置：三个分量分别取出来
	int red = set.value("COLOR/red").toInt();       // 读红色分量
	int green = set.value("COLOR/green").toInt();   // 读绿色分量
	int blue = set.value("COLOR/blue").toInt();     // 读蓝色分量

	return QColor(red, green, blue);   // 拼成 QColor 返回
}

/**
 * @brief 保存账号密码到 exe 目录 / ini / uer.ini 的 [USER] 节（"记住密码"功能）。
 *        注意文件名是 uer.ini（原文拼写如此，不是 user），保持兼容不要改。
 * @param account 账号字符串
 * @param password 密码明文（练习项目直存，真实项目要加密哈希）
 */
void ConfigIni::saveUserProfile(const QString& account, const QString& password)
{
	const QString path = INI_PATH("uer.ini");   // 注意文件名是 uer.ini（原文拼写如此，不是 user）

	QSettings set(path, QSettings::IniFormat);   // 以 ini 格式打开
	set.setValue("USER/account", account);     // [USER] 节下写账号
	set.setValue("USER/password", password);     // 写密码（练习项目明文存，真实项目要加密）
}

/**
 * @brief 读回上次保存的账号密码；文件不存在（首次运行）返回 false，不修改出参。
 * @param account [out] 引用参数，把读到的账号带出来
 * @param password [out] 引用参数，把读到的密码带出来
 * @return true=ini 文件存在且读到了；false=没存过账号
 */
bool ConfigIni::getUserProfile(QString& account, QString& password)
{
	const QString path = INI_PATH("uer.ini");   // 拼 uer.ini 路径
	if (!QFile::exists(path))   // 没存过账号
	{
		return false;   // 首次运行，直接返回失败
	}

	QSettings set(path, QSettings::IniFormat);   // 打开已有 ini
	account = set.value("USER/account").toString();    // 把账号通过引用参数带出
	password = set.value("USER/password").toString();   // 密码带出
	return true;   // 读取成功
}
