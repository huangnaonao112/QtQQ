#pragma once

#include <QLabel>
#include <qpropertyanimation.h>

// ============================================================================
// 类：RootContactItem — 联系人树最顶层"根节点"的自定义控件
//
// 为什么要单独做一个控件，而不是和 ContactItem 复用：
//   根节点（"苏州科技大学"公司名）需要带一个展开箭头，并且点击/展开时
//   箭头要做 0°→90° 的旋转动画（Qt 树控件展开/折叠的视觉反馈）。
//   普通群组节点不需要这个动画，所以单独继承 QLabel 自己画。
//
// 关键技术点：Q_PROPERTY
//   Q_PROPERTY(int rotation READ getRotation WRITE setRotation)
//   这行代码把成员变量 rotation "注册"成 Qt 的元对象系统可识别的属性，
//   之后 QPropertyAnimation 才能用字符串 "rotation" 绑定它做属性动画。
//   原理：QPropertyAnimation 每帧调 setRotation(新值)，setRotation 里
//   调 update() 触发重绘，paintEvent 读当前 rotation 画箭头，
//   于是箭头角度随时间平滑变化 → 肉眼看到旋转动画。
// ============================================================================

/**
 * @class RootContactItem
 * @brief 联系人树根节点控件：自绘标题文字 + 可 0°↔90° 旋转的展开箭头。
 *
 * 通过 Q_PROPERTY 注册 rotation 属性，配合 QPropertyAnimation 在展开/折叠时
 * 平滑旋转箭头；setText 不走基类绘制，文字暂存后由 paintEvent 自绘。
 */
class RootContactItem  : public QLabel
{
	Q_OBJECT

	/// 把 rotation 暴露给 Qt 元对象系统，供 QPropertyAnimation 按名字绑定
	Q_PROPERTY(int rotation READ getRotation WRITE setRotation)

public:
	/**
	 * @brief 构造函数：初始化成员并调用 init() 创建箭头旋转的属性动画。
	 *
	 * 与参考项目一致的签名：hasArrow 控制是否画展开箭头（默认 true）；
	 * CCMainWindow::initContact 里 new RootContactItem(true, ui.treeWidget)
	 * 用的就是这个两参版本。
	 * @param hasArrow 是否绘制展开箭头（默认 true）
	 * @param parent 父控件，默认 nullptr
	 */
	RootContactItem(bool hasArrow = true, QWidget *parent = nullptr);

	/**
	 * @brief 析构函数：动画对象 an 的 parent 是 this，随控件一起析构，无需手动 delete。
	 */
	~RootContactItem();

	/**
	 * @brief 初始化：固定行高/尺寸策略 + 创建箭头旋转的属性动画（构造内调用）。
	 */
	void init();

	/**
	 * @brief 设置根节点标题文字（暂存后触发重绘，由 paintEvent 自绘）。
	 *
	 * 必须公开：CCMainWindow 的 initContact/展开折叠处理要直接调用
	 * （之前是 private，外部调用直接报 C2248 无法访问 private 成员）。
	 * @param text 根节点标题文字（如"苏州科技大学"）
	 */
	void setText(const QString& text);

	/**
	 * @brief 切换展开/折叠：只设置动画结束值再 start()，从当前角度平滑插值过去。
	 * @param expand true=展开（箭头转到 90° 朝下）；false=折叠（回到 0° 朝右）
	 */
	void setExpanded(bool expand);

private:
	/**
	 * @brief Q_PROPERTY 的 getter：供动画引擎每帧读取当前角度。
	 * @return 当前箭头角度（0=朝右，90=朝下）
	 */
	int getRotation();

	/**
	 * @brief Q_PROPERTY 的 setter：动画引擎每帧调用，保存新角度并请求重绘。
	 * @param r 新的箭头角度（度）
	 */
	void setRotation(int r);

protected:
	/**
	 * @brief 自绘整行内容：先画标题文字（左侧留 24px），再画按 rotation 旋转的箭头。
	 *
	 * 箭头旋转的坐标变换三步：translate 移原点到图片中心 → rotate 旋转坐标系 →
	 * drawPixmap 用负偏移把图片中心对准原点，避免"绕着左上角转"的诡异效果。
	 * @param event 绘制事件，转交基类处理默认背景/边框
	 */
	void paintEvent(QPaintEvent* event) override;

private:
	int rotation;               // 当前箭头角度（0 = 朝右，90 = 朝下）
	bool hasArrow;              // 是否绘制展开箭头
	QString titleText;          // 根节点标题文字（如"苏州科技大学"）
	QPropertyAnimation* an;     // 属性动画对象：驱动 rotation 从 0 平滑变到 90
};
