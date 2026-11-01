#pragma once

#include <QString>

// ============================================================================
// ReplyTarget — 一次请求的"回复地址"
//
//   recv     群号（群聊）或对方账号（单聊）
//   isGroup  true  → 协议 type=0，客户端按群号路由到群窗口
//            false → 协议 type=1，客户端按"对方账号"路由到单聊窗口
//
// 为什么单独抽成一个值类型，而不是 AgentService 的成员变量：
//   回复地址属于"这一次请求"，不属于服务本身。挂在成员上时，新消息一进来
//   就会把它覆盖掉——正在生成的群聊回答会被改成发给刚私聊的那个用户。
//   做成值类型后，它跟着异步回调一路传下去，与请求同生命周期，串台不可能发生。
// ============================================================================
/**
 * @brief 一次请求的回复地址值类型：recv=群号/对方账号，isGroup 决定协议 type
 */
struct ReplyTarget
{
    QString recv;                 // 群号（群聊）或对方账号（单聊）
    bool    isGroup;              // true→type=0 群聊；false→type=1 单聊

    /**
     * @brief 默认构造：isGroup=true（历史默认，实际使用都走两参构造）
     */
    ReplyTarget() : isGroup(true) {}
    /**
     * @brief 显式构造回复地址
     * @param recv 群号或对方账号
     * @param isGroup 是否群聊（决定协议 type 与唤醒词策略）
     */
    ReplyTarget(const QString& recv, bool isGroup) : recv(recv), isGroup(isGroup) {}
};
