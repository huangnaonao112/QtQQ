#include "stdafx.h"
#include "SendFile.h"
#include <qfiledialog.h>
#include <qfile.h>
#include <qmessagebox.h>
#include <qjsonobject.h>
#include <qfileinfo.h>
#include "WindowManager.h"

/**
 * @brief 构造函数：搭出"发送文件"对话框。
 *
 * WA_DeleteOnClose：这窗口是一次性的，点发送/关闭后由 Qt 直接 delete，
 *   调用方（主界面工具栏按钮）不必关心它的生命周期，也绝不能在 close 后
 *   再碰它的指针——之前的用法都是 show 完就不管。
 * @param parent 父控件；传当前聊天窗仅用于定位，窗口实际是独立弹窗
 */
SendFile::SendFile(QWidget *parent)
	: BasicWindow(parent)                // 转交父类构造
{
	ui.setupUi(this);                    // 生成 ui 控件并绑定到本窗

	setAttribute(Qt::WA_DeleteOnClose);   // 关闭即销毁，避免残留悬空窗口
	init();                              // 标题栏/样式 + 信号连接
}

/**
 * @brief 析构：窗口一次性使用，无资源需手动释放。
 */
SendFile::~SendFile()
{
}

/**
 * @brief 初始化标题栏/样式，并把"发送文件"信号接到聊天主框架。
 *
 * 协议契约：signalSendFile 只发一个 QJsonValue（文件名+长度+base64 内容），
 * 对端是 TalkWindowShell::sendMsgToServer，由它包成完整消息发给服务端。
 * 坑：SendFile 是独立弹窗，自己不知道当前在跟谁聊，所以必须走
 *   不带 talkId 的重载，由 shell 拿"当前显示的聊天窗"兜底。
 */
void SendFile::init()
{
	initTitleBar(CLOSE_BUTTON, ":/Resources/MainWindow/qqlogoclassic.png");   // 关闭按钮 + logo
	loadStyleSheet("SendFile");          // 加载 SendFile.qss 样式
	this->move(50, 400);                 // 摆在屏幕左侧，和接收窗(200,400)错开

	// SendFile 属于"不知道自己属于哪个会话"的调用方：接不带 talkId 的重载，
	// 由 shell 按当前显示的聊天窗兜底（见 TalkWindowShell::sendMsgToServer 的两个重载）
	connect(this, SIGNAL(signalSendFile(QJsonValue)), WindowManager::instance()->getTalkWindowShell(), SLOT(sendMsgToServer(QJsonValue)));
}

/**
 * @brief 选择要发送的文件：结果暂存 filePath 并回显到输入框（此时还没读文件）。
 */
void SendFile::on_openBtn_clicked()
{
	filePath = QFileDialog::getOpenFileName(this, QStringLiteral("选择文件"), "/", QStringLiteral("所有文件(*.*)"));   // 弹原生选文件框，取消则空串
	ui.lineEdit->setText(filePath);      // 把选中路径回显到输入框
}

/**
 * @brief 读整个文件 → 打成 JSON → 发给 shell → 关窗。
 *
 * 关键约束：
 *   1) 整个文件读进内存、塞进一条消息（协议与 RecvFile 约定：文件名/长度/
 *      base64 内容三字段），所以必须卡 5MB 上限，超了直接弹窗拒绝；
 *   2) 内容必须 base64 后再放进 QJsonValue——原始字节直接转 QString 会按
 *      UTF-8 解码，图片/压缩包会被截成乱码，对端解出来就是坏文件；
 *   3) 打不开文件（路径失效/没权限）就清掉 filePath 让用户重选，不发半条消息。
 * 发完即 close：WA_DeleteOnClose 会把窗口销毁，一步到位。
 */
void SendFile::on_sendBtn_clicked()
{
	if (filePath.isEmpty()) return;   // 没选文件，什么都不做（此时 filePath 为""）

	QFile file(filePath);
	if (!file.open(QFile::ReadOnly))
	{
		// 打开失败多半是文件被删/被占用：提示后清空选择，避免下次再点发送
		// 用同一个失效路径去 open 又失败一遍
		QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("打开文件 %1 失败.").arg(filePath));   // 通知用户打开失败
		ui.lineEdit->clear();          // 清掉失效路径的回显
		filePath = "";                 // 清空暂存路径，强制下次重新选
		return;                        // 不发半条消息
	}

	QJsonObject fileObj; 
	QString fileName = QFileInfo(filePath).fileName();           // 文件名称（只要文件名不要路径）
	fileObj.insert(FILE_NAME, fileName);   // 塞入文件名字段

	QByteArray fileContent = file.readAll();   // 整个文件读进内存

	// 单个文件直接塞进一条消息，太大就会把 TCP/JSON 撑爆；
	// 这里给个 5MB 的上限，够发文档/图片，暂时不支持大文件
	if (fileContent.size() > 5 * 1024 * 1024)
	{
		QMessageBox::information(this, QStringLiteral("提示"),
			QStringLiteral("文件太大（超过 5MB），暂时不支持。"));   // 超限弹窗拒绝
		file.close();                  // 关闭文件句柄
		return;                        // 不发送超限文件
	}

	fileObj.insert(FILE_LENGTH, QString::number(fileContent.size()));          // 文件长度（字节）
	// 必须 base64：QString(QByteArray) 会按 UTF-8 解码，图片/压缩包里的
	// 非文本字节会被替换成 U+FFFD，收到的文件就损坏了
	fileObj.insert(FILE_CONTENT, QString::fromLatin1(fileContent.toBase64()));  // 文件内容（base64）

	emit signalSendFile(fileObj);      // 把文件 JSON 发给 shell，由它发到服务端
	file.close();                      // 关闭文件句柄
	this->close();                     // 关窗（WA_DeleteOnClose 触发销毁）
}
