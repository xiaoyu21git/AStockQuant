// FactorMetaService.cpp
// 因子元数据服务实现 - 专门处理因子参数元数据的加载和查询

#include "FactorMetaService.h"

#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDir>
#include <QCoreApplication>
#include <QStandardPaths>
#include <memory>
#include <stdexcept>

// 静态成员初始化
const QMap<factor::FactorType, QString> FactorMetaService::FACTOR_TYPE_TO_ID = {
    {factor::FactorType::VALUE, "value"},
    {factor::FactorType::MOMENTUM, "momentum"},
    {factor::FactorType::SIZE, "size"},
    {factor::FactorType::QUALITY, "quality"},
    {factor::FactorType::LOW_VOLATILITY, "low_volatility"},
    {factor::FactorType::GROWTH, "growth"},
    {factor::FactorType::DIVIDEND, "dividend"},
    {factor::FactorType::TECHNICAL, "technical"},
    {factor::FactorType::LIQUIDITY, "liquidity"},
    {factor::FactorType::MACRO, "macro"},
    {factor::FactorType::INDUSTRY, "industry"},
    {factor::FactorType::SENTIMENT, "sentiment"},
    {factor::FactorType::CUSTOM, "custom"}
};

const QMap<QString, factor::FactorType> FactorMetaService::ID_TO_FACTOR_TYPE = {
    {"value", factor::FactorType::VALUE},
    {"momentum", factor::FactorType::MOMENTUM},
    {"size", factor::FactorType::SIZE},
    {"quality", factor::FactorType::QUALITY},
    {"low_volatility", factor::FactorType::LOW_VOLATILITY},
    {"growth", factor::FactorType::GROWTH},
    {"dividend", factor::FactorType::DIVIDEND},
    {"technical", factor::FactorType::TECHNICAL},
    {"liquidity", factor::FactorType::LIQUIDITY},
    {"macro", factor::FactorType::MACRO},
    {"industry", factor::FactorType::INDUSTRY},
    {"sentiment", factor::FactorType::SENTIMENT},
    {"custom", factor::FactorType::CUSTOM}
};

const QMap<factor::FactorType, QString> FactorMetaService::FACTOR_TYPE_TO_DISPLAY_NAME = {
    {factor::FactorType::VALUE, "价值因子"},
    {factor::FactorType::MOMENTUM, "动量因子"},
    {factor::FactorType::SIZE, "规模因子"},
    {factor::FactorType::QUALITY, "质量因子"},
    {factor::FactorType::LOW_VOLATILITY, "低波因子"},
    {factor::FactorType::GROWTH, "成长因子"},
    {factor::FactorType::DIVIDEND, "红利因子"},
    {factor::FactorType::TECHNICAL, "技术因子"},
    {factor::FactorType::LIQUIDITY, "流动性因子"},
    {factor::FactorType::MACRO, "宏观因子"},
    {factor::FactorType::INDUSTRY, "行业因子"},
    {factor::FactorType::SENTIMENT, "情绪因子"},
    {factor::FactorType::CUSTOM, "自定义因子"}
};

