#include "HistoryStore.h"     // 会话历史声明

// capacity = 每个会话保留的尾部条数，超出即丢最老的（见 append）
/**
 * @brief 构造：记录 capacity；parent 交给 QObject
 * @param capacity 每会话尾部条数
 * @param parent Qt 父对象
 */
HistoryStore::HistoryStore(int capacity, QObject* parent)
    : QObject(parent), m_capacity(capacity)        // 挂父并保存容量
{
}

// append — 按会话（channel）分桶追加；超过容量从头删，
// 留下的永远是最近 m_capacity 条。operator[] 对新 channel 自动建桶，
// 调用方不用先判存在
/**
 * @brief 追加 Entry 到 channel 桶；超容量 removeFirst 丢最老
 * @param channel 会话 key
 * @param sender 发送者
 * @param text 内容
 */
void HistoryStore::append(const QString& channel, const QString& sender, const QString& text)
{
    QVector<Entry>& entries = m_channels[channel];  // operator[] 自动建桶
    entries.append(Entry{ sender, text });           // 追加新条目
    while (entries.size() > m_capacity)
        entries.removeFirst();                     // 丢最老，保持环形语义
}

// lastNAsText — 该会话最近 n 条，按时间正序拼成 "账号: 内容"（直接可喂 LLM）。
// n 大于存量时取全部：begin 夹到 0，不会越界
/**
 * @brief 最近 n 条按正序拼 "账号: 内容"；n 过大则 begin=0 取全部
 * @param channel 会话 key
 * @param n 最多取几条
 * @return 多行文本（去掉尾部空白），空会话为空串
 */
QString HistoryStore::lastNAsText(const QString& channel, int n) const
{
    const QVector<Entry> entries = m_channels.value(channel);  // value 对不存在 key 返回空 vector

    QString out;                                    // 输出缓冲
    const int begin = entries.size() > n ? entries.size() - n : 0;  // 起点夹紧，防越界
    for (int i = begin; i < entries.size(); ++i)
        out += entries[i].sender + ": " + entries[i].text + "\n";  // 每行一条
    return out.trimmed();                           // 去尾部空白
}

// isEmpty — 该会话还没有任何记录（AgentService 在"总结"前判空，
// 没有历史就不浪费一次 LLM 调用）
/**
 * @brief 判断 channel 是否尚无记录
 * @param channel 会话 key
 * @return true=空
 */
bool HistoryStore::isEmpty(const QString& channel) const
{
    return m_channels.value(channel).isEmpty();     // 缺失 key 视为 空
}
