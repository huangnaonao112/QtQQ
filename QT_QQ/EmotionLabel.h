#pragma once

#include "QClickLabel.h"

/**
 * @brief 联系人/表情窗里的单个表情格子：按编号加载 emotion/<name>.png，
 *        点击后补上自己的编号再发出 emotionClicked(int)，面板层拿到编号才知道用户选了哪张。
 */
class EmotionLabel : public QClickLabel
{
	Q_OBJECT

public:
	/**
	 * @brief 表情格子构造：按编号加载 emotion/<name>.png，固定 32×32；
	 *        把基类无参 clicked 信号补上自己的编号再转发为 emotionClicked(int)。
	 * @param name 表情编号（由 EmotionWindow 网格布局统一生成）
	 * @param parent 父窗口指针（一般传 EmotionWindow）
	 */
	EmotionLabel(int name, QWidget *parent);
	/**
	 * @brief 析构：无自有资源，pixmap 随控件释放
	 */
	~EmotionLabel();

signals:
	/**
	 * @brief 点击后带编号发出（QClickLabel::clicked 无参，这里补上自己是谁）
	 * @param emotionName 表情编号
	 */
	void emotionClicked(int emotionName);

private:
	int emotionName;   // 表情编号，决定加载哪张图以及点击时转发的编号
};
