// FactorParamController.h
// 因子参数控制器 - 提供QML接口，管理因子参数配置和初始化
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QDateTime>
#include <QMutex>
#include <memory>
#include <map>

// 包含因子模型头文件 - 使用正确路径
#include "../../domain/model/include/Factor.h"

// 因子参数控制器 - QML交互接口
class FactorParamController : public QObject {
    Q_OBJECT
    
public:
    // 因子类型枚举（UI层使用）
    enum class FactorTypeUI {
        MOMENTUM,      // 动量类
        VALUE,         // 价值类
        QUALITY,       // 质量类
        GROWTH,        // 成长类
        TECHNICAL,     // 技术指标
        SENTIMENT,     // 情绪类
        RISK,          // 风险类
        MACRO,         // 宏观类
        CUSTOM         // 自定义
    };
    Q_ENUM(FactorTypeUI)
    
private:
    Q_PROPERTY(QString currentFactorName READ currentFactorName WRITE setCurrentFactorName NOTIFY currentFactorNameChanged)
    Q_PROPERTY(FactorTypeUI currentFactorType READ currentFactorType WRITE setCurrentFactorType NOTIFY currentFactorTypeChanged)
    Q_PROPERTY(QVariantList parameterDefinitions READ parameterDefinitions NOTIFY parameterDefinitionsChanged)
    Q_PROPERTY(QVariantMap parameterValues READ parameterValues WRITE setParameterValues NOTIFY parameterValuesChanged)
    Q_PROPERTY(bool parametersValid READ parametersValid NOTIFY parametersValidChanged)
    Q_PROPERTY(QString validationMessage READ validationMessage NOTIFY validationMessageChanged)
    Q_PROPERTY(QStringList availableFactorTypes READ availableFactorTypes NOTIFY availableFactorTypesChanged)
    
public:
    explicit FactorParamController(QObject* parent = nullptr);
    ~FactorParamController();
    
    // QML可调用的方法
    Q_INVOKABLE void initialize();
    Q_INVOKABLE void loadFactorTypeParameters(FactorTypeUI factorType);
    Q_INVOKABLE void setParameterValue(const QString& paramName, const QVariant& value);
    Q_INVOKABLE QVariant getParameterValue(const QString& paramName) const;
    Q_INVOKABLE void validateParameters();
    Q_INVOKABLE bool createFactor();
    Q_INVOKABLE void resetParameters();
    Q_INVOKABLE QVariantMap getDefaultParameters(FactorTypeUI factorType);
    
    // 静态工具方法
    Q_INVOKABLE static QString factorTypeToString(FactorTypeUI type);
    Q_INVOKABLE static FactorTypeUI stringToFactorType(const QString& typeStr);
    Q_INVOKABLE static QStringList getParameterNames(FactorTypeUI factorType);
    
    // 属性访问器
    QString currentFactorName() const;
    void setCurrentFactorName(const QString& name);
    
    FactorTypeUI currentFactorType() const;
    void setCurrentFactorType(FactorTypeUI type);
    
    QVariantList parameterDefinitions() const;
    QVariantMap parameterValues() const;
    void setParameterValues(const QVariantMap& values);
    
    bool parametersValid() const;
    QString validationMessage() const;
    QStringList availableFactorTypes() const;
    
signals:
    // 属性变化信号
    void currentFactorNameChanged();
    void currentFactorTypeChanged();
    void parameterDefinitionsChanged();
    void parameterValuesChanged();
    void parametersValidChanged();
    void validationMessageChanged();
    void availableFactorTypesChanged();
    
    // 操作信号
    void factorCreated(bool success, const QString& message, const QString& factorId);
    void parametersLoaded(FactorTypeUI factorType);
    void parameterValidationResult(bool valid, const QString& message);
    void errorOccurred(const QString& error);
    
private:
    // 初始化C++后端
    bool initializeBackend();
    
    // 从C++后端加载参数定义
    void loadParameterDefinitionsFromBackend(FactorTypeUI factorType);
    
    // 验证参数值
    bool validateParameter(const QString& paramName, const QVariant& value, const QVariantMap& paramDef);
    
    // 类型转换
    QVariantMap convertParamToVariantMap(const AStockQuantEngine::Domain::Model::FactorParam& param);
    QVariant convertJsonValueToVariant(const std::string& jsonValue, const QString& paramType);
    std::string convertVariantToJsonValue(const QVariant& value, const QString& paramType);
    
    // 创建C++因子对象
    std::shared_ptr<AStockQuantEngine::Domain::Model::Factor> createBackendFactor();
    
    // 辅助方法
    void updateError(const QString& error);
    void cleanup();
    
private:
    // UI状态
    QString m_currentFactorName;
    FactorTypeUI m_currentFactorType{FactorTypeUI::MOMENTUM};
    QVariantList m_parameterDefinitions;
    QVariantMap m_parameterValues;
    bool m_parametersValid{false};
    QString m_validationMessage{"请设置参数"};
    QStringList m_availableFactorTypes;
    
    // C++后端对象
    std::shared_ptr<AStockQuantEngine::Domain::Model::FactorFactory> m_factorFactory;
    std::map<FactorTypeUI, std::shared_ptr<AStockQuantEngine::Domain::Model::Factor>> m_factorPrototypes;
    
    // 互斥锁
    mutable QMutex m_mutex;
    
    // 参数类型映射
    static const std::map<FactorTypeUI, std::string> FACTOR_TYPE_MAPPING;
    static const std::map<std::string, FactorTypeUI> STRING_TO_FACTOR_TYPE;
};