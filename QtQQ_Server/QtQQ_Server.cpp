#include "QtQQ_Server.h"
#include "UseMySQL.h"           // 数据库单例（增删查员工）
#include "AppConfig.h"          // 读 server.ini 的端口/库配置
#include <QFileDialog>          // 头像选择对话框
#include <QMessageBox>          // 提示框

/**
 * @brief 构造：装 UI → 去掉 QDialog 默认的"?"帮助按钮 → init() 完成全部装配
 *
 * 实现细节：model 先置空，init() 里第一次刷新才 new。
 * @param parent 父窗口指针，缺省 nullptr
 */
QtQQ_Server::QtQQ_Server(QWidget *parent)
    : QDialog(parent), model(nullptr)     // 先把 model 置空，避免未初始化悬空指针
{
    ui.setupUi(this);                     // uic 生成：创建控件、布局、connectSlotsByName

    // 只保留最小化/最大化/关闭按钮（QDialog 默认还有"?"帮助按钮，去掉）
    setWindowFlags(Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
    init();                               // 启动装配：TCP 监听 → 数据库单例 → 初始表格
}

/**
 * @brief 析构：无手动释放（tcpServer/model 挂在 this 上，随窗口一起销毁）
 */
QtQQ_Server::~QtQQ_Server()
{
}

/**
 * @brief 启动装配：先起 TCP 监听，再懒加载数据库单例，最后刷一次初始表格
 *
 * 顺序有意为之：先起监听再连库——数据库暂时不可用时窗口照常出来、
 * 端口照常监听，排错看 server_debug.log，不把整个程序卡死在启动阶段。
 * instance() 是懒加载：这一行才真正发起 MySQL 连接，连接失败只打日志
 * （connectMySQL 的返回值这里不检查），员工查询会整体失败但投递不受影响。
 */
void QtQQ_Server::init()
{
    initTcpServer();                                              // 先起监听：端口可用不依赖数据库
    UseMySQL::instance();    // 触发单例构造：第一次调用时才连数据库
    updateTableData();       // 启动即显示公司群成员
}

/**
 * @brief 创建 TcpServer 并开始监听（端口来自 server.ini 的 AppConfig）
 *
 * 监听与投递都在 TcpServer 里完成：它自己维护"连接 → 账号"的登记表，
 * 群聊广播、私聊点对点，主窗口不再插手转发。
 */
void QtQQ_Server::initTcpServer()
{
    // 监听与投递都在 TcpServer 里完成：它自己维护"连接 → 账号"的登记表，
    // 群聊广播、私聊点对点，主窗口不再插手转发
    tcpServer = new TcpServer(AppConfig::instance().tcpPort, this); // 端口从 server.ini 读，挂 this 自动释放
    tcpServer->run();        // 开始监听（AnyIPv4，失败只打日志不弹窗）
}

/**
 * @brief 刷新员工表格：按 departmentID / employeeID 三分支查询后重填 tableView
 *
 * 实现细节：三分支走完得到 fields/info 后，deleteLater 旧 model、新建 model、
 * 逐格填 QStandardItem；departmentID 列翻译成部门名，status 列 1→有效 / 0→注销。
 * @param departmentID 部门ID，传空时不按部门过滤（显示公司群）
 * @param employeeID 员工ID，传空时不按员工过滤
 */
void QtQQ_Server::updateTableData(QString departmentID, QString employeeID)
{
    QString companyID = UseMySQL::instance()->getDepartmentID(QStringLiteral("公司群"));   // 获取公司群ID

    QStringList fields;                                           // SELECT 结果的字段名（列序 = 表头序）
    QVector<QVector<QString>> info;                               // [行][列] 的查询结果
    if (!departmentID.isEmpty() && departmentID != companyID)       // 部门ID非空且不是公司群
    {
        UseMySQL::instance()->getDepartmentInfo(departmentID, fields, info); // 查该部门全部成员
    }
    else if (!employeeID.isEmpty())        // 员工ID非空
    {
        info.resize(1);                    // 预留一行，getEmployeeInfo 往 info[0] 里填
        UseMySQL::instance()->getEmployeeInfo(employeeID, fields, info[0]); // 查单个员工
    }
    else                                   // 默认显示公司群成员
    {
        UseMySQL::instance()->getDepartmentInfo(companyID, fields, info); // 公司群 = 全公司
    }

    if (model)
    {
        model->deleteLater();    // 旧模型挂在自己身上，回到事件循环后安全释放
        model = nullptr;         // 置空避免野指针，下一行马上重新 new
    }

    int rows = info.size();                                        // 行数：可能为 0（查无结果）
    int columns = rows > 0 ? info[0].size() : 0;    // 查无结果时给空表格，不能 info[0]（原版这里会崩）

    model = new QStandardItemModel(rows, columns, this);           // 新建模型，挂 this
    model->setHorizontalHeaderLabels(getHeaderLabelsText());        // 设置表头
    ui.tableView->verticalHeader()->setVisible(false);              // 隐藏左侧垂直表头（行号）

    // 每个单元格一个 QStandardItem 塞进模型；两列需要"翻译"：
    //   departmentID 列显示部门名，status 列 1->有效 / 0->注销
    for (int i = 0; i < rows; i++)                                 // 逐行
    {
        for (int j = 0; j < columns; j++)                          // 逐列
        {
            QString str = info[i][j];                              // 取原始单元格文本
            if (fields[j] == "departmentID")                       // 部门ID列要翻译成部门名
            {
                model->setItem(i, j, new QStandardItem(UseMySQL::instance()->getDepartmentName(str))); // 2001 → "人事部"
                continue;                                          // 本格处理完，跳到下一列
            }
            else if (fields[j] == "status")                        // 状态列要翻译成中文
            {
                model->setItem(i, j,
                    new QStandardItem(str.toInt() ? QStringLiteral("有效") : QStringLiteral("注销"))); // 1→有效 0→注销
                continue;                                          // 本格处理完，跳到下一列
            }
            model->setItem(i, j, new QStandardItem(str));          // 其余列原样显示
        }
    }

    ui.tableView->setModel(model);                                 // 把新模型挂到表格视图
}

/**
 * @brief 返回表头文案列表（顺序必须等于 tab_employees 列序）
 *
 * 实现细节：与 SELECT * 的返回列一一对应，顺序错一位整表就对不上号。
 * @return QStringList 七列中文表头
 */
QStringList QtQQ_Server::getHeaderLabelsText()
{
    // 顺序必须和 tab_employees 表的列顺序一致（SELECT * 按列序返回）：
    // id / departmentID / employeeID / employeeName / employee_sign / picture / status
    QStringList list;                                              // 空列表，下面按序拼
    list << QStringLiteral("序号") << QStringLiteral("部门") << QStringLiteral("账号")
        << QStringLiteral("员工姓名") << QStringLiteral("员工签名") << QStringLiteral("员工头像")
        << QStringLiteral("员工状态");                             // 七列，顺序不可乱

    return list;                                                   // 交给调用方 setHorizontalHeaderLabels
}

/**
 * @brief [槽] 选择员工头像：只记路径不复制文件，取消则不覆盖上一次选择
 *
 * 实现细节：只记路径不复制图片文件——入库存的就是这个路径字符串，客户端按同一路径
 * 读图，部署换机器时图片目录要跟着走（库里现存数据全是正斜杠绝对路径）。
 * 取消对话框（路径为空）直接返回，不覆盖上一次已选的 picturePath。
 */
void QtQQ_Server::on_selectPictureBtn_clicked()
{
    picturePath = QFileDialog::getOpenFileName(this, QStringLiteral("选择头像"), "/", QStringLiteral("*.png;;*.jpg")); // 只允许 png/jpg

    if (picturePath.isEmpty()) return;                             // 用户点了取消：保留上次路径

    QPixmap pixmap(picturePath);                                   // 把图片加载成 QPixmap

    /*
        Qt::KeepAspectRatio 缩放时保持纵横比，省略则强行拉满
        Qt::SmoothTransformation 平滑变换，缩放后的图像边缘更平滑
        headLabel 在 .ui 里设了 scaledContents=true，塞进去就会自动缩放到 100x100
    */
    ui.headLabel->setPixmap(pixmap);                               // 塞进 headLabel 预览（自动缩放到 100×100）
}

/**
 * @brief [槽] 添加员工：校验 → 入库 → 复位输入区 → 刷新表格并定位新行
 *
 * 校验按"姓名 → 照片 → 部门 → 入库"顺序短路，哪步失败停在哪步并只弹提示；
 * 失败路径一律提前 return，输入区保持原样，用户改完可以继续提交。
 * 全部成功才复位表单，并用 updateTableData(空, 新工号) 刷新——
 * 表格直接定位到刚添加的那一行，操作者能立刻确认结果。
 */
void QtQQ_Server::on_addEmployeeBtn_clicked()
{
    // 校验按"姓名 -> 照片 -> 部门 -> 入库"顺序，哪步失败提示哪步
    QString employeeName = ui.employeeNameEdit->text();            // 取姓名输入框文本
    if (employeeName.isEmpty())                                    // 姓名为空：第一道校验
    {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请输入员工姓名！")); // 明确告诉用户缺什么
        ui.employeeNameEdit->setFocus();                           // 焦点拉回姓名框，方便直接改
        return;                                                     // 短路：后面步骤都不做
    }

    if (picturePath.isEmpty())                                     // 没选照片：第二道校验
    {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请选择员工照片！")); // 提示选照片
        return;                                                     // 短路：不入库
    }

    QString departmentName = ui.employeeDepBox->currentText();      // 取下拉框当前选中的部门名
    QString departmentID = UseMySQL::instance()->getDepartmentID(departmentName); // 部门名 → 部门ID

    if (departmentID.isEmpty())                                    // 部门查不到：下拉框数据可能被改过
    {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("[departmentID Error]获取部门失败！")); // 带错误码方便排查
        return;                                                     // 短路：不入库
    }

    // picturePath 用的是正斜杠且走占位符传参：不做转义、原样入库，
    // 与库里现有数据（C:/c++/qt/QtQQ/QT_QQ/...）格式一致，Qt 客户端直接能读
    QString employeeID = UseMySQL::instance()->addEmployee(departmentID, employeeName, picturePath); // 入库，返回新工号
    if (employeeID.isEmpty())                                      // addEmployee 内部任一步失败
    {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("[employeeID Error]添加员工失败！")); // 失败提示
        return;                                                     // 短路：不复位表单
    }

    QMessageBox::information(this, QStringLiteral("提示"),
        QStringLiteral("添加员工 %1 成功！").arg(employeeName));   // 成功提示带姓名，更友好

    // 复位输入区，再刷新表格定位到刚添加的员工
    picturePath.clear();                                           // 清掉已用掉的头像路径
    ui.employeeDepBox->setCurrentIndex(0);                         // 部门下拉回到第 0 项
    ui.employeeNameEdit->clear();                                  // 清空姓名框
    ui.headLabel->setText(QStringLiteral("员工寸照"));             // 头像预览恢复占位文字

    updateTableData(QString(), employeeID);                        // 刷新并定位到刚添加的那一行
}

