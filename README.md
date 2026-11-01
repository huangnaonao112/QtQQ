# QtQQ — 局域网 IM + AI 助手

一套跑在局域网里的简易 QQ：三个独立工程 + 一个 MySQL 库，合起来是
「客户端聊天 + 服务端转发 + AI 助手机器人」。

```
QtQQ/
├─ QT_QQ/        客户端（Qt Widgets + WebEngine，聊天界面）
├─ QtQQ_Server/  服务端（消息转发中枢 + 员工/部门管理界面）
├─ QtQQ_Bot/     AI 助手（无界面进程，接大模型）
├─ scripts/      一键启停 / 一键编译
└─ README.md
```

三个工程各自是一个 git 仓库，互不依赖代码，只共享 **协议** 和 **数据库**。

## 架构

```
        ┌──────────────┐   NDJSON over TCP 57777   ┌───────────────┐
        │   QT_QQ      │ ────────────────────────► │               │
        │  (客户端)     │ ◄──────────────────────── │  QtQQ_Server  │
        └──────────────┘        逐连接单播          │   (中枢)      │
        ┌──────────────┐                            │               │
        │  QtQQ_Bot    │ ◄────────────────────────► │               │
        │  (AI 助手)    │                            └───────┬───────┘
        └──────┬───────┘                                    │
               │ OpenAI 兼容 API                      ┌──────┴───────┐
               ▼                                      │  MySQL 8.0   │
        open.bigmodel.cn                              │  qt_qq_main  │
                                                      └──────────────┘
```

- 服务端**不解析业务报文**：收到一行 JSON 就转发给所有在线连接，协议演进不用改服务端。
- 客户端和机器人共用一条 TCP 连接收发（服务端按行拆包）。
- 机器人挂了不影响聊天；服务端挂了客户端会自动重连、机器人有 5 秒心跳重连。

## 环境要求

| 组件 | 版本 |
|---|---|
| Qt | 5.14.1（客户端 `msvc2017` 32 位，机器人和服务端 `msvc2017_64`） |
| Qt VS Tools | VS 扩展，负责 moc/uic/rcc 与部署（套件在扩展里注册好这两个 kit） |
| Visual Studio | 2017+（工程工具集 v141；MSBuild 由 `vswhere` 自动查找） |
| MySQL | 8.0，库名 `qt_qq_main` |

