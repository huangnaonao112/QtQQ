#include "stdafx.h"
#include "WindowManager.h"
#include "TalkWindowShell.h"
#include "TalkWindow.h"
#include "TalkWindowItem.h"
#include "UseMySQL.h"

Q_GLOBAL_STATIC(WindowManager, theInstance)   // 单例宏：懒加载 + 线程安全 + 自动析构

/**
 * @brief 构造函数：talkWindowShell 置空，在 addNewTalkWindow 里才懒创建。
 * @param parent 父 QObject
 */
WindowManager::WindowManager(QObject* parent)
	: QObject(parent), talkWindowShell(nullptr)   // 壳指针先置空
{}

/**
 * @brief 析构函数：壳指针不负责 delete（壳有 WA_DeleteOnClose 自己的生命周期）。
 */
WindowManager::~WindowManager()
{}

/**
 * @brief 全局单例访问器：Q_GLOBAL_STATIC 保证程序退出时自动析构，
 * 且首次调用才构造——比裸 static 还能避免"析构顺序不定"的老坑。
 * @return 唯一的 WindowManager* 指针
 */
WindowManager* WindowManager::instance()
{
	return theInstance();               // 宏生成的工厂
}

/**
 * @brief 按 id（群号/对方账号）查已注册的聊天窗口，查不到返回 nullptr。
 *
 * 调用方靠 nullptr 区分"窗口还没开过"和"窗口开着"，见 addNewTalkWindow。
 * @param id 群号/对方账号
 * @return 窗口指针或 nullptr
 */
QWidget* WindowManager::findWindowMap(const QString& id)
{
	//如果存在，则返回窗口，否则返回nullptr
	return map.value(id, nullptr);      // 查表，缺省 nullptr
}

/**
 * @brief 注册窗口。只在不存在时插入、不覆盖：同一 id 重复调用时保留老窗口指针，
 * 防止旧指针被顶掉后既没人用、也没人销毁（内存泄漏 + 状态错乱）。
 * @param id 群号/对方账号
 * @param widget 对应聊天窗
 */
void WindowManager::addWindowMap(const QString& id, QWidget* widget)
{
	if (!map.contains(id))       // 如果不存在，则添加窗口
	{
		map.insert(id, widget);  // 注册新窗口
	}
}

/**
 * @brief 注销：只从表里摘掉，不负责 delete 窗口本体——
 * 窗口销毁归 Qt 对象树/父窗口管，这里要是顺手 delete 反而会双重释放。
 * @param id 群号/对方账号
 */
void WindowManager::removeWindowMap(const QString& id)
{
	map.remove(id);              // 摘表，不 delete
}

/**
 * @brief 取聊天主框架壳：懒初始化前是 nullptr，SendFile 等弹窗会拿它连信号，
 * 所以调用方一般都在 addNewTalkWindow 之后才用（那时壳必已存在）。
 * @return TalkWindowShell* 或 nullptr
 */
TalkWindowShell* WindowManager::getTalkWindowShell() const
{
	return talkWindowShell;      // 返回壳指针（可能为空）
}

/**
 * @brief 一行 getter：返回正在创建中的窗口 id（TalkWindow 构造函数里会读它来定位分组）。
 * @return createID 字符串
 */
QString WindowManager::getCreateID() const
{
	return createID;             // "施工中"标记
}

// ============================================================================
// addNewTalkWindow：打开（或切到）id 对应的聊天窗口——客户端窗口生命周期的总入口
// 三条关键逻辑：
//   1) 聊天壳 TalkWindowShell 懒创建，且连 destroyed 置空指针：用户关掉壳
//      再点好友时不能拿着悬空指针去 addTalkWindow，之前没这根连线会崩；
//   2) createID 是给 TalkWindow 构造函数看的"施工中"标记——它构造时会调
//      getCreateID() 决定自己是群聊还是单聊身份，所以必须先赋值、new 完
//      马上 clear，避免影响下一次创建；
//   3) 窗口已存在就只切换显示（updateCurrentWindow），绝不重复 new——
//      重复 new 会出现同一 id 两个窗口、消息路由随机挑一个的老 bug。
// 群/单聊的判定依据是 id 长度（群号固定 4 位 ≤ GROUP_ID_LENGTH，单聊是
//   完整 QQ 号更长），名字、头像也走 MySQL 里不同的表。
// ============================================================================

