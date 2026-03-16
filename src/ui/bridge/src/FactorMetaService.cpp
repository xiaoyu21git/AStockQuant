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
const QMap<FactorMetaService::FactorType, QString> FactorMetaService::FACTOR_TYPE_TO_ID = {
    {FactorMetaService::FactorType::VALUE, "value"},
    {FactorMetaService::FactorType::MOMENTUM, "momentum"},
    {FactorMetaService::FactorType::SIZE, "size"},
    {FactorMetaService::FactorType::QUALITY, "quality"},
    {FactorMetaService::FactorType::LOW_VOLATILITY, "low_volatility"},
    {FactorMetaService::FactorType::GROWTH, "growth"},
    {FactorMetaService::FactorType::DIVIDEND, "dividend"},
    {FactorMetaService::FactorType::TECHNICAL, "technical"},
    {FactorMetaService::FactorType::MACRO_SECTOR, "macro_sector"},
    {FactorMetaService::FactorType::CUSTOM, "custom"}
};

const QMap<QString, FactorMetaService::FactorType> FactorMetaService::ID_TO_FACTOR_TYPE = {
    {"value", FactorMetaService::FactorType::VALUE},
    {"momentum", FactorMetaService::FactorType::MOMENTUM},
    {"size", FactorMetaService::FactorType::SIZE},
    {"quality", FactorMetaService::FactorType::QUALITY},
    {"low_volatility", FactorMetaService::FactorType::LOW_VOLATILITY},
    {"growth", FactorMetaService::FactorType::GROWTH},
    {"dividend", FactorMetaService::FactorType::DIVIDEND},
    {"technical", FactorMetaService::FactorType::TECHNICAL},
    {"macro_sector", FactorMetaService::FactorType::MACRO_SECTOR},
    {"custom", FactorMetaService::FactorType::CUSTOM}
};

const QMap<FactorMetaService::FactorType, QString> FactorMetaService::FACTOR_TYPE_TO_DISPLAY_NAME = {
    {FactorMetaService::FactorType::VALUE, "价值"},
    {FactorMetaService::FactorType::MOMENTUM, "动量"},
    {FactorMetaService::FactorType::SIZE, "规模"},
    {FactorMetaService::FactorType::QUALITY, "质量"},
    {FactorMetaService::FactorType::LOW_VOLATILITY, "低波"},
    {FactorMetaService::FactorType::GROWTH, "成长"},
    {FactorMetaService::FactorType::DIVIDEND, "红利"},
    {FactorMetaService::FactorType::TECHNICAL, "技术"},
    {FactorMetaService::FactorType::MACRO_SECTOR, "宏观/行业"},
    {FactorMetaService::FactorType::CUSTOM, "自定义"}
};

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
QVariantMap FactorMetaService::getFactorCategory(FactorType factorType)
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
    FactorType factorType = stringToFactorType(factorTypeId);
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

// 获取参数定义
QVariantMap FactorMetaService::getParameterDefinition(const QString& paramName, FactorType factorType)
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
QVariantList FactorMetaService::getCommonParameters(FactorType factorType)
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
QVariantList FactorMetaService::getSpecificParameters(FactorType factorType)
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
QVariantList FactorMetaService::getAllParameters(FactorType factorType)
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
QVariantMap FactorMetaService::getDefaultParameterValues(FactorType factorType)
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
QVariant FactorMetaService::getDefaultParameterValue(const QString& paramName, FactorType factorType)
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
bool FactorMetaService::validateParameter(const QString& paramName, const QVariant& value, FactorType factorType)
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
QString FactorMetaService::validateAllParameters(const QVariantMap& parameters, FactorType factorType)
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
QString FactorMetaService::factorTypeToString(FactorType type)
{
    return FACTOR_TYPE_TO_ID.value(type, "custom");
}

// 静态工具方法：字符串转因子类型
FactorMetaService::FactorType FactorMetaService::stringToFactorType(const QString& typeStr)
{
    return ID_TO_FACTOR_TYPE.value(typeStr, FactorType::CUSTOM);
}

// 静态工具方法：因子类型转显示名称
QString FactorMetaService::factorTypeToDisplayName(FactorType type)
{
    return FACTOR_TYPE_TO_DISPLAY_NAME.value(type, "自定义");
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
            FactorType factorType = stringToFactorType(categoryId);
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

QVariantMap FactorMetaService::mergeCommonAndSpecificParams(FactorType factorType)
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
