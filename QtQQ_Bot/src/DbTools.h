#pragma once

class ToolRegistry;
class SqlTools;

// ============================================================================
// DbTools — 把数据库查询能力注册成 function calling 工具
//
// 每个工具的"名字/描述/参数 schema/执行器"写在一起：新增一个查询 =
// 在 DbTools.cpp 里加一段 + 在 SqlTools 里加一个方法，main 与 AgentService
// 都不用改。main 只负责把 registry 和 sql 传进来。
// ============================================================================
/**
 * @brief 工具注册命名空间：集中登记 function calling 工具定义
 */
namespace DbTools
{
/**
 * @brief 启动时调用一次；登记全部只读查询工具（执行器按引用捕获 sql）
 * @param registry 工具注册表（写入方）
 * @param sql SQL 实现（须比 registry 活得久——执行器引用捕获）
 */
void registerAll(ToolRegistry& registry, SqlTools& sql);
}
