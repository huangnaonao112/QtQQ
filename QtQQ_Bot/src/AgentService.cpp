#include "AgentService.h"      // 调度核心声明
#include "BotConfig.h"         // 配置字段（trigger/llm* 等）
#include "LlmClient.h"         // LLM 非流式/流式调用
#include "ToolRegistry.h"      // function calling 工具执行
#include "HistoryStore.h"      // 会话历史（摘要数据源）
#include "ChatTransport.h"     // 消息发送（msg / msg_stream）
#include <QJsonDocument>       // 工具 arguments 字符串 → 对象
#include <QJsonObject>         // OpenAI 消息/工具 JSON 拼装

/**
 * @brief 构造：持有协作组件指针；创建 400ms 流式节流定时器并接线
 * @param cfg 配置（引用，只读）
 * @param llm LLM 客户端
 * @param tools 工具注册表
 * @param history 会话历史
 * @param transport 网络发送
 * @param parent Qt 父对象
 */
AgentService::AgentService(const BotConfig& cfg,
                           LlmClient* llm, ToolRegistry* tools,
                           HistoryStore* history, ChatTransport* transport,
                           QObject* parent)
    : QObject(parent), m_cfg(cfg), m_llm(llm), m_tools(tools),
      m_history(history), m_transport(transport)
{
    // 流式节流：每 400ms 把积累的增量打包发一次，
    // 避免 LLM 每吐一个 token 就往 TCP 写一条消息
    m_flushTimer = new QTimer(this);                 // 挂在 this 上，随对象析构释放
    m_flushTimer->setInterval(400);                  // 400ms：打字机效果与包数折中
    connect(m_flushTimer, &QTimer::timeout, this, &AgentService::flushStreamBuf);  // 到点批发增量
}

// ----------------------------------------------------------------------------
// handleMessage — 群聊/私聊统一入口
//
// 群聊与私聊的差异只有两点：
//   (1) 群聊要唤醒词，私聊不用（用户专门打开单聊窗口，意图是明确的）；
//   (2) 回复目标不同——这一点由 ChatTransport 算好后随 target 传进来。
//
// 忙的时候不再直接丢弃：先排队（上限 kMaxQueue），当前流程结束自动开始下一条。
// 之前是"群里直接丢、私聊回稍等"，慢问题会把同一个群的其他提问全部吃掉。
// ----------------------------------------------------------------------------

/**
 * @brief 统一入口：记历史 → 群聊唤醒词过滤 → 忙则排队，否则置 busy 并进入流程
 * @param target 回复地址
 * @param sender 发送者
 * @param text 消息正文
 */
void AgentService::handleMessage(const ReplyTarget& target, const QString& sender, const QString& text)
{
    m_history->append(target.recv, sender, text);   // 全部消息进历史（摘要的数据源）

    if (target.isGroup && !text.contains(m_cfg.trigger, Qt::CaseInsensitive))
        return;                                     // 群里没叫它，只记账不插话

    const Pending p{ target, sender, text };        // 打包成排队单元（按值，异步安全）

    if (m_busy)     // 上一个流程还在跑：排队等它
    {
        if (m_queue.size() >= kMaxQueue)
        {
            qWarning() << "[agent] 队列已满，丢弃一条 →" << target.recv;   // 满队：丢弃并告警
            if (!target.isGroup)
                m_transport->sendText(target, QStringLiteral("排队的人太多了，请稍后再试。"));  // 私聊给友好提示
            return;                                 // 群聊静默丢弃，避免刷屏
        }
        m_queue.enqueue(p);                         // 入队，等 finishFlow 取出
        qInfo() << "[agent] 排队（前面还有" << m_queue.size() << "条）→" << target.recv;  // 观察排队深度
        return;
    }

    m_busy = true;                                  // 占用单飞锁
    startFlow(p);                                   // 立刻进入分发
}

/**
 * @brief 剥离唤醒词后分发：重置清上下文、总结走摘要、其余走智能体
 * @param p 待处理请求
 */
