#include "AppConfig.h"
#include <QCoreApplication> // 提供 applicationDirPath()，定位 exe 同目录
#include <QSettings>        // 读写 server.ini
#include <QFile>            // 判断 ini 是否存在
#include <QDebug>           // qInfo 日志

/**
 * @brief 返回全局唯一配置单例：首次调用读 server.ini，不存在则生成默认配置后复用 static 缓存
 *
 * 实现细节：用 static 局部变量 + 立即调用 lambda 只初始化一次；
 * 缓存后进程内改 ini 不会重读，必须重启才生效（契约见 .h）。
 * @return const AppConfig& 只读配置引用（tcpPort/db* 字段）
 */
const AppConfig& AppConfig::instance()
{
    static const AppConfig cfg = []() {                      // 惰性构造：首次调用才跑一次 lambda
        AppConfig c;                                         // 先用默认值填好可写的拷贝
        const QString path = QCoreApplication::applicationDirPath() + "/server.ini"; // ini 固定放 exe 旁边

        if (!QFile::exists(path))                            // 首次运行：ini 还不存在
        {
            QSettings w(path, QSettings::IniFormat);         // 用 QSettings 写 ini（自动处理转义）
            w.setIniCodec("UTF-8");                          // 中文注释/中文值必须 UTF-8，否则乱码
            w.setValue("net/port",    c.tcpPort);            // 写回 TCP 监听端口
            w.setValue("db/host",     c.dbHost);             // 写回 MySQL 主机
            w.setValue("db/port",     c.dbPort);             // 写回 MySQL 端口
            w.setValue("db/user",     c.dbUser);             // 写回用户名
            w.setValue("db/password", c.dbPassword);         // 写回密码（明文，部署时注意权限）
            w.setValue("db/name",     c.dbName);             // 写回库名
            qInfo() << "[config] 已生成默认配置:" << path;   // 让运维知道 ini 是这次新建的
            return c;                                        // 用默认值当配置直接返回
        }

        QSettings r(path, QSettings::IniFormat);             // 已有 ini：按 INI 读
        r.setIniCodec("UTF-8");                              // 读侧同样按 UTF-8 解码
        c.tcpPort    = r.value("net/port",  c.tcpPort).toInt();           // 缺项时回退默认端口
        c.dbHost     = r.value("db/host",   c.dbHost).toString();         // 缺项时回退 127.0.0.1
        c.dbPort     = r.value("db/port",   c.dbPort).toInt();            // 缺项时回退 3306
        c.dbUser     = r.value("db/user",   c.dbUser).toString();         // 缺项时回退 root
        c.dbPassword = r.value("db/password", c.dbPassword).toString();   // 缺项时回退默认密码
        c.dbName     = r.value("db/name",   c.dbName).toString();         // 缺项时回退 qt_qq_main
        return c;
    }();

    return cfg;                                              // 返回 static 缓存，全进程共享一份
}
