#pragma once

#include "BasicWindow.h"
#include "ui_RecvFile.h"
#include <qjsonobject.h>

/**
 * @class RecvFile
 * @brief "接收文件"确认弹窗：拿着对端发来的文件 JSON 问用户存不存、存哪，校验后落盘。
 *
 * 数据来自信号链透传的 fileObj（SendFile::signalSendFile 原样传来），
 * 含 FILE_NAME / FILE_LENGTH / FILE_CONTENT（base64）三字段；本窗口
 * 只负责确认与落盘，配合 WA_DeleteOnClose 关闭即销毁。
 */
class RecvFile : public BasicWindow
{
	Q_OBJECT

public:
	/**
	 * @brief 构造函数：用对端发来的文件 JSON 初始化确认框。
	 *
	 * data 即信号链透传的 fileObj；设置 WA_DeleteOnClose，关闭即自动销毁，
	 * 调用方无需手动 delete。
	 * @param data 对端发来的文件 JSON（文件名/长度/base64 内容）
	 * @param parent 父控件，默认 Q_NULLPTR
	 */
	RecvFile(const QJsonObject& data, QWidget *parent = Q_NULLPTR);

	/**
	 * @brief 析构函数：无手动资源，控件随对象树释放。
	 */
	~RecvFile();

	/**
	 * @brief 一行 setter：把提示文案刷到提示标签上。
	 * @param msg 提示文案（如"是否接收 xxx 的文件"）
	 */
	void setLabelMsg(const QString& msg);

private:
	/**
	 * @brief 初始化：标题栏 + 样式 + 摆位（和发送窗错开，两个弹窗同时开不重叠）。
	 */
	void init();

private slots:
	/**
	 * @brief 确定接收：base64 解码 → 长度校验 → 选目录落盘；任一失败统一提示"接受失败"。
	 */
	void on_okBtn_clicked();

	/**
	 * @brief 取消接收：直接关窗（WA_DeleteOnClose 负责销毁），什么都不落盘。
	 */
	void on_cancelBtn_clicked();

private:
	Ui::ReceiveFile ui;   // ui 指针持有本窗的全部控件

	QJsonObject fileObj;  // 对端发来的文件 JSON（文件名/长度/base64 内容）
};