namespace {

QVariantMap buildFactorUiMeta(const QString& id,
                              int factorType,
                              const QString& displayName,
                              const QString& description,
                              const QString& placeholderName,
                              const QString& placeholderDesc,
                              const QString& color,
                              const QString& subCategory)
{
    return QVariantMap{
        {QStringLiteral("id"), id},
        {QStringLiteral("factorType"), factorType},
        {QStringLiteral("displayName"), displayName},
        {QStringLiteral("description"), description},
        {QStringLiteral("placeholderName"), placeholderName},
        {QStringLiteral("placeholderDesc"), placeholderDesc},
        {QStringLiteral("color"), color},
        {QStringLiteral("subCategory"), subCategory}
    };
}

const QMap<factor::FactorType, QVariantMap>& factorUiMetaCatalog()
{
    static const QMap<factor::FactorType, QVariantMap> kCatalog = {
        {factor::FactorType::VALUE, buildFactorUiMeta(QStringLiteral("value"), 0,
            QStringLiteral("价值因子"),
            QStringLiteral("基于BP、EP、股息率和CF/P构建的价值因子"),
            QStringLiteral("例如：低估值组合因子"),
            QStringLiteral("描述价值因子的计算方法、应用场景等..."),
            QStringLiteral("#F59E0B"),
            QStringLiteral("估值"))},
        {factor::FactorType::MOMENTUM, buildFactorUiMeta(QStringLiteral("momentum"), 1,
            QStringLiteral("动量因子"),
            QStringLiteral("基于价格动量、收益率趋势构建的动量因子"),
            QStringLiteral("例如：60日动量因子"),
            QStringLiteral("描述动量因子的计算方法、应用场景等..."),
            QStringLiteral("#3B82F6"),
            QStringLiteral("趋势动量"))},
        {factor::FactorType::SIZE, buildFactorUiMeta(QStringLiteral("size"), 2,
            QStringLiteral("规模因子"),
            QStringLiteral("基于市值规模、流通市值构建的规模因子"),
            QStringLiteral("例如：小市值因子"),
            QStringLiteral("描述规模因子的计算方法、应用场景等..."),
            QStringLiteral("#8B5CF6"),
            QStringLiteral("市值规模"))},
        {factor::FactorType::QUALITY, buildFactorUiMeta(QStringLiteral("quality"), 3,
            QStringLiteral("质量因子"),
            QStringLiteral("基于财务健康、盈利能力构建的质量因子"),
            QStringLiteral("例如：高ROE质量因子"),
            QStringLiteral("描述质量因子的计算方法、应用场景等..."),
            QStringLiteral("#10B981"),
            QStringLiteral("盈利能力"))},
        {factor::FactorType::GROWTH, buildFactorUiMeta(QStringLiteral("growth"), 4,
            QStringLiteral("成长因子"),
            QStringLiteral("基于营收、利润增长率构建的成长因子"),
            QStringLiteral("例如：高增长潜力因子"),
            QStringLiteral("描述成长因子的计算方法、应用场景等..."),
            QStringLiteral("#8B5CF6"),
            QStringLiteral("营收增长"))},
        {factor::FactorType::DIVIDEND, buildFactorUiMeta(QStringLiteral("dividend"), 5,
            QStringLiteral("红利因子"),
            QStringLiteral("基于股息率、股息支付率构建的红利因子"),
            QStringLiteral("例如：高股息率组合"),
            QStringLiteral("描述红利因子的计算方法、应用场景等..."),
            QStringLiteral("#EC4899"),
            QStringLiteral("股息"))},
        {factor::FactorType::TECHNICAL, buildFactorUiMeta(QStringLiteral("technical"), 6,
            QStringLiteral("技术因子"),
            QStringLiteral("基于RSI、MACD等技术指标构建的技术因子"),
            QStringLiteral("例如：RSI超卖信号"),
            QStringLiteral("描述技术因子的计算方法、应用场景等..."),
            QStringLiteral("#EF4444"),
            QStringLiteral("技术指标"))},
        {factor::FactorType::LIQUIDITY, buildFactorUiMeta(QStringLiteral("liquidity"), 7,
            QStringLiteral("流动性因子"),
            QStringLiteral("基于换手率、买卖价差构建的流动性因子"),
            QStringLiteral("例如：高流动性组合"),
            QStringLiteral("描述流动性因子的计算方法、应用场景等..."),
            QStringLiteral("#8B5CF6"),
            QStringLiteral("市场微观结构"))},
        {factor::FactorType::MACRO, buildFactorUiMeta(QStringLiteral("macro"), 8,
            QStringLiteral("宏观因子"),
            QStringLiteral("基于利率、通胀、经济周期构建的宏观因子"),
            QStringLiteral("例如：利率敏感度因子"),
            QStringLiteral("描述宏观因子的计算方法、应用场景等..."),
            QStringLiteral("#F97316"),
            QStringLiteral("宏观"))},
        {factor::FactorType::INDUSTRY, buildFactorUiMeta(QStringLiteral("industry"), 9,
            QStringLiteral("行业因子"),
            QStringLiteral("基于行业景气度、行业动量构建的行业因子"),
            QStringLiteral("例如：行业动量因子"),
            QStringLiteral("描述行业因子的计算方法、应用场景等..."),
            QStringLiteral("#EA580C"),
            QStringLiteral("行业"))},
        {factor::FactorType::SENTIMENT, buildFactorUiMeta(QStringLiteral("sentiment"), 10,
            QStringLiteral("情绪因子"),
            QStringLiteral("基于新闻情感、社交媒体构建的情绪因子"),
            QStringLiteral("例如：市场情绪指标"),
            QStringLiteral("描述情绪因子的计算方法、应用场景等..."),
            QStringLiteral("#EC4899"),
            QStringLiteral("行为金融"))},
        {factor::FactorType::CUSTOM, buildFactorUiMeta(QStringLiteral("custom"), 11,
            QStringLiteral("自定义因子"),
            QStringLiteral("用户自定义表达式构建的因子"),
            QStringLiteral("例如：自定义组合因子"),
            QStringLiteral("描述自定义因子的计算方法、应用场景等..."),
            QStringLiteral("#94A3B8"),
            QStringLiteral("自定义"))},
        {factor::FactorType::LOW_VOLATILITY, buildFactorUiMeta(QStringLiteral("low_volatility"), 12,
            QStringLiteral("低波因子"),
            QStringLiteral("基于波动率、贝塔值构建的低波因子"),
            QStringLiteral("例如：低波动率组合"),
            QStringLiteral("描述低波因子的计算方法、应用场景等..."),
            QStringLiteral("#06B6D4"),
            QStringLiteral("波动率"))}
    };
    return kCatalog;
}

}  // namespace