/**
 * @brief [槽] 按下拉框选中的部门查成员（先清员工号输入框，避免两条件同时生效）
 *
 * 部门ID查不到时 updateTableData 走默认分支，回落显示公司群成员。
 */
void QtQQ_Server::on_queryDepBtn_clicked()
{
    ui.queryIDEdit->clear();                                       // 清掉员工号，让当前生效条件只有一个
    QString departmentName = ui.queryDepBox->currentText();        // 取下拉框选中的部门名
    QString departmentID = UseMySQL::instance()->getDepartmentID(departmentName); // 部门名 → 部门ID
    updateTableData(departmentID);      // 若departmentID为空，则默认查询公司群成员
}

/**
 * @brief [槽] 按工号查单个员工（部门下拉复位只是 UI 提示，查询仍只传 employeeID）
 *
 * 工号为空只提示不刷新：空串传给 updateTableData 等于"不过滤"，
 * 会把全公司列出来，用户却以为查的是"什么都没有"。
 */
void QtQQ_Server::on_queryIDBtn_clicked()
{
    ui.queryDepBox->setCurrentIndex(0);                            // 部门下拉复位（仅 UI 提示，不参与查询）

    QString employeeID = ui.queryIDEdit->text();                   // 取工号输入框文本
    if (employeeID.isEmpty())                                      // 工号为空：不刷新，只提示
    {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请输入员工号！")); // 明确缺输入
        ui.queryIDEdit->setFocus();                                // 焦点拉回工号框
        return;                                                     // 短路：不触发默认"全公司"刷新
    }

    updateTableData(QString(), employeeID);                        // 只按工号查，部门条件不传
}

