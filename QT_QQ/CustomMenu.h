#pragma once

#include <QMenu>
#include <qmap.h>

// ============================================================================
// 类：CustomMenu — 自定义右键菜单
//
// 为什么需要这个类：
//   程序里的右键菜单（如系统托盘的"显示/退出"菜单）需要 QQ 风格外观：
//   半透明背景 + 统一 QSS 样式（Resources/QSS/Menu.css）。
//   直接用系统默认的 QMenu 样式会和整个应用的视觉风格不一致，
//   所以继承 QMenu，在构造函数里加载 Menu.css 并开启透明背景。
//
// 为什么用 QMap 按名字管理菜单项：
//   菜单项以"名字"注册（如 "show"、"quit"），外部按名字取回 QAction*，
//   再 connect 它的 triggered 信号。用名字代替下标/指针，
//   调用方无需关心 addAction 的返回值和插入顺序，代码意图更清晰。
// ============================================================================
class CustomMenu : public QMenu
{
	Q_OBJECT

public:
	/**
	 * @brief 构造函数：开启透明背景（WA_TranslucentBackground），并加载 Menu.css 实现半透明圆角菜单。
	 * @param parent 父窗口指针（一般传宿主主窗口，如 CCMainWindow）
	 */
	CustomMenu(QWidget* parent = nullptr);
	/**
	 * @brief 析构：QAction 都是 QMenu 的子对象，随菜单释放；map 只存裸指针不拥有所有权
	 */
	~CustomMenu();

public:
	/**
	 * @brief 添加一个带图标的菜单项，并按名字登记到 map，便于后续按名字取回。
	 * @param name 菜单项名字（内部 key），如 "show"、"quit"，供 getMenuAction 取回
	 * @param icon 菜单项左侧图标的 QRC 路径
	 * @param text 菜单项显示文字（如 "显示"、"退出"）
	 */
	void addMenuAction(const QString& name, const QString& icon, const QString& text);

	/**
	 * @brief 按名字取回之前登记的 QAction*；外部拿到后 connect triggered 信号即可响应菜单点击。
	 *        注意 QMap::operator[] 在 key 不存在时会自动插入空指针并返回，调用方需判空。
	 * @param name addMenuAction 登记时的名字
	 * @return QAction* 对应的菜单项；名字不存在返回 nullptr
	 */
	QAction* getMenuAction(const QString& name);

private:
	QMap<QString, QAction*> map;      // 名字 → QAction 的映射表，实现按名字管理菜单项
};
