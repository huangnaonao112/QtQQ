#include "stdafx.h"
#include "EmotionWindow.h"
#include "CommonUtils.h"
#include <qpainter.h>

// ============================================================================
// 构造函数：表情选择面板——无边框 + 半透明的顶层小窗
// Qt::SubWindow 而不是 Window：它逻辑上从属于聊天壳，不该在任务栏单独占位；
// WA_TranslucentBackground 配合 paintEvent 画 QSS 背景，圆角边缘才不发黑。
// ============================================================================
/**
 * @brief 表情选择面板构造：无边框（FramelessWindowHint）+ 子窗口（SubWindow，不进任务栏）+
 *        透明背景（配合 QSS 画圆角），然后铺满 rowCount×columnCount 网格。
 * @param parent 父窗口指针（一般传聊天壳，整壳只建一个、常驻隐藏）
 */
EmotionWindow::EmotionWindow(QWidget *parent)
	: QWidget(parent)
{
	ui.setupUi(this);   // 根据 EmotionWindow.ui 创建子控件（含 gridLayout）

	setWindowFlags(Qt::FramelessWindowHint | Qt::SubWindow);         //无边框/子窗口
	setAttribute(Qt::WA_TranslucentBackground, true);                //透明

	initControl();   // 铺满表情网格
}

/**
 * @brief 析构：表情 label 都是本窗的子对象，随对象树释放
 */
EmotionWindow::~EmotionWindow()
{
}

/**
 * @brief 按 rowCount×columnCount 网格铺满全部表情（共 rowCount*columnCount 个），
 *        编号 = 行*列数+列，和 EmotionLabel / MsgTextEdit 里的资源名一一对应——
 *        改布局必须同步改这套编号，否则点 A 出 B。
 */
void EmotionWindow::initControl()
{
	CommonUtils::loadStyleSheet(this, "EmotionWindow");   // 加载 Resources/QSS/EmotionWindow.css

	//168个表情
	for (int row = 0; row < rowCount; row++)   // 遍历每一行
	{
		for (int column = 0; column < columnCount; column++)   // 遍历每一列
		{
			ui.gridLayout->addWidget(initLabel(row * columnCount + column), row, column);   // 编号=行*列数+列，贴进网格
		}
	}
}

/**
 * @brief 造一个表情格子并把它的点击接到自己的 addEmotion 信号——
 *        本面板不关心谁在听，插入逻辑归 TalkWindowShell::onAddEmotion。
 * @param name 表情编号（= 行*列数+列）
 * @return EmotionLabel* 新建的表情格子（parent 已设为 this）
 */
EmotionLabel * EmotionWindow::initLabel(int name)
{
	EmotionLabel* label = new EmotionLabel(name, this);   // 新建格子，parent 为本窗
	connect(label, &EmotionLabel::emotionClicked, this, &EmotionWindow::addEmotion);   // 点击→转发为面板级信号

	return label;   // 返回给调用方贴进网格
}

/**
 * @brief 无边框窗必须手动触发 PE_Widget 才能画出 QSS 的 background（和 BasicWindow 同理）
 * @param event Qt 传入的绘制事件对象，转发给父类
 */
void EmotionWindow::paintEvent(QPaintEvent * event)
{
	QWidget::paintEvent(event);   // 保留默认绘制行为

	QStyleOption opt;                          // 样式选项容器
	opt.initFrom(this);       //获取控件的当前样式

	QPainter p(this);   // 在本窗体上开画笔
	style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);   // PE_Widget 让 QSS 背景生效
}
