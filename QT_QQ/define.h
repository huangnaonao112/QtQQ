#pragma once

// ============================================================================
// 宏：QRC_QSS(x)
// 用途：将样式表文件名转换为 Qt 资源系统 (QRC) 中的完整路径
// 参数 x  ：QSS 文件名（不含后缀），如 "Title"、"UserLogin"
// 展开示例：QRC_QSS("Title") → ":/Resources/QSS/Title.css"
// 为什么用宏而不是运行时拼接：QRC 路径前缀 ":/Resources/QSS/" 是固定的，
// 用宏可以统一管理，如果资源目录结构变动，只需改这一处。
// ============================================================================
#define QRC_QSS(x) ":/Resources/QSS/" + x + ".css"   // QSS 文件名 → ":/Resources/QSS/<x>.css"

// 宏：INI_PATH(f)
// 用途：把配置文件名拼到 exe 所在目录的 ini/ 子目录下
// 参数 f  ：ini 文件名，如 "color.ini"、"uer.ini"
// 展开示例：INI_PATH("color.ini") → "<exe目录>/ini/color.ini"
#define INI_PATH(f) QApplication::applicationDirPath()+"/ini/" + f   // 配置文件路径 → "<exe目录>/ini/<f>"

// 宏：APP_ICON(i)
// 用途：拼应用小图标的 QRC 路径
// 参数 i  ：图标名，如 "app_7"、"app_skin"
// 展开示例：APP_ICON("app_7") → ":/Resources/MainWindow/app/app_7.png"
#define APP_ICON(i) ":/Resources/MainWindow/app/" + i + ".png"   // 图标名 → ":/Resources/MainWindow/app/<i>.png"

// ============================================================================
// 枚举：ButtonType — 控制标题栏上系统按钮的显示组合
// 为什么用枚举而非 bool 标志：按钮组合是互斥的几种固定模式，
// 枚举比多个 bool 更清晰，且 switch 语句会强制处理所有分支。
//
// MIN_CLOSE_BUTTON (0)：最小化 + 关闭 → 适用登录窗口这类不需要最大化的对话框
// MIN_MAX_CLOSE_BUTTON    ：最小化 + 最大化 + 关闭 → 适用主窗口，支持最大化切换
// CLOSE_BUTTON            ：仅关闭 → 适用纯提示类弹窗
//
// 为什么 CLOSE_BUTTON 和 MIN_CLOSE_BUTTON 都隐藏 buttonMax：
//   这两种模式本身不支持窗口最大化，隐藏 max 按钮是视觉上的约定，与窗口状态无关。
//   因此移动判断时需要区分「不支持最大化」和「已最大化」，详见 TitleBar::mousePressEvent。
// ============================================================================


#define GROUP_ID_LENGTH 4   // 群 id（部门 4 位，如 2001）与员工 id（10001 起，5 位以上）的长度分界

enum ButtonType
{
	MIN_CLOSE_BUTTON = 0,        // 最小化 + 关闭（登录窗用）
	MIN_MAX_CLOSE_BUTTON,        // 最小化 + 最大化 + 关闭（主窗用）
	CLOSE_BUTTON,                // 只关闭（提示弹窗用）
};

// ============================================================================
// 聊天消息相关宏（MsgPage / MsgTextEdit / MsgWebView 使用）
// 这些宏是"内容模板"而不是数值：把 HTML 片段集中到这里，
// cpp 里拼消息时不会散落一堆魔法字符串。
// ============================================================================
//表情图片的 QRC 目录前缀（emotion/0.png ~ 170.png 共 171 张；表情窗按 12×14 摆 168 格）
#define IMG_QRC ":/Resources/MainWindow/emotion/"   // 表情图目录前缀
#define IMG_PNG ".png"                              // 表情图后缀
//往网页里插入一张表情图：IMG_SRC 作为 %1 模板被 .arg() 填入图片路径
#define IMG_SRC "<img src=\"%1\">"                  // HTML 模板：一张表情图
//消息换行
#define BR "<br>"                                   // HTML 换行标签
//聊天表情/消息页面的 QRC 路径：HTML_PATH("userLeft") → :/Resources/.../userLeft.html
#define HTML_PATH(x) QString(":/Resources/MainWindow/MsgHtml/") + x + ".html"   // HTML 模板 QRC 路径
//消息归属标记：appendMsg 的 obj 参数等于 MYSELF 时是"自己发的"（右对齐显示并转发给服务器）
#define MYSELF "myself"                             // 消息归属标记：自己发的
//消息 JSON 片段的 key：一段消息由若干块组成，每块是文本或表情图
#define TXT "txt"                                   // JSON key：文本片段
#define IMG "img"                                   // JSON key：图片/表情片段

// ----------------------------------------------------------------------------
// 聊天窗口/消息协议宏（TalkWindowShell / WindowManager / SendFile / RecvFile）
// ----------------------------------------------------------------------------
#define WHAT            "what"      //JSON key：消息种类
#define WHAT_MSG        "msg"       //种类值：普通消息
#define WHAT_FILE       "file"      //种类值：文件传输
#define SEND            "send"      //JSON key：发送者
#define RECV            "recv"      //JSON key：接收者
#define TYPE            "type"      //JSON key：聊天类型
#define TYPE_GROUP      0           //类型值：群聊
#define TYPE_SINGLE     1           //类型值：单聊
//群 id（部门 4 位，如 2001）与员工 id（10001 起，5 位以上）的长度分界：
//≤4 位按群聊处理，≥5 位按单聊处理
#define GROUP_ID_LENGTH 4                              // 再次定义，值与上面一致（保持兼容，勿删）
#define MSG_LENGTH      255         //单条聊天消息最大长度
#define FILE_NAME       "name"      //文件传输 JSON key
#define FILE_LENGTH     "length"    //文件传输 JSON key：文件长度
#define FILE_CONTENT    "content"   //文件传输 JSON key：文件内容
//表情路径：IMG_PATH("3") → :/Resources/MainWindow/emotion/3.png
#define IMG_PATH(x)     QString(":/Resources/MainWindow/emotion/") + x + ".png"   // 表情图完整 QRC 路径
