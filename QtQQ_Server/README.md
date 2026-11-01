# QtQQ_Server — QtQQ 局域网聊天系统服务端

本工程是课程资料原版 `QtQQ_Server`（VS2017 + Qt 5.9.6 msvc2017_64 工程）按本机环境的完整复刻，
功能、接口、数据库操作与原版一一对应，已通过 Qt 5.14.1 编译验证。

## 一、功能与架构

```
QtQQ 客户端 A ──TCP──> ┌─────────────────────────────────────────┐
QtQQ 客户端 B ──57777──> │ TcpServer (TCP 57777)                   │
                       │   收到消息 → signalTcpMsgData 信号        │
                       │        ↓                                 │
                       │ QtQQ_Server 主窗口                        │
                       │   onUdpBroadcast：UDP 广播 (端口 59999)    │
                       │        ↓                                 │
                       │ 局域网所有客户端监听 59999 → 群聊消息送达    │
                       └─────────────────────────────────────────┘
                       │ UseMySQL (单例)                           │
                       │   员工增/删/查 → tab_accounts/tab_employees│
```

* **TCP 57777**：客户端把聊天消息发给服务端（可靠传输）；
* **UDP 59999**：服务端向 `255.255.255.255` 广播，同网段所有客户端都能收到，服务端无需维护在线名单；
* **员工管理界面**：表格显示员工（部门/账号/姓名/签名/头像/状态），支持按部门查询、按工号查询、添加员工（姓名+部门+寸照入库）、注销员工（`status` 置 0，逻辑删除）。

## 二、环境适配（与原版的差异）

| 项目 | 原版 | 本工程 |
|---|---|---|
| 构建体系 | Visual Studio .vcxproj + Qt 5.9.6 | qmake `.pro` + Qt 5.14.1（Qt Creator 直接打开） |
| 数据库 | `qtqq_main`，密码为原作者的 | 本机已有的 `qt_qq_main`（与 `C:\c++\qt\QT_QQ` 客户端共用），root/【MYSQL密码已移除】@127.0.0.1:3306 |
| 中文处理 | `QString::fromLocal8Bit`（原文件中文已损坏成乱码） | 源码 UTF-8 + `QStringLiteral`，任意套件下都不乱码 |
| SQL 拼接 | `.arg()` 字符串拼接（有注入风险） | 用户输入处改为 `prepare + addBindValue`（与客户端 QT_QQ 写法一致），查询行为不变 |
| logo 图片 | `.ui` 引用作者机器的相对路径 | 工程内资源 `:/QtQQ_Server/Resources/images/gsq.png` |
| 已知缺陷 | 查询无结果时 `info[0]` 越界崩溃 | 已修复：显示空表格 |

**功能未删减**：TCP 监听/转发、UDP 广播、部门查询、员工查询、添加（双表写入+失败回滚）、注销（逻辑删除）、表格刷新逻辑均与原版一致。

## 三、构建与运行

1. 用 Qt Creator 打开 `QtQQ_Server.pro`；
2. **套件选择 "Desktop Qt 5.14.1 msvc2017 (32-bit)"** —— 本机只有这套件编译过 QMYSQL 驱动
   （`C:\Qt\Qt5.14.1\5.14.1\msvc2017\plugins\sqldrivers\qsqlmysql.dll`，与 QT_QQ 客户端同款配置）；
   选 MinGW 套件能编译但运行时会报 "Driver not loaded"；
3. 直接 Ctrl+R 运行。启动后控制台会打印：
   * `QSqlDatabase::drivers()` 列表（应含 `QMYSQL`）；
   * `successed to open database` / `failed to open database` + 具体原因；
   * `服务端监听端口 57777 成功！`。

数据库连接参数集中在 `UseMySQL.cpp` 的 `connectMySQL()`，换库/换密码只改这一处。

## 四、数据库

本机 MySQL 8.0（root/【MYSQL密码已移除】@3306）中 `qt_qq_main` 库已存在并含初始数据，无需导入。
`sql/qt_qq_main.sql` 是完整建库脚本（表结构 + 初始数据，picture 路径已指向本机 QT_QQ 资源），
换机重建时执行：`mysql -uroot -p < sql/qt_qq_main.sql`。

三张表：
* `tab_accounts`：工号 → 登录码（客户端登录验证的就是它；服务端添加员工时往里写一行）；
* `tab_department`：部门表（2000 公司群是根，"查询全公司"即取它的 ID）；
* `tab_employees`：员工表（`status` 1=有效 0=注销，注销后客户端登录被拒）。

## 五、文件清单

| 文件 | 说明 |
|---|---|
| `QtQQ_Server.pro` | qmake 工程文件（network + sql 模块，msvc 加 /utf-8） |
| `main.cpp` | 程序入口 |
| `QtQQ_Server.h/.cpp` | 主窗口：调度 TCP→UDP 广播、表格展示、增删查交互 |
| `TcpServer.h/.cpp` | TCP 监听器：57777 端口，事件驱动接收客户端连接与数据 |
| `UseMySQL.h/.cpp` | 数据库单例：全部 SQL 封装在此 |
| `QtQQ_Server.ui` | 界面（控件名与原版一致，槽靠 connectSlotsByName 自动连接） |
| `QtQQ_Server.qrc` + `Resources/images/gsq.png` | 界面 logo 资源 |
| `sql/qt_qq_main.sql` | 建库脚本（备用） |
