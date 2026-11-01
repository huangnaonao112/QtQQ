#include "stdafx.h"
#include "UseMySQL.h"
#include <qdebug.h>
#include <qsqlerror.h>
#include <qsqlquery.h>
#include <qsqlquerymodel.h>
#include <QFile>
#include <QFileInfo>
#include "define.h"
#include "AppConfig.h"

// ============================================================================
// Q_GLOBAL_STATIC(UseMySQL, theInstance) — 单例的"Qt 官方写法"
//
// 它生成一个 theInstance() 函数，语义等价于手写：
//     static UseMySQL* theInstance() {
//         static UseMySQL* p = new UseMySQL;   // 第一次调用才创建
//         return p;
//     }
//
// 为什么用宏而不是手写：
//   (1) 懒加载：进程启动不会创建对象，第一次 instance() 时才 new、
//       才执行构造函数、才连数据库——登录窗口还没显示时不需要数据库；
//   (2) 线程安全：宏内部用原子操作保证多线程同时首次调用也只创建一份；
//   (3) 自动销毁：进程退出时宏管理的对象会被析构，不用手写释放代码。
//
// 对比"单例为什么不在 main() 里 new 一个再到处传指针"：
//   全局传指针会让每个界面都耦合这个参数，链表式传递很乱；
//   静态全局 + instance() 让"谁用谁拿"，代码自描述且不破坏分层。
// ============================================================================
Q_GLOBAL_STATIC(UseMySQL,theInstance)   // 宏生成懒加载、线程安全、自动析构的单例工厂

/**
 * @brief 构造函数（只被 Q_GLOBAL_STATIC 调一次）：连接动作放构造里。
 *
 * 保证第一次 instance() 返回时连接已经可用，调用方永远不用自己再连。
 * @param parent 父 QObject，一般 nullptr
 */
UseMySQL::UseMySQL(QObject *parent)
	: QObject(parent)
{
	//连接动作放在构造函数里：保证"第一次 instance() 调用后连接一定可用"，
	//调用方拿到的单例永远不需要自己再调 connectMySQL()
	connectMySQL();                     // 建立与 MySQL 的连接
}

/**
 * @brief 析构函数：数据库连接 base 是轻引用，进程退出时由 Qt 自动释放，无需手动 close。
 */
UseMySQL::~UseMySQL()
{}

/**
 * @brief 拿单例：theInstance() 即宏生成的工厂，全程同一个对象。
 * @return 唯一的 UseMySQL* 指针
 */
UseMySQL* UseMySQL::instance()
{
	//theInstance() 即上面宏生成的工厂函数，全程返回同一个对象
	return theInstance();
}

// ============================================================================
// verifyLogin — 登录验证
//
// 查询的表：tab_accounts(employeeID, code)，账号=员工工号，密码明文存 code 列
// （课程练习库，真实项目密码必须加盐哈希存储，绝不能明文）
//
// 为什么用 prepare + addBindValue 而不是 QString("...%1").arg(account) 拼接：
//   拼接有 SQL 注入漏洞——用户在账号框输入
//       1001 OR 1=1 --
//   拼出来变成 SELECT code FROM tab_accounts WHERE employeeID = 1001 OR 1=1 --，
//   WHERE 条件恒成立，随便什么密码都能登录成功。
//   prepare 写法把 %1 的位置变成占位符 '?'，用户输入永远只当作"数据"
//   传给服务器，不会被解析成 SQL 语法，从原理上杜绝注入。
//
// 为什么用 next() 判断而不是判断查询"返回了什么"：
//   QSqlQuery 结果集初始位置在第一行之前，next() 把游标移到下一行；
//   查不到数据（账号不存在）时 next() 返回 false，正好当作"验证失败"，
//   账号不存在和密码错误统一返回 false，不向界面层暴露区别。
// ============================================================================

/**
 * @brief 登录验证：账号密码与 tab_accounts 表比对。
 *
 * 用 prepare + bindValue 防注入（用户输入永远只当"数据"传给服务器）；
 * next() 无数据即账号不存在，与密码错误统一返回 false。
 * @param account 用户输入的账号（员工工号）
 * @param password 用户输入的密码明文
 * @return true=登录成功；false=账号不存在或密码错误
 */
