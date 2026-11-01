#include "stdafx.h"
#include "SkinWindow.h"
#include "NotifyManager.h"

/**
 * @brief 构造函数：换肤面板——一次性临时窗（WA_DeleteOnClose，每次点工具栏按钮重开）。
 *
 * 颜色块不在这里摆，留给 initControl 数据驱动生成（见其 Doxygen 说明）。
 * @param parent 父窗口指针（一般 nullptr）
 */
SkinWindow::SkinWindow(QWidget *parent)
	: BasicWindow(parent)                // 转交父类构造
{
	ui.setupUi(this);                    // 生成 ui 控件并绑定到本窗

	setAttribute(Qt::WA_DeleteOnClose);       // 关闭时释放窗体（皮肤窗是临时面板，不常驻）
	loadStyleSheet("SkinWindow");             // 加载 SkinWindow.qss 样式
	initControl();                            // 生成 12 宫格色块 + 接线系统按钮
}

/**
 * @brief 析构函数：WA_DeleteOnClose 已保证关闭时自动释放本窗口，此处无额外清理。
 */
SkinWindow::~SkinWindow()
{
}

/**
 * @brief 生成 12 宫格颜色块 + 接线标题栏系统按钮。
 *
 * 为什么颜色用代码生成而不是画在 .ui 里：
 *   12 块布局完全相同、只有颜色不同，典型的"数据驱动"场景——
 *   一个双重循环 + 颜色数组 3 行搞定，比在 Designer 里手摆 12 个
 *   Label 再逐个改 palette 省事得多，以后要加减颜色也只改数组。
 *
 * 为什么用 QClickLabel 而不是 QPushButton：
 *   颜色块本质是"一块可点的纯色区域"，按钮自带边框/按下效果/QSS，
 *   还得想办法把样式清掉；QLabel 填个 palette 就是最干净的色块，
 *   缺的"点击"能力由 QClickLabel（重写 mousePressEvent 发 clicked 信号）补上。
 */
void SkinWindow::initControl()
{
	//12 个候选皮肤色（3 行 x 4 列，与 ui 里的 gridLayout 对应）
	QList<QColor> colorList =
	{
		QColor(22, 154, 128), QColor(40, 138, 221), QColor(49, 166, 107), QColor(218, 67, 68),
		QColor(177, 99, 158), QColor(107, 81, 92), QColor(89, 92, 160), QColor(21, 156, 199),
		QColor(79, 169, 172), QColor(155, 183, 154), QColor(128, 77, 77), QColor(240, 188, 189)
	};

	for (int row = 0; row < 3; row++)      // 遍历 3 行
	{
		for (int column = 0; column < 4; column++)   // 遍历 4 列
		{
			//row*4+column 把一维数组按行优先映射到二维网格
			ui.gridLayout->addWidget(initLabel(colorList[row * 4 + column]), row, column);   // 生成色块并按行列摆入网格
		}
	}

	//标题栏按钮：SkinWindow.ui 自带 sysmin/sysclose（同 CCMainWindow 的套路）
	connect(ui.sysmin, &QPushButton::clicked, this, &BasicWindow::onMin);      // 最小化 → 基类 onMin
	connect(ui.sysclose, &QPushButton::clicked, this, &BasicWindow::onClose);  // 关闭 → 基类 onClose
}

/**
 * @brief 构造一个颜色块 QClickLabel，并接上"点击 → 换肤通知"的 lambda。
 *
 * 为什么用 setPalette 而不是样式表：palette 是控件自身的调色板，
 * 不经过 QSS 引擎；而这些色块不参与换肤（皮肤窗自己永远长这样），
 * 用最底层、最不会被外部样式覆盖的方式上色最稳。
 *
 * 为什么 lambda 里按值捕捉 color 而不是引用：
 *   color 是 initLabel 的形参（栈变量），循环下一轮就会被销毁/复用，
 *   引用捕捉到点击触发时早已悬空；按值拷贝一份进 lambda 才安全。
 *   （所有"捕捉局部变量且信号延迟触发"的 lambda 都要遵守这条）
 * @param color 色块要显示的颜色
 * @return 建好的 QClickLabel*（parent 为 this），由 initControl 加进 gridLayout
 */
QClickLabel* SkinWindow::initLabel(const QColor& color)
{
	QClickLabel* label = new QClickLabel(this);   // 建色块，parent 为本窗随窗销毁
	label->setCursor(Qt::PointingHandCursor);        // 鼠标悬停变手型，暗示可点击
	label->setFixedSize(84, 84);                     // 固定 84x84，保证 12 块等大
	label->setAutoFillBackground(true);              // 必须开：QLabel 默认不画背景
	label->setPalette(QPalette(color));              // 把色块颜色填进调色板

	//点击色块 → 发布换肤通知（NotifyManager 负责广播 + 写 color.ini）
	connect(label, &QClickLabel::clicked, [color]()   // 按值捕捉，防止形参出栈后悬空
	{
		NotifyManager::instance()->notifySkinChanged(color);   // 广播换肤通知 + 写配置
	});

	return label;                           // 交给调用方摆进 gridLayout
}
