#include "stdafx.h"
#include <QCoreApplication>
#include <QSettings>
#include <QFile>
#include <QDebug>
#include "AppConfig.h"

// ============================================================================
// instance：进程级唯一的客户端配置（连接地址、数据库账号）
// 静态局部变量 + 立即调用的 lambda：首次访问时构造一次，之后永远返回同一份，
//   且线程安全（C++11 保证静态局部初始化只跑一次）。
// 首次运行没有 client.ini 时按默认值写一份出来——用户/运维直接改文件就能
//   换服务器地址，不用重新编译；之后每次启动都从这个文件读。
// 配置固定放 exe 同目录（applicationDirPath），不看工作目录：
//   从快捷方式、命令行不同 cwd 启动时读到的必须是同一份配置。
// ============================================================================
/**
 * @brief 进程级唯一的客户端配置单例：首次访问时从 exe 目录的 client.ini 读取（不存在则按默认值写一份再返回）。
 *        静态局部变量保证线程安全只构造一次；applicationDirPath 保证无论 cwd 如何读到的都是同一份。
 * @return const AppConfig& 全程同一份配置对象的引用
 */
const AppConfig& AppConfig::instance()
{
    static const AppConfig cfg = []() {          // 静态局部：首次访问构造，之后永远返回同一份
        AppConfig c;                             // 暂存最终要返回的配置（先填默认值，再被 ini 覆盖）
        const QString path = QCoreApplication::applicationDirPath() + "/client.ini";   // 配置固定放 exe 同目录，不看 cwd

        if (!QFile::exists(path))                // 首次运行 / 用户删掉了 ini
        {
            // 没有配置就按默认值写一份，下次直接改这个文件
            QSettings w(path, QSettings::IniFormat);   // 以 ini 格式写出
            w.setIniCodec("UTF-8");                     // 中文注释/路径按 UTF-8 写盘，防 MSVC 本地码页乱码
            w.setValue("net/server",      c.serverHost);   // 写服务端地址默认值
            w.setValue("net/port",        c.serverPort);   // 写服务端端口
            w.setValue("db/host",         c.dbHost);       // 写 MySQL 地址
            w.setValue("db/port",         c.dbPort);       // 写 MySQL 端口
            w.setValue("db/user",         c.dbUser);       // 写 MySQL 用户
            w.setValue("db/password",     c.dbPassword);   // 写 MySQL 密码（练习项目明文，真实项目要加密）
            w.setValue("db/name",         c.dbName);       // 写库名
            qInfo() << "[config] 已生成默认配置:" << path; // 日志告知运维：默认 ini 落在哪
            return c;                              // 默认值直接返回，不必再读一遍
        }

        QSettings r(path, QSettings::IniFormat);  // 已有 ini：打开读取
        r.setIniCodec("UTF-8");                   // 与写盘同一编码，防中文路径读乱
        c.serverHost = r.value("net/server", c.serverHost).toString();   // 读服务端地址，缺键时回退默认值
        c.serverPort = r.value("net/port",   c.serverPort).toInt();      // 读服务端端口
        c.dbHost     = r.value("db/host",    c.dbHost).toString();       // 读 MySQL 地址
        c.dbPort     = r.value("db/port",    c.dbPort).toInt();          // 读 MySQL 端口
        c.dbUser     = r.value("db/user",    c.dbUser).toString();       // 读 MySQL 用户
        c.dbPassword = r.value("db/password",c.dbPassword).toString();   // 读 MySQL 密码
        c.dbName     = r.value("db/name",    c.dbName).toString();       // 读库名
        return c;                              // 以 ini 覆盖后的最终配置返回
    }();                                       // 立即调用：lambda 只跑一次，结果存进静态 cfg

    return cfg;                                // 之后每次调用都返回这同一份
}
