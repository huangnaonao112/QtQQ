#include "SqlTools.h"         // SQL 工具声明
#include <QSqlDatabase>        // 连接登记/open
#include <QSqlQuery>           // 查询执行
#include <QSqlError>           // 错误文案
#include <QHash>               // 部门分组桶
#include <QStringList>         // 部门顺序 / 成员列表
#include <QDebug>              // 连接/查询日志

// 构造不连库：连接推迟到 connectDB 显式调用，失败可降级运行（main 只警告不退出）
/**
 * @brief 构造：仅挂 parent，不碰数据库
 * @param parent Qt 父对象
 */
SqlTools::SqlTools(QObject* parent)
    : QObject(parent)                              // 挂父对象
{
}

// connectDB — 登记并打开名为 "bot" 的 MySQL 连接（端口写死 3306）
//
// 连接名 "bot" 是后续所有 QSqlQuery 紧凑构造的查找键（database("bot")），
// 与服务端的 "main" 连接互不干扰——两个进程各持一个名字。
// 失败只打日志并返回 false：机器人继续跑，查询类工具降级为报错文案，
// 纯问答（不查库）不受影响
/**
 * @brief addDatabase("QMYSQL","bot") 并 set 主机/端口/账号/密码/库名后 open
 * @param host 主机
 * @param user 用户
 * @param password 密码
 * @param dbName 库名
 * @return true=open 成功；false=失败（仅告警）
 */
bool SqlTools::connectDB(const QString& host, const QString& user,
                         const QString& password, const QString& dbName)
{
    m_dbName = dbName;                             // 留作报错文案
    QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL", "bot");  // 连接名 "bot" 与服务端 "main" 隔离
    db.setHostName(host);                          // 主机
    db.setPort(3306);                              // MySQL 默认端口（写死）
    db.setUserName(user);                          // 用户
    db.setPassword(password);                      // 密码
    db.setDatabaseName(dbName);                    // 库名

    if (!db.open())
    {
        qWarning() << "[sql] 连接失败:" << db.lastError().text();  // 只告警
        return false;                              // 降级：查询工具会返回失败文案
    }
    qInfo() << "[sql] 已连接" << dbName;            // 成功日志
    return true;
}

// employeesByDepartment — 按部门查员工（空部门 = 全公司），返回分组排版文本
//
// 契约：人数必须在这里算好——模型自己数会把行首工号当人数、
// 各部门相加算错（两次实测），所以描述里也写死"以工具返回为准"。
// LEFT JOIN 部门表：没挂部门的员工归入"未分组"而不是被丢掉。
// 查不到不是错误：返回明确文案，LLM 据此回答"没有这个部门/公司还没有人"
/**
 * @brief LEFT JOIN 查员工；空部门=全公司按部门分组；人数在本函数算好
 * @param department 部门名（空=全公司）
 * @return 分组排版文本；查询失败/空结果有明确文案
 */
QString SqlTools::employeesByDepartment(const QString& department)
{
    // 不指定部门 = 查全公司。这里一次给出「按部门分组 + 每组人数 + 总人数」：
    // 人数一律由工具算好，模型只负责组织语言。实测过两次：让它自己数，
    // 它会把行首的工号当人数，或者把各部门人数加错。
    QSqlQuery q(QSqlDatabase::database("bot"));    // 用连接名 "bot"
    const QString sql = QStringLiteral(
        "SELECT d.departmentName, e.employeeID, e.employeeName "
        "FROM tab_employees e "
        "LEFT JOIN tab_department d ON e.departmentID = d.departmentID ");  // 公共 SELECT 前缀

    if (department.isEmpty())
    {
        if (!q.exec(sql + QStringLiteral("ORDER BY d.departmentName, e.employeeID")))  // 全公司：排序后 exec
            return QString("查询执行失败：%1").arg(q.lastError().text());  // 失败文案
    }
    else
    {
        q.prepare(sql + QStringLiteral("WHERE d.departmentName = :dept ORDER BY e.employeeID"));  // 占位符绑定防注入
        q.bindValue(":dept", department);           // 部门名入参
        if (!q.exec())
            return QString("查询执行失败：%1").arg(q.lastError().text());  // 失败文案
    }

    QHash<QString, QStringList> groups;   // 部门 → 「姓名（工号 编号）」列表
    QStringList order;                    // 保持查询结果里的部门顺序
    int total = 0;                        // 全公司总人数

    while (q.next())
    {
        QString dep = q.value(0).toString();        // 部门名（可能为空）
        if (dep.isEmpty())
            dep = QStringLiteral("未分组");      // LEFT JOIN 没匹配上部门的员工

        if (!groups.contains(dep))
            order << dep;                           // 首次出现：记顺序

        groups[dep] << QStringLiteral("%1（工号 %2）").arg(q.value(2).toString(),
                                                          q.value(1).toString());  // 姓名+工号
        ++total;                                    // 累计总人数
    }

    if (total == 0)
    {
        return department.isEmpty()
                   ? QStringLiteral("公司里还没有员工")
                   : QString("没有找到部门「%1」的员工").arg(department);  // 空结果明确文案
    }

    const QString head = department.isEmpty()
                             ? QStringLiteral("全公司共 %1 人").arg(total)
                             : QStringLiteral("部门「%1」共 %2 人").arg(department).arg(total);  // 头行人数

    QString out = head + QStringLiteral("\n");      // 输出缓冲
    for (const QString& dep : order)
        out += QStringLiteral("%1（%2 人）：%3\n").arg(dep)
                                                 .arg(groups[dep].size())  // 本组人数（工具算好）
                                                 .arg(groups[dep].join(QStringLiteral("、")));  // 成员串
    return out.trimmed();                           // 去尾空白
}

