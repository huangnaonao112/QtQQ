#include "ToolRegistry.h"     // 注册表声明
#include <QJsonDocument>      // 参数 Compact 打印日志
#include <QDebug>             // 执行日志

// 构造空表：工具由 DbTools::registerAll 在启动时统一登记
/**
 * @brief 构造空注册表；parent 挂给 QObject
 * @param parent Qt 父对象
 */
ToolRegistry::ToolRegistry(QObject* parent)
    : QObject(parent)                              // 挂父对象
{
}

// addTool — 登记一个工具；name 是唯一键，同名后注册的覆盖先注册的
//（不会追加成两个，toolsJson/invoke 都只看到最后一个）
/**
 * @brief 以 name 为键插入 Tool；同名直接覆盖（QMap::insert 语义）
 * @param name 工具名
 * @param description 描述
 * @param parameters JSON Schema
 * @param handler 执行器
 */
void ToolRegistry::addTool(const QString& name,
                           const QString& description,
                           const QJsonObject& parameters,
                           std::function<QString(const QJsonObject& args)> handler)
{
    m_tools.insert(name, Tool{ name, description, parameters, handler });  // 后写覆盖
}

// 生成 OpenAI 格式：
// [{"type":"function","function":{"name":..,"description":..,"parameters":{JSON Schema}}}]
/**
 * @brief 把 m_tools 映射为 OpenAI tools 数组
 * @return tools JSON 数组
 */
QJsonArray ToolRegistry::toolsJson() const
{
    QJsonArray arr;                                // 输出数组
    for (const Tool& t : m_tools)                  // 按 name 有序遍历
    {
        QJsonObject fn;                            // function 对象
        fn.insert("name", t.name);                 // 工具名
        fn.insert("description", t.description);   // 描述
        fn.insert("parameters", t.parameters);     // Schema

        QJsonObject o;                             // 外层包装
        o.insert("type", "function");              // 类型 = function
        o.insert("function", fn);                  // 嵌入 function
        arr.append(o);                             // 追加一项
    }
    return arr;
}

// invoke — 按名执行并打日志（参数原样打印，排查"模型到底传了什么"用）。
// 未知工具不抛异常而是返回错误文案：它会作为 role:tool 结果回喂，
// 模型读到"未知工具"会自己改口换个查法（契约见 .h）
/**
 * @brief 按名执行工具；未知工具返回错误文案而非抛错
 * @param name 工具名
 * @param args 参数对象
 * @return handler 结果或错误文案
 */
QString ToolRegistry::invoke(const QString& name, const QJsonObject& args) const
{
    if (!m_tools.contains(name))
        return QString("未知工具：%1").arg(name);  // 未知工具：回喂可改口

    qInfo() << "[tools] 执行" << name << QJsonDocument(args).toJson(QJsonDocument::Compact);  // 记录入参
    return m_tools[name].handler(args);            // 调执行器（[] 因 contains 已保证存在）
}
