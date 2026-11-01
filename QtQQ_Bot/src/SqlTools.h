#pragma once

#include <QObject>
#include <QString>

// ============================================================================
// SqlTools — function calling 的"手"：封装对 qt_qq_main 库的查询
//
// 设计约束：
//   (1) 只读。工具永远不写库，防止大模型输出失控破坏数据；
//   (2) 返回人类可读的格式化文本（直接可作为 LLM 的工具结果回喂）；
//   (3) 查不到返回明确文案，让 LLM 能组织出得体的回答。
// ============================================================================
/**
 * @brief function calling 的 SQL 执行体：只读查询 + 面向 LLM 的格式化文本
 */
class SqlTools : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造：不连库（连接推迟到 connectDB，失败可降级）
     * @param parent Qt 父对象
     */
    explicit SqlTools(QObject* parent = nullptr);

    /**
     * @brief 登记并打开连接名 "bot" 的 MySQL（端口 3306）；失败只告警返回 false
     * @param host 主机
     * @param user 用户
     * @param password 密码
     * @param dbName 库名（存入 m_dbName 供报错文案）
     * @return true=连接成功；false=失败（main 不退出，工具降级为报错文案）
     */
    // 登记并打开连接（连接名 "bot"，端口 3306）。
    // 失败返回 false：调用方（main）只警告不退出，查询工具降级为报错文案
    bool connectDB(const QString& host, const QString& user,
                   const QString& password, const QString& dbName);

    /**
     * @brief 按部门查员工（空部门=全公司）；人数在工具侧算好，查不到返回明确文案
     * @param department 部门名（空=全公司）
     * @return 格式化文本（含分组人数/总人数）
     */
    // 各工具的执行实现（返回格式化查询结果文本）
    QString employeesByDepartment(const QString& department);   // 按部门查员工
    /**
     * @brief 列出所有部门及部门ID；每行必须带"部门ID"字样防被当成人数
     * @return "公司群「X」的ID是 N" 多行；空表返回提示
     */
    QString allDepartments();                                   // 列出所有部门
    /**
     * @brief 按姓名（模糊）或工号（精确）查详情；LIKE + CAST 双匹配
     * @param keyword 姓名或工号
     * @return 带字段名的详情行；无命中返回提示
     */
    QString employeeDetail(const QString& keyword);             // 按姓名/账号查详情

private:
    QString m_dbName;   // 保存库名，报错文案里用
};
