#include "stdafx.h"
#include "EmotionLabel.h"
#include "define.h"

// ============================================================================
// 构造函数：一个 32×32 的表情格子
// name = 表情编号，决定加载 emotion/<name>.png；图片和编号的对应关系
//   由 EmotionWindow::initControl 的网格布局统一生成，本类只管按号取图。
// 点击转发：QClickLabel::clicked（无参）→ 本类补上自己的编号再发
//   emotionClicked(int)，面板层拿到编号才知道用户选了哪张。
// ============================================================================
/**
 * @brief 表情格子构造：按编号加载 emotion/<name>.png，固定 32×32；
 *        把基类无参 clicked 信号补上自己的编号再转发为 emotionClicked(int)。
 * @param name 表情编号（由 EmotionWindow 网格布局统一生成）
 * @param parent 父窗口指针（一般传 EmotionWindow）
 */
EmotionLabel::EmotionLabel(int name, QWidget *parent)
	: QClickLabel(parent), emotionName(name)
{
	setAlignment(Qt::AlignCenter);      //居中对齐
	setObjectName("emotionLabelItem");  // 对象名，供 QSS 用 #emotionLabelItem 选择器定位
	setFixedSize(32, 32);               // 固定 32×32，网格布局按此排

	QString imagePath = IMG_PATH(QString::number(name));   // 拼出 :/Resources/MainWindow/emotion/<name>.png
	this->setPixmap(QPixmap(imagePath));   // 加载表情图并贴到 label

	// 捕获 this 而不是 name：统一从成员取，改成员时所有点击都跟着变
	connect(this, &QClickLabel::clicked, [this]()
	{
		emit emotionClicked(emotionName);   // 补上编号后发出，面板层才知道用户选了哪张
	});
}

/**
 * @brief 析构：无自有资源，pixmap 随控件释放
 */
EmotionLabel::~EmotionLabel()
{
}
