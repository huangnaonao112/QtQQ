#pragma once
#include <QWidget>

// ============================================================================
// 类：CommonUtils — 通用工具类，提供静态辅助方法
// 为什么设计为静态方法类：这些操作（如加载样式表）是无状态的，
// 不需要实例化对象，直接通过类名调用即可，类似于命名空间。
// 构造函数为 protected 以防止外部实例化。
// ============================================================================
class CommonUtils
{
protected:
	/**
	 * @brief 构造函数：protected 禁止外部实例化（工具类只用静态方法）
	 */
	CommonUtils();
	/**
	 * @brief 析构函数：protected 禁止外部实例化（工具类只用静态方法）
	 */
	~CommonUtils();

public:
	/**
	 * @brief 加载 QSS 样式表到指定控件：先 setStyleSheet(QString()) 复位避免新旧样式叠加，
	 *        再把 QRC 里的文件内容与 otherStyle 拼在一起设置上去；打开失败时静默返回。
	 *        BasicWindow 用 otherStyle 注入运行时皮肤色。
	 * @param widget 要设置样式的目标控件指针（传 nullptr 直接 return）
	 * @param fileName 样式表文件名（不含路径和后缀），如 "Title"、"UserLogin"；内部通过 QRC_QSS 宏拼成 ":/Resources/QSS/<fileName>.css"
	 * @param otherStyle 可选的额外样式字符串，会追加在文件内容之后；BasicWindow::loadStyleSheet 用它注入运行时皮肤色
	 */
	static void loadStyleSheet(QWidget* widget,
		const QString& fileName,
		const QString& otherStyle = QString());

	/**
	 * @brief 用 mask 的 alpha 通道把 src 裁成圆形/圆角头像：先画 mask，再用 CompositionMode_SourceIn
	 *        画 src（只在已有不透明像素处画），mask 透明处保持透明。mask 和 src 必须缩放成完全相同尺寸且居中对齐。
	 * @param src 原始图片（QPixmap）
	 * @param mask 遮罩图片（不透明区域=头像显示区域，一般是 head_mask.png）
	 * @param maskSize 目标输出尺寸；传 QSize(0,0)（默认）则用 mask 自身尺寸
	 * @return QPixmap 裁好的圆角头像，直接 setPixmap 到 QLabel 即可
	 */
	static QPixmap getRoundedPixmap(QPixmap src, QPixmap mask, QSize maskSize = QSize(0, 0));
};
