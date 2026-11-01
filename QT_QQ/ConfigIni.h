#pragma once
#include <qcolor.h>

// ============================================================================
// 类：ConfigIni — 配置持久化（写入 / 读取 ini 文件）
//
// 为什么需要这个类：
//   用户更换皮肤、勾选"记住密码"后，这些设置需要保存到磁盘，
//   下次启动时才能恢复，否则每次启动都回到默认状态。
//
// 为什么用静态方法 + protected 构造函数（不可实例化）：
//   读写配置是无状态的工具操作，直接 ConfigIni::getColor() 调用即可，
//   与 CommonUtils 采用同一套路，无需创建对象。
// ============================================================================
class ConfigIni
{
protected:
	/**
	 * @brief 保护构造：本类只用静态方法，禁止外部实例化（与 CommonUtils 同套路）
	 */
	ConfigIni() = default;
	/**
	 * @brief 保护析构：配合保护构造，确保不可在栈/堆上创建对象
	 */
	~ConfigIni() = default;

public:
	/**
	 * @brief 把皮肤颜色的 R/G/B 三分量写入 exe 目录 / ini / color.ini 的 [COLOR] 节。
	 *        路径用 applicationDirPath 定位，保证无论从哪个 cwd 启动都写到同一位置。
	 * @param color 要保存的颜色（内部只取 R/G/B 三个分量存）
	 */
	static void saveColor(const QColor& color);

	/**
	 * @brief 从 exe 目录 / ini / color.ini 读皮肤颜色；文件不存在则先用默认蓝 (22,154,218) 写一份再读。
	 *        先写默认值的原因：QSettings 对不存在的键返回 0，直接读会得到黑色背景。
	 * @return QColor 读到的皮肤色；首次运行返回默认蓝 (22,154,218)
	 */
	static QColor getColor();

	//用户配置
	/**
	 * @brief 保存账号密码到 exe 目录 / ini / uer.ini 的 [USER] 节（"记住密码"功能）。
	 *        注意文件名是 uer.ini（原文拼写如此，不是 user），保持兼容不要改。
	 * @param account 账号字符串
	 * @param password 密码明文（练习项目直存，真实项目要加密哈希）
	 */
	static void saveUserProfile(const QString& account, const QString& password);

	/**
	 * @brief 读回上次保存的账号密码；文件不存在（首次运行）返回 false，不修改出参。
	 * @param account [out] 引用参数，把读到的账号带出来
	 * @param password [out] 引用参数，把读到的密码带出来
	 * @return true=ini 文件存在且读到了；false=没存过账号
	 */
	static bool getUserProfile(QString& account, QString& password);
};

