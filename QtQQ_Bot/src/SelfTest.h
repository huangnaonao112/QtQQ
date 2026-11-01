#pragma once

class LlmClient;
class ToolRegistry;
class SqlTools;

// ============================================================================
// SelfTest — --selftest 的分层自检（不依赖客户端，单独验证机器人各层）
//
// 依次检查：DB 查询 → 工具注册表执行 → LLM 非流式 → LLM 流式，
// 每项打印 PASS/FAIL，全部跑完退出进程。
//
// 从 main 里搬出来的原因：自检只是一条开发/回归用的支路，不该占据
// 生产入口四分之一的篇幅，main 只留"装配 + 参数解析"。
// ============================================================================
/**
 * @brief 排队 --selftest 分层自检链（DB→工具→非流式→流式），全跑完 quit()
 * @param sql 数据库查询（层 1）
 * @param registry 工具注册表（层 2）
 * @param llm LLM 客户端（层 3/4；层 3 完成后串行发起层 4）
 */
void scheduleSelfTest(SqlTools& sql, ToolRegistry& registry, LlmClient& llm);
