#include "stdafx.h"
#include "CustomMenu.h"
#include "CommonUtils.h"

/**
 * @brief 构造函数：开启透明背景（配合 Menu.css 实现半透明圆角菜单），并加载 Resources/QSS/Menu.css。
 * @param parent 父窗口指针（一般传宿主主窗口，如 CCMainWindow）
 */
CustomMenu::CustomMenu(QWidget* parent)
	: QMenu(parent)
{
	setAttribute(Qt::WA_TranslucentBackground);       // 透明背景：配合 Menu.css 实现半透明圆角菜单
	CommonUtils::loadStyleSheet(this, "Menu");         // 加载 Resources/QSS/Menu.css（菜单项样式、选中高亮等）
}

/**
 * @brief 析构：QAction 都是 QMenu 的子对象，随菜单释放；map 只存裸指针不拥有所有权
 */
CustomMenu::~CustomMenu()
{}

/**
 * @brief 添加菜单项：创建一个"图标+文字"的 QAction，并按名字登记到 map，便于后续按名字取回。
 * @param name 菜单项名字（内部 key），如 "show"、"quit"，供 getMenuAction 取回
 * @param icon 菜单项左侧图标的 QRC 路径
 * @param text 菜单项显示文字（如 "显示"、"退出"）
 */
void CustomMenu::addMenuAction(const QString& name, const QString& icon, const QString& text)
{
	QAction* action = this->addAction(QIcon(icon), text);   // QMenu::addAction：在菜单末尾追加一个菜单项
	map.insert(name, action);                                // 把 QAction 按名字存进映射表，便于后面取回
}

/**
 * @brief 按名字取回 QAction：外部拿到后 connect triggered 信号即可响应菜单点击。
 *        注意 QMap::operator[] 在 key 不存在时会自动插入空指针并返回，调用方需判空。
 * @param name addMenuAction 登记时的名字
 * @return QAction* 对应的菜单项；名字不存在返回 nullptr
 */
QAction* CustomMenu::getMenuAction(const QString& name)
{
	return map[name];   // QMap 的 operator[]：key 不存在时自动插入空指针并返回，调用方需判空
}