void AgentService::startFlow(const Pending& p)
{
    QString question = p.text;                      // 先取原始正文
    if (p.target.isGroup)
        question.remove(m_cfg.trigger, Qt::CaseInsensitive);  // 群聊剥掉唤醒词（大小写不敏感）
    question = question.trimmed();                  // 去首尾空白，避免空串命令误判

    // 日志脱敏：群里的问题本来就人人可见，可以打；私聊只打长度，
    // 避免控制台/投屏把别人的私聊内容曝光
    if (p.target.isGroup)
        qInfo() << "[agent] 群提问:" << question.left(60) << "→" << p.target.recv;  // 群聊可打前 60 字
    else
        qInfo() << "[agent] 私聊提问（" << question.size() << "字）→" << p.target.recv;  // 私聊只打字数

    // 会话内命令：清空上下文（模型答歪了、或者换话题时用）
    if (question == QStringLiteral("重置") || question == QStringLiteral("清空对话")
        || question.compare(QStringLiteral("reset"), Qt::CaseInsensitive) == 0)
    {
        m_sessions.remove(p.target.recv);           // 丢掉该 channel 的历史问题
        sendAnswer(p.target, QStringLiteral("好的，之前的对话我忘掉了，我们重新开始。"));  // 确认已重置
        finishFlow();                               // 释放 busy 并驱动队列
        return;
    }

    if (question.startsWith(QStringLiteral("总结")) || question == QStringLiteral("summary"))
        startSummary(p.target);                     // 总结 → 摘要链路
    else
        startAgent(question.isEmpty()
                       ? QStringLiteral("请用一句话介绍你自己")
                       : question,                  // 空问题兜底成自我介绍
                   p.target);                       // 其余 → 智能体主流程
}

/**
 * @brief 释放 m_busy；若队列非空则立刻 dequeue 并 startFlow
 */
void AgentService::finishFlow()
{
    m_busy = false;                                 // 解除单飞锁

    if (m_queue.isEmpty())
        return;                                     // 没有排队的，直接返回

    const Pending next = m_queue.dequeue();         // 取出最早排队的一条
    qInfo() << "[agent] 开始处理排队中的请求 →" << next.target.recv;  // 记录恢复处理
    m_busy = true;                                  // 重新占锁
    startFlow(next);                                // 立刻处理下一条
}

// ----------------------------------------------------------------------------
// 智能体流程
// ----------------------------------------------------------------------------
/**
 * @brief 拼 system + 历史问题 + 当前问题，进入首轮工具调用（非流式）
 * @param question 当前问题
 * @param target 回复地址（按值贯穿异步链）
 */
void AgentService::startAgent(const QString& question, const ReplyTarget& target)
{
    m_round = 0;                                    // 重置工具轮计数

    m_messages = QJsonArray();                      // 清空本次请求消息数组
    QJsonObject sys;                                // system 消息
    sys.insert("role", "system");                   // 角色 = system
    sys.insert("content", m_cfg.llmSystemPrompt +
        "\n凡涉及查询公司员工/部门/组织信息的问题，必须调用提供的工具查询数据库，不要编造。"
        // 多轮上下文的副作用：模型看到上一轮答案，会以为"我已知数据"而跳过工具，
        // 把上一个部门的名单套到新部门头上。这条规则专门堵这个口子。
        "\n历史对话只用于理解指代（例如此前提到的是哪个部门、哪个人）；"
        "任何具体名单、人数、部门归属都必须重新调用工具查询，"
        "不得根据历史回答推测、复述或改写。");   // 叠加防"复述旧名单"约束
    m_messages.append(sys);                         // 放入消息数组首位

    // 带上本会话最近几个问题（只存问题、不存答案），支持"那研发部呢"这类指代
    const QJsonArray session = m_sessions.value(target.recv);  // 取本 channel 的问题环
    for (const QJsonValue& turn : session)
        m_messages.append(turn);                    // 逐条追加历史 user 问题

    m_messages.append(QJsonObject{ {"role", "user"}, {"content", question} });  // 当前问题

    qInfo() << "[agent] 上下文问题数=" << session.size();  // 观察上下文深度
    runToolRound(target, question);                 // 进入首轮工具调用
}

