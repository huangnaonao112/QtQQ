#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QJsonArray>
#include <QHash>
#include <QQueue>

#include "ReplyTarget.h"

class LlmClient;
class ToolRegistry;
class HistoryStore;
class ChatTransport;
struct BotConfig;

// ============================================================================
// AgentService — 智能体调度核心
//
// 职责：把"一条消息"变成"一次（或多次）大模型调用 + 发送"：
//
//   <问题>              → 智能体循环：
//       第一阶段（工具轮，非流式）：带 tools 发问，模型决定是否调用工具；
//           调用 → ToolRegistry 执行 SQL → 结果以 role:tool 回喂 → 最多 6 轮（kMaxToolRounds）
//       第二阶段（回答轮，SSE 流式）：把工具结果+问题交给模型，
//           增量文本按 400ms 节流以 msg_stream 协议推给客户端（打字机效果）
//
//   总结 / summary      → 会话摘要：取本会话最近 40 条历史喂给模型
//   重置 / reset        → 清空本会话的上下文
//
// 群聊/私聊共用一条处理链路，差别只有两点：
//   群聊要唤醒词（@ai），私聊不用；两者的 ReplyTarget 由 ChatTransport 算好。
//
// 上下文：m_sessions 按会话（群号/对方账号）缓存最近 kMaxTurns 个问题，
// 这样"那研发部呢"这种指代能接上上一句。只存问题、不存答案——模型看到
// 历史答案会把旧名单当成事实复述（实测过），事实必须每轮重新查库。
// 摘要与工具轮内部的 tool 消息同样不进上下文。
//
// 回复地址（ReplyTarget）随请求一路按值传递，不存成员：
// 群里正在生成的回答不会因为"此时来了条私聊"而被改发到私聊窗口。
//
// 防御性设计：
//   m_busy 保证同一时刻只有一个智能体流程（机器人单线程足够）；
//   工具轮上限 6 轮（kMaxToolRounds），防止模型无限调用工具；超限不放弃，用已有结果作答。
// ============================================================================
class AgentService : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造调度核心：持有各协作组件指针与配置引用（调用方保证它们比本对象活得久）
     * @param cfg 配置（引用：只读字段，配置对象生命周期须覆盖本服务）
     * @param llm LLM 客户端（非空，所有模型请求都经它）
     * @param tools function calling 工具注册表
     * @param history 会话历史（摘要数据源）
     * @param transport 网络发送（msg / msg_stream 都走它）
     * @param parent Qt 父对象
     */
    AgentService(const BotConfig& cfg,
                 LlmClient* llm, ToolRegistry* tools,
                 HistoryStore* history, ChatTransport* transport,
                 QObject* parent = nullptr);

    /**
     * @brief 群聊/私聊统一入口：先记历史，再按唤醒词过滤，忙则排队（上限 kMaxQueue）
     * @param target 回复地址（含 isGroup：决定唤醒词是否必需）
     * @param sender 发送者账号
     * @param text 消息正文（群聊可能仍含唤醒词，内部会剥离）
     */
    void handleMessage(const ReplyTarget& target, const QString& sender, const QString& text);

private slots:
    /**
     * @brief 流式节流定时器到点：把积累的增量以 msg_stream 发出，空则停表
     */
    void flushStreamBuf();      // 流式节流定时器：把积累的增量发出去