bool UseMySQL::verifyLogin(const QString& account, const QString& password)
{
	//关键：QSqlQuery 必须显式指定用哪个连接！
	//不传参数的 QSqlQuery sql; 绑定的是"默认连接"（addDatabase 不带连接名创建的那个），
	//而本项目的连接名是 "main"，默认连接从未打开过——
	//这正是之前报 "QSqlQuery::prepare: database not open" 的原因
	QSqlQuery sql(base);                 // 显式用 "main" 连接
	//占位符 ':' + 名字 是 Qt 对 ODBC/Oracle 风格绑定变量的写法，
	//exec 时再由 addBindValue 把实际值填进去
	sql.prepare("SELECT code FROM tab_accounts WHERE employeeID = :id");   // 带占位符
	sql.bindValue(":id", account);   // 把 account 绑到 :id 占位符上
	sql.exec();                       // 真正执行 SQL

	if (sql.next())   // next() 把游标移到第一行，有数据说明账号存在
	{
		//value(0)：当前行的第 0 列，即 SELECT 里的 code 列
		QString result = sql.value(0).toString();
		if (result == password)   // 密码匹配
		{
			return true;                 // 验证通过
		}
	}

	return false;   // 账号不存在 或 密码不对，统一返回 false
}

/**
 * @brief 按账号查用户姓名（登录成功后显示在主界面）。
 *
 * 查不到资料时兜底返回 account 本身——注册流程只写 tab_accounts，
 * tab_employees 可能没有资料行，避免主界面姓名空白。
 * @param account 登录账号（工号）
 * @return employeeName；查不到兜底返回 account
 */
QString UseMySQL::getUserName(const QString& account)
{
	QSqlQuery sql(base);                 // 用 "main" 连接
	sql.prepare("SELECT employeeName FROM tab_employees WHERE employeeID = :id");   // 查姓名
	sql.bindValue(":id", account);       // 绑定账号
	sql.exec();                          // 执行

	//==== 排错临时日志：SQL 语法/连接问题会在这里暴露 ====
	if (!sql.isActive())   // isActive()=false 表示 SQL 没执行成功
	{
		qDebug() << "[getUserName] 查询未执行成功:" << sql.lastError().text();   // 错误打日志
	}

	if (sql.next())                      // 有数据行
	{
		QString result = sql.value(0).toString();   // 取 employeeName 列
		if (!result.isEmpty())            // 非空才用
		{
			return result;                // 返回查到的姓名
		}
	}

	//兜底：tab_accounts 里的注册账号不一定在 tab_employees 有资料
	//（注册流程只写 tab_accounts），这时直接拿账号当显示名，
	//避免主界面姓名空白。将来注册流程完善后可删掉这个兜底
	return account;                      // 兜底返回账号本身
}

// ============================================================================
// getUserGroup：当前账号所在的群 id 列表（公司群 + 所在部门群）
//
// 两次查询的来源：
//   公司群：tab_department 表里名字叫"公司群"的记录的 departmentID，人人都有；
//   部门群：tab_employees 里当前账号那一行的 departmentID。
// 全部使用 prepare + bindValue：虽然公司群匹配的是代码内写死的中文常量
// （不存在注入风险），但全项目统一成占位符风格，review 时一眼放心，
// 以后有人把常量换成用户输入也不会埋雷。
// ============================================================================

/**
 * @brief 当前账号所在的群 id 列表（公司群 + 所在部门群）。
 *
 * 全项目统一 prepare + bindValue 风格，防止以后有人把常量换成用户输入埋雷。
 * @param account 登录账号（工号）
 * @return [公司群id, 部门群id]
 */
QStringList UseMySQL::getUserGroup(const QString& account)
{
	QStringList result;                  // 结果列表
	QSqlQuery sql(base);                 // 用 "main" 连接

	//公司群：按群名"公司群"查 departmentID
	sql.prepare("SELECT departmentID FROM tab_department WHERE departmentName = :name");   // 查公司群
	sql.bindValue(":name", QStringLiteral("公司群"));   // 中文常量源码即 UTF-8，用 QStringLiteral 直接构造
	if (sql.exec())                      // 执行成功才取
	{
		if (sql.next()) result.append(sql.value(0).toString());   // 查到就把群 id 加进列表
	}

	//部门群：按账号在 tab_employees 里查所在部门 id
	sql.prepare("SELECT departmentID FROM tab_employees WHERE employeeID = :id");   // 查所在部门
	sql.bindValue(":id", account);       // 绑定账号
	if (sql.exec())                      // 执行成功才取
	{
		if (sql.next()) result.append(sql.value(0).toString());   // 部门群 id 加入列表
	}

	return result;   // 返回 [公司群id, 部门群id]
}

