#pragma once

#include <QString>

// ============================================================================
// AppConfig — 服务端运行参数（server.ini，exe 同目录）
//
// 原来 TCP 端口写死在 QtQQ_Server.h，数据库账号写死在 UseMySQL.cpp，
// 换机器/换密码就得重新编译。首次运行会在 exe 目录生成一份 server.ini，
// 之后改 ini 即可。
// ============================================================================
/**
 * @brief 服务端运行时配置：TCP 端口 + MySQL 连接参数，优先读 exe 同目录 server.ini
 *
 * 全部字段提供合理默认值：ini 缺项/缺文件时按默认值运行，不阻断启动。
 */
struct AppConfig
{
    int tcpPort = 57777;          // 客户端/机器人连接的端口

    QString dbHost     = QStringLiteral("127.0.0.1");   // MySQL 主机（本机）
    int     dbPort     = 3306;                          // MySQL 默认端口
    QString dbUser     = QStringLiteral("root");        // MySQL 用户名
    QString dbPassword = QStringLiteral("【MYSQL密码已移除】");   // 本机 MySQL 密码，换机器改 server.ini
    QString dbName     = QStringLiteral("qt_qq_main");  // 库名，与客户端工程共用

    /**
     * @brief 返回全局唯一的配置单例（首次调用读/生成 server.ini，之后复用缓存）
     * @return const AppConfig& 只读配置对象；进程内改 ini 需重启才生效
     */
    static const AppConfig& instance();
};