/**
 * @brief 打开（或切到）id 对应的聊天窗口——客户端窗口生命周期的总入口。
 *
 * 三条关键逻辑：
 *   1) 壳懒创建 + destroyed 置空指针：用户关壳再点好友时不能拿悬空指针；
 *   2) createID 是给 TalkWindow 构造函数看的"施工中"标记——new 完立刻 clear；
 *   3) 窗口已存在只切换显示，绝不重复 new（重复 new 会有同 id 两窗口、
 *      消息路由随机挑一个的老 bug）。
 * @param id 群号或对方账号
 */
void WindowManager::addNewTalkWindow(const QString& id)
{
	if (!talkWindowShell)        // 壳还没建
	{
		talkWindowShell = new TalkWindowShell;   // 懒创建聊天主框架
		// 壳被销毁（用户关窗）时把指针清掉，下次进来重新创建，防止野指针
		connect(talkWindowShell, &TalkWindowShell::destroyed, [this]() { talkWindowShell = nullptr; });   // destroyed 置空
	}

	//判断聊天窗口是否已经存在
	QWidget* widget = findWindowMap(id);   // 查已有窗口
	if (!widget)                   // 不存在才新建
	{
		// 先亮牌再施工：TalkWindow 构造函数里要读 createID 决定群/单聊身份
		createID = id;             // 立"施工中"标记
		TalkWindow* talkWindow = new TalkWindow(id, talkWindowShell);   // 建聊天窗（注册进 map）
		TalkWindowItem* talkWindowItem = new TalkWindowItem(talkWindow);   // 建侧边栏项（parent 为聊天窗）
		createID.clear();   // 施工完毕立刻收起标记，别把上一次的 id 泄漏给下次创建

		//获取聊天窗口相关信息
		QString itemMsg;           // 侧边栏显示的名字
		QString windowName;        // 顶部标题
		QString headPath;          // 头像路径
		if (id.length() <= GROUP_ID_LENGTH)       // 群聊：4 位群号
		{
			itemMsg = UseMySQL::instance()->getGroupName(id);   // 查群名
			windowName = itemMsg;                // 群聊标题=群名
			headPath = UseMySQL::instance()->getGroupHeadPixmap(id);   // 查群头像
		}
		else          // 单聊：对方账号
		{
			// 单聊标题 = 部门 + 姓名（侧边栏和顶部标题都要能看出对方是谁）
			itemMsg = UseMySQL::instance()->getUserName(id);   // 查姓名
			windowName = UseMySQL::instance()->getUserDepartment(id) + itemMsg;   // 部门+姓名
			headPath = UseMySQL::instance()->getUserHeadPixmap(id);   // 查个人头像
		}

		//设置部件
		talkWindow->setWindowName(windowName);   // 刷顶部标题
		talkWindowItem->setMsgText(itemMsg);     // 刷侧边栏名字
		talkWindowItem->setHeadPixmap(headPath); // 刷侧边栏头像

		//聊天窗口添加到窗口框架
		talkWindowShell->addTalkWindow(talkWindow, talkWindowItem);   // 上架到壳（列表+堆栈+映射）

		// 顺手拉一次历史（服务端落库的记录），关掉窗口再打开就能看到之前聊了什么
		talkWindowShell->requestHistory(id);     // 拉最近历史（未连上会排队）
	}
	else        // 窗口已经存在
	{
		// 不是新建，只是把壳里已有的这个窗口切到前台显示
		talkWindowShell->updateCurrentWindow(widget);   // 切到已有窗
	}

	talkWindowShell->onShow();       // 显示聊天窗口
}
