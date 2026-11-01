#include "stdafx.h"
#include "MsgTextEdit.h"
#include "define.h"

/**
 * @brief 消息输入框构造：只是个套了样式的 QTextEdit，无自有资源
 * @param parent 父窗口指针（一般传 TalkWindow）
 */
MsgTextEdit::MsgTextEdit(QWidget *parent)
	: QTextEdit(parent)
{
}

/**
 * @brief 析构：无自有资源，随对象树自动释放
 */
MsgTextEdit::~MsgTextEdit()
{
}

/**
 * @brief 在光标处插一张表情 <img>：路径按 IMG_QRC + 编号 + IMG_PNG 拼出来，
 *        和发送端 parseDocumentToJson 剥路径取名的逻辑互为逆运算——协议里传的永远只是那个编号。
 * @param emotionName 表情编号（和协议里传的一致）
 */
void MsgTextEdit::addEmotionHtml(int emotionName)
{
	const QString emotionPath = IMG_QRC + QString::number(emotionName) + IMG_PNG;   // 拼 :/Resources/MainWindow/emotion/<n>.png
	insertHtml(QString(IMG_SRC).arg(emotionPath));   // 按 <img src="%1"> 模板插入到光标处
}