// 构造函数
FactorMetaService::FactorMetaService(QObject* parent) : QObject(parent)
{
    qDebug() << "FactorMetaService created";
}

// 析构函数
FactorMetaService::~FactorMetaService()
{
    qDebug() << "FactorMetaService destroyed";
}

// 初始化服务
void FactorMetaService::initialize()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_initialized) {
        qDebug() << "FactorMetaService already initialized";
        return;
    }
    
    if (!loadMetaData()) {
        updateError("Failed to load factor metadata");
        return;
    }
    
    m_initialized = true;
    emit initializedChanged();
    
    qDebug() << "FactorMetaService initialized successfully";
    emit metaDataLoaded(true, "因子元数据加载成功");
}

// 重新加载元数据
void FactorMetaService::reloadMetaData()
{
    QMutexLocker locker(&m_mutex);
    
    m_commonMetaData.clear();
    m_parameterMetaData.clear();
    m_factorCategories.clear();
    m_mergedParameters.clear();
    m_initialized = false;
    
    if (!loadMetaData()) {
        updateError("Failed to reload factor metadata");
        return;
    }
    
    m_initialized = true;
    emit initializedChanged();
    emit factorCategoriesChanged();
    emit parameterTypesChanged();
    
    qDebug() << "FactorMetaService metadata reloaded";
    emit metaDataLoaded(true, "因子元数据重新加载成功");
}

// 获取因子分类信息
QVariantMap FactorMetaService::getFactorCategory(factor::FactorType factorType)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_initialized) {
        qWarning() << "FactorMetaService not initialized";
        return QVariantMap();
    }
    
    if (m_factorCategories.contains(factorType)) {
        return m_factorCategories[factorType];
    }
    
    // 如果缓存中没有，尝试从commonMetaData中查找
    QString factorTypeId = factorTypeToString(factorType);
    if (m_commonMetaData.contains("categories")) {
        QVariantList categories = m_commonMetaData["categories"].toList();
        for (const QVariant& category : categories) {
            QVariantMap categoryMap = category.toMap();
            if (categoryMap["id"].toString() == factorTypeId) {
                m_factorCategories[factorType] = categoryMap;
                return categoryMap;
            }
        }
    }
    
    return QVariantMap();
}

// 通过ID获取因子分类信息
QVariantMap FactorMetaService::getFactorCategoryById(const QString& factorTypeId)
{
    factor::FactorType factorType = stringToFactorType(factorTypeId);
    return getFactorCategory(factorType);
}

// 获取可用因子类型列表
QStringList FactorMetaService::getAvailableFactorTypes()
{
    QStringList types;
    for (auto it = FACTOR_TYPE_TO_ID.constBegin(); it != FACTOR_TYPE_TO_ID.constEnd(); ++it) {
        types.append(it.value());
    }
    return types;
}

QVariantMap FactorMetaService::getFactorUiMeta(const QVariant& factorType) const
{
    const factor::FactorType resolvedType = variantToFactorType(factorType);
    return factorUiMetaCatalog().value(resolvedType);
}

// 获取参数定义
QVariantMap FactorMetaService::getParameterDefinition(const QString& paramName, factor::FactorType factorType)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_initialized) {
        qWarning() << "FactorMetaService not initialized";
        return QVariantMap();
    }
    
    // 确保合并参数已加载
    if (!m_mergedParameters.contains(factorType)) {
        m_mergedParameters[factorType] = mergeCommonAndSpecificParams(factorType);
    }
    
    QVariantMap mergedParams = m_mergedParameters[factorType];
    if (mergedParams.contains(paramName)) {
        return mergedParams[paramName].toMap();
    }
    
    return QVariantMap();
}

