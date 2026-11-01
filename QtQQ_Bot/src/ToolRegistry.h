#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <functional>
#include <QMap>

// ============================================================================
// ToolRegistry — function calling 的工具注册表
//
// 职责：
//   (1) add()：登记工具（名称/描述/参数 schema/执行器）；
//   (2) toolsJson()：生成 OpenAI 格式的 tools 数组（随请求发给大模型）；
//   (3) invoke()：按名称+参数执行工具，返回人类可读的结果文本。
//
// 扩展新工具 = 写一个函数 + add 一行，LLM 侧无需任何改动——
// 这就是 function calling 相对"关键词路由"的核心优势。
// ============================================================================
/**
 * @brief function calling 工具注册表：addTool 登记 / toolsJson 出 schema / invoke 执行
 */
class ToolRegistry : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 单个工具定义：名称 + 描述 + JSON Schema 参数 + 执行器
     */
    struct Tool
    {
        QString name;                 // 工具名（唯一键，模型调用入口）
        QString description;          // 进 prompt 的工具说明
        QJsonObject parameters;                                  // JSON Schema
        std::function<QString(const QJsonObject& args)> handler; // 执行器
    };

    /**
     * @brief 构造空注册表；工具由 DbTools::registerAll 在启动时统一登记
     * @param parent Qt 父对象
     */
    explicit ToolRegistry(QObject* parent = nullptr);

    /**
     * @brief 登记工具；name 唯一键，同名后注册覆盖先注册
     * @param name 工具名
     * @param description 工具描述（模型可见纪律）
     * @param parameters JSON Schema
     * @param handler 执行器：args → 结果文本
     */
    // 登记工具；name 为唯一键，同名后注册的覆盖先注册的
    void addTool(const QString& name,
                 const QString& description,
                 const QJsonObject& parameters,
                 std::function<QString(const QJsonObject& args)> handler);

    /**
     * @brief 生成 OpenAI tools 数组：[{type:function,function:{name,description,parameters}}]
     * @return 可直接放进请求体 tools 字段的 JSON 数组
     */
    QJsonArray toolsJson() const;                                // 生成 tools 参数

    /**
     * @brief 按名执行；未知工具返回错误文案（回喂后模型会改口）
     * @param name 工具名
     * @param args 解析后的参数对象
     * @return 工具结果或错误文案
     */
    // 执行工具；name 不存在返回错误文案（LLM 会据此改口）
    QString invoke(const QString& name, const QJsonObject& args) const;

private:
    QMap<QString, Tool> m_tools;                                 // name → tool
};