private:
    /**
     * @brief 一条待处理请求（m_busy 时先入 m_queue，避免慢问题吞掉后续提问）
     */
    struct Pending
    {
        ReplyTarget target;     // 回复地址（群号/对方账号 + 是否群聊）
        QString     sender;     // 发送者账号
        QString     text;       // 原始消息正文
    };

    /**
     * @brief 按剥离唤醒词后的内容分发：重置命令 / 会话摘要 / 智能体问答
     * @param p 待处理请求（含 target/sender/text）
     */
    void startFlow(const Pending& p);       // 按内容分发：重置 / 摘要 / 智能体
    /**
     * @brief 流程收尾：释放 m_busy，若有排队请求则立刻取出并进入下一条
     */
    void finishFlow();                      // 收尾：释放 m_busy，并启动队列下一条
    /**
     * @brief 智能体主流程：拼 system+历史问题+当前问题，进入首轮工具调用（非流式）
     * @param question 当前问题（已剥离唤醒词）
     * @param target 本条请求的回复地址（按值贯穿整条异步链）
     */
    void startAgent(const QString& question, const ReplyTarget& target);
    /**
     * @brief 会话摘要：无历史直接提示；有则取最近 40 条拼 prompt 后非流式作答
     * @param target 摘要要发到的回复地址（按会话 channel 取历史）
     */
    void startSummary(const ReplyTarget& target);
    /**
     * @brief 工具轮：非流式调用 LLM；到 kMaxToolRounds 上限时改用已有结果直接作答
     * @param target 回复地址（按值捕获进异步回调，防串台）
     * @param question 当前问题（工具耗尽/最终流式阶段都要原样带回）
     */
    void runToolRound(const ReplyTarget& target, const QString& question);   // 工具轮（非流式）
    /**
     * @brief 回答轮：SSE 流式；增量进 m_streamBuf 由 400ms 定时器批发，结束发 done+全文
     * @param target 回复地址
     * @param question 最终回答针对的问题（追加在工具结果后，防模型答成上一轮）
     */
    void startFinalStream(const ReplyTarget& target, const QString& question); // 回答轮（SSE 流式）
    /**
     * @brief 整段回答收尾：msg_stream 一次性写全文+done，并写入会话历史
     * @param target 回复地址
     * @param text 完整回答文本
     */
    void sendAnswer(const ReplyTarget& target, const QString& text);         // 整段回答收尾
    /**
     * @brief 只把问题记入会话上下文（答案绝不入列——见成员说明里的实测教训）
     * @param target 回复地址（recv 即会话 key）
     * @param question 本问
     * @param answer 本答（保留语义参数；实现中 Q_UNUSED，答案不写入 m_sessions）
     */
    void recordTurn(const ReplyTarget& target, const QString& question,
                    const QString& answer);                                  // 记入会话上下文

    const BotConfig& m_cfg;                  // 配置只读引用
    LlmClient*      m_llm;                   // LLM 客户端（不拥有）
    ToolRegistry*   m_tools;                 // 工具注册表（不拥有）
    HistoryStore*   m_history;               // 会话历史（不拥有）
    ChatTransport*  m_transport;             // 网络发送（不拥有）

    QTimer*  m_flushTimer;      // 流式节流（400ms）
    QString  m_streamBuf;       // 未发送的流式增量
    ReplyTarget m_streamTarget; // 流式增量的收件人（定时器回调没有参数，只能放成员；
                                // 只由 startFinalStream 写，一次只有一个流）
    bool     m_busy = false;    // 同一时刻只允许一个流程
    QQueue<Pending> m_queue;    // 忙的时候先排队，不再直接丢弃
    static const int kMaxQueue = 20;         // 排队上限：满了丢弃/私聊回稍等

    // 智能体循环状态
    QJsonArray m_messages;      // 当前这一次请求的消息数组（含工具轮结果）
    int        m_round = 0;     // 已进行的工具轮数

    // 会话上下文：channel(群号/对方账号) → 最近 kMaxTurns 个问题
    // （只存 role:user 的问题，不存 assistant 答案——见 recordTurn）
    QHash<QString, QJsonArray> m_sessions;
    static const int kMaxTurns = 6;         // 上下文保留的问题条数
    // 工具轮上限。"公司架构"这种要逐部门查的问题，实测要 4~6 轮；
    // 到上限不会直接放弃，而是拿已有结果作答（见 runToolRound）
    static const int kMaxToolRounds = 6;    // 防无限调用工具的硬上限
};
