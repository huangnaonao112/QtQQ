#pragma once

#include <QObject>
#include <qsqldatabase.h>
#include "CCMainWindow.h"

// ============================================================================
// 类：UseMySQL — 数据访问层（DAL），全项目唯一与 MySQL 打交道的地方
//
// 在整体框架中的位置（三层结构）：
//
//     界面层 UserLogin / CCMainWindow ...   ← 只管控件、样式、交互
//                │  只调用 instance()->verifyLogin(...) 这类接口
//                ▼
//     数据访问层 UseMySQL（本类）            ← 封装连接、SQL 语句
//                │  QSqlDatabase / QSqlQuery
//                ▼
//     MySQL 服务器 qt_qq_main 库            ← tab_accounts 等表
//
// 为什么要把数据库代码单独抽成一层：
//   (1) 界面层不出现任何 SQL，将来换数据库（MySQL→SQLite）或改表结构时，
//       只改本类即可，界面代码一行不动；
//   (2) 数据库连接必须"全项目只有一份"（见下方单例说明），集中管理最可控；
//   (3) 登录、改密、查好友等验证逻辑写在本类的成员函数里，
//       多个界面（登录窗、注册窗、主界面）都能复用同一份代码。
//
// 为什么是单例：
//   Qt 里每个 QSqlDatabase 连接都要建立真实的 TCP 连接，开销大；
//   而且 addDatabase 用默认连接名重复创建会打印
//   "QSqlDatabasePrivate::addDatabase: duplicate connection name" 警告并
//   覆盖旧连接。所以全项目只允许存在一个 UseMySQL 实例、一个连接，
//   谁要用数据库都通过 UseMySQL::instance() 拿到同一个对象。
// ============================================================================

/**
 * @class UseMySQL
 * @brief 数据访问层（DAL）：全项目唯一与 MySQL 打交道的地方，单例 + 全项目唯一连接。
 *
 * 界面层不写 SQL，只调 instance()->xxx 接口；Q_GLOBAL_STATIC 实现懒加载
 * （第一次 instance() 才建对象连库）、线程安全、进程退出自动析构。
 * SQL 一律 prepare + bindValue 防注入；密码练习项目明文，真实项目须哈希。
 */
class UseMySQL  : public QObject
{
	Q_OBJECT

public:
	/**
	 * @brief 构造函数：连接动作放构造里，保证第一次 instance() 返回时连接已可用。
	 * @param parent 父 QObject，Qt 对象树自动析构用，传 nullptr 即可
	 */
	UseMySQL(QObject* parent = nullptr);

	/**
	 * @brief 析构函数：数据库连接 base 是轻引用，进程退出时由 Qt 自动释放，无需手动 close。
	 */
	~UseMySQL();

	/**
	 * @brief 全局访问点：任何界面需要操作数据库时调用 UseMySQL::instance()。
	 *
	 * 第一次调用时才会真正创建对象并连数据库（懒加载，见 .cpp 中 Q_GLOBAL_STATIC）。
	 * @return 唯一的 UseMySQL* 指针，全程同一个对象
	 */
	static UseMySQL* instance();

public:
	/**
	 * @brief 登录验证：账号密码与 tab_accounts 表比对。
	 *
	 * 用 prepare + addBindValue 防 SQL 注入；next() 无数据即账号不存在，
	 * 与密码错误统一返回 false，不向界面层暴露区别。
	 * 返回 bool 而非抛异常/弹窗：本层只负责"判断"，弹提示是界面层的职责。
	 * @param account 用户输入的账号（员工工号字符串，如 "1001"）
	 * @param password 用户输入的密码明文（练习项目直存库，真实项目要哈希）
	 * @return true=登录成功；false=账号不存在或密码错误
	 */
	bool verifyLogin(const QString& account, const QString& password);

	/**
	 * @brief 按账号查用户姓名（登录成功后显示在主界面 nameLabel）。
	 * @param account 登录账号（工号）
	 * @return tab_employees.employeeName；查不到兜底返回 account 本身（防姓名空白）
	 */
	QString getUserName(const QString& account);

	/**
	 * @brief 按账号查用户头像图片路径（已解析成本机可用路径）。
	 * @param account 登录账号（工号）
	 * @return 图片路径字符串；查不到返回空串
	 */
	QString getUserHeadPixmap(const QString& account);

	/**
	 * @brief 查当前账号所在的所有群 id（公司群 + 部门群）。
	 * @param account 登录账号（工号）
	 * @return QStringList，形如 [公司群id, 部门群id]，传给 CCMainWindow::addUserGroup 用
	 */
	QStringList getUserGroup(const QString& account);

	/**
	 * @brief 按群 id 查群头像图片路径。
	 * @param id 群号（departmentID）
	 * @return tab_department.picture 列的图片路径；查不到返回空串
	 */
	QString getGroupHeadPixmap(const QString& id);

	/**
	 * @brief 按群 id 查群名称。
	 * @param id 群号（departmentID）
	 * @return tab_department.departmentName；查不到返回空串
	 */
	QString getGroupName(const QString& id);

	/**
	 * @brief 按账号查部门名（JOIN tab_department，聊天窗标题"部门+姓名"用）。
	 * @param account 登录账号（工号）
	 * @return 部门名；查不到返回空串
	 */
	QString getUserDepartment(const QString& account);

	/**
	 * @brief 按账号查员工个性签名（tab_employees.employee_sign）。
	 * @param account 登录账号（工号）
	 * @return 签名字符串；查不到返回空串
	 */
	QString getUserSign(const QString& account);

	/**
	 * @brief 所有在册员工（status=1）账号列表。
	 *
	 * 必须用 QString：账号可能是超 int 上限的大数字。
	 * @return QStringList 账号列表
	 */
	QStringList getUserList();

	/**
	 * @brief 取会话对应的"账号+头像"列表（群成员树 / WebChannel 注册共用）。
	 *
	 * 三分支：公司群=全员；其他群=该 department 的员工；单聊=对方一人。
	 * @param id 会话标识（公司群 id / 4 位群号 / 对方账号）
	 * @param infoList [out] 填入 (账号, 头像路径) 对
	 * @return 成功填 infoList 返回 true；查不到数据或公司群记录缺失返回 false
	 */
	bool getRegisterObjectInfo(const QString& id, QList<QPair<QString, QString>>& infoList);

	/**
	 * @brief 把数据库里的图片路径解析成本机可用路径。
	 *
	 * 顺序：原路径能打开 → qrc 里同名图片 → 原样返回。
	 * @param stored 数据库里存的路径
	 * @return 本机可加载的路径
	 */
	static QString resolvePicturePath(const QString& stored);

private:
	/**
	 * @brief 连接数据库：只在构造函数里调用一次，不对外暴露。
	 *
	 * 参数来自 AppConfig（client.ini）；错误信息打在 qDebug。
	 * @return true=连接成功；false=连接失败
	 */
	bool connectMySQL();

private:
	// 数据库连接对象。QSqlDatabase 本身是"轻引用"，
	// 真正持有 TCP 连接的是 Qt 底层，析构时记得 close（本项目进程退出即释放，故未显式处理）
	QSqlDatabase base;   // 全项目唯一连接（名字 "main"）
};