// 工具轮：非流式请求（需要完整解析 tool_calls）。模型可能：
//   (a) 返回 tool_calls → 执行工具、结果回喂、进入下一轮（最多 6 轮，kMaxToolRounds）；
//   (b) 直接返回回答文本 → 视为最终回答：还没用过工具的纯聊天整段发送（不需要流式），
//       已有工具结果的转回答轮（startFinalStream）流式生成。
//
// target 与 question 都按值捕获：这是"这次请求发给谁、问的是什么"的唯一真相，
// 期间来的新消息不会影响它。
/**
 * @brief 递归工具轮：非流式带 tools 调用；到 kMaxToolRounds 上限改用已有结果直接作答
 * @param target 回复地址（按值捕获）
 * @param question 当前问题
 */
void AgentService::runToolRound(const ReplyTarget& target, const QString& question)
{
    if (++m_round > kMaxToolRounds)
    {
        // 工具轮到上限了：不要直接放弃——前面几轮的查询结果都在 m_messages 里，
        // 直接把它们喂回去，让模型组织一次回答（这次不给工具，它就没法继续查）。
        // 原来这里是"我查不动了"，用户问"公司架构"这种要逐个部门查的问题
        // 必然踩中（实测 4 轮就到头），数据白查了。
        qWarning() << "[agent] 工具轮到上限" << kMaxToolRounds << "，改用已有结果直接作答";  // 超限告警
        m_messages.append(QJsonObject{
            {"role", "user"},
            {"content", QStringLiteral("请基于上面已经查到的结果直接回答，不要再调用任何工具；"
                                       "如果信息不够，就说明还缺哪部分。")}
        });                                         // 追加"就用已有结果作答"提示（无 tools）
        m_llm->chat(m_messages, QJsonArray(), /*stream*/false, nullptr,
                    [this, target, question](const QString& content, const QJsonArray&, const QString& err) {
                        const QString text = (err.isEmpty() && !content.trimmed().isEmpty())
                                                 ? content.trimmed()
                                                 : QStringLiteral("抱歉，这个问题我查了几轮还没查完整，换个问法试试？");  // 空/失败兜底文案
                        sendAnswer(target, text);   // 整段发送
                        if (err.isEmpty())
                            recordTurn(target, question, text);  // 成功才记上下文
                        finishFlow();               // 释放 busy 并驱动队列
                    });
        return;                                     // 不再递归工具轮
    }

    m_llm->chat(m_messages, m_tools->toolsJson(), /*stream*/false,
                nullptr,
                [this, target, question](const QString& content, const QJsonArray& toolCalls, const QString& err) {
                    if (!err.isEmpty())
                    {
                        sendAnswer(target, "[AI助手] 调用大模型失败：" + err);  // 模型层失败直接告知
                        finishFlow();               // 释放 busy
                        return;
                    }

                    if (toolCalls.isEmpty())
                    {
                        if (m_round > 1)
                        {
                            // 已有工具查询结果：流式生成最终回答（打字机效果）
                            startFinalStream(target, question);  // 转回答轮
                            return;     // m_busy 由流式结束回调释放
                        }
                        // 没用到工具的纯聊天：整段回答走 msg_stream 一次性写入
                        const QString answer = content.trimmed().isEmpty()
                                                   ? QStringLiteral("（我一时没想到怎么回答）")
                                                   : content.trimmed();  // 空内容兜底
                        sendAnswer(target, answer); // 整段发送
                        recordTurn(target, question, answer);  // 记上下文（仅问题）
                        finishFlow();               // 释放 busy
                        return;
                    }

                    // 有工具调用：把 assistant 消息原样入列，再逐个执行工具
                    QJsonObject assistant;          // assistant 消息
                    assistant.insert("role", "assistant");  // 角色
                    if (!content.isEmpty())
                        assistant.insert("content", content);  // 可能带的思考文本
                    assistant.insert("tool_calls", toolCalls);  // 工具调用列表
                    m_messages.append(assistant);   // 原样入列（OpenAI 要求保留）

                    for (const auto& t : toolCalls)
                    {
                        const QJsonObject tc = t.toObject();  // 单个 tool_call 对象
                        const QString name = tc.value("function").toObject()
                                                 .value("name").toString();  // 工具名
                        QJsonObject args = QJsonDocument::fromJson(
                            tc.value("function").toObject()
                                .value("arguments").toString().toUtf8()).object();  // arguments 字符串 → 对象

                        const QString result = m_tools->invoke(name, args);  // 执行 SQL 工具

                        QJsonObject toolMsg;        // role=tool 回喂消息
                        toolMsg.insert("role", "tool");  // 角色 = tool
                        toolMsg.insert("tool_call_id", tc.value("id").toString());  // 对应 tool_call.id
                        toolMsg.insert("content", result);  // 工具返回的格式化文本
                        m_messages.append(toolMsg); // 追加结果
                    }

                    runToolRound(target, question);     // 回喂结果，继续下一轮
                });
}