// 获取通用参数
QVariantList FactorMetaService::getCommonParameters(factor::FactorType factorType)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_initialized) {
        qWarning() << "FactorMetaService not initialized";
        return QVariantList();
    }
    
    QVariantList commonParams;
    
    if (m_parameterMetaData.contains("commonParams")) {
        QVariantMap commonParamsMap = m_parameterMetaData["commonParams"].toMap();
        for (auto it = commonParamsMap.constBegin(); it != commonParamsMap.constEnd(); ++it) {
            QVariantMap paramDef = parseParameterDefinition(it.value().toMap());
            paramDef["name"] = it.key();
            commonParams.append(paramDef);
        }
    }
    
    return commonParams;
}

// 获取特定类型参数
QVariantList FactorMetaService::getSpecificParameters(factor::FactorType factorType)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_initialized) {
        qWarning() << "FactorMetaService not initialized";
        return QVariantList();
    }
    
    QVariantList specificParams;
    QString factorTypeId = factorTypeToString(factorType);
    
    if (m_parameterMetaData.contains("factorTypeSpecificParams")) {
        QVariantMap specificParamsMap = m_parameterMetaData["factorTypeSpecificParams"].toMap();
        if (specificParamsMap.contains(factorTypeId)) {
            QVariantMap typeParams = specificParamsMap[factorTypeId].toMap();
            if (typeParams.contains("params")) {
                QVariantMap paramsMap = typeParams["params"].toMap();
                for (auto it = paramsMap.constBegin(); it != paramsMap.constEnd(); ++it) {
                    QVariantMap paramDef = parseParameterDefinition(it.value().toMap());
                    paramDef["name"] = it.key();
                    specificParams.append(paramDef);
                }
            }
        }
    }
    
    return specificParams;
}

// 获取所有参数（通用+特定）
QVariantList FactorMetaService::getAllParameters(factor::FactorType factorType)
{
    QVariantList allParams;
    
    // 添加通用参数
    QVariantList commonParams = getCommonParameters(factorType);
    for (const QVariant& param : commonParams) {
        allParams.append(param);
    }
    
    // 添加特定参数
    QVariantList specificParams = getSpecificParameters(factorType);
    for (const QVariant& param : specificParams) {
        allParams.append(param);
    }
    
    return allParams;
}

// 获取默认参数值
QVariantMap FactorMetaService::getDefaultParameterValues(factor::FactorType factorType)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_initialized) {
        qWarning() << "FactorMetaService not initialized";
        return QVariantMap();
    }
    
    QVariantMap defaultValues;
    
    // 从UI配置中获取默认值
    if (m_parameterMetaData.contains("uiConfig") && 
        m_parameterMetaData["uiConfig"].toMap().contains("defaultValues")) {
        QVariantMap uiDefaults = m_parameterMetaData["uiConfig"].toMap()["defaultValues"].toMap();
        for (auto it = uiDefaults.constBegin(); it != uiDefaults.constEnd(); ++it) {
            defaultValues[it.key()] = it.value();
        }
    }
    
    return defaultValues;
}

// 获取单个参数的默认值
QVariant FactorMetaService::getDefaultParameterValue(const QString& paramName, factor::FactorType factorType)
{
    QVariantMap defaultValues = getDefaultParameterValues(factorType);
    if (defaultValues.contains(paramName)) {
        return defaultValues[paramName];
    }
    
    // 尝试从参数定义中获取默认值
    QVariantMap paramDef = getParameterDefinition(paramName, factorType);
    if (paramDef.contains("default")) {
        return paramDef["default"];
    }
    
    return QVariant();
}

// 验证单个参数
bool FactorMetaService::validateParameter(const QString& paramName, const QVariant& value, factor::FactorType factorType)
{
    QVariantMap paramDef = getParameterDefinition(paramName, factorType);
    if (paramDef.isEmpty()) {
        return false;
    }
    
    QString type = paramDef["type"].toString();
    
    if (type == "integer") {
        return validateIntegerParameter(value, paramDef);
    } else if (type == "float") {
        return validateFloatParameter(value, paramDef);
    } else if (type == "boolean") {
        return validateBooleanParameter(value, paramDef);
    } else if (type == "enum") {
        return validateEnumParameter(value, paramDef);
    } else if (type == "string") {
        return validateStringParameter(value, paramDef);
    }
    
    return false;
}

