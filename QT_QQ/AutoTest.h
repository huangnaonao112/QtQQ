#pragma once

class QObject;

// ============================================================================
// AutoTest — 无人值守的冒烟测试钩子（--autotest）
//
// 用法：QT_QQ.exe --autotest --account <账号> --password <密码>
//                  [--talk <群号/对方账号>] [--msg <要发送的文本>]
//
// 流程：自动登录（见 UserLogin）→ 等 3 秒主界面初始化 → 打开 --talk 指定的
//       聊天窗（默认公司群 2000）→ 再等 4 秒让 WebEngine 页面加载完 →
//       把 --msg 的内容填进输入框，走正常发送链路（onSendBtnClicked）。
//
// 从 CCMainWindow::init 里搬出来的原因：这是测试支路，不该常驻生产代码；
// 放在这里也方便配合机器人的 --selftest 做端到端回归。
// ============================================================================
/**
 * @brief 无人值守冒烟测试钩子：登录完成后自动打开聊天窗并发一条消息，给"收→发→渲染"提供回归入口。
 *        仅在命令行带 --autotest 时由 CCMainWindow::init 调用；时序 3s+4s 是为主界面初始化
 *        与 WebEngine 页面加载预留的时间窗，太早发送会打在空页面上。
 * @param context 定时器与 lambda 的宿主 QObject（传 CCMainWindow，随窗口析构取消挂起任务）
 */
void scheduleAutoTest(QObject* context);
