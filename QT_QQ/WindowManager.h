#pragma once

#include <QObject>
#include <QString>
#include "TalkWindowShell.h"
#include <qmap.h>

// ============================================================================
// WindowManager — 聊天窗口注册表
//
// key（id）用 QString 而不是 int：IM 账号可能是 QQ 号那种大数字
// （如 2652344414），超过 int 上限（2147483647）。用 int 时 QString::toInt()
// 会直接返回 0——窗口全挤到 key=0 上，JS 里生成的函数名也变成 recvHtml_0，
// 表现为"某个人的消息在别人窗口里显示不出来"。
// 群号（2000 这种 4 位）同样按字符串处理，代码里不再区分两套类型。
// ============================================================================

/**
 * @class WindowManager
 * @brief 聊天窗口注册表 + 打开/切换聊天窗的总入口（单例）。
 *
 * key 用 QString：IM 账号可能是超 int 上限的大数字，用 int 会全部挤到
 * key=0、JS 函数名变 recvHtml_0 导致消息渲染错乱；壳 TalkWindowShell
 * 懒创建并连 destroyed 置空，防关窗后再点好友拿悬空指针。
 */
class WindowManager  : public QObject
{
	Q_OBJECT

public:
	/**
	 * @brief 构造函数：不直接 new，外部统一走 instance()；talkWindowShell 置空懒创建。
	 * @param parent 父 QObject
	 */
	WindowManager(QObject *parent = nullptr);

	/**
	 * @brief 析构函数：壳指针不负责 delete（壳有自己的 WA_DeleteOnClose 生命周期）。
	 */
	~WindowManager();

public:
	/**
	 * @brief 全局唯一注册表访问器（Q_GLOBAL_STATIC，退出时自动析构）。
	 * @return 唯一的 WindowManager* 指针
	 */
	static WindowManager* instance();

	/**
	 * @brief 按 id 查已注册的聊天窗口，查不到返回 nullptr。
	 * @param id 群号/对方账号
	 * @return 聊天窗指针或 nullptr（"窗口还没开过"）
	 */
	QWidget* findWindowMap(const QString& id);

	/**
	 * @brief 注册窗口；id 已存在时不覆盖（先到先得，防旧指针被顶掉泄漏）。
	 * @param id 群号/对方账号
	 * @param widget 对应聊天窗
	 */
	void addWindowMap(const QString& id, QWidget* widget);

	/**
	 * @brief 注销窗口：只从表里摘掉，不负责 delete 窗口本体（否则双重释放）。
	 * @param id 群号/对方账号
	 */
	void removeWindowMap(const QString& id);

	/**
	 * @brief 聊天主框架壳指针；懒初始化前是 nullptr，调用方一般在 addNewTalkWindow 之后用。
	 * @return TalkWindowShell* 或 nullptr
	 */
	TalkWindowShell* getTalkWindowShell() const;

	/**
	 * @brief 正在创建中的窗口 id，仅 new TalkWindow 的构造过程内有效。
	 * @return createID 字符串
	 */
	QString getCreateID() const;

	/**
	 * @brief 核心入口：打开（或切到）id 对应的聊天窗——客户端窗口生命周期的总入口。
	 *
	 * 壳懒创建 + destroyed 置空；createID 是给 TalkWindow 构造函数看的
	 * "施工中"标记；已存在只切换显示绝不重复 new（重复 new 会有同 id
	 * 两窗口、消息路由随机挑一个的老 bug）。
	 * @param id 群号或对方账号
	 */
	void addNewTalkWindow(const QString& id);

private:
	TalkWindowShell* talkWindowShell;   // 聊天主框架壳（懒创建）
	QMap<QString, QWidget*> map;   // key: id（群号/对方账号，字符串） value: 聊天窗口
	QString createID;              // 正在创建的窗口 ID（TalkWindow 构造时读）
};