// 回答轮：SSE 流式。带上工具轮的查询结果让模型组织最终回答，
// 增量按节流推给客户端（打字机效果）。
/**
 * @brief 回答轮：追加"请据此回答本问题"后 SSE 流式；结束发 done+全文并记历史
 * @param target 回复地址
 * @param question 最终回答针对的问题
 */
void AgentService::startFinalStream(const ReplyTarget& target, const QString& question)
{
    // 在工具轮结果之后追加用户提示，让模型聚焦组织回答。
    // 必须把"当前这个问题"原样带上：上下文里有多个历史问题时，
    // "回答我最初的问题"会指到最早那句上去（实测会答成上一轮的内容）
    m_messages.append(QJsonObject{
        {"role", "user"},
        {"content", QStringLiteral("请根据上面的工具查询结果，用简洁的中文回答这个问题：")
                    + question}
    });                                             // 追加聚焦提示

    m_streamTarget = target;    // 节流回调没有参数，这里记一次当前流的收件人
    m_streamBuf.clear();                            // 清空上一次残留增量

    m_llm->chat(m_messages, QJsonArray(), /*stream*/true,
                [this](const QString& delta) {
                    m_streamBuf += delta;           // 攒批，不立刻发包
                    if (!m_flushTimer->isActive())   // 有数据才开始计时，空转不占用定时器
                        m_flushTimer->start();      // 启动 400ms 节流
                },
                [this, target, question](const QString& full, const QJsonArray&, const QString& err) {
                    flushStreamBuf();               // 先把缓冲里的尾巴发掉

                    if (err.isEmpty())
                    {
                        m_transport->sendStreamDelta(target, QString(), true, full);   // 结束标记（带全文，供服务端落库）
                        m_history->append(target.recv, m_cfg.botId, full);  // 回答也进历史（摘要用）
                        recordTurn(target, question, full);  // 只记问题进上下文
                    }
                    else
                    {
                        qWarning() << "[agent] 流式回答失败:" << err;  // 记录失败原因
                        if (full.isEmpty())          // 一个字都没收到：告诉用户失败了
                            m_transport->sendStreamDelta(target, "[AI助手] 回答生成失败：" + err, false);  // 失败提示
                        m_transport->sendStreamDelta(target, QString(), true);  // 无论如何发结束标记
                    }
                    finishFlow();                   // 释放 busy 并驱动队列
                });
}

// 会话摘要：本会话最近 40 条历史 + 摘要指令，非流式一次性回答。
// 群聊总结群、私聊总结这个人的单聊记录——历史按会话（channel）分开存。
/**
 * @brief 会话摘要：无历史直接提示；有则取最近 40 条非流式生成 3-5 条要点
 * @param target 摘要要发到的回复地址
 */