/**
 * @brief [槽] 注销员工：status 置 0 的逻辑删除，不物理删行
 *
 * 入口先复位两个查询输入框，避免注销后表格上还挂着上一次的过滤条件。
 * 成功后仍按"单员工"刷新：该行不消失，只有状态列变成"注销"，
 * 操作者能就地确认结果；返回 false 只可能是工号不存在（见 deleteEmployee）。
 */
void QtQQ_Server::on_deleteIDBtn_clicked()
{
    ui.queryIDEdit->clear();                                       // 复位员工号查询框
    ui.queryDepBox->setCurrentIndex(0);                            // 复位部门下拉

    QString employeeID = ui.deleteIDEdit->text();                  // 取要注销的工号
    if (employeeID.isEmpty())                                      // 工号为空：只提示不操作
    {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请输入员工号！")); // 明确缺输入
        ui.deleteIDEdit->setFocus();                               // 焦点拉回删除框
        return;                                                     // 短路：不发 SQL
    }

    // 返回 false 只有"工号不存在"一种可能（存在则必然 UPDATE 成功）
    if (!UseMySQL::instance()->deleteEmployee(employeeID))         // 执行注销（UPDATE status=0）
    {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("查无此人，请确认员工号！")); // 唯一失败原因提示
        ui.deleteIDEdit->setFocus();                               // 焦点留在框里方便改工号
        return;                                                     // 短路：不刷新
    }

    QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("员工 %1 注销成功！").arg(employeeID)); // 成功提示
    updateTableData(QString(), employeeID);    // 刷新后该行状态列变为"注销"
    ui.deleteIDEdit->clear();                                     // 清掉已用掉的工号
}