/**
 * @brief 按群 id 查群头像图片路径（已解析成本机可用）。
 * @param id 群号（departmentID）
 * @return 图片路径；没查到返回空串
 */
QString UseMySQL::getGroupHeadPixmap(const QString& id)
{
	QSqlQuery sql(base);                 // 用 "main" 连接
	sql.prepare("SELECT picture FROM tab_department WHERE departmentID = :id");   // 查群头像
	sql.bindValue(":id", id);            // 绑定群号
	sql.exec();                          // 执行

	if (sql.next())                      // 有数据
	{
		return resolvePicturePath(sql.value(0).toString());   // 解析成本机可用路径
	}

	return QString();   // 没查到返回空串
}

/**
 * @brief 按群 id 查群名称。
 * @param id 群号（departmentID）
 * @return departmentName；查不到返回空串
 */
QString UseMySQL::getGroupName(const QString& id)
{
	QSqlQuery sql(base);                 // 用 "main" 连接
	sql.prepare("SELECT departmentName FROM tab_department WHERE departmentID = :id");   // 查群名
	sql.bindValue(":id", id);            // 绑定群号
	sql.exec();                          // 执行

	if (sql.next())                      // 有数据
	{
		return sql.value(0).toString();  // 返回群名
	}

	return QString();                    // 查不到返回空串
}

/**
 * @brief 按账号查部门名：员工表 JOIN 部门表（单条 JOIN 比查两次干净）。
 *
 * 群 id 就存在员工行里，部门名要去部门表换；查不到（账号没资料）返回空串。
 * @param account 登录账号（工号）
 * @return 部门名；查不到返回空串
 */
QString UseMySQL::getUserDepartment(const QString& account)
{
	//占位符绑定：account 来自登录框，是最该参数化的一处
	QSqlQuery sql(base);                 // 用 "main" 连接
	sql.prepare("SELECT d.departmentName FROM tab_department d "   // 部门表别名 d
				"JOIN tab_employees e ON e.departmentID = d.departmentID "   // JOIN 员工表别名 e
				"WHERE e.employeeID = :id");   // 按账号过滤
	sql.bindValue(":id", account);       // 绑定账号
	sql.exec();                          // 执行
	if (sql.next())                      // 有数据
	{
		return sql.value(0).toString();  // 返回部门名
	}

	return QString();                    // 查不到返回空串
}

/**
 * @brief 全部在册员工（status=1）账号列表。
 *
 * 启动时生成 msghandling.js 的 recvHtml_<账号> 函数、注册 WebChannel
 * 对象都靠它遍历；必须用 QString 存账号，int 会把超上限的大号截成 0/负数。
 * @return QStringList 账号列表
 */
QStringList UseMySQL::getUserList()
{
	QSqlQueryModel sql;       // 查询模型
	sql.setQuery(QString("SELECT employeeID FROM tab_employees WHERE status = 1"), base);       // 执行查询

	QStringList result;                  // 结果列表
	int rowCount = sql.rowCount();      // 获取行数
	if (rowCount > 0)                    // 有数据才遍历
	{
		for (int i = 0; i < rowCount; i++)   // 逐行取
		{
			QModelIndex index = sql.index(i, 0);   // 第 0 列 = employeeID
			result.append(sql.data(index).toString());   // QString：账号可能超过 int 上限
		}
	}

	return result;                       // 返回账号列表
}

// ============================================================================
// getRegisterObjectInfo：取会话对应的"账号 + 头像路径"列表
// 一个函数三种查询，由 id 分流（和聊天窗群/单聊判定同一套规则）：
//   id == 公司群id  → 全员（公司群里谁都可能发言）；
//   id 是 4 位群号   → 该部门的员工（部门群）；
//   其余            → 单聊对象一个人。
// 返回 false 表示"查不到可用数据"（公司群记录缺失 / 结果为空），
//   调用方（成员树、通道注册）据此中止，不摆半截空界面。
// ============================================================================

/**
 * @brief 取会话对应的"账号 + 头像路径"列表（群成员树 / WebChannel 注册共用）。
 *
 * 三分支按 id 分流：公司群=全员；4 位群号=该部门员工；其余=单聊一人。
 * 公司群记录缺失或结果为空返回 false，调用方据此中止不摆半截空界面。
 * @param id 会话标识
 * @param infoList [out] 填入 (账号, 头像路径) 对
 * @return 成功 true；查不到可用数据 false
 */
