#include "stdafx.h"
#include "NotifyManager.h"
#include "ConfigIni.h"

//单例：全局唯一的广播站（懒加载 + 线程安全 + 退出自动销毁，详见 UseMySQL.cpp 注释）
Q_GLOBAL_STATIC(NotifyManager, theInstance)   // Q_GLOBAL_STATIC：懒加载、线程安全、进程退出自动销毁

/**
 * @brief 构造函数：一般不直接 new，外部走 instance() 拿单例
 * @param parent 父 QObject，对象树析构用，传 nullptr 即可
 */
NotifyManager::NotifyManager(QObject *parent)
	: QObject(parent)
{
}

/**
 * @brief 析构函数：单例由 Q_GLOBAL_STATIC 管理，进程退出时自动销毁
 */
NotifyManager::~NotifyManager()
{
}

/**
 * @brief 拿单例：和 UseMySQL/WindowManager 同一套 Q_GLOBAL_STATIC 套路，全程返回同一个对象
 * @return NotifyManager* 唯一的 NotifyManager 实例
 */
NotifyManager* NotifyManager::instance()
{
	return theInstance();   // Q_GLOBAL_STATIC 生成的访问函数：首次调用构造，之后永远同一份
}

// ============================================================================
// notifySkinChanged：换肤的总开关——广播 + 落盘一步完成
// 为什么先广播再存盘：订阅方（各窗口 onSkinChanged）刷新不依赖 ini 写成功，
//   即使写盘失败当前会话的皮肤也已经换好；反过来先存盘则多一次无谓等待。
// ============================================================================
/**
 * @brief 换肤的总开关：先 emit skinChanged(color) 广播给所有订阅窗口刷新，再调 ConfigIni::saveColor 落盘。
 *        为什么先广播再存盘：订阅方刷新不依赖 ini 写成功，即使写盘失败当前会话皮肤也已换好。
 * @param color 用户选中的新皮肤颜色
 */
void NotifyManager::notifySkinChanged(const QColor& color)
{
	//先广播：所有 connect 了 skinChanged 的窗口立即各自刷新
	emit skinChanged(color);   // 广播给所有订阅方

	//再持久化：写进 color.ini，下次启动 BasicWindow 构造时
	//ConfigIni::getColor 读到的就是新颜色，皮肤选择跨进程保留
	ConfigIni::saveColor(color);   // 写进 exe 目录 / ini / color.ini
}
