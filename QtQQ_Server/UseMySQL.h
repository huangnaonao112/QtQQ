#pragma once

#include <QObject>        // 基类：支持信号槽 / parent 生命周期
#include <QSqlDatabase>   // MySQL 连接对象
#include <QVector>        // 查询结果的行列容器

// ============================================================================
// UseMySQL — 数据库访问单例（服务端唯一的 SQL 出入口）
//
// 接口和原版一一对应：部门/员工的查询、添加、注销，主窗口只管调
// instance()->xxx()，SQL 细节全部封在这一个类里。
//
// 为什么用 Q_GLOBAL_STATIC 单例而不是每次 new 一个：
//   (1) QSqlDatabase 连接创建开销大（TCP 三次握手 + 认证），
//       全局一份、全程复用最划算；
//   (2) 宏内部用原子操作保证多线程首次调用也只创建一份；
//   (3) 进程退出时自动析构，不用手写释放。
// ============================================================================

/**
 * @brief 数据库访问单例：部门/员工 CRUD + 消息持久化，服务端唯一的 SQL 出入口
 *
 * 为什么用 Q_GLOBAL_STATIC 单例：(1) QSqlDatabase 连接创建开销大，全局一份最划算；
 * (2) 宏内部用原子操作保证多线程首次调用也只创建一份；(3) 进程退出时自动析构。
 * 所有查询走 prepare + bindValue（占位符），杜绝 SQL 注入。
 */
class UseMySQL : public QObject
{
	Q_OBJECT

public:
	/**
	 * @brief 构造时自动连库（懒加载：第一次 instance() 才执行，进程内仅一次）
	 * @param parent 父对象指针，缺省 nullptr
	 */
	UseMySQL(QObject *parent = nullptr);
	/**
	 * @brief 析构：无手动释放（QSqlDatabase 连接随进程退出由 Qt 统一回收）
	 */
	~UseMySQL();

	/**
	 * @brief 返回全局唯一实例（Q_GLOBAL_STATIC 惰性构造）
	 * @return UseMySQL* 单例指针，永不为 null
	 */
	static UseMySQL* instance();

	/**
	 * @brief 按部门名查部门ID（如 "人事部" -> 2001）
	 * @param name 部门名（走占位符绑定，防注入）
	 * @return QString 部门ID；查不到（部门名拼错/库里没有）返回空串
	 */
	QString getDepartmentID(QString name);
	/**
	 * @brief 拉取一个部门的员工表（id 为公司群时拉全公司）
	 *
	 * 用 QSqlQueryModel 一次取回整个结果集（SELECT * 的列顺序就是数据库里的列顺序），
	 * fields 记下字段名，info 按 [行][列] 存值，交给主窗口填表格。
	 * 为什么不在 SQL 里 JOIN 部门名：主窗口要把 departmentID 列"翻译"成部门名显示，
	 * 这里保持原始数据、翻译交给界面层，职责更清楚。
	 * @param id 部门ID（传公司群ID则拉全表）
	 * @param fields[out] 结果集字段名列表（列序 = SELECT * 列序）
	 * @param info[out] 按 [行][列] 组织的查询结果
	 * @return bool 有数据返回 true；空结果或失败返回 false
	 */
	bool getDepartmentInfo(QString id, QStringList& fields, QVector<QVector<QString>>& info);
	/**
	 * @brief 按员工ID查单个员工（查询员工按钮用）
	 * @param id 员工ID（走占位符绑定，防注入）
	 * @param fields[out] 结果集字段名列表
	 * @param info[out] 该员工各列的值
	 * @return bool 找到返回 true；工号不存在返回 false
	 */
	bool getEmployeeInfo(QString id, QStringList& fields, QVector<QString>& info);
	/**
	 * @brief 按部门ID查部门名（表格里把 2001 翻译成 "人事部"）
	 * @param id 部门ID
	 * @return QString 部门名；查不到返回空串
	 */
	QString getDepartmentName(QString id);