bool UseMySQL::getRegisterObjectInfo(const QString& id, QList<QPair<QString, QString>>& infoList)
{
	//获取公司群ID（占位符绑定，不拼字符串）
	QSqlQuery company(base);             // 用 "main" 连接
	company.prepare("SELECT departmentID FROM tab_department WHERE departmentName = :name");   // 查公司群
	company.bindValue(":name", QStringLiteral("公司群"));   // 绑定群名
	company.exec();                      // 执行
	if (!company.next()) return false;   // 公司群记录不存在，没法走公司群分支

	const QString companyID = company.value(0).toString();   // 记住公司群 id

	//判断聊天窗口类型：三种查询都用 prepare + bindValue
	QSqlQuery q(base);                   // 按 id 分流的查询
	if (id == companyID)                 // 公司群 = 全员
	{
		// 公司群 = 全员，常量 SQL 没有绑定需求，直接 exec
		q.exec("SELECT employeeID, picture FROM tab_employees WHERE status = 1");   // 全员
	}
	else if (id.length() <= GROUP_ID_LENGTH)          // 其他群聊（4 位群号）
	{
		q.prepare("SELECT employeeID, picture FROM tab_employees "   // 部门成员
				  "WHERE status = 1 AND departmentID = :id");   // 按部门过滤
		q.bindValue(":id", id);          // 绑定群号
		q.exec();                        // 执行
	}
	else                                              // 单聊
	{
		q.prepare("SELECT employeeID, picture FROM tab_employees "   // 单人
				  "WHERE status = 1 AND employeeID = :id");   // 按账号过滤
		q.bindValue(":id", id);          // 绑定对方账号
		q.exec();                        // 执行
	}

	QSqlQueryModel model;                // 模型承载结果集
	model.setQuery(q);                   // 把查询交给模型

	int rowCount = model.rowCount();     // 结果行数
	if (rowCount <= 0) return false;     // 无数据返回 false

	for (int i = 0; i < rowCount; i++)   // 逐行收集
	{
		QModelIndex accountIndex = model.index(i, 0);   // 第 0 列账号
		QModelIndex pictureIndex = model.index(i, 1);   // 第 1 列头像路径

		QString account = model.data(accountIndex).toString();         // 账号
		QString picture = resolvePicturePath(model.data(pictureIndex).toString());   // 头像（已解析成可用路径）

		infoList.append(qMakePair<QString, QString>(account, picture));   // 追加到结果
	}

	return true;                         // 成功填好列表
}

/**
 * @brief 按账号查员工个性签名：聊天窗成员列表/联系人行展示用。
 * @param account 登录账号（工号）
 * @return employee_sign 列；查不到返回空串
 */
QString UseMySQL::getUserSign(const QString& account)
{
	QSqlQuery sql(base);                 // 用 "main" 连接
	sql.prepare("SELECT employee_sign FROM tab_employees WHERE employeeID = :id");   // 查签名
	sql.bindValue(":id", account);       // 绑定账号
	sql.exec();                          // 执行
	if (sql.next())                      // 有数据
	{
		return sql.value(0).toString();  // 返回签名
	}

	return QString();                    // 查不到返回空串
}

/**
 * @brief 按账号查用户个人头像路径（picture 列，返回前经 resolvePicturePath 解析）。
 * @param account 登录账号（工号）
 * @return 解析后的头像路径；查不到返回空串
 */
QString UseMySQL::getUserHeadPixmap(const QString& account)
{
	QSqlQuery sql(base);                 // 用 "main" 连接
	sql.prepare("SELECT picture FROM tab_employees WHERE employeeID = :id");   // 查头像
	sql.bindValue(":id", account);       // 绑定账号
	sql.exec();                          // 执行

	if (sql.next())                      // 有数据
	{
		return resolvePicturePath(sql.value(0).toString());   // 解析成本机可用路径
	}

	return QString();                    // 查不到返回空串
}

// ============================================================================
// resolvePicturePath — 把数据库里的图片路径解析成本机真正能加载的路径
//
// 数据库里存的一直是"某个机器上的绝对路径"（如
// C:/c++/qt/QtQQ/QT_QQ/Resources/images/r1.png）。客户端换台机器、或者
// 项目目录搬过家，这个路径就失效——表现是头像全空。
//
// 解析顺序：
//   1) 原路径直接能打开 → 用它（本机开发时最常见）；
//   2) 取文件名，到 qrc 的 :/Resources/images/<文件名> 里找（图片已打进 exe，
//      任何机器都能显示）；
//   3) 都不行 → 原样返回，界面退回"没有头像"的空白。
// ============================================================================

