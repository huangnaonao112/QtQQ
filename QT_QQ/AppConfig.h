#pragma once

#include <QString>

// ============================================================================
// AppConfig — 客户端运行参数（client.ini，exe 同目录）
//
// 这些参数原来写死在代码里：服务端地址在 TalkWindowShell，数据库账号在
// UseMySQL。结果就是"换台机器/换个 MySQL 密码"必须重新编译。
//
// 首次运行会在 exe 目录生成一份 client.ini（内容就是下面的默认值），
// 之后改 ini 就能生效，不用动代码。
// ============================================================================
struct AppConfig
{
    // 服务端（消息中转）
    QString serverHost = QStringLiteral("127.0.0.1");   // 服务端 IP，默认本机
    int     serverPort = 57777;                        // 服务端端口，协议约定 57777

    // MySQL
    QString dbHost     = QStringLiteral("127.0.0.1");   // MySQL 地址，默认本机
    int     dbPort     = 3306;                         // MySQL 端口，官方默认
    QString dbUser     = QStringLiteral("root");       // MySQL 用户名
    QString dbPassword = QStringLiteral("【MYSQL密码已移除】");   // 本机 MySQL 密码，换机器改 client.ini
    QString dbName     = QStringLiteral("qt_qq_main"); // 库名，全项目共用

    // 进程内单例：第一次调用时读 ini（不存在就写一份默认的）
    /**
     * @brief 进程级唯一的客户端配置单例：首次访问时从 exe 目录的 client.ini 读取（不存在则按默认值写一份）。
     * @return const AppConfig& 全程同一份配置对象的引用
     */
    static const AppConfig& instance();
};
