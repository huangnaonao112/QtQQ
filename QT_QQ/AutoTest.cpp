#include "stdafx.h"
#include <QCoreApplication>
#include <QStringList>
#include <QTimer>
#include <QDebug>
#include "AutoTest.h"
#include "WindowManager.h"
#include "TalkWindow.h"

extern QString LoginUserAccount;      // 定义在 CCMainWindow.cpp

// --autotest：打开指定聊天窗并发一条消息，给"收→发→渲染"提供无人值守的回归入口。
// 时间点选 3s / +4s：主界面要等登录回包 + 联系人树构建完，聊天窗里的
// WebEngine 页面还要再从磁盘加载一次脚本，太早发送会打在一个空页面上。
/**
 * @brief 无人值守冒烟测试钩子：登录完成后等 3s 打开 --talk 指定聊天窗（默认公司群 2000），
 *        再等 4s 让 WebEngine 页面加载完，最后把 --msg 文本走正常发送链路发出。
 *        仅在命令行带 --autotest 时由 CCMainWindow::init 调用，属测试支路不进生产流程。
 * @param context 定时器与 lambda 的宿主 QObject（传 CCMainWindow，随窗口析构取消挂起任务）
 */
void scheduleAutoTest(QObject* context)
{
    QTimer::singleShot(3000, context, [context]() {   // 等 3s：主界面要完成登录回包+联系人树构建
        const QStringList args = QCoreApplication::arguments();   // 取命令行参数，解析 --talk/--msg

        QString openId = QStringLiteral("2000");   // 默认公司群（账号/群号统一用字符串）
        const int ti = args.indexOf("--talk");     // 找 --talk 的位置
        if (ti >= 0 && ti + 1 < args.size())       // 找到了且后面还有值
            openId = args.at(ti + 1);              // 用命令行指定的群号/账号覆盖默认值

        WindowManager::instance()->addNewTalkWindow(openId);   // 打开（或激活）目标聊天窗口

        QTimer::singleShot(4000, context, [context, openId, args]() {   // 再等 4s：WebEngine 要从磁盘加载脚本
            QWidget* w = WindowManager::instance()->findWindowMap(openId);   // 按 id 找刚建好的聊天窗
            TalkWindow* talk = qobject_cast<TalkWindow*>(w);                  // 转成 TalkWindow* 才能调 sendTestMsg
            if (!talk)                       // 没找到 / 类型不对（比如只开了壳没建会话）
            {
                qWarning() << "[autotest] 没找到聊天窗口" << openId;   // 日志告知回归失败点
                return;                      // 放弃本次发送，避免空指针崩溃
            }

            const int mi = args.indexOf("--msg");   // 找 --msg 的位置
            const QString text = (mi >= 0 && mi + 1 < args.size())
                ? args.at(mi + 1)                                                // 用命令行指定的消息文本
                : QString("auto-test from %1").arg(LoginUserAccount);            // 缺省用当前登录账号拼一条

            qInfo() << "[autotest] 窗口" << openId << "发送:" << text;  // 日志记录实际发送内容
            talk->sendTestMsg(text);   // 走正常发送链路（onSendBtnClicked 同路径），保证覆盖真实代码
        });
    });
}
