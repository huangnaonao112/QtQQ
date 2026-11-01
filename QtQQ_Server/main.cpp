#include "QtQQ_Server.h"
#include <QtWidgets/QApplication> // QApplication 应用对象（事件循环）
#include <QFile>                  // 调试日志文件
#include <QDateTime>              // 日志时间戳

/**
 * @brief 把 qDebug/qWarning 落到 exe 同目录 server_debug.log（GUI 程序没有控制台，不装此 handler 则日志全丢）
 *
 * 实现细节：WriteOnly|Append 追加打开；每次写一行 [hh:mm:ss.zzz] msg + '\n'。
 * 排查投递/连库问题时优先看这个文件（诊断盲区的补救措施）。
 * @param 未用 QtMsgType 日志级别（本 handler 不区分级别，统一落盘）
 * @param 未用 QMessageLogContext 上下文（文件/行号未写入，保持输出简洁）
 * @param msg 要落盘的一行消息文本
 */
static void fileMessageHandler(QtMsgType, const QMessageLogContext&, const QString& msg)
{
	QFile f(QCoreApplication::applicationDirPath() + "/server_debug.log"); // 日志与 exe 同目录，部署即生效
	if (f.open(QIODevice::WriteOnly | QIODevice::Append)) // 追加打开：进程重启不清空历史
	{
		f.write(("[" + QDateTime::currentDateTime().toString("hh:mm:ss.zzz") + "] " + msg + QChar(0x0A)).toUtf8()); // 时间戳+消息+换行一次写完，UTF-8 落盘
		f.flush(); // 立刻刷盘，崩溃时也能保住最后一段
	}
}

/**
 * @brief 服务端进程入口：先装文件日志 handler，再建主窗口进事件循环
 *
 * GUI 程序没有控制台：不装 handler 的话 qDebug/qWarning 全部丢失，
 * 排查投递、连库问题只能靠 server_debug.log（见 fileMessageHandler）。
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组（Qt 会解析并改写）
 * @return int 事件循环退出码
 */
int main(int argc, char *argv[])
{
    QApplication a(argc, argv); // 创建 Qt 应用对象（必须最先）
	qInstallMessageHandler(fileMessageHandler);   // 文件日志初始化，此后 qDebug 全部落盘
    QtQQ_Server w;                                 // 主窗口对象（构造里会装 UI / 起监听 / 连库）
    w.show();                                      // 显示主窗口，用户可见服务端界面

    return a.exec();                               // 进入事件循环，主线程在这里阻塞直到退出
}