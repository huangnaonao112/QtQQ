#pragma once

#include "BasicWindow.h"
#include "ui_TalkWindowShell.h"
#include <qmap.h>
#include "EmotionWindow.h"
#include <qjsonvalue.h>
#include <qtcpsocket.h>
#include "TalkWindow.h"
#include "TalkWindowItem.h"

/*
//类声明 防止头文件相互包含
class TalkWindow;
class TalkWindowItem;
*/

/**
 * @class TalkWindowShell
 * @brief 聊天主框架壳：管理会话列表↔聊天页映射、表情面板、TCP 收发与历史请求。
 *
 * WA_DeleteOnClose：最后一个聊天窗关掉时壳自毁，WindowManager 靠 destroyed
 * 清指针后下次重建；TCP 走 NDJSON 协议，带心跳重连与未连上时的历史请求排队。
 */
class TalkWindowShell : public BasicWindow
{
	Q_OBJECT

public:
	/**
	 * @brief 构造函数：初始化 TCP/表情面板/样式，并把自毁信号接到 WindowManager。
	 *
	 * WA_DeleteOnClose：最后一个聊天窗关掉时壳自毁，WindowManager 靠 destroyed
	 * 清指针后下次重建（见 .cpp）。
	 * @param parent 父控件，默认 Q_NULLPTR
	 */
	TalkWindowShell(QWidget *parent = Q_NULLPTR);

	/**
	 * @brief 析构函数：关闭 socket、停止重连定时器，控件随对象树释放。
	 */
	~TalkWindowShell();

private:
	/**
	 * @brief 样式→列表信号→表情面板→生成 JS→连网（顺序有讲究，见 .cpp）。
	 */
	void init();

	/**
	 * @brief QSS + 列表代理样式 + 分栏比例。
	 */
	void initStyle();

	/**
	 * @brief 建唯一的表情面板（常驻隐藏，按钮切换显隐）。
	 */
	void initEmotionWindow();

	/**
	 * @brief 把 qrc 里的 JS 模板落盘到临时目录，并现场生成 msghandling.js。
	 */
	void initJsFile();

	/**
	 * @brief 按员工列表展开 JS 模板。
	 * @param fileContent 模板文本（就地替换）
	 * @return 展开成功 true；失败 false
	 */
	bool replaceJsTxt(QString& fileContent);

	/**
	 * @brief NDJSON 连接 + hello 自报 + 心跳重连。
	 */
	void initTcpSocket();

public:
	/**
	 * @brief 新窗口上架：列表项/堆栈页/映射三处同步挂载，并接好关闭拆卸链路。
	 * @param window 新建的聊天窗
	 * @param windowItem 对应的侧边栏列表项
	 */
	void addTalkWindow(TalkWindow* window, TalkWindowItem* windowItem);

	/**
	 * @brief 已有窗口切前台（按 map 反查列表项选中）。
	 * @param widget 要显示的聊天窗
	 */
	void updateCurrentWindow(QWidget* widget);

	/**
	 * @brief 打开聊天窗时拉最近的历史；未连上服务端则先排队，连上后补发。
	 * @param with 对端标识（群号或对方账号）
	 */
	void requestHistory(const QString& with);

public slots:
	/**
	 * @brief 发送消息（带 talkId）：由 TalkWindow 调用，多会话时不会发错人。
	 * @param talkId 目标会话标识
	 * @param data 消息 JSON
	 */
	void sendMsgToServer(const QString& talkId, QJsonValue data);

	/**
	 * @brief 发送消息（不带 talkId）：留给 SendFile 这类不知道自己是哪个会话的调用方。
	 *
	 * 按"当前显示的聊天窗"兜底；带 talkId 的版本由 TalkWindow 调用。
	 * @param data 消息 JSON
	 */
	void sendMsgToServer(QJsonValue data);

private slots:
	/**
	 * @brief 点侧边会话列表项 → 右侧堆栈页翻到对应聊天窗。
	 * @param item 被点击的列表项
	 */
	void onListItemClicked(QListWidgetItem* item);

	/**
	 * @brief 表情按钮 → 面板显隐切换 + 摆位。
	 */
	void onEmotionBtnClicked();

	/**
	 * @brief 表情点选 → 收面板 + 把表情插入当前输入框。
	 * @param name 表情资源编号
	 */
	void onAddEmotion(int name);

	/**
	 * @brief TCP 可读：按 NDJSON 拆行，逐条 processDatagram 路由。
	 */
	void onTcpReadyRead();

private:
	/**
	 * @brief 单条消息路由：按 JSON 类型字段分发（文本/文件/历史等）。
	 * @param msg 单条完整 NDJSON 文本
	 */
	void processDatagram(const QByteArray& msg);

	Ui::TalkWindowShell ui;   // ui 指针持有壳窗全部控件

	QMap<QListWidgetItem*, QWidget*> map;    // 列表项与窗口的映射
	EmotionWindow* emotionWindow;            // 唯一表情面板（常驻隐藏）

	QTcpSocket* tcp;                 // 与服务端的收发连接
	QTimer*     m_reconnectTimer;    // 心跳重连（服务端重启/闪断后自动恢复接收）

	QByteArray m_tcpBuf;             // TCP 接收缓冲（NDJSON 拆行用）
	QStringList m_pendingHistory;    // 连接建立前攒下的历史请求（连上后补发）
};