	/**
	 * @brief 添加员工：两张表各插一行（tab_accounts + tab_employees），第二张失败时回滚第一张
	 *
	 * 工号分配：SELECT MAX(employeeID) 再 +1，保证新工号不与已有工号冲突。
	 * tab_accounts 存账号（工号 -> 登录码），tab_employees 存员工资料；
	 * 两张表必须同时成功，所以第二张表失败时要把第一张表刚插的行删掉（手动回滚），
	 * 避免留下"有账号没资料"的脏数据。
	 * picture 直接绑定路径字符串：占位符传参不做转义，正反斜杠都原样入库。
	 * @param departmentID 部门ID
	 * @param employeeName 员工姓名（同时写进 code 列当登录码）
	 * @param picture 头像路径字符串
	 * @return QString 新员工的工号；任何一步失败返回空串
	 */
	QString addEmployee(QString departmentID, QString employeeName, QString picture);
	/**
	 * @brief 注销员工（逻辑删除）：SELECT 确认存在后 UPDATE status=0，不物理删行
	 *
	 * 先 SELECT 确认工号存在：不存在返回 false。存在则 status 置 0——
	 * 不物理删行，历史聊天记录等外键数据不受影响，客户端登录时按 status=0 拒绝登录。
	 * @param id 要注销的员工ID
	 * @return bool 成功（工号存在并已 UPDATE）返回 true；工号不存在返回 false
	 */
	bool deleteEmployee(QString id);

	// —— 消息持久化（tab_messages）——
	// 一条消息的最小表示：谁发的、内容。渲染时接收方自己决定气泡归属。
	/**
	 * @brief 一条已存消息的最小表示：谁发的、内容（渲染时接收方自己决定气泡归属）
	 */
	struct StoredMessage
	{
		QString sender;  // 发送者账号
		QString text;    // 纯文本内容
	};

	/**
	 * @brief 落库一条消息；delivered 不写、靠表默认 0 进入待投递
	 *
	 * 空字段直接忽略，失败只打日志不报错（不阻断在线投递）。
	 * @param type 0=群聊 1=私聊
	 * @param sender 发送者账号
	 * @param recv 群号（群聊）或收件人账号（私聊）
	 * @param text 纯文本内容
	 */
	void saveMessage(int type, const QString& sender, const QString& recv, const QString& text);
	/**
	 * @brief 取该账号尚未收到的私聊（type=1、delivered=0），按时间升序，单次最多取 100 条
	 * @param account 上线者的账号
	 * @return QList<StoredMessage> 待补发的私聊列表；无或账号为空返回空列表
	 */
	QList<StoredMessage> pendingPrivateMessages(const QString& account);
	/**
	 * @brief 把该账号全部 delivered=0 的私聊一次性置为已投递
	 *
	 * 注意不只上面那 100 条：积压超过 100 条时，超出部分会被标成"已投递"却没补发
	 * （已知边界，见 LIMIT 100）。UPDATE 不带 LIMIT：宁可多标也不漏标——
	 * 漏标的后果是同一条离线消息每次上线都被重发一遍。
	 * @param account 上线者的账号
	 */
	void markPrivateDelivered(const QString& account);
	/**
	 * @brief 会话历史（含自己发的），按时间升序最多 limit 条
	 *
	 * 群聊按群号查 type=0；私聊按两个方向 OR 查询（对方发给我的 + 我发给对方的），
	 * 缺一边窗口就断档。SQL 倒序取，返回前再翻回正序，聊天窗才能从上往下按时间排。
	 * @param self 本机账号（私聊 OR 查询要用）
	 * @param with 群号（isGroup=true）或对方账号（isGroup=false）
	 * @param isGroup true=群聊 false=私聊
	 * @param limit 最多返回多少条（调用方应先 qBound 夹到合理范围）
	 * @return QList<StoredMessage> 正序历史；无数据返回空列表
	 */
	QList<StoredMessage> history(const QString& self, const QString& with, bool isGroup, int limit);

private:
	/**
	 * @brief 建立与 MySQL 的连接（进程里只执行一次）
	 *
	 * 连接参数按本机环境填写，与客户端工程共用同一个库 qt_qq_main。
	 * 排错顺序：(1) drivers() 里有没有 "QMYSQL"；(2) open() 失败时 lastError().text()。
	 * @return bool 连接成功返回 true；失败返回 false（原因已打到 qDebug）
	 */
	bool connectMySQL();

private:
	QSqlDatabase base;   // 名为 "main" 的 MySQL 连接（轻引用，进程退出时 Qt 自动释放）
};
