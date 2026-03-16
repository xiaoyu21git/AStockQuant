// FactorMetaService.h
// 因子元数据服务 - 专门处理因子参数元数据的加载和查询
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>
#include <QMap>
#include <QMutex>
#include <memory>
#include <string>
#include <vector>

// 因子元数据服务 - QML交互接口
class FactorMetaService : public QObject {
    Q_OBJECT
    
public:
    // 因子类型枚举（与factor_common.json保持一致）
    enum class FactorType {
        VALUE,              // 价值因子
        MOMENTUM,           // 动量因子
        SIZE,               // 规模因子
        QUALITY,            // 质量因子
        LOW_VOLATILITY,     // 低波因子
        GROWTH,             // 成长因子
        DIVIDEND,           // 红利因子
        TECHNICAL,          // 技术因子
        MACRO_SECTOR,       // 宏观/行业因子
        CUSTOM              // 自定义因子
    };
    Q_ENUM(FactorType)
    
private:
    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)
    Q_PROPERTY(QVariantList factorCategories READ factorCategories NOTIFY factorCategoriesChanged)
    Q_PROPERTY(QVariantMap parameterTypes READ parameterTypes NOTIFY parameterTypesChanged)
    
public:
    explicit FactorMetaService(QObject* parent = nullptr);
    ~FactorMetaService();
    
    // QML可调用的方法
    Q_INVOKABLE void initialize();
    Q_INVOKABLE void reloadMetaData();
    
    // 获取因子类型信息
    Q_INVOKABLE QVariantMap getFactorCategory(FactorType factorType);
    Q_INVOKABLE QVariantMap getFactorCategoryById(const QString& factorTypeId);
    Q_INVOKABLE QStringList getAvailableFactorTypes();
    
    // 获取参数定义
    Q_INVOKABLE QVariantMap getParameterDefinition(const QString& paramName, FactorType factorType);
    Q_INVOKABLE QVariantList getCommonParameters(FactorType factorType);
    Q_INVOKABLE QVariantList getSpecificParameters(FactorType factorType);
    Q_INVOKABLE QVariantList getAllParameters(FactorType factorType);
    
    // 获取默认参数值
    Q_INVOKABLE QVariantMap getDefaultParameterValues(FactorType factorType);
    Q_INVOKABLE QVariant getDefaultParameterValue(const QString& paramName, FactorType factorType);
    
    // 验证参数
    Q_INVOKABLE bool validateParameter(const QString& paramName, const QVariant& value, FactorType factorType);
    Q_INVOKABLE QString validateAllParameters(const QVariantMap& parameters, FactorType factorType);
    
    // 类型转换
    Q_INVOKABLE static QString factorTypeToString(FactorType type);
    Q_INVOKABLE static FactorType stringToFactorType(const QString& typeStr);
    Q_INVOKABLE static QString factorTypeToDisplayName(FactorType type);
    
    // 属性访问器
    bool isInitialized() const;
    QVariantList factorCategories() const;
    QVariantMap parameterTypes() const;
    
signals:
    // 属性变化信号
    void initializedChanged();
    void factorCategoriesChanged();
    void parameterTypesChanged();
    
    // 操作信号
    void metaDataLoaded(bool success, const QString& message);
    void errorOccurred(const QString& error);
    
private:
    // 加载元数据文件
    bool loadMetaData();
    bool loadCommonMetaData();
    bool loadParameterMetaData();
    
    // 解析和合并参数
    QVariantMap mergeCommonAndSpecificParams(FactorType factorType);
    QVariantMap parseParameterDefinition(const QVariantMap& paramDef);
    
    // 验证参数值
    bool validateIntegerParameter(const QVariant& value, const QVariantMap& paramDef);
    bool validateFloatParameter(const QVariant& value, const QVariantMap& paramDef);
    bool validateBooleanParameter(const QVariant& value, const QVariantMap& paramDef);
    bool validateEnumParameter(const QVariant& value, const QVariantMap& paramDef);
    bool validateStringParameter(const QVariant& value, const QVariantMap& paramDef);
    
    // 辅助方法
    QString getConfigFilePath(const QString& relativePath);
    void updateError(const QString& error);
    
private:
    // 元数据缓存
    QVariantMap m_commonMetaData;          // factor_common.json 数据
    QVariantMap m_parameterMetaData;       // factor_common_params.json 数据
    QMap<FactorType, QVariantMap> m_factorCategories;  // 因子分类信息
    QMap<FactorType, QVariantMap> m_mergedParameters;  // 合并后的参数定义
    
    // 状态
    bool m_initialized{false};
    QString m_lastError;
    
    // 互斥锁
    mutable QMutex m_mutex;
    
    // 类型映射
    static const QMap<FactorType, QString> FACTOR_TYPE_TO_ID;
    static const QMap<QString, FactorType> ID_TO_FACTOR_TYPE;
    static const QMap<FactorType, QString> FACTOR_TYPE_TO_DISPLAY_NAME;
};