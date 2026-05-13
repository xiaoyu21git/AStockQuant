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

#include "../../../domain/factor/include/factor_enums.h"

// 因子元数据服务 - QML交互接口
class FactorMetaService : public QObject {
    Q_OBJECT
    
public:
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
    Q_INVOKABLE QVariantMap getFactorCategory(factor::FactorType factorType);
    Q_INVOKABLE QVariantMap getFactorCategoryById(const QString& factorTypeId);
    Q_INVOKABLE QStringList getAvailableFactorTypes();
    
    // 获取参数定义
    Q_INVOKABLE QVariantMap getParameterDefinition(const QString& paramName, factor::FactorType factorType);
    Q_INVOKABLE QVariantList getCommonParameters(factor::FactorType factorType);
    Q_INVOKABLE QVariantList getSpecificParameters(factor::FactorType factorType);
    Q_INVOKABLE QVariantList getAllParameters(factor::FactorType factorType);
    
    // 获取默认参数值
    Q_INVOKABLE QVariantMap getDefaultParameterValues(factor::FactorType factorType);
    Q_INVOKABLE QVariant getDefaultParameterValue(const QString& paramName, factor::FactorType factorType);
    
    // 验证参数
    Q_INVOKABLE bool validateParameter(const QString& paramName, const QVariant& value, factor::FactorType factorType);
    Q_INVOKABLE QString validateAllParameters(const QVariantMap& parameters, factor::FactorType factorType);
    
    // 类型转换
    Q_INVOKABLE static QString factorTypeToString(factor::FactorType type);
    Q_INVOKABLE static factor::FactorType stringToFactorType(const QString& typeStr);
    Q_INVOKABLE static QString factorTypeToDisplayName(factor::FactorType type);
    
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
    QVariantMap mergeCommonAndSpecificParams(factor::FactorType factorType);
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
    QMap<factor::FactorType, QVariantMap> m_factorCategories;  // 因子分类信息
    QMap<factor::FactorType, QVariantMap> m_mergedParameters;  // 合并后的参数定义
    
    // 状态
    bool m_initialized{false};
    QString m_lastError;
    
    // 互斥锁
    mutable QMutex m_mutex;
    
    // 类型映射
    static const QMap<factor::FactorType, QString> FACTOR_TYPE_TO_ID;
    static const QMap<QString, factor::FactorType> ID_TO_FACTOR_TYPE;
    static const QMap<factor::FactorType, QString> FACTOR_TYPE_TO_DISPLAY_NAME;
};