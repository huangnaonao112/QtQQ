#pragma once

#include <QtWidgets/QDialog>     // 基类：带模态语义的对话框窗口
#include "ui_QtQQ_Server.h"      // uic 生成的界面类（.ui 文件对应）
#include "TcpServer.h"           // TCP 监听与消息投递
#include <QStandardItemModel>    // 表格数据模型

// ============================================================================
// QtQQ_Server — 服务端主窗口
//
// 它是整个服务端的"调度中心"，串起两件事：
//   (1) TcpServer：监听 TCP 端口（server.ini 配置，默认 57777），报文解析、
//       落库与投递全部在 TcpServer 里完成，主窗口不插手转发；
//   (2) UseMySQL：员工增删查，结果用 QStandardItemModel 填到 tableView。
//
// 消息怎么走：全程一条 TCP——群聊广播给所有在线客户端，私聊点对点只发给
// 收件人（不在线则先落库，等上线再补发）。
// 早期版本是"TCP 收、UDP 广播"，但 Windows 上多个 socket 用 SO_REUSEADDR
// 绑同一 UDP 端口时广播包只会投递给其中一个（实测），多开/私聊场景丢消息，
// 已改成按连接投递（实现见 TcpServer：hello 登记账号后私聊点对点、群聊广播）。
// 服务端会解析报文：hello 登记连接归属、history 回历史、业务消息先落库再投递。
// ============================================================================

/**
 * @brief 服务端主窗口：装 UI、起 TCP 监听、管员工表（增删查改）
 *
 * 不负责消息转发——转发全在 TcpServer；不负责 SQL——SQL 全在 UseMySQL。
 * 消息走法：群聊广播、私聊点对点（在线）或先落库等补发（离线）。
 */
class QtQQ_Server : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief 构造主窗口：装 UI → 去掉 QDialog 默认"?"帮助按钮 → init() 完成全部装配
     * @param parent 父窗口指针，缺省 nullptr（顶层窗口）
     */
    QtQQ_Server(QWidget *parent = nullptr);
    /**
     * @brief 析构：无手动释放（tcpServer/model 挂在 this 上，随窗口一起销毁）
     */
    ~QtQQ_Server();

private:
    /**
     * @brief 启动装配：先起 TCP 监听，再懒加载数据库单例，最后刷一次初始表格
     *
     * 顺序有意为之：数据库暂时不可用时窗口照常出来、端口照常监听，
     * 排错看 server_debug.log，不把整个程序卡死在启动阶段。
     */
    void init();
    /**
     * @brief 创建 TcpServer 并开始监听（端口来自 server.ini 的 AppConfig）
     *
     * 监听与投递都在 TcpServer 里完成：它自己维护"连接 → 账号"的登记表，
     * 群聊广播、私聊点对点，主窗口不再插手转发。
     */
    void initTcpServer();
    /**
     * @brief 刷新员工表格：按 departmentID / employeeID 三分支查询后重填 tableView
     *
     * 三分支：(1) 部门ID非空且不是公司群 → 查该部门成员；
     *         (2) 员工ID非空 → 查单个员工（添加/注销后用它刷新）；
     *         (3) 都不传 → 默认显示公司群（即全公司）成员。
     * 查无结果时给空表格，不会 info[0] 越界（原版这里会崩）。
     * @param departmentID 部门ID，传空时不按部门过滤
     * @param employeeID 员工ID，传空时不按员工过滤
     */
    void updateTableData(QString departmentID = QString(), QString employeeID = QString());
    /**
     * @brief 返回表头文案列表（顺序必须等于 tab_employees 列序）
     * @return QStringList 七列中文表头：序号/部门/账号/姓名/签名/头像/状态
     */
    QStringList getHeaderLabelsText();

private slots:
    /**
     * @brief [槽] 选择员工头像：只记路径不复制文件，取消则不覆盖上一次选择
     */
    void on_selectPictureBtn_clicked();
    /**
     * @brief [槽] 添加员工：校验 → 入库 → 复位输入区 → 刷新表格并定位新行
     */
    void on_addEmployeeBtn_clicked();
    /**
     * @brief [槽] 按下拉框选中的部门查成员（先清员工号输入框，避免两条件同时生效）
     */
    void on_queryDepBtn_clicked();
    /**
     * @brief [槽] 按工号查单个员工（部门下拉复位只是 UI 提示，查询仍只传 employeeID）
     */
    void on_queryIDBtn_clicked();
    /**
     * @brief [槽] 注销员工：status 置 0 的逻辑删除，不物理删行
     */
    void on_deleteIDBtn_clicked();

private:
    Ui::QtQQ_ServerClass ui;               // uic 生成的界面控件访问入口

    TcpServer* tcpServer;                  // 端口来自 server.ini（AppConfig），挂 this 自动释放

    QStandardItemModel* model;             // 数据模型（tableView 的数据源），刷新时 deleteLater 旧的
    QString picturePath;                   // 选中的头像路径（正斜杠绝对路径，直接入库）
};
