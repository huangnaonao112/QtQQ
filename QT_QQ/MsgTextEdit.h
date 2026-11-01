#pragma once

#include <QTextEdit>

/**
 * @brief 消息输入框：只是个套了样式的 QTextEdit，提供在光标处插入表情 <img> 的能力
 */
class MsgTextEdit : public QTextEdit
{
	Q_OBJECT

public:
	/**
	 * @brief 消息输入框构造：空实现，只是套了样式的 QTextEdit，无自有资源
	 * @param parent 父窗口指针（一般传 TalkWindow）
	 */
	MsgTextEdit(QWidget *parent);
	/**
	 * @brief 析构：无自有资源，随对象树自动释放
	 */
	~MsgTextEdit();

	/**
	 * @brief 在光标处插入表情 <img>；路径按 IMG_QRC + 编号 + IMG_PNG 拼出，
	 *        与发送端 parseDocumentToJson 剥路径取名互为逆运算
	 * @param emotionName 表情编号（和协议里传的一致）
	 */
	void addEmotionHtml(int emotionName);
};
