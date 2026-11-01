#include "stdafx.h"
#include "RecvFile.h"
#include <qfiledialog.h>
#include <qfile.h>
#include <qtextstream.h>
#include <qmessagebox.h>

/**
 * @brief 构造函数：拿着对端发来的文件 JSON 开"接收文件"确认框。
 *
 * data 就是信号链里透传过来的 fileObj，本窗口只负责问用户"存不存、存哪"。
 * setAttribute(Qt::WA_DeleteOnClose)：一次性确认框，点确定/取消/关闭都由
 * Qt 自动销毁，调用方不用管释放。
 * @param data 对端发来的文件 JSON（文件名/长度/base64 内容）
 * @param parent 父控件，默认 Q_NULLPTR
 */
RecvFile::RecvFile(const QJsonObject& data, QWidget *parent)
	: BasicWindow(parent), fileObj(data)   // 初始化列表：转交基类 + 保存文件 JSON
{
	ui.setupUi(this);                      // 生成 ui 控件并绑定到本窗
	setAttribute(Qt::WA_DeleteOnClose);    // 关闭即销毁，调用方不用管释放
	init();                                // 标题栏 + 样式 + 摆位
}

/**
 * @brief 析构：无手动资源，控件随对象树释放。
 */
RecvFile::~RecvFile()
{
}

/**
 * @brief 一行 setter：把提示文案（如"对方发来文件 xxx"）刷到提示标签上。
 * @param msg 要显示的提示文案
 */
void RecvFile::setLabelMsg(const QString & msg)
{
	ui.label->setText(msg);                // 更新提示标签文本
}

/**
 * @brief 初始化：标题栏 + 样式表 + 摆个和发送窗错开的位置。
 *
 * 位置固定 (200, 400)，保证与发送窗同时弹出时不重叠。
 */
void RecvFile::init()
{
	initTitleBar(CLOSE_BUTTON, ":/Resources/MainWindow/qqlogoclassic.png");   // 关闭按钮 + logo
	loadStyleSheet("ReceiveFile");          // 加载 ReceiveFile.qss 样式
	this->move(200, 400);                   // 错开发送窗位置，避免两弹窗重叠
}

/**
 * @brief 校验 base64 → 让用户选目录 → 落盘；任一失败统一提示并关窗。
 *
 * 与发送端（SendFile::on_sendBtn_clicked）的协议契约：
 *   FILE_NAME / FILE_LENGTH / FILE_CONTENT 三字段，内容是 base64 文本。
 * 校验思路：先把 base64 解回原始字节，再比对长度——长度对不上说明传输/编码
 *   出了问题，宁可报"接受失败"也不落一个残缺文件。
 * 所有失败分支（数据空、长度不符、用户取消选目录、打开写失败）最后都统一
 *   走到函数末尾的"接受失败"提示并关窗，保证弹窗不会挂着没人管。
 */
void RecvFile::on_okBtn_clicked()
{
	if (!fileObj.isEmpty())                // JSON 非空才继续处理
	{
		QString fileName = fileObj[FILE_NAME].toString();          // 文件名称
		QString fileLength = fileObj[FILE_LENGTH].toString();      // 文件长度
		QString fileContent = fileObj[FILE_CONTENT].toString();    // 文件内容（base64）

		// 发送端用 base64 编码，这里解回原始字节
		const QByteArray raw = QByteArray::fromBase64(fileContent.toLatin1());

		// 解出来的字节数必须等于发送端声称的文件长度，否则视为数据损坏
		if (fileLength.toInt() == raw.size())   // 文件长度和内容长度相等
		{
			//获取文件保存路径
			QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择文件保存路径"), "/");

			if (!dir.isEmpty())                // 用户选了目录才继续
			{
				QString filePath = dir + "/" + fileName;   // 拼出完整落盘路径

				QFile file(filePath);                      // 准备写文件
				// 二进制直接 WriteOnly：走 QTextStream 会做文本换行转换，图片必坏
				if (file.open(QFile::WriteOnly))           // 打开失败则落到函数末尾的失败提示
				{
					file.write(raw);                       // 写入全部解码后的字节

					file.close();                          // 及时关闭文件句柄
					QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("接受文件 %1 成功.").arg(fileName));   // 成功提示
					this->close();                         // 关闭弹窗（触发销毁）
					return;                                // 成功路径到此结束
				}
			}
		}
	}

	QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("接受文件失败."));   // 所有失败分支统一提示
	this->close();                         // 关闭弹窗（触发销毁）
}

/**
 * @brief 取消接收：直接关窗（WA_DeleteOnClose 负责销毁），什么都不落盘。
 */
void RecvFile::on_cancelBtn_clicked()
{
	this->close();                         // 关闭即销毁，无任何落盘动作
}
