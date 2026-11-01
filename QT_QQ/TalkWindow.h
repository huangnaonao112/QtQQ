#pragma once

#include <QWidget>
#include <QString>
#include "ui_TalkWindow.h"

// 一个聊天窗：talkId 是"群号"或"对方账号"，统一用 QString——
// 账号可能是 QQ 号那种超过 int 上限的大数字（见 WindowManager.h 的说明）。

/**
 * @class TalkWindow
 * @brief 单个聊天窗（群聊或单聊）：按 talkId 长度分流，管理输入框序列化、成员树与消息上下屏。
 *
 * talkId 为 4 位群号走 initGroupTalk（右侧成员树），更长则视为对方账号走
 * initSingleTalk（装饰图）；构造注册进 WindowManager、析构注销，两边必须成对。
 */
class TalkWindow : public QWidget
{
	Q_OBJECT

public:
	/**
	 * @brief 构造函数：把本窗注册进 WindowManager，按 talkId 分流初始化群/单聊。
	 *
	 * 构造时注册、析构时注销——两边必须成对，否则 WindowManager 里会留悬空指针。
	 * @param id 群号（4 位）或对方账号
	 * @param parent 必须是 TalkWindowShell（聊天窗随壳的堆栈页一起管理）
	 */
	TalkWindow(const QString& id, QWidget *parent = Q_NULLPTR);

	/**
	 * @brief 析构函数：从 WindowManager 注销本 talkId，控件随对象树释放。
	 */
	~TalkWindow();

private:
	/**
	 * @brief 三步总入口：样式 + 信号连接 + 群/单聊分栏（构造内调用）。
	 */
	void init();

	/**
	 * @brief 初始化分栏比例与输入框初始焦点。
	 */
	void initStyle();

	/**
	 * @brief 把标题栏/表情/发送等按钮接到壳和本窗的槽上。
	 */
	void initConnect();

	/**
	 * @brief 按 talkId 长度分流：≤4 位走群聊，否则单聊。
	 */
	void initTalkWindow();

	/**
	 * @brief 群聊：右侧成员树——根项 + 查库逐个 addMember。
	 */
	void initGroupTalk();

	/**
	 * @brief 单聊：不显示成员树，贴一张装饰图占位。
	 */
	void initSingleTalk();

	/**
	 * @brief 把编辑器 QTextDocument 序列化成协议 JSON（发消息/落库共用）。
	 * @param document 输入框对应的富文本文档
	 * @return 协议 JSON 数组
	 */
	QJsonArray parseDocumentToJson(QTextDocument* document);

	/**
	 * @brief 往成员树 rootItem 下挂一个成员（头像/姓名/签名都现场查库）。
	 * @param rootItem 根节点（"苏州科技大学"公司项）
	 * @param info 成员信息对：(账号, 昵称)
	 */
	void addMember(QTreeWidgetItem* rootItem, const QPair<QString, QString>& info);

public:
	/**
	 * @brief 设置顶部标题（群名 或 部门+姓名）。
	 * @param name 标题文本
	 */
	void setWindowName(const QString& name);

	/**
	 * @brief autotest 用：把文本填进输入框并走正常发送链路。
	 * @param text 要发送的文本
	 */
	void sendTestMsg(const QString& text);

	/**
	 * @brief AI 流式增量：把本会话的增量文本转给 WebView 渲染。
	 * @param sender 发送者
	 * @param delta 本次增量文本
	 * @param done 是否已结束（结束时做收尾渲染）
	 */
	void onStreamDelta(const QString& sender, const QString& delta, bool done);

	/**
	 * @brief 表情面板点选后，把表情图插入输入框光标处。
	 * @param name 表情资源编号
	 */
	void addEmotionImage(int name);

	/**
	 * @brief 取本窗对应的群号/对方账号。
	 * @return talkId 字符串
	 */
	QString getTalkId();

	/**
	 * @brief 收到新消息：渲染上屏 + 回发服务端确认。
	 * @param jsonMsg 消息 JSON 数组
	 * @param obj 附加对象（如文件描述）
	 */
	void appendObjectMsg(const QJsonArray& jsonMsg, QString obj);

	/**
	 * @brief 历史消息：只渲染、不回发（打开聊天窗时由服务端下发的记录）。
	 * @param sender 发送者
	 * @param text 消息正文
	 */
	void appendHistoryMsg(const QString& sender, const QString& text);

private slots:
	/**
	 * @brief 发送按钮：空白/超长拦截 → 序列化 → 上屏（由 WebView 回发）。
	 */
	void onSendBtnClicked();

	/**
	 * @brief 双击成员树：双击成员开单聊，双击根项无动作。
	 * @param item 被双击的树节点
	 */
	void onTreeItemDoubleClicked(QTreeWidgetItem *item);

	/**
	 * @brief 打开一次性 SendFile 弹窗（WA_DeleteOnClose，用完即毁）。
	 */
	void onFileOpenBtnClicked();

private:
	Ui::TalkWindow ui;   // ui 指针持有本窗全部控件

	const QString talkId;  // 群号或对方账号（const：会话身份不变）
};
