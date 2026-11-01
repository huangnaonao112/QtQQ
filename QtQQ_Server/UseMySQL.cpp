#include "UseMySQL.h"
#include <QDebug>          // 调试日志
#include <QSqlError>       // lastError().text()
#include <QSqlQuery>       // 执行 SQL
#include <QSqlQueryModel>  // 一次取回整个结果集
#include <QSqlRecord>      // 取字段名 / 列值
#include "AppConfig.h"     // 连接参数（server.ini）

Q_GLOBAL_STATIC(UseMySQL, theInstance)   // 进程级单例：首次调用才构造，退出时析构

/**
 * @brief 构造即连库：Q_GLOBAL_STATIC 保证整个进程只在第一次 instance() 时执行一次
 * @param parent 父对象指针，缺省 nullptr
 */
UseMySQL::UseMySQL(QObject *parent)
	: QObject(parent)
{
	connectMySQL();                           // 构造即发起连接（失败只打日志，不抛）
}

/**
 * @brief 析构：无手动释放（QSqlDatabase 连接随进程退出由 Qt 统一回收）
 */
UseMySQL::~UseMySQL()
{
}

/**
 * @brief 返回全局唯一实例（Q_GLOBAL_STATIC 惰性构造）
 * @return UseMySQL* 单例指针，永不为 null
 */
UseMySQL* UseMySQL::instance()
{
	return theInstance();                     // 触发宏内部的惰性构造 + 返回全局对象
}

/**
 * @brief 建立与 MySQL 的连接（进程里只执行一次）
 *
 * 连接参数按本机环境填写，与客户端工程共用同一个库 qt_qq_main：
 * 服务端在这里添加/注销员工，客户端登录查的就是同一张 tab_accounts，
 * 两边必须连同一个库整个系统才能联动。
 * 排错顺序（连不上时照这个查）：
 *   (1) drivers() 里有没有 "QMYSQL" —— 没有说明 plugins\sqldrivers 缺
 *       qsqlmysql.dll（32/64 位必须和 Qt 套件一致）；
 *   (2) open() 失败时 lastError().text() 是服务器返回的真实原因
 *       （密码错 / 库名错 / SSL 失败），比弹窗信息完整，所以打到 qDebug。
 * @return bool 连接成功返回 true；失败返回 false（原因已打到 qDebug）
 */
bool UseMySQL::connectMySQL()
{
	// 调试期保留：一眼确认 QMYSQL 是否加载成功
	qDebug() << QSqlDatabase::drivers();      // 打印当前可用的 SQL 驱动列表

	// addDatabase 只是"登记连接参数"，真正建立 TCP 连接发生在 open()
	// 连接参数来自 server.ini（AppConfig），不再写死在代码里
	const AppConfig& cfg = AppConfig::instance(); // 读一次配置（端口/主机/账号/密码/库名）
	base = QSqlDatabase::addDatabase("QMYSQL", "main");   // 显式命名连接，避免默认连接被别处覆盖
	base.setDatabaseName(cfg.dbName);         // 库名：qt_qq_main
	base.setHostName(cfg.dbHost);             // 主机：127.0.0.1
	base.setUserName(cfg.dbUser);             // 用户名：root
	base.setPassword(cfg.dbPassword);         // 密码（明文，部署时注意权限）
	base.setPort(cfg.dbPort);                 // 端口：3306

	if (base.open())                          // 真正建立 TCP 连接 + 认证
	{
		qDebug() << "successed to open database"; // 连接成功
		return true;                           // 告诉构造函数连上了
	}
	else
	{
		qDebug() << "failed to open database"; // 连接失败
		qDebug() << base.lastError().text();   // 关键排错信息：驱动缺失 / 认证失败 / SSL 失败都在这里
		return false;                          // 失败不抛异常，员工查询会整体失败但投递不受影响
	}
}

/**
 * @brief 按部门名查部门ID（如 "人事部" -> 2001）
 *
 * prepare + addBindValue：'?' 是占位符，name 只当作"数据"传给服务器，
 * 不会被拼进 SQL 语法，从原理上杜绝注入（比原版的 .arg() 字符串拼接安全）。
 * @param name 部门名（走占位符绑定，防注入）
 * @return QString 部门ID；查不到（部门名拼错/库里没有）返回空串
 */
QString UseMySQL::getDepartmentID(QString name)
{
	// prepare + addBindValue：'?' 是占位符，name 只当作"数据"传给服务器，
	// 不会被拼进 SQL 语法，从原理上杜绝注入（比原版的 .arg() 字符串拼接安全）
	QSqlQuery sql(base);                     // 用命名连接 "main" 建查询对象
	sql.prepare("SELECT departmentID FROM tab_department WHERE departmentName = ?"); // 占位符 SQL
	sql.addBindValue(name);                  // 绑定部门名，防注入
	if (sql.exec() && sql.next())            // 执行且有结果行
	{
		return sql.value(0).toString();      // 第 0 列 = departmentID
	}

	return QString();                        // 查不到：返回空串让调用方报错
}

