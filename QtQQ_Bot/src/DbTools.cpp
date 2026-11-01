#include "DbTools.h"          // 注册入口声明
#include "SqlTools.h"         // 工具执行体（employeesByDepartment 等）
#include "ToolRegistry.h"     // addTool / invoke
#include <QJsonArray>         // JSON schema 的 required 数组
#include <QJsonObject>        // JSON schema / properties

namespace DbTools
{

// registerAll — 把三个只读查询登记成 function calling 工具（启动时调用一次）
//
// description 不是给人看的文档，是直接进 prompt 的指令：防"模型自己数数"、
// 防"把部门ID当人数"的约束必须写在这里——工具返回的文本只描述数据，
// 使用纪律只有靠 description 传达。
// lambda 按引用捕获 sql：registry 与 sql 都是 main 的栈对象、同生命周期，
// 且 AgentService 只在两者存活期间 invoke，不存在悬空引用
/**
 * @brief 注册 query_employees / query_departments / query_employee_detail 三个只读工具
 * @param registry 注册表
 * @param sql SQL 实现（引用捕获，要求与 registry 同生命周期）
 */
void registerAll(ToolRegistry& registry, SqlTools& sql)
{
    registry.addTool(
        "query_employees",                         // 工具名（模型调用入口）
        "查询公司员工。不指定部门 = 查全公司：返回「全公司共 N 人」+ 每个部门"
        "「部门名（N 人）：姓名（工号 编号）...」；指定部门则只返回该部门的结果。"
        "人数一律以工具返回的为准，不要自己数。",   // 进 prompt 的纪律说明
        QJsonObject{
            {"type", "object"},                    // schema 根类型
            {"properties", QJsonObject{
                {"department", QJsonObject{         // 可选部门参数
                    {"type", "string"},            // 字符串
                    {"description", "部门名称，可选"}}}  // 参数说明
            }},
            {"required", QJsonArray{}}             // 无必填：不传=查全公司
        },
        [&sql](const QJsonObject& args) {
            return sql.employeesByDepartment(args.value("department").toString());  // 可能为空串
        });

    registry.addTool(
        "query_departments",                       // 列部门工具
        "列出公司所有部门及其部门ID。返回的 ID 是部门编号，不是人数。",  // 防把 ID 当人数
        QJsonObject{
            {"type", "object"},                    // 无参数 schema
            {"properties", QJsonObject{}}          // 空 properties
        },
        [&sql](const QJsonObject&) {
            return sql.allDepartments();           // 全量部门列表
        });

    registry.addTool(
        "query_employee_detail",                   // 单人详情工具
        "按姓名（支持模糊）或工号查询某位员工的详细信息（工号/姓名/部门/个性签名）。",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"keyword", QJsonObject{           // 关键字参数
                    {"type", "string"},            // 字符串
                    {"description", "员工姓名（支持模糊匹配）或工号"}}}  // 二义匹配说明
            }},
            {"required", QJsonArray{ QJsonObject{{"type", "string"}} }}  // schema 里的 required 占位
        },
        [&sql](const QJsonObject& args) {
            return sql.employeeDetail(args.value("keyword").toString());  // LIKE + 精确双匹配
        });
}

} // namespace DbTools
