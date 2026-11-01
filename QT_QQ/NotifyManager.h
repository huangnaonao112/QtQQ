#pragma once

#include <QObject>

// ============================================================================
// 类：NotifyManager — 全局通知中心（信号广播站）
//
// 为什么需要它：
//   点了皮肤窗口的某个颜色后，"换肤"要影响的不只是 SkinWindow 自己，
//   而是所有打开着的窗口（登录窗、主界面……）。如果 SkinWindow 直接去
//   遍历/刷新其他窗口，就得认识所有窗口类，耦合死而且每加一个窗口都要改。
//
//   解法是经典的"发布/订阅"解耦（Qt 里就是信号槽）：
//     - SkinWindow 只管 emit 一个"皮肤变了(color)"通知；
//     - 任何关心皮肤变化的窗口自己 connect 这个信号，收到后刷新自己；
//     - 两边互不认识，都只认识 NotifyManager 这个中转站。
//
// 为什么是单例：
//   广播站必须全局唯一——SkinWindow 发的和 CCMainWindow 收的
//   必须是同一个对象上的信号，否则根本连不上。
//   连带职责：通知时顺手把颜色持久化到 color.ini（ConfigIni::saveColor），
//   下次启动 ConfigIni::getColor 就能恢复这次选的皮肤。
// ============================================================================
class NotifyManager : public QObject
{
	Q_OBJECT

public:
	/**
	 * @brief 构造函数：一般不直接 new，外部走 instance() 拿单例
	 * @param parent 父 QObject，对象树析构用，传 nullptr 即可
	 */
	NotifyManager(QObject *parent = nullptr);
	/**
	 * @brief 析构：单例由 Q_GLOBAL_STATIC 管理，进程退出时自动销毁
	 */
	~NotifyManager();

	/**
	 * @brief 全局访问点（和 UseMySQL 同款 Q_GLOBAL_STATIC 单例写法）
	 * @return NotifyManager* 唯一的 NotifyManager*，全程同一个对象
	 */
	static NotifyManager* instance();

signals:
	/**
	 * @brief 皮肤变化信号：想响应换肤的窗口都 connect 它
	 * @param color 新选中的皮肤颜色
	 */
	void skinChanged(const QColor& color);

public:
	/**
	 * @brief 发布通知：SkinWindow 点击颜色后调用；先 emit skinChanged(color) 广播给所有订阅窗口，
	 *        再调 ConfigIni::saveColor(color) 写进 ini 持久化。先广播再存盘的原因：订阅方刷新不依赖 ini 写成功。
	 * @param color 用户选中的新皮肤颜色
	 */
	void notifySkinChanged(const QColor& color);
};
