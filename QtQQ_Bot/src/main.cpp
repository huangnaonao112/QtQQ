#include <QCoreApplication>   // 事件循环 / 路径 / 参数
#include <QDebug>             // 启动日志
#include <QStringList>        // CLI 参数列表
#include "BotConfig.h"        // 配置加载
#include "ChatTransport.h"    // TCP 通信
#include "LlmClient.h"        // LLM 客户端
#include "SqlTools.h"         // DB 查询
#include "ToolRegistry.h"     // 工具注册表
#include "HistoryStore.h"     // 会话历史
#include "AgentService.h"     // 智能体调度
#include "DbTools.h"          // 工具登记
#include "SelfTest.h"         // --selftest

// ============================================================================
// main — 只做"装配"：创建各组件、注入依赖、启动事件循环。
//
// 业务逻辑都在各组件里：
//   DbTools        把 SQL 查询注册成 function calling 工具
//   ChatTransport  TCP/NDJSON 收发与 HTML 转义
//   LlmClient      OpenAI 兼容接口（非流式 + SSE 流式）
//   AgentService   智能体循环与回复策略
//   SelfTest       --selftest 分层自检
// ============================================================================
/**
 * @brief 装配入口：配置→DB/工具→LLM/历史/传输/调度→接线→事件循环；支持 --echo/--selftest
 * @param argc 命令行参数个数
 * @param argv 命令行参数
 * @return 0=正常退出；1=配置缺失（刚生成模板）
 */
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);              // 无 GUI 事件循环
    QCoreApplication::setApplicationName("QtQQ_Bot");  // 应用名（日志/身份）

    // 1. 配置
    BotConfig cfg;                                  // 配置容器
    if (!BotConfig::load(QCoreApplication::applicationDirPath(), cfg))
        return 1;                                   // 缺 ini：已写模板，让部署者先填

    // 2. 数据库 + 工具能力
    SqlTools sql;                                   // SQL 工具实现
    const bool dbOk = sql.connectDB(cfg.dbHost, cfg.dbUser,
                                    cfg.dbPassword, cfg.dbName);  // 连接（失败可降级）
    if (!dbOk)
        qWarning() << "[bot] 数据库不可用：查询类工具将返回错误提示，问答功能不受影响";  // 不退出

    ToolRegistry registry;                          // 工具注册表
    DbTools::registerAll(registry, sql);            // 登记三个只读查询

    // 3. 通信 / 大模型 / 历史 / 调度
    LlmClient llm;                                  // LLM 客户端
    llm.setApi(cfg.llmApiUrl, cfg.llmApiKey, cfg.llmModel);  // 接口三元组
    llm.setLimits(cfg.llmMaxTokens, cfg.llmTimeoutMs);  // 生成上限 + 超时

    HistoryStore history(50);                       // 每会话保留 50 条
    ChatTransport transport(cfg);                   // 通信层
    AgentService agent(cfg, &llm, &registry, &history, &transport);  // 调度核心

    // 4. 接线：--echo 只回声（自测收发链路），默认接智能体
    const QStringList cliArgs = QCoreApplication::arguments();  // 启动参数
    const bool echoMode = cliArgs.contains("--echo");  // 是否回环模式

    // 回环模式不接智能体：否则一条消息会同时得到 [echo] 和大模型回答，
    // 反而看不出"收→发"这条链路到底通没通
    if (!echoMode)
    {
        QObject::connect(&transport, &ChatTransport::messageReceived,
                         &agent, &AgentService::handleMessage);  // 默认：智能体入口
    }
    else
    {
        QObject::connect(&transport, &ChatTransport::messageReceived,
            &transport, [&transport](const ReplyTarget& target,
                                     const QString& sender, const QString& text) {
                if (!target.isGroup)
                    return;             // 回环只测群聊（私聊走同一套代码，没必要重复）
                qInfo() << "[echo] 收到" << sender << text.left(40) << "→ 回显";  // 回显日志
                transport.sendText(target, "[echo] " + text);  // 原样前缀回显
            });
        qInfo() << "[bot] --echo 回环模式已启用（不接大模型）";  // 模式提示
    }

    // 5. 启动
    transport.start();                              // TCP 连接 + 心跳
    qInfo() << "[bot] 就绪：botId=" << cfg.botId    // 启动摘要
            << "model=" << cfg.llmModel
            << "trigger=" << cfg.trigger
            << "群=" << cfg.groupIds.join(",")
            << "db=" << (dbOk ? "OK" : "FAIL");

    // 6. 开发钩子：--selftest 分层自检（跑完自动退出）
    if (cliArgs.contains("--selftest"))
        scheduleSelfTest(sql, registry, llm);       // 挂自检定时链

    return app.exec();                              // 进入事件循环
}