// allDepartments — 列出全部部门及部门ID，供模型把"人事部"这类称呼
// 翻成 departmentID 后再调别的工具。每行必须带"部门ID"字样：
// 裸写 "2000 公司群" 会被模型当成人数（实测回答过"公司共2000人"）
/**
 * @brief SELECT 全部 departmentID/Name；输出行显式带"部门ID"字样
 * @return 多行文本；空表返回"部门表为空"
 */
QString SqlTools::allDepartments()
{
    QSqlQuery q(QSqlDatabase::database("bot"));    // 连接 "bot"
    q.prepare("SELECT departmentID, departmentName FROM tab_department");  // 两列
    if (!q.exec())
        return QString("查询执行失败：%1").arg(q.lastError().text());  // 失败文案

    QString out;                                    // 输出缓冲
    while (q.next())
    {
        // 输出里必须把"部门ID"三个字写出来：只写 "2000 公司群" 时，
        // 模型会把 2000 当成人数（实测回答过"公司共2000人"）
        out += QStringLiteral("公司群「%1」的ID是 %2\n").arg(q.value(1).toString(),
                                                            q.value(0).toString());  // 名+ID
    }
    return out.isEmpty() ? QString("部门表为空") : out.trimmed();  // 空表提示
}

// employeeDetail — 按姓名（模糊）或工号（精确）查单个/多个员工的详情
//
// 关键字同时喂 LIKE 和 CAST 等值两种匹配：模型传来的可能是名字也可能是
// 工号，一次查询两种都命中，不用让模型猜字段类型。
// 输出每个字段都带名字（工号/部门/签名），避免裸数字被误读成人数
/**
 * @brief LIKE 姓名 + CAST(工号)=双路径匹配；输出每字段都带名字
 * @param keyword 姓名或工号
 * @return 员详情行；无命中返回提示
 */
QString SqlTools::employeeDetail(const QString& keyword)
{
    QSqlQuery q(QSqlDatabase::database("bot"));    // 连接 "bot"
    q.prepare("SELECT e.employeeID, e.employeeName, d.departmentName, "
              "IFNULL(e.employee_sign,'') "
              "FROM tab_employees e "
              "LEFT JOIN tab_department d ON e.departmentID = d.departmentID "
              "WHERE e.employeeName LIKE :kw1 OR CAST(e.employeeID AS CHAR) = :kw2");  // 双条件 OR
    q.bindValue(":kw1", "%" + keyword + "%");       // 模糊姓名
    q.bindValue(":kw2", keyword);                   // 工号等值
    if (!q.exec())
        return QString("查询执行失败：%1").arg(q.lastError().text());  // 失败文案

    QString out;                                    // 输出缓冲
    while (q.next())
    {
        // 同样不给裸数字：字段都写上名字，模型不会把工号/人数混为一谈
        out += QStringLiteral("员工 %1（工号 %2，部门 %3，个性签名 %4）\n")
                   .arg(q.value(1).toString(), q.value(0).toString(),
                        q.value(2).toString(), q.value(3).toString());  // 全字段带名
    }
    return out.isEmpty() ? QString("没有找到与「%1」匹配的员工").arg(keyword) : out.trimmed();  // 无命中提示
}
