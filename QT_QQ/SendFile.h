#pragma once

#include "BasicWindow.h"
#include "ui_SendFile.h"
#include <qjsonvalue.h>

/**
 * @class SendFile
 * @brief "发送文件"一次性弹窗：选文件 → 5MB 校验 → base64 打成 JSON → 经 shell 发出。
 *
 * WA_DeleteOnClose：发送/关闭后由 Qt 直接 delete，调用方绝不能在 close 后
 * 再碰它的指针。协议契约见 signalSendFile 注释（与 RecvFile 一一对应）。
 */
class SendFile : public BasicWindow
{
	Q_OBJECT

public:
	/**
	 * @brief 构造函数：搭出"发送文件"对话框，设置 WA_DeleteOnClose 并初始化。
	 * @param parent 父控件；这里传当前聊天窗仅用于定位，窗口实际是独立弹窗
	 */
	SendFile(QWidget *parent = Q_NULLPTR);

	/**
	 * @brief 析构函数：窗口一次性使用，无资源需手动释放。
	 */
	~SendFile();

private:
	/**
	 * @brief 初始化标题栏/样式 + 把 signalSendFile 接到 shell（不带 talkId 的兜底重载）。
	 */
	void init();

signals:
	/**
	 * @brief 出口信号：把 {文件名, 长度, base64 内容} 三字段 JSON 发给 shell。
	 *
	 * 与 RecvFile 的解析字段一一对应，改字段两边必须同步；对端是
	 * TalkWindowShell::sendMsgToServer，由它包成完整消息发给服务端。
	 * @param data 文件 JSON（FILE_NAME / FILE_LENGTH / FILE_CONTENT）
	 */
	void signalSendFile(QJsonValue data);

private slots:
	/**
	 * @brief 选文件：只把路径暂存到 filePath 并回显输入框，此时还没读文件内容。
	 */
	void on_openBtn_clicked();

	/**
	 * @brief 读全文件 → 5MB 校验 → base64 打包 → emit 信号发出 → 关窗销毁。
	 *
	 * 三条硬约束：整文件进内存必须卡 5MB；内容必须 base64 后进 QJsonValue
	 * （原始字节按 UTF-8 解码会把图片/压缩包截成乱码）；打不开文件就清
	 * filePath 让用户重选，绝不发半条消息。
	 */
	void on_sendBtn_clicked();

private:
	Ui::SendFile ui;   // ui 指针持有本窗的全部控件

	QString filePath;  // 用户选中的文件绝对路径（选文件时暂存，发送时才读内容）
};