/**
 * @brief 拉取一个部门的员工表（id 为公司群时拉全表）
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
bool UseMySQL::getDepartmentInfo(QString id, QStringList& fields, QVector<QVector<QString>>& info)
{
	QString companyID = getDepartmentID(QStringLiteral("公司群"));        // 获取公司群ID

	QSqlQuery q(base);                       // 用命名连接建查询对象
	if (id == companyID)                     // 公司群：拉全表
	{
		q.exec("SELECT * FROM tab_employees"); // 无 WHERE，返回所有员工
	}
	else
	{
		//占位符绑定：id 可能来自界面输入框
		q.prepare("SELECT * FROM tab_employees WHERE departmentID = :id"); // 具名占位符 SQL
		q.bindValue(":id", id);              // 绑定部门ID，防注入
		q.exec();                            // 执行查询
	}

	QSqlQueryModel model;                    // 用 Model 一次取回整个结果集
	model.setQuery(q);                       // 把已执行的 query 塞进 model

	int rowCount = model.rowCount();         // 行数
	int columnCount = model.columnCount();   // 列数

	if (rowCount <= 0 || columnCount <= 0) return false; // 空结果：直接返回 false，fields/info 保持空

	for (int j = 0; j < columnCount; j++)
	{
		fields.push_back(model.record(0).fieldName(j));     // 获取字段名（从第 0 行的 record 拿列名）
	}

	for (int i = 0; i < rowCount; i++)
	{
		QVector<QString> rowTemp;            // 每一行
		for (int j = 0; j < columnCount; j++)
		{
			// record(i) 返回第 i 行的记录，value(j) 返回第 j 列的值
			rowTemp.push_back(model.record(i).value(j).toString()); // 取值转字符串
		}
		info.push_back(rowTemp);             // 整行加入 info
	}

	return true;                             // 有数据：返回 true
}

/**
 * @brief 按员工ID查单个员工（查询员工按钮用）
 *
 * 占位符绑定：id 来自界面的"查询员工"输入框。
 * @param id 员工ID（走占位符绑定，防注入）
 * @param fields[out] 结果集字段名列表
 * @param info[out] 该员工各列的值
 * @return bool 找到返回 true；工号不存在返回 false
 */
bool UseMySQL::getEmployeeInfo(QString id, QStringList& fields, QVector<QString>& info)
{
	//占位符绑定：id 来自界面的"查询员工"输入框
	QSqlQuery q(base);                       // 用命名连接建查询对象
	q.prepare("SELECT * FROM tab_employees WHERE employeeID = :id"); // 具名占位符 SQL
	q.bindValue(":id", id);                  // 绑定工号，防注入
	q.exec();                                // 执行查询

	QSqlQueryModel model;                    // 用 Model 取结果集
	model.setQuery(q);                       // 塞进 model

	int rowCount = model.rowCount();         // 行数
	int columnCount = model.columnCount();   // 列数

	if (rowCount <= 0 || columnCount <= 0) return false; // 工号不存在：返回 false

	QSqlRecord record = model.record(0);     // 获取第一行记录
	for (int j = 0; j < columnCount; j++)
	{
		fields.push_back(record.fieldName(j));       // 获取字段名
		info.push_back(record.value(j).toString());  // 获取记录值
	}

	return true;                             // 找到：返回 true
}

/**
 * @brief 按部门ID查部门名（表格里把 2001 翻译成 "人事部"）
 * @param id 部门ID
 * @return QString 部门名；查不到返回空串
 */
QString UseMySQL::getDepartmentName(QString id)
{
	QSqlQuery sql(base);                     // 用命名连接建查询对象
	sql.prepare("SELECT departmentName FROM tab_department WHERE departmentID = ?"); // 占位符 SQL
	sql.addBindValue(id);                    // 绑定部门ID，防注入
	if (sql.exec() && sql.next())            // 执行且有结果行
	{
		return sql.value(0).toString();      // 第 0 列 = departmentName
	}

	return QString();                        // 查不到：返回空串
}

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
QString UseMySQL::addEmployee(QString departmentID, QString employeeName, QString picture)
{
	QSqlQuery sql(base);                     // 用命名连接建查询对象
	if (!sql.exec(QString("SELECT MAX(employeeID) FROM tab_accounts"))) return QString(); // 取当前最大工号，失败返回空

	sql.first();                                    // 指向第一条可用记录
	int employeeID = sql.value(0).toInt() + 1;      // 员工ID（新工号）

	sql.prepare("INSERT INTO tab_accounts(employeeID, code) VALUES(?, ?)"); // 插账号表
	sql.addBindValue(employeeID);              // 绑定新工号
	sql.addBindValue(employeeName);            // 绑定登录码（=姓名）
	if (!sql.exec())                           // 插失败
	{
		return QString();                      // 返回空串让调用方报错
	}

	sql.prepare("INSERT INTO tab_employees(departmentID, employeeID, employeeName, picture, status) VALUES(?, ?, ?, ?, 1)"); // 插资料表
	sql.addBindValue(departmentID);            // 绑定部门ID
	sql.addBindValue(employeeID);              // 绑定工号
	sql.addBindValue(employeeName);            // 绑定姓名
	sql.addBindValue(picture);                 // 绑定头像路径
	if (!sql.exec())                           // 插失败：回滚第一张表
	{
		sql.prepare("DELETE FROM tab_accounts WHERE employeeID = ?"); // 删除刚才插的账号行
		sql.addBindValue(employeeID);          // 绑定要删的工号
		sql.exec();   // 删除之前添加的记录
		return QString();                      // 返回空串让调用方报错
	}

	return QString::number(employeeID);        // 两张表都成功：返回新工号字符串
}

/**
 * @brief 注销员工（逻辑删除）：SELECT 确认存在后 UPDATE status=0，不物理删行
 *
 * 先 SELECT 确认工号存在：不存在返回 false，主窗口提示"查无此人"。
 * 存在则 status 置 0——不物理删行，历史聊天记录等外键数据不受影响，
 * 客户端登录时按 status=0 拒绝登录，这就是"注销"的全部语义。
 * @param id 要注销的员工ID
 * @return bool 成功（工号存在并已 UPDATE）返回 true；工号不存在返回 false
 */
bool UseMySQL::deleteEmployee(QString id)
{
	QSqlQuery sql(base);                     // 用命名连接建查询对象
	sql.prepare("SELECT * FROM tab_employees WHERE employeeID = ?"); // 先查是否存在
	sql.addBindValue(id);                    // 绑定工号，防注入
	sql.exec();                              // 执行查询

	if (!sql.next()) return false;       // 不存在

	sql.prepare("UPDATE tab_employees SET status = 0 WHERE employeeID = ?"); // 逻辑删除
	sql.addBindValue(id);                    // 绑定工号
	sql.exec();                              // 执行 UPDATE

	return true;                             // 注销成功
}

// ============================================================================
// 消息持久化（tab_messages）
//
// 表结构：
//   id        自增主键，天然等于时间顺序
//   type      0=群聊 1=私聊
//   sender    发送者账号
//   recv      群号（群聊）或收件人账号（私聊）
//   text      纯文本（文件只存 "[文件] 文件名"，不存内容）
//   delivered 私聊专用：0=收件人还没收到（离线消息），1=已投递；
//     INSERT 不写这列，靠表默认值 0——消息一落库就自动进入"待投递"状态
//
// 为什么消息放服务端：客户端关掉就什么都没有了，换台机器也看不到历史。
// 服务端本来就在中转所有消息，顺手落库最自然。
// ============================================================================

/**
 * @brief 往 tab_messages 插一条消息（表结构与设计动机见 UseMySQL.h 章节注释）
 *
 * delivered 列故意不写：靠表默认值 0，消息一落库就自动进入"待投递"，
 * 收件人上线即被 deliverPending 捞起，不需要投递方再做登记。
 * 空字段直接丢弃（残包不入库）；失败只打日志不向上报错——
 * 落库失败不应该阻断在线投递，历史缺一条比消息卡死强。
 * @param type 0=群聊 1=私聊
 * @param sender 发送者账号
 * @param recv 群号（群聊）或收件人账号（私聊）
 * @param text 纯文本内容
 */
void UseMySQL::saveMessage(int type, const QString& sender, const QString& recv, const QString& text)
{
	if (text.isEmpty() || sender.isEmpty() || recv.isEmpty()) // 空字段：残包直接丢弃
		return;

	QSqlQuery sql(base);                     // 用命名连接建查询对象
	sql.prepare("INSERT INTO tab_messages(type, sender, recv, text) " // 落库 SQL（不含 delivered）
				"VALUES(:type, :sender, :recv, :text)");       // 四个具名占位符
	sql.bindValue(":type", type);            // 绑定 0=群聊 1=私聊
	sql.bindValue(":sender", sender);        // 绑定发送者
	sql.bindValue(":recv", recv);            // 绑定收件人/群号
	sql.bindValue(":text", text);            // 绑定正文
	if (!sql.exec())                         // 插失败
		qWarning() << "[sql] 保存消息失败:" << sql.lastError().text(); // 只打日志，不阻断投递
}

