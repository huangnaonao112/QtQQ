#pragma once

#include <QString>
#include <QStringList>

// ============================================================================
// BotConfig — 机器人全部配置（bot.ini，exe 同目录）
//
// 为什么集中成一个结构体：
//   各组件（Transport/LlmClient/AgentService）只依赖自己需要的字段，
//   从这里取值而不是各自读 ini——配置格式变化只改这一个类。
//
// 首次运行：load() 写出带说明的模板（连同"请填 key"提示）并返回 false，
// main 据此直接退出。
// ============================================================================
/**
 * @brief 机器人配置结构体：llm / bot / net / db 四段字段的强类型载体
 */
struct BotConfig
{
    // llm
    QString llmApiUrl;        // OpenAI 兼容 /chat/completions 地址
    QString llmApiKey;        // API key（不进代码不进 git）
    QString llmModel;         // 模型名，默认 glm-4-flash（免费）
    QString llmSystemPrompt;  // 系统提示词（含工具调用约束）
    int     llmMaxTokens = 2048;   // 单次生成长度上限；<=0 表示不限制
    int     llmTimeoutMs = 60000;  // 超时上限：流式按"等新数据"空闲计（增量推后、不掐断），非流式按总时长计

    // bot
    QString botId;            // 机器人在 IM 里的账号（须为 int 安全的小数字）
    QString trigger;          // 唤醒词，默认 @ai

    // net
    // 监听的群（可多个，逗号分隔）。私聊不受这个列表限制：
    // 只要 recv 是机器人账号，就一定处理。
    QStringList groupIds;
    int         tcpPort;      // 服务端 TCP 端口（收发共用这一条连接）

    // db（function calling 查询用）
    QString dbHost;
    QString dbUser;
    QString dbPassword;
    QString dbName;

    /**
     * @brief 从 appDir/bot.ini 读配置；文件不存在则写出带说明的模板并返回 false
     * @param appDir 可执行文件所在目录（ini 与其同目录）
     * @param cfg 输出参数：读到的配置字段
     * @return true=已存在 ini 且已填充（即便缺 key 也 true）；false=刚生成模板
     */
    static bool load(const QString& appDir, BotConfig& cfg);
};