// 验证所有参数
QString FactorMetaService::validateAllParameters(const QVariantMap& parameters, factor::FactorType factorType)
{
    QStringList validationErrors;
    
    for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
        if (!validateParameter(it.key(), it.value(), factorType)) {
            QVariantMap paramDef = getParameterDefinition(it.key(), factorType);
            QString displayName = paramDef["displayName"].toString();
            validationErrors.append(QString("%1 参数值无效").arg(displayName));
        }
    }
    
    if (validationErrors.isEmpty()) {
        return "参数验证通过";
    } else {
        return validationErrors.join("; ");
    }
}

// 静态工具方法：因子类型转字符串
QString FactorMetaService::factorTypeToString(factor::FactorType type)
{
    return FACTOR_TYPE_TO_ID.value(type, "custom");
}

// 静态工具方法：字符串转因子类型
factor::FactorType FactorMetaService::stringToFactorType(const QString& typeStr)
{
    return ID_TO_FACTOR_TYPE.value(typeStr.trimmed().toLower(), factor::FactorType::UNKNOWN);
}

// 静态工具方法：因子类型转显示名称
QString FactorMetaService::factorTypeToDisplayName(factor::FactorType type)
{
    return FACTOR_TYPE_TO_DISPLAY_NAME.value(type, "自定义因子");
}

factor::FactorType FactorMetaService::variantToFactorType(const QVariant& factorType)
{
    bool ok = false;
    const int typeIndex = factorType.toInt(&ok);
    if (ok) {
        return factor::factorTypeFromIndex(typeIndex);
    }

    if (factorType.metaType().id() == qMetaTypeId<factor::FactorType>()) {
        return factorType.value<factor::FactorType>();
    }

    return stringToFactorType(factorType.toString());
}

// 属性访问器
bool FactorMetaService::isInitialized() const
{
    QMutexLocker locker(const_cast<QMutex*>(&m_mutex));
    return m_initialized;
}

QVariantList FactorMetaService::factorCategories() const
{
    QMutexLocker locker(const_cast<QMutex*>(&m_mutex));
    
    QVariantList categories;
    for (auto it = m_factorCategories.constBegin(); it != m_factorCategories.constEnd(); ++it) {
        categories.append(it.value());
    }
    
    return categories;
}

QVariantMap FactorMetaService::parameterTypes() const
{
    QMutexLocker locker(const_cast<QMutex*>(&m_mutex));
    
    if (m_parameterMetaData.contains("parameterTypes")) {
        return m_parameterMetaData["parameterTypes"].toMap();
    }
    
    return QVariantMap();
}

// 私有方法实现
bool FactorMetaService::loadMetaData()
{
    if (!loadCommonMetaData()) {
        qWarning() << "Failed to load common metadata";
        return false;
    }
    
    if (!loadParameterMetaData()) {
        qWarning() << "Failed to load parameter metadata";
        return false;
    }
    
    // 初始化因子分类缓存
    if (m_commonMetaData.contains("categories")) {
        QVariantList categories = m_commonMetaData["categories"].toList();
        for (const QVariant& category : categories) {
            QVariantMap categoryMap = category.toMap();
            QString categoryId = categoryMap["id"].toString();
            factor::FactorType factorType = stringToFactorType(categoryId);
            m_factorCategories[factorType] = categoryMap;
        }
    }
    
    return true;
}

bool FactorMetaService::loadCommonMetaData()
{
    QString filePath = getConfigFilePath("config/views/factor_common.json");
    QFile file(filePath);
    
    if (!file.exists()) {
        qWarning() << "Common metadata file not found:" << filePath;
        return false;
    }
    
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open common metadata file:" << filePath;
        return false;
    }
    
    QByteArray jsonData = file.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (doc.isNull()) {
        qWarning() << "Failed to parse common metadata JSON";
        return false;
    }
    
    m_commonMetaData = doc.object().toVariantMap();
    qDebug() << "Loaded common metadata from:" << filePath;
    return true;
}

bool FactorMetaService::loadParameterMetaData()
{
    QString filePath = getConfigFilePath("config/views/factor_common_params.json");
    QFile file(filePath);
    
    if (!file.exists()) {
        qWarning() << "Parameter metadata file not found:" << filePath;
        return false;
    }
    
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open parameter metadata file:" << filePath;
        return false;
    }
    
    QByteArray jsonData = file.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (doc.isNull()) {
        qWarning() << "Failed to parse parameter metadata JSON";
        return false;
    }
    
    m_parameterMetaData = doc.object().toVariantMap();
    qDebug() << "Loaded parameter metadata from:" << filePath;
    return true;
}