/**
 * @brief 把数据库里的图片路径解析成本机真正能加载的路径。
 *
 * 顺序：原路径能打开 → qrc 里同名图片（图片已打进 exe 任何机器可显示）→
 * 原样返回（界面退回无头像空白）。反斜杠统一转正斜杠后再判存在性。
 * @param stored 数据库里存的路径
 * @return 本机可加载的路径
 */
QString UseMySQL::resolvePicturePath(const QString& stored)
{
	if (stored.isEmpty())                // 空路径直接原样返回
		return stored;

	QString normalized = stored;
	normalized.replace('\\', '/');       // Windows 反斜杠统一成正斜杠
	if (QFile::exists(normalized))       // 原路径本机存在（开发机常见）
		return normalized;               // 直接用原路径

	const QString baseName = QFileInfo(normalized).fileName();   // 取文件名
	if (baseName.isEmpty())              // 取不出文件名就放弃
		return stored;                   // 原样返回

	const QString inQrc = QStringLiteral(":/Resources/images/") + baseName;   // qrc 里同名图
	if (QFile::exists(inQrc))            // qrc 里有就用它（打进 exe 的图）
		return inQrc;                    // 返回 qrc 路径

	return stored;                       // 都不行，原样返回（无头像空白）
}

// ============================================================================
// connectMySQL — 建立与 MySQL 的连接（整个项目只执行一次）
//
// 排错方法论（这次调试就是按这个顺序定位的）：
//   (1) drivers() 打印 Qt 当前能加载的驱动列表——没有 "QMYSQL" 说明
//       plugins\sqldrivers 里缺 qsqlmysql.dll（32/64 位还必须和 Qt 一致）；
//   (2) open() 失败时 lastError().text() 给出服务器返回的真实原因
//       （密码错 / SSL 协商失败 / 库名错），比界面弹窗的信息完整得多，
//       所以错误必须打到 qDebug/输出窗口而不是只弹一个 "failed"。
//
// 各参数为什么这样填：
//   "QMYSQL"   ：驱动关键字，对应 qsqlmysql.dll（由 Qt 源码 + 32 位
//                Connector C 编译而来，因为本项目 Qt 是 32 位）；
//   "main"     ：连接名。Qt 允许多个命名连接并存；不传名字会用默认连接，
//                代码里到处 addDatabase 默认连接容易互相覆盖，显式命名最稳；
//   127.0.0.1  ：用回环 IP 而不是 "localHost"——localhost 在 MySQL 客户端
//                有特殊含义（named pipe/shared memory）且易拼错大小写；
//   3306       ：MySQL 默认端口，显式写出比依赖默认值更清晰。
// ============================================================================

/**
 * @brief 建立与 MySQL 的连接（整个项目只执行一次）。
 *
 * 参数来自 AppConfig（client.ini）；错误必须打到 qDebug 而不是只弹 "failed"。
 * "main" 连接名显式命名，防止默认连接互相覆盖；127.0.0.1 避免 localhost
 * 在 MySQL 客户端的 named pipe 特殊含义。
 * @return true=连接成功；false=连接失败（错误信息打 qDebug）
 */
bool UseMySQL::connectMySQL()
{
	//调试期保留：驱动列表一眼可见，确认 QMYSQL 是否加载成功
	qDebug()<<QSqlDatabase::drivers();   // 打印当前可用驱动列表

	//addDatabase 只是"登记连接参数"，真正建立 TCP 连接发生在 open()
	//连接参数来自 client.ini（AppConfig），不再写死在代码里
	const AppConfig& cfg = AppConfig::instance();   // 读取配置
	base = QSqlDatabase::addDatabase("QMYSQL", "main");   // 创建名为 "main" 的 MySQL 连接
	base.setDatabaseName(cfg.dbName);   // 库名
	base.setHostName(cfg.dbHost);       // 主机（回环 IP）
	base.setUserName(cfg.dbUser);       // 用户名
	base.setPassword(cfg.dbPassword);    // 密码
	base.setPort(cfg.dbPort);           // 端口
	if (base.open())   // 真正发起 TCP 连接并认证
	{
		qDebug() << "successed to open database";   // 成功日志
		return true;                    // 连接可用
	}
	else {
		qDebug() << "failed to open database";      // 失败日志
		//关键排错信息：驱动加载失败 / 认证失败 / SSL 失败 都在这里
		qDebug() << base.lastError().text();   // 打印真实原因
		return false;                   // 连接失败
	}

}