> 路径改过：原来是 `C:\c++\qt\QT_QQ` 等三个平级目录，现在收进 `C:\c++\qt\QtQQ\`。
> 数据库里员工/部门的头像路径、`QtQQ_Server/sql/qt_qq_main.sql` 里的种子数据都已同步。

## 构建

三个工程都是 **Visual Studio 工程**（`.slnx` + `.vcxproj`），用 Qt VS Tools 生成
moc/uic/rcc。在 VS 里直接打开各自的 `.slnx` 就能编译调试。

命令行一键编译（不需要先开开发者命令行，脚本自己用 `vswhere` 找 MSBuild）：

```bat
scripts\build-all.bat
```

各工程配置：

| 工程 | 平台 | 套件 | 产物目录 |
|---|---|---|---|
| `QtQQ_Server.vcxproj` | x64 | `5.14.1_msvc2017_64` | `QtQQ_Server\build-msvc64\release\` |
| `QtQQ_Bot.vcxproj` | x64 | `5.14.1_msvc2017_64` | `QtQQ_Bot\build-x64\release\` |
| `QT_QQ.vcxproj` | Win32 | `5.14.1_msvc2017` | `QT_QQ\Debug\` |

产物目录刻意对齐原来的 qmake 目录，已部署的 Qt DLL、`qsqlmysql.dll`、
`libmysql.dll`、`bot.ini` 等运行时文件原地复用，构建后不用重新部署。

> 机器人源码在 `src/` 子目录，vcxproj 里的路径要写 `src\xxx.cpp`——
> 少了这个前缀 moc 会拿不到头文件（报 `moc exited with code 1`）。
>
> 服务端和机器人**请用 Release 配置编译**：64 位套件的
> `plugins\sqldrivers` 里只有 `qsqlmysql.dll`（release），没有
> `qsqlmysqld.dll`，Debug 配置编出来连不上 MySQL。
> 客户端不受影响（32 位套件两个驱动都有，客户端一直用 Debug）。

## 运行

```bat
scripts\start-all.bat     :: 服务端 → 机器人 → 客户端，按顺序拉起
scripts\stop-all.bat      :: 全部关掉
```

手动启动顺序必须是 **服务端 → 机器人/客户端**。

### 配置

- `QtQQ_Bot/build-x64/release/bot.ini`：大模型 key、模型名、唤醒词、监听群。
  ```ini
  [llm]
  api_key=...
  model=glm-4-flash
  [bot]
  id=10010
  trigger=@ai
  [net]
  group_ids=2000          ; 多个群用逗号：2000,2001
  tcp_port=57777
  [db]
  host=127.0.0.1
  user=root
  password=...
  name=qt_qq_main
  ```
- 客户端读 `client.ini`（exe 同目录，首次运行自动生成）：服务端地址 + MySQL 参数
- 服务端读 `server.ini`（同上）：监听端口 + MySQL 参数
- 机器人读 `bot.ini`（同上）：大模型 key、群列表、MySQL 参数

三个 ini 都是首次运行自动生成的，换机器只改 ini，不用重新编译。

### 数据库

```bat
mysql -uroot -p < QtQQ_Server\sql\qt_qq_main.sql
```

种子账号：`10001/renyi`、`10002/rener`…（密码见 `tab_accounts.code`），
`10010/ai123456` 是 AI 助手在 IM 里的账号。

## 协议一句话

每条消息是一行 JSON，字段：

```json
{"what":"msg","send":"10001","recv":"2000","type":0,"msg":[[{"txt":"内容"}]]}
```

`type` 0=群聊 1=单聊；`what` 另外还有 `file`（文件）和 `msg_stream`（AI 流式增量）。

连接建立后客户端/机器人会先发一条握手：

```json
{"what":"hello","id":"10010","role":"bot"}
```

服务端用它建立「连接 → 账号」映射：群聊广播、私聊只投递给 `recv` 那条连接
（收件人不在线时**不投递也不广播**，等他上线再补发）。`role:"bot"` 的机器人
不参与"离线补发"，否则每次重连都会被重新投喂一堆旧问题。

客户端打开聊天窗时会请求历史：

```json
{"what":"history","with":"2000","limit":50}
```

服务端回 `{what:"history", with:"2000", items:[{send,text}...]}`，
窗口按时间顺序铺开，不会重复发送。私聊只查双方之间的记录。

## 消息落库

服务端每转发一条消息就写一行 `tab_messages`（`sql/qt_qq_main.sql` 里有建表语句）：

| 列 | 说明 |
|---|---|
| `type` | 0=群聊 1=私聊 |
| `recv` | 群号（群聊）或收件人账号（私聊） |
| `text` | 纯文本；文件只记 `[文件] 文件名`，不存内容 |
| `delivered` | 私聊专用：0=收件人还没收到，登录时由服务端补发后置 1 |

流式回答的正文散在增量里，机器人在 `done` 包里额外带 `full` 全文，
服务端据此落一条完整记录（没有 `full` 就退回本次 `delta`）。

## 一键回归

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\e2e.ps1
```

覆盖：服务端转发、机器人应答、两个账号互发、AI 私聊、消息落库、
离线私聊补发、服务端重启后客户端自动重连。输出逐项 PASS/FAIL，
退出码 0=全过。

## 测试/调试钩子

| 程序 | 参数 | 用途 |
|---|---|---|
| QtQQ_Bot | `--selftest` | 分层自检：DB → 工具表 → LLM 非流式 → 流式，跑完退出 |
| QtQQ_Bot | `--echo` | 协议回环：群里发什么就回 `[echo] ...`，不接大模型 |
| QT_QQ | `--autotest --account X --password Y [--talk 10010] [--msg 你好]` | 自动登录、打开指定聊天窗、发一条消息 |

## 已知边界

- 服务端单线程、不校验报文（`send` 是客户端自己填的，可冒充）；面向局域网十几人规模。
- 消息落库但**不清理**，长期运行 `tab_messages` 会一直变大。
- 机器人只在配置的群里应答；私聊总是应答。
- 会话上下文只记「问过什么」，不记答案（避免模型复述旧名单），最多 6 个问题。
- 文件传输整文件塞进一条 JSON（base64），上限 5MB，分块没做。
