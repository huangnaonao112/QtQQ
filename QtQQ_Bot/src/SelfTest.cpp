#include "SelfTest.h"         // 自检入口
#include "LlmClient.h"        // LLM 调用
#include "ToolRegistry.h"     // 工具 invoke
#include "SqlTools.h"         // DB 查询
#include <QCoreApplication>   // qApp / quit
#include <QDebug>             // PASS/FAIL 日志
#include <QJsonArray>         // 消息数组
#include <QJsonObject>        // 单条消息
#include <QPair>              // pass/fail 计数
#include <QTimer>             // 延时启动
#include <memory>             // shared_ptr 跨异步捕获

// scheduleSelfTest — --selftest 分层自检链：
//   DB 查询 → 工具注册表执行 → LLM 非流式 → LLM 流式，全部跑完后 quit()
//
// 层 3 完成才发起层 4（串行而非并行）：LlmClient 一次只允许一个在途请求，
// 并发的第二个 chat() 会直接被拒，自检结果就没有意义了。
// 延迟 300ms 是给数据库/网络初始化留落地时间；跨异步边界的
// 按值捕获（shared_ptr）等坑见函数体注释
/**
 * @brief singleShot(300) 后串行跑四层自检；结束 quit()；异步状态一律 shared_ptr 按值捕
 * @param sql DB 层
 * @param registry 工具层
 * @param llm LLM 层（非流式 + 流式）
 */
void scheduleSelfTest(SqlTools& sql, ToolRegistry& registry, LlmClient& llm)
{
    // 等数据库/网络这些初始化落地后再跑
    QTimer::singleShot(300, qApp, [&sql, &registry, &llm]() {
        //跨异步边界的计数/累积必须按值捕获（shared_ptr）：
        //singleShot 的 lambda 设置完异步链就返回，LLM 回调稍后才执行，
        //若通过引用捕获栈变量就是 use-after-free
        auto counters = std::make_shared<QPair<int, int>>(0, 0);  // {pass, fail}
        auto check = [counters](const QString& name, bool ok) {
            if (ok) ++counters->first;              // 计 pass
            else    ++counters->second;             // 计 fail
            qInfo() << "[selftest]" << name << (ok ? "PASS" : "FAIL");  // 单项结果
        };

        // 层 1：SQL 工具
        const QString r1 = sql.employeesByDepartment(QStringLiteral("人事部"));  // 同步查 DB
        check("DB employeesByDepartment", !r1.isEmpty() && !r1.contains("失败"));  // 非空且非失败文案

        // 层 2：工具注册表执行
        QJsonObject targs;                          // 工具参数
        targs.insert("department", QStringLiteral("人事部"));  // 指定部门
        const QString r2 = registry.invoke("query_employees", targs);  // 走注册表路径
        check("ToolRegistry invoke", !r2.isEmpty() && !r2.contains("失败"));  // 同上判据

        // 层 3：LLM 非流式（完成后串行跑层 4）
        QJsonArray msgs;                            // 极简 user 消息
        msgs.append(QJsonObject{{"role", "user"},
                                {"content", QStringLiteral("回复OK两个字母即可")}});  // 尽量短

        auto deltas = std::make_shared<int>(0);     // 层 4 增量计数
        auto acc    = std::make_shared<QString>();  // 层 4 累计文本

        llm.chat(msgs, QJsonArray(), /*stream*/false, nullptr,
            [&llm, check, counters, msgs, deltas, acc]
            (const QString& content, const QJsonArray&, const QString& err) {
                check("LLM non-stream", err.isEmpty() && !content.trimmed().isEmpty());  // 层 3 结果

                // 层 4 严格排在层 3 回调之后：单在途约束
                llm.chat(msgs, QJsonArray(), /*stream*/true,
                    [deltas, acc](const QString& d) { ++*deltas; *acc += d; },  // 每个增量计数并拼接
                    [check, counters, deltas, acc]
                    (const QString&, const QJsonArray&, const QString& e2) {
                        check("LLM stream", e2.isEmpty() && *deltas > 0 && !acc->trimmed().isEmpty());  // 流式有增量且非空
                        qInfo() << "[selftest] 完成：pass=" << counters->first  // 汇总
                                << "fail=" << counters->second
                                << (counters->second == 0 ? "—— 全部通过" : "—— 存在失败项");
                        QCoreApplication::quit();  // 全部跑完退出进程
                    });
            });
    });
}