/**
 * @brief 上线补发的取数口：type=1 且 delivered=0 的私聊，按 id 升序，LIMIT 100
 *
 * 按 id 升序保证补发顺序与当初投递顺序一致；LIMIT 100 是单次补发上限
 * （与 markPrivateDelivered 的"全量置位"配对产生的已知边界，契约见 .h）。
 * @param account 上线者的账号
 * @return QList<StoredMessage> 待补发的私聊列表；无或账号为空返回空列表
 */
QList<UseMySQL::StoredMessage> UseMySQL::pendingPrivateMessages(const QString& account)
{
	QList<StoredMessage> out;                // 结果列表
	if (account.isEmpty())                   // 空账号：直接返回空
		return out;

	QSqlQuery sql(base);                     // 用命名连接建查询对象
	sql.prepare("SELECT sender, text FROM tab_messages " // 只取需要的两列
				"WHERE type = 1 AND recv = :account AND delivered = 0 " // 私聊 + 收件人 + 未投递
				"ORDER BY id ASC LIMIT 100"); // 按 id 升序，单次最多 100 条
	sql.bindValue(":account", account);      // 绑定账号，防注入
	if (sql.exec())                          // 查询成功
	{
		while (sql.next())                   // 逐行读取
			out.append(StoredMessage{ sql.value(0).toString(), sql.value(1).toString() }); // sender + text
	}
	return out;                              // 可能为空列表
}

/**
 * @brief 补发完成后把该账号全部 delivered=0 一次性置 1
 *
 * UPDATE 不带 LIMIT：宁可多标（已知边界见 .h 的 LIMIT 100 说明）也不
 * 漏标——漏标的后果是同一条离线消息每次上线都被重发一遍。
 * @param account 上线者的账号
 */
void UseMySQL::markPrivateDelivered(const QString& account)
{
	if (account.isEmpty())                   // 空账号：无事可做
		return;

	QSqlQuery sql(base);                     // 用命名连接建查询对象
	sql.prepare("UPDATE tab_messages SET delivered = 1 " // 置为已投递
				"WHERE type = 1 AND recv = :account AND delivered = 0"); // 该账号所有未投递私聊
	sql.bindValue(":account", account);      // 绑定账号，防注入
	sql.exec();                              // 执行 UPDATE（不带 LIMIT：宁可多标也不漏标）
}

/**
 * @brief 打开聊天窗时的最近 N 条：群聊按群号查 type=0；私聊按两个方向 OR 查询
 *
 * 契约（limit 上限、isGroup 判定）见 .h；实现要点：SQL 倒序取，
 * 返回前再翻回正序，聊天窗才能从上往下按时间排。
 * @param self 本机账号（私聊 OR 查询要用）
 * @param with 群号（isGroup=true）或对方账号（isGroup=false）
 * @param isGroup true=群聊 false=私聊
 * @param limit 最多返回多少条（调用方应先 qBound 夹到合理范围）
 * @return QList<StoredMessage> 正序历史；无数据返回空列表
 */
QList<UseMySQL::StoredMessage> UseMySQL::history(const QString& self, const QString& with,
												bool isGroup, int limit)
{
	QList<StoredMessage> out;                // 结果列表
	if (with.isEmpty() || limit <= 0)        // 会话对象为空或 limit 非法
		return out;                           // 直接返回空

	QSqlQuery sql(base);                     // 用命名连接建查询对象
	if (isGroup)                             // 群聊：按群号查 type=0
	{
		sql.prepare("SELECT sender, text FROM tab_messages " // 只取需要的两列
					"WHERE type = 0 AND recv = :with ORDER BY id DESC LIMIT :limit"); // 倒序取最近
		sql.bindValue(":with", with);        // 绑定群号
	}
	else
	{
		// 私聊：两个方向都要
		sql.prepare("SELECT sender, text FROM tab_messages " // 只取需要的两列
					"WHERE type = 1 AND ((sender = :with AND recv = :self) " // 对方发给我的
					"                   OR (sender = :self AND recv = :with)) " // 我发给对方的
					"ORDER BY id DESC LIMIT :limit"); // 倒序取最近
		sql.bindValue(":with", with);        // 绑定对方账号
		sql.bindValue(":self", self);        // 绑定本机账号
	}
	sql.bindValue(":limit", limit);          // 绑定返回条数上限

	if (sql.exec())                          // 查询成功
	{
		// 倒序取最近 N 条，再翻回正序（聊天窗口从上往下按时间排）
		QList<StoredMessage> desc;           // 临时倒序列表
		while (sql.next())                   // 逐行读取
			desc.append(StoredMessage{ sql.value(0).toString(), sql.value(1).toString() }); // sender + text
		for (int i = desc.size() - 1; i >= 0; --i) // 从后往前遍历 = 翻回正序
			out.append(desc[i]);             // 逐条加入正序结果
	}
	return out;                              // 返回正序历史
}