QVariantMap FactorMetaService::mergeCommonAndSpecificParams(factor::FactorType factorType)
{
    QVariantMap mergedParams;
    
    // 添加通用参数
    QVariantList commonParams = getCommonParameters(factorType);
    for (const QVariant& param : commonParams) {
        QVariantMap paramMap = param.toMap();
        QString paramName = paramMap["name"].toString();
        mergedParams[paramName] = paramMap;
    }
    
    // 添加特定参数
    QVariantList specificParams = getSpecificParameters(factorType);
    for (const QVariant& param : specificParams) {
        QVariantMap paramMap = param.toMap();
        QString paramName = paramMap["name"].toString();
        mergedParams[paramName] = paramMap;
    }
    
    return mergedParams;
}

QVariantMap FactorMetaService::parseParameterDefinition(const QVariantMap& paramDef)
{
    QVariantMap parsedDef = paramDef;
    
    // 确保有必要的字段
    if (!parsedDef.contains("displayName")) {
        parsedDef["displayName"] = parsedDef["name"];
    }
    
    if (!parsedDef.contains("description")) {
        parsedDef["description"] = "";
    }
    
    if (!parsedDef.contains("type")) {
        parsedDef["type"] = "string";
    }
    
    return parsedDef;
}

bool FactorMetaService::validateIntegerParameter(const QVariant& value, const QVariantMap& paramDef)
{
    bool ok;
    int intValue = value.toInt(&ok);
    if (!ok) return false;
    
    if (paramDef.contains("minValue") && intValue < paramDef["minValue"].toInt()) return false;
    if (paramDef.contains("maxValue") && intValue > paramDef["maxValue"].toInt()) return false;
    
    return true;
}

bool FactorMetaService::validateFloatParameter(const QVariant& value, const QVariantMap& paramDef)
{
    bool ok;
    double doubleValue = value.toDouble(&ok);
    if (!ok) return false;
    
    if (paramDef.contains("minValue") && doubleValue < paramDef["minValue"].toDouble()) return false;
    if (paramDef.contains("maxValue") && doubleValue > paramDef["maxValue"].toDouble()) return false;
    
    return true;
}

bool FactorMetaService::validateBooleanParameter(const QVariant& value, const QVariantMap& paramDef)
{
    return value.canConvert<bool>();
}

bool FactorMetaService::validateEnumParameter(const QVariant& value, const QVariantMap& paramDef)
{
    if (!value.canConvert<QString>()) return false;
    
    if (paramDef.contains("options")) {
        QVariantList options = paramDef["options"].toList();
        QString strValue = value.toString();
        for (const QVariant& option : options) {
            QVariantMap optionMap = option.toMap();
            if (optionMap["value"].toString() == strValue) {
                return true;
            }
        }
        return false;
    }
    
    return true;
}

bool FactorMetaService::validateStringParameter(const QVariant& value, const QVariantMap& paramDef)
{
    if (!value.canConvert<QString>()) return false;
    
    QString strValue = value.toString();
    if (paramDef.contains("maxLength") && strValue.length() > paramDef["maxLength"].toInt()) {
        return false;
    }
    
    return true;
}

QString FactorMetaService::getConfigFilePath(const QString& relativePath)
{
    // 首先尝试当前工作目录
    QString currentDir = QDir::currentPath();
    QString filePath = QDir::cleanPath(currentDir + "/" + relativePath);
    
    if (QFile::exists(filePath)) {
        return filePath;
    }
    
    // 尝试应用程序目录
    QString appDir = QCoreApplication::applicationDirPath();
    filePath = QDir::cleanPath(appDir + "/" + relativePath);
    
    if (QFile::exists(filePath)) {
        return filePath;
    }
    
    // 尝试项目根目录
    QString projectRoot = QDir::cleanPath(currentDir + "/../../..");
    filePath = QDir::cleanPath(projectRoot + "/" + relativePath);
    
    if (QFile::exists(filePath)) {
        return filePath;
    }
    
    // 返回原始路径
    return relativePath;
}

void FactorMetaService::updateError(const QString& error)
{
    m_lastError = error;
    qWarning() << "FactorMetaService error:" << error;
    emit errorOccurred(error);
}
