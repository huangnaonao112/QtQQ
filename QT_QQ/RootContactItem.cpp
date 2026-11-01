#include "stdafx.h"
#include "RootContactItem.h"
#include <qpainter.h>

/**
 * @brief 构造函数：初始化成员并调用 init() 创建箭头旋转的属性动画。
 * @param hasArrow 是否绘制展开箭头
 * @param parent 父控件，默认 nullptr
 */
RootContactItem::RootContactItem(bool hasArrow, QWidget *parent)
	: QLabel(parent), hasArrow(hasArrow), rotation(0), an(nullptr)   // 基类先构造，再初始化自有成员
{
	init();                              // 设置尺寸策略 + new 属性动画
}

/**
 * @brief 析构函数：动画对象 an 的 parent 是 this，随控件一起析构，无需手动 delete。
 */
RootContactItem::~RootContactItem()
{}

/**
 * @brief 设置控件尺寸 + 创建属性动画对象。
 *
 * an = new QPropertyAnimation(this, "rotation", this)：
 *   第 1 个 this=动画作用对象，"rotation"=Q_PROPERTY 注册的属性名，
 *   第 3 个 this=父对象，Qt 在 this 析构时自动 delete 动画对象。
 */
void RootContactItem::init()
{
	setFixedHeight(32);       // 固定高度：联系人树每一行都是 32px 高，视觉统一
	// sizePolicy：水平方向 Expanding（随父容器拉伸），垂直方向 Fixed（固定 32px）
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	// new QPropertyAnimation(this, "rotation", this)：
	//   - 第 1 参数 this：动画作用对象是当前控件
	//   - 第 2 参数 "rotation"：要做动画的属性名（就是 Q_PROPERTY 注册的那个）
	//   - 第 3 参数 this：父对象，Qt 会在 this 析构时自动 delete 动画对象
	an = new QPropertyAnimation(this, "rotation", this);   // 创建并绑定 rotation 属性的动画对象
	an->setDuration(30);      // 动画持续时间 30ms：很短，模拟"立刻翻转"的干脆感
	an->setEasingCurve(QEasingCurve::InQuad);   // 动画缓动曲线
	                                            // QEasingCurve::InQuad = 渐进式加速，开始慢、后面快，
	                                            // 视觉上像箭头被"甩"过去
}

/**
 * @brief Q_PROPERTY 的 getter：供动画引擎每帧读取当前角度。
 * @return 当前箭头角度（0=朝右，90=朝下）
 */
int RootContactItem::getRotation()
{
	return rotation;                     // 返回成员里缓存的当前角度
}

/**
 * @brief Q_PROPERTY 的 setter：保存新角度并 update() 请求重绘。
 *
 * 动画引擎每帧调 setRotation(中间值)：
 *   1) 保存新角度  2) update() → Qt 随后调 paintEvent，画出新角度的箭头。
 * @param r 新的箭头角度（度）
 */
void RootContactItem::setRotation(int r)
{
	rotation = r;                        // 记录动画本帧写入的新角度
	update();                            // 旋转角度更新，触发重绘
}

/**
 * @brief 覆盖基类 setText：文字暂存到 titleText，再触发重绘。
 *
 * 不直接交给父类画（父类会画在默认位置），而是把文字存起来，
 * 在 paintEvent 里和箭头一起按自定义布局绘制。
 * @param text 根节点标题文字
 */
void RootContactItem::setText(const QString& text)
{
	titleText = text;                    // 暂存文字，供 paintEvent 自绘使用
	update();                            // 文本改变，触发重绘
}

/**
 * @brief 展开/折叠时由外部调用：设置动画结束值再启动，从当前角度平滑插值。
 *
 * 原理：只设置动画的"结束值"然后 start()，
 *       QPropertyAnimation 会从当前角度平滑插值到目标角度。
 *   expand=true  → 结束值 90°（箭头朝下，表示展开）
 *   expand=false → 结束值 0° （箭头朝右，表示折叠）
 * @param expand true=展开（90°）；false=折叠（0°）
 */
void RootContactItem::setExpanded(bool expand)
{
	if (expand)
	{
		an->setEndValue(90);            // 设置属性动画的结束值：转到 90 度
	}
	else
	{
		an->setEndValue(0);             // 折回去：0 度
	}

	an->start();             // 启动动画：从当前角度向 endValue 插值
}

/**
 * @brief 自绘整行内容：标题文字 + 按 rotation 旋转的展开箭头。
 *
 * 绘制分两层：
 *   1) 先画标题文字（从 x=24 开始，给左边的箭头留出位置）
 *   2) 如果 hasArrow=true，再画箭头图标并按 rotation 旋转
 *
 * 箭头旋转的坐标变换三步（Qt 2D 绘图经典套路）：
 *   translate(cx, cy)：把坐标系原点移到箭头图片中心
 *   rotate(rotation)  ：以新原点为轴顺时针旋转坐标系
 *   drawPixmap(-w/2, -h/2, ...)：按"以原点为中心"的方式画图
 * 这样旋转中心恰好是图片中心，不会出现"绕着左上角转"的诡异效果。
 * @param event 绘制事件，转交基类处理默认背景
 */
void RootContactItem::paintEvent(QPaintEvent* event)
{
	QLabel::paintEvent(event);      // 默认绘制行为（背景、边框等交给父类）

	//绘制文本
	QPainter p1(this);              // 在本控件上绘图
	p1.setRenderHint(QPainter::TextAntialiasing, true);         // 文本抗锯齿，字体边缘更平滑

	QFont font;
	font.setPointSize(10);          // 字体大小 10pt
	p1.setFont(font);
	// drawText( x, y, w, h, 对齐方式, 文字 )：
	//   文字区域从 (24, 0) 开始，宽 = 控件宽 - 24（左边留 24px 给箭头），高 = 整行
	p1.drawText(24, 0, width() - 24, height(), Qt::AlignLeft | Qt::AlignVCenter, titleText);   // 左对齐垂直居中画标题

	//绘制箭头
	if (hasArrow)       // 存在箭头
	{
		QPixmap arrow(":/Resources/MainWindow/arrow.png");   // 加载箭头原图
		QPixmap pixmap(arrow.size());            // 底片：和原图一样大的透明画布
		pixmap.fill(Qt::transparent);            // 填充透明，避免白底盖住文字

		QPainter p2(&pixmap);                    // 在透明画布上画旋转后的箭头
		p2.setRenderHint(QPainter::SmoothPixmapTransform, true);          // 提高图片质量，旋转时边缘抗锯齿

		// 关键：把坐标系原点移到图片中心，后续 rotate 才是"绕中心转"
		p2.translate(pixmap.width() / 2, pixmap.height() / 2);            // 平移坐标系到图像中心
		p2.rotate(rotation);                                              // 在中心位置顺时针旋转坐标系

		// 因为原点已经在中心，画图时要把图片往左上方挪半宽/半高，
		// 这样图片中心正好落在原点上
		p2.drawPixmap(0 - pixmap.width() / 2, 0 - pixmap.height() / 2, arrow);   // 以原点为中心画旋转后的箭头

		// 把画好的旋转后的箭头贴到控件左侧 (6, 垂直居中) 的位置
		p1.drawPixmap(6, (this->height() - pixmap.height()) / 2, pixmap);        // 贴到控件左侧垂直居中
	}
}

