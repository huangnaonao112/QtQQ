#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include <QHash>

// ============================================================================
// HistoryStore — 会话历史环形缓存（按会话分开存）
//
// channel 就是回复地址（ReplyTarget::recv）：
//   群聊 → 群号（2000）          私聊 → 对方账号（10001）
// 这样"总结"永远总结当前这个会话：群里不会总结出私聊内容，
// 私聊里也不会莫名其妙把公司群倒出来。
//
// 只缓存机器人在线期间的消息（不持久化，重启即清零——课程项目可接受；
// 持久化可后续写 SQLite）。
// ============================================================================
/**
 * @brief 按 channel（群号/对方账号）分桶的有界历史环形缓存（仅内存）
 */
class HistoryStore : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 一条历史记录（发送者账号 + 纯文本）
     */
    struct Entry
    {
        QString sender;     // 发送者账号
        QString text;       // 纯文本内容
    };

    /**
     * @brief 构造：设置每 channel 保留条数上限
     * @param capacity 每会话尾部保留条数（超出丢最老）
     * @param parent Qt 父对象
     */
    explicit HistoryStore(int capacity = 50, QObject* parent = nullptr);

    /**
     * @brief 追加一条；新 channel 自动建桶，超出 capacity 丢最老
     * @param channel 会话 key（群号/对方账号）
     * @param sender 发送者
     * @param text 内容
     */
    // 追加一条；新 channel 自动建桶，超出 capacity 丢最老
    void append(const QString& channel, const QString& sender, const QString& text);

    /**
     * @brief 取最近 n 条，按时间正序拼 "账号: 内容" 每行一条（可直接喂 LLM）
     * @param channel 会话 key
     * @param n 条数（n>存量则取全部）
     * @return 多行文本，空会话返回空串
     */
    // 该会话最近 n 条，格式："账号: 内容" 每行一条（直接可作 LLM 输入）
    QString lastNAsText(const QString& channel, int n) const;

    /**
     * @brief 该会话是否还没有任何记录（总结前判空用，避免无谓 LLM 调用）
     * @param channel 会话 key
     * @return true=空
     */
    // 该会话是否还没有任何记录（总结前判空用）
    bool isEmpty(const QString& channel) const;

private:
    QHash<QString, QVector<Entry>> m_channels;   // 每个会话只保留尾部 capacity 条
    int m_capacity;                              // 每会话条数上限
};
