#include "BotConfig.h"        // 配置结构体
#include <QSettings>          // INI 读写
#include <QFile>              // 判断 ini 是否存在
#include <QDebug>             // 告警日志

// load — 读 bot.ini 到 cfg；文件不存在则写出带说明的模板并返回 false，
// main 据此直接退出（让部署者先填 api_key / db/password 再启动）。
//
// 边界：返回 false 只发生在"刚生成模板"这一种情况；已存在的 ini 即便
// 缺 key 也返回 true——缺 llm/api_key 时错误会在第一次 LLM 请求时暴露，
// 启动阶段不挡（DB 也可能只是暂时没起）。
// 坑：QSettings 读 "2000,2001" 既可能返回 QStringList 也可能返回一个
// 带逗号的 QString，下面群列表段两种形态都拆（见该段注释）
/**
 * @brief 读 bot.ini；不存在则写模板并 false（main 退出让部署者填 key）
 * @param appDir ini 所在目录（通常为 applicationDirPath）
 * @param cfg 输出配置
 * @return true=已有 ini 并填充完毕；false=刚生成模板
 */
bool BotConfig::load(const QString& appDir, BotConfig& cfg)
{
    const QString iniPath = appDir + "/bot.ini";    // 与 exe 同目录的配置路径
    if (!QFile::exists(iniPath))
    {
        // 写出带说明的模板：key 不进代码不进 git，填完重启即可
        QSettings def(iniPath, QSettings::IniFormat);  // 以 INI 格式创建
        def.setIniCodec("UTF-8");                   // 中文说明用 UTF-8 落盘
        def.setValue("llm/api_url", "https://open.bigmodel.cn/api/paas/v4/chat/completions");  // 默认智谱地址
        def.setValue("llm/api_key", "在这里填入你的APIKey");  // 明确占位提示
        def.setValue("llm/model",   "glm-4-flash");  // 免费档默认模型
        def.setValue("llm/max_tokens", 2048);     // 单次生成上限（<=0 不限制）
        def.setValue("llm/timeout_ms", 60000);    // 超时上限（流式按等新数据的空闲超时计，长回答会被推后）
        def.setValue("llm/system_prompt",
            "你是局域网IM QtQQ 群里的AI助手。回复保持简洁（150字以内），"
            "不要使用markdown语法，纯文本会直接显示在聊天窗口里。");  // 默认系统提示
        def.setValue("bot/id",      "10010");     // 机器人账号（模板默认）
        def.setValue("bot/trigger", "@ai");       // 群聊唤醒词
        def.setValue("net/group_ids", "2000");      // 多个群用逗号分隔：2000,2001
        def.setValue("net/tcp_port", 57777);      // 服务端 TCP 端口
        def.setValue("db/host", "127.0.0.1");     // MySQL 主机
        def.setValue("db/user", "root");          // MySQL 用户
        def.setValue("db/password", "");          // 密码留空由部署者填写
        def.setValue("db/name", "qt_qq_main");    // 库名（与服务端共用）
        qWarning() << "[bot] 已生成配置模板" << iniPath;  // 提示已落盘
        qWarning() << "[bot] 请填入 llm/api_key（和 db/password）后重新启动！";  // 行动指引
        return false;                             // 让 main 退出
    }

    QSettings cfgReader(iniPath, QSettings::IniFormat);  // 打开已有 ini
    cfgReader.setIniCodec("UTF-8");               // 与写入编码一致

    cfg.llmApiUrl   = cfgReader.value("llm/api_url").toString();  // 接口地址
    cfg.llmApiKey   = cfgReader.value("llm/api_key").toString();  // 密钥
    cfg.llmModel    = cfgReader.value("llm/model", "glm-4-flash").toString();  // 模型名（缺省 flash）
    cfg.llmSystemPrompt = cfgReader.value("llm/system_prompt").toString();  // 系统提示
    cfg.llmMaxTokens = cfgReader.value("llm/max_tokens", 2048).toInt();  // 生成上限
    cfg.llmTimeoutMs = cfgReader.value("llm/timeout_ms", 60000).toInt();  // 超时

    cfg.botId    = cfgReader.value("bot/id", "10010").toString();  // 机器人账号
    cfg.trigger  = cfgReader.value("bot/trigger", "@ai").toString();  // 唤醒词

    cfg.tcpPort  = cfgReader.value("net/tcp_port", 57777).toInt();  // TCP 端口

    // 群列表：优先 net/group_ids，兼容旧的 net/group_id（单值，也允许写逗号）。
    // 注意 QSettings 会把 "2000,2001" 直接读成 QStringList，
    // 也可能原样返回一个带逗号的 QString——两种都要能拆。
    QStringList rawIds = cfgReader.value("net/group_ids").toStringList();  // 新键（可能是 list 或单串）
    if (rawIds.isEmpty())
        rawIds = cfgReader.value("net/group_id", "2000").toStringList();  // 兼容旧键

    cfg.groupIds.clear();                          // 先清空，避免重复加载叠加
    for (const QString& chunk : rawIds)
    {
        for (const QString& id : chunk.split(',', Qt::SkipEmptyParts))  // 再按逗号拆（兼容 QString 形态）
        {
            const QString trimmed = id.trimmed();  // 去空白
            if (!trimmed.isEmpty())
                cfg.groupIds << trimmed;           // 收集到最终列表
        }
    }
    if (cfg.groupIds.isEmpty())
    {
        qWarning() << "[bot] 没有配置任何监听群（net/group_ids），群里不会应答";  // 空群列表告警
    }

    cfg.dbHost     = cfgReader.value("db/host", "127.0.0.1").toString();  // DB 主机
    cfg.dbUser     = cfgReader.value("db/user", "root").toString();  // DB 用户
    cfg.dbPassword = cfgReader.value("db/password").toString();  // DB 密码
    cfg.dbName     = cfgReader.value("db/name", "qt_qq_main").toString();  // 库名

    return true;                                    // 已有 ini：即便缺 key 也放行
}
