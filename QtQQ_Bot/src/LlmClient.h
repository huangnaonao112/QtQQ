#pragma once

#include <QObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

// ============================================================================
// LlmClient — 大模型客户端（OpenAI 兼容 /chat/completions）
//
// 两种调用形态（由 stream 参数决定，回调里的行为与之一致）：
//   (1) 非流式：用于 function calling 的工具轮（需要完整解析 tool_calls）；
//   (2) 流式：SSE（data: {...} 逐块返回），用于最终回答的打字机效果，
//       每收到一段增量回调一次 onDelta；onFinished 里给完整文本。
//
// 线程模型：全部在主线程，QNetworkAccessManager 异步回调，不阻塞。
// 一次只允许一个在途请求（Bot 场景足够；并发由 AgentService 的 m_busy 挡住）。
// ============================================================================
/**
 * @brief OpenAI 兼容 LLM 客户端：非流式（工具轮）+ SSE 流式（打字机）；主线程单在途
 */
class LlmClient : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造：创建 QNetworkAccessManager 挂在本对象名下
     * @param parent Qt 父对象
     */
    explicit LlmClient(QObject* parent = nullptr);

    /**
     * @brief 设置接口三元组（url/key/model）；启动前调用一次，对在途请求不生效
     * @param url OpenAI 兼容 /chat/completions 地址
     * @param key API key（Bearer）
     * @param model 模型名
     */
    // 配置接口地址/key/模型；启动前调用一次，对在途请求不生效
    void setApi(const QString& url, const QString& key, const QString& model);
    /**
     * @brief 设置生成上限与超时；timeoutMs<=0 保留当前值（不允许关掉超时）
     * @param maxTokens <=0 表示不限制
     * @param timeoutMs 流式=空闲超时，非流式=总时长；<=0 不改
     */
    // 生成长度上限与超时（都可配在 bot.ini 里）：
    //   maxTokens<=0 表示不限制；timeoutMs：流式按"等新数据"的空闲超时计，
    //   非流式没有增量可等，按请求总时长判死（见 .cpp 的注释）
    void setLimits(int maxTokens, int timeoutMs);

    /**
     * @brief 发起一次请求；无论成败都恰好回调一次 onFinished
     *
     * 超时语义（关键）：流式=空闲超时（每次 readyRead 推后），
     * 非流式=总时长超时；默认 60s，timeoutMs<=0 时保留旧值。
     *
     * @param messages OpenAI 格式消息数组
     * @param tools 工具 schema 数组（可空 = 不带工具）
     * @param stream true=SSE 流式（必须提供 onDelta）
     * @param onDelta 流式增量回调（stream=false 可传 nullptr）
     * @param onFinished 结束回调：content 全文；toolCalls 非流式工具调用；err 非空=失败
     */
    // messages：OpenAI 格式消息数组（role/content/tool_calls/tool_call_id）
    // tools：工具 schema 数组（可为空数组 = 不带工具）
    // onDelta：流式增量文本（stream=true 时必须提供）
    // onFinished：结束回调。content=完整文本；toolCalls=工具调用数组（非流式）；
    //             err 非空表示请求失败。
    using DeltaFn    = std::function<void(const QString& delta)>;
    using FinishedFn = std::function<void(const QString& content,
                                          const QJsonArray& toolCalls,
                                          const QString& err)>;
    void chat(const QJsonArray& messages,
              const QJsonArray& tools,
              bool stream,
              DeltaFn onDelta,
              FinishedFn onFinished);

private:
    /**
     * @brief SSE 增量解析：行缓冲 + data: 前缀 + [DONE] 收尾
     * @param data 新到的网络字节
     */
    void onStreamData(const QByteArray& data);        // SSE 增量解析
    /**
     * @brief 流式收尾：把累计全文交 onFinished（[DONE] 与 finished 双入口只跑一次）
     */
    void finishStream();                              // 流结束收尾

    QString m_url;                     // 接口地址
    QString m_key;                     // API key
    QString m_model;                   // 模型名
    int     m_maxTokens = 0;     // 生成长度上限（0=不限）
    int     m_timeoutMs = 60000; // 超时上限（流式按空闲计，非流式按总时长）
    QElapsedTimer m_elapsed;     // 记一次请求的耗时，方便排查"为什么这么慢"

    QNetworkAccessManager* m_nam = nullptr;  // 网络管理器（子对象）
    QNetworkReply*  m_reply    = nullptr;             // 当前在途请求
    bool            m_isStream = false;               // 本次请求的形态（onDelta 不能当开关用）
    QByteArray      m_sseBuf;                         // SSE 行缓冲（跨 readyRead 粘包）
    QString         m_streamContent;                  // 已累计的回答文本
    bool            m_streamDoneSent = false;         // 收尾只执行一次的标志

    DeltaFn    m_deltaFn;                             // 流式增量回调副本
    FinishedFn m_finishedFn;                          // 结束回调副本
};
