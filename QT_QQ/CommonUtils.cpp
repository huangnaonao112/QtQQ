#include "stdafx.h"
#include "CommonUtils.h"
#include "define.h"
#include <qpainter.h>

/**
 * @brief 构造函数：protected 禁止外部实例化（工具类只用静态方法，见头文件说明）
 */
CommonUtils::CommonUtils()
{}

/**
 * @brief 析构函数：protected 禁止外部实例化（工具类只用静态方法，见头文件说明）
 */
CommonUtils::~CommonUtils()
{}

// ============================================================================
// 从 QRC 资源系统中读取样式表文件并应用到控件
//
// 为什么用 QRC 而非外部文件：
//   QRC（Qt Resource Collection）将资源编译进可执行文件，
//   好处是：(1) 不依赖外部文件路径，不会出现"找不到文件"的问题；
//          (2) 资源被压缩存储，减小分发体积；
//          (3) 防止用户误删/篡改 UI 资源。
//
// 为什么先清空再设置：
//   如果控件已有样式表（比如在 Qt Designer 中设置的），
//   新的样式表会与旧规则叠加。先 setStyleSheet(QString()) 相当于"复位"，
//   保证最终的样式完全由 QSS 文件决定，不受历史状态影响。
// ============================================================================
/**
 * @brief 从 QRC 资源系统读取样式表文件并应用到控件。
 *        先 setStyleSheet(QString()) 复位避免新旧样式叠加，再把文件内容与 otherStyle
 *        拼在一起设置上去；打开失败时静默返回（控件保持无样式，不崩溃）。
 *        用 QRC 而非外部文件：资源编译进 exe，不会"找不到文件"，也防误删篡改。
 * @param widget 要设置样式的目标控件指针（传 nullptr 直接 return）
 * @param fileName 样式表文件名（不含路径和后缀），如 "Title"、"UserLogin"；内部通过 QRC_QSS 宏拼成 ":/Resources/QSS/<fileName>.css"
 * @param otherStyle 可选的额外样式字符串，会追加在文件内容之后；BasicWindow::loadStyleSheet 用它注入运行时皮肤色
 */
void CommonUtils::loadStyleSheet(QWidget* widget,
	const QString& fileName,
	const QString& otherStyle)
{
	if (!widget) {
		return;  // 空指针保护，避免对空控件操作
	}

	widget->setStyleSheet(QString()); // 清除控件当前样式，防止叠加

	// 用宏拼接 QRC 路径，例如 loadStyleSheet(w, "Title") → ":/Resources/QSS/Title.css"
	const QString filePath = QRC_QSS(fileName);   // 拼出完整 QRC 路径

	QFile file(filePath);   // 打开 QRC 文件（QFile 能透明读 QRC）

	if (file.open(QFile::ReadOnly)) {   // 打开成功
		QString qss = file.readAll();  // 读取 QSS 文件全部内容
		qss += otherStyle;             // 拼接头文件传入的附加样式（默认为空）

		widget->setStyleSheet(qss);    // 将完整的样式表应用到控件
		file.close();                  // 关闭文件句柄
	}
	// 如果文件打开失败（文件不存在），静默失败：
	// 控件保持无样式状态，不会崩溃。可在调试时加 qDebug 输出。
}

// ============================================================================
// 用 mask 的 alpha 通道抠出 src，生成带透明背景的圆形/圆角头像
//
//   arg1 src      原始图片
//   arg2 mask     遮罩图片（不透明区域 = 头像显示区域）
//   arg3 maskSize 目标尺寸，为空时使用遮罩自身尺寸
//
// 原理：先把 mask 画上去，再用 CompositionMode_SourceIn 画 src。
//   SourceIn 只在"已有像素不透明"的地方绘制 src，
//   于是 src 被 mask 的形状裁剪，mask 透明处保持透明。
//
// 关键点：mask 和 src 必须缩放成完全相同的尺寸，且居中对齐，
//   否则两者错位，会出现露边或头像偏移。
// ============================================================================
/**
 * @brief 用 mask 的 alpha 通道抠出 src，生成带透明背景的圆形/圆角头像。
 *        原理：先画 mask，再用 CompositionMode_SourceIn 画 src——SourceIn 只在"已有像素不透明"
 *        的地方绘制 src，于是 src 被 mask 形状裁剪，mask 透明处保持透明。
 *        关键点：mask 和 src 必须缩放成完全相同的尺寸且居中对齐，否则会露边或偏移。
 * @param src 原始图片
 * @param mask 遮罩图片（不透明区域 = 头像显示区域，一般是 head_mask.png）
 * @param maskSize 目标输出尺寸；传 QSize(0,0)（默认）则用 mask 自身尺寸
 * @return QPixmap 裁好的圆角头像，直接 setPixmap 到 QLabel 即可
 */
QPixmap CommonUtils::getRoundedPixmap(QPixmap src, QPixmap mask, QSize maskSize)
{
	// scaled() 返回缩放后的"新" QPixmap，不会修改自身，必须接收返回值！
	// 未指定尺寸时，直接使用遮罩自身的尺寸
	if (maskSize.isEmpty())
	{
		maskSize = mask.size();   // 未指定则用遮罩自身尺寸
	}

	// 遮罩缩放到目标尺寸；IgnoreAspectRatio 保证结果和目标尺寸完全一致
	// （KeepAspectRatio 在非正方形图上会有一边缩不满，导致和 src 尺寸不一致）
	mask = mask.scaled(maskSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);   // 遮罩强制缩放到目标尺寸

	// 原图按 KeepAspectRatioByExpanding 缩放：等比缩放且保证覆盖整个遮罩区域，
	// 不出现某一边留白；超出遮罩的部分后面居中绘制时会被自然裁掉
	src = src.scaled(maskSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);   // 原图等比放大到盖满遮罩

	QImage image(maskSize, QImage::Format_ARGB32_Premultiplied);   // 创建目标尺寸 ARGB 画布
	QPainter p(&image);   // 在画布上开画笔

	p.setCompositionMode(QPainter::CompositionMode_Source);   // Source 模式：整块覆盖
	p.fillRect(image.rect(), Qt::transparent);        // 整张图先铺满透明
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);   // 回到默认叠加模式
	p.drawPixmap(0, 0, mask);                          // 第 1 层：画遮罩形状

	// 第 2 层：居中画原图。长宽方向各把超出遮罩的部分对称裁掉，
	// 不能左上角 (0,0) 对齐，否则尺寸有偏差时头像整体偏向右下
	const int x = (maskSize.width() - src.width()) / 2;   // 水平居中偏移（可能为负，表示要裁掉左右）
	const int y = (maskSize.height() - src.height()) / 2; // 垂直居中偏移
	p.setCompositionMode(QPainter::CompositionMode_SourceIn);   // SourceIn：只在已有不透明像素处画
	p.drawPixmap(x, y, src);   // 居中绘制原图，透明区域外自然被裁掉

	return QPixmap::fromImage(image);   // QImage 转 QPixmap 返回
}