void AgentService::startSummary(const ReplyTarget& target)
{
    if (m_history->isEmpty(target.recv))
    {
        sendAnswer(target, target.isGroup
            ? QStringLiteral("我在线期间还没有收到群聊消息，暂时没什么可总结的。")
            : QStringLiteral("我们还没聊几句，暂时没什么可总结的。"));  // 空历史短路
        finishFlow();
        return;
    }

    QJsonArray msgs;                                // 摘要用消息数组
    QJsonObject sys;                                // system：摘要指令
    sys.insert("role", "system");                   // 角色
    sys.insert("content", "你是IM里的AI助手。请把下面的聊天记录总结成 3-5 条要点，"
                          "格式为每行一条，简洁准确。");  // 摘要格式约束
    msgs.append(sys);                               // 放入
    msgs.append(QJsonObject{
        {"role", "user"},
        {"content", "聊天记录：\n" + m_history->lastNAsText(target.recv, 40) +
                    "\n\n请总结以上聊天内容。"}   // 最近 40 条历史拼进 user
    });

    m_llm->chat(msgs, QJsonArray(), /*stream*/false, nullptr,
                [this, target](const QString& content, const QJsonArray&, const QString& err) {
                    if (!err.isEmpty())
                        sendAnswer(target, "[AI助手] 摘要生成失败：" + err);  // 失败提示
                    else
                        sendAnswer(target, (target.isGroup ? QStringLiteral("【群聊摘要】\n")
                                                           : QStringLiteral("【聊天摘要】\n"))
                                               + content.trimmed());  // 按会话类型加标题
                    finishFlow();                   // 释放 busy
                });
}

// sendAnswer — 整段回答收尾：msg_stream 一次性写入全文 + done。
// 和流式回答共用同一条渲染路径（气泡归属、出现顺序一致），
// 换行也能保留（普通 msg 早期会被压成一行）。
/**
 * @brief 整段回答：sendStreamDelta(text,true) 一次写完并写入会话历史
 * @param target 回复地址
 * @param text 完整回答
 */
void AgentService::sendAnswer(const ReplyTarget& target, const QString& text)
{
    m_transport->sendStreamDelta(target, text, true);  // 全文+done（复用流式渲染路径）
    m_history->append(target.recv, m_cfg.botId, text);  // 记入摘要数据源
}

// recordTurn — 把这一轮的问题记进会话上下文，供下一句指代使用。
//
// 只记"问了什么"，不记"答了什么"：模型（尤其 flash 档）一旦在历史里看到
// 上一轮的名单，就会把它当成已知数据、跳过工具直接编一个新部门的名单
// （实测：问完人事部再问"那研发部呢"，它把人事部的名单改个名字就发出来了）。
// 上下文只负责让"那…呢"知道说的是哪个部门，事实仍必须现查。
/**
 * @brief 只把问题追加进 m_sessions[channel]；答案参数刻意忽略（防模型复述旧名单）
 * @param target 回复地址（recv 即会话 key）
 * @param question 本问
 * @param answer 本答（Q_UNUSED：保留调用点语义，实现不写入）
 */
void AgentService::recordTurn(const ReplyTarget& target, const QString& question, const QString& answer)
{
    Q_UNUSED(answer);      // 答案不入上下文，参数保留是为了调用点语义清楚

    QJsonArray& session = m_sessions[target.recv];  // 取/建本 channel 的问题环
    session.append(QJsonObject{ {"role", "user"}, {"content", question} });  // 只追加问题

    while (session.size() > kMaxTurns)     // 只留最近 kMaxTurns 个问题
        session.removeFirst();                     // 丢最老的
}

/**
 * @brief 节流回调：有缓冲就发一包 msg_stream，空则停表
 */
void AgentService::flushStreamBuf()
{
    if (m_streamBuf.isEmpty())
    {
        m_flushTimer->stop();                       // 没数据不再空转
        return;
    }
    m_transport->sendStreamDelta(m_streamTarget, m_streamBuf, false);  // 发送本批增量
    m_streamBuf.clear();                            // 清空缓冲
}
