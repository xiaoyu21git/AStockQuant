// FactorParamController.cpp
// 因子参数控制器实现 - 管理因子参数配置和C++后端交互
#include "FactorParamController.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <memory>
#include <stdexcept> //

#include "foundation/json/json_facade.h"
#include "../../domain/model/include/Factor.h"

using namespace AStockQuantEngine::Domain::Model;

// 静态成员初始化
const std::map<FactorParamController::FactorTypeUI, std::string> FactorParamController::FACTOR_TYPE_MAPPING = {
    {FactorParamController::FactorTypeUI::MOMENTUM, "momentum"},
    {FactorParamController::FactorTypeUI::VALUE, "value"},
    {FactorParamController::FactorTypeUI::QUALITY, "quality"},
    {FactorParamController::FactorTypeUI::GROWTH, "growth"},
    {FactorParamController::FactorTypeUI::TECHNICAL, "technical"},
    {FactorParamController::FactorTypeUI::SENTIMENT, "sentiment"},
    {FactorParamController::FactorTypeUI::RISK, "risk"},
    {FactorParamController::FactorTypeUI::MACRO, "macro"},
    {FactorParamController::FactorTypeUI::CUSTOM, "custom"}
};

const std::map<std::string, FactorParamController::FactorTypeUI> FactorParamController::STRING_TO_FACTOR_TYPE = {
    {"momentum", FactorParamController::FactorTypeUI::MOMENTUM},
    {"value", FactorParamController::FactorTypeUI::VALUE},
    {"quality", FactorParamController::FactorTypeUI::QUALITY},
    {"growth", FactorParamController::FactorTypeUI::GROWTH},
    {"technical", FactorParamController::FactorTypeUI::TECHNICAL},
    {"sentiment", FactorParamController::FactorTypeUI::SENTIMENT},
    {"risk", FactorParamController::FactorTypeUI::RISK},
    {"macro", FactorParamController::FactorTypeUI::MACRO},
    {"custom", FactorParamController::FactorTypeUI::CUSTOM}
};

// 辅助函数：FactorParamType转字符串
static QString factorParamTypeToString(FactorParamType type) {
    switch (type) {
        case FactorParamType::INTEGER: return "int";
        case FactorParamType::FLOAT: return "double";
        case FactorParamType::BOOLEAN: return "bool";
        case FactorParamType::ENUM: return "enum";
        case FactorParamType::STRING: return "string";
        default: return "string";
    }
}

// 构造函数
FactorParamController::FactorParamController(QObject* parent) : QObject(parent)
{
    // 初始化可用因子类型列表
    m_availableFactorTypes = {
        factorTypeToString(FactorTypeUI::MOMENTUM),
        factorTypeToString(FactorTypeUI::VALUE),
        factorTypeToString(FactorTypeUI::QUALITY),
        factorTypeToString(FactorTypeUI::GROWTH),
        factorTypeToString(FactorTypeUI::TECHNICAL),
        factorTypeToString(FactorTypeUI::SENTIMENT),
        factorTypeToString(FactorTypeUI::RISK),
        factorTypeToString(FactorTypeUI::MACRO),
        factorTypeToString(FactorTypeUI::CUSTOM)
    };
    
    qDebug() << "FactorParamController created";
}

// 析构函数
FactorParamController::~FactorParamController()
{
    cleanup();
    qDebug() << "FactorParamController destroyed";
}

// 初始化控制器
void FactorParamController::initialize()
{
    QMutexLocker locker(&m_mutex);
    
    if (!initializeBackend()) {
        updateError("Failed to initialize C++ backend");
        return;
    }
    
    qDebug() << "FactorParamController initialized";
}

// 加载指定因子类型的参数定义
void FactorParamController::loadFactorTypeParameters(FactorTypeUI factorType)
{
    QMutexLocker locker(&m_mutex);
    
    m_currentFactorType = factorType;
    emit currentFactorTypeChanged();
    
    loadParameterDefinitionsFromBackend(factorType);
    
    // 加载默认参数值
    m_parameterValues = getDefaultParameters(factorType);
    emit parameterValuesChanged();
    
    // 验证参数
    validateParameters();
    
    qDebug() << "Loaded parameters for factor type:" << factorTypeToString(factorType);
    emit parametersLoaded(factorType);
}

// 设置参数值
void FactorParamController::setParameterValue(const QString& paramName, const QVariant& value)
{
    QMutexLocker locker(&m_mutex);
    
    if (paramName.isEmpty()) {
        updateError("Parameter name cannot be empty");
        return;
    }
    
    m_parameterValues[paramName] = value;
    emit parameterValuesChanged();
    
    // 自动验证参数
    validateParameters();
    
    qDebug() << "Set parameter" << paramName << "=" << value;
}

// 获取参数值
QVariant FactorParamController::getParameterValue(const QString& paramName) const
{
    QMutexLocker locker(const_cast<QMutex*>(&m_mutex));
    
    if (m_parameterValues.contains(paramName)) {
        return m_parameterValues[paramName];
    }
    
    return QVariant();
}

// 验证所有参数
void FactorParamController::validateParameters()
{
    QMutexLocker locker(&m_mutex);
    
    bool allValid = true;
    QStringList validationErrors;
    
    // 检查因子名称
    if (m_currentFactorName.isEmpty()) {
        validationErrors << "因子名称不能为空";
        allValid = false;
    }
    
    // 验证每个参数
    for (size_t i = 0; i < m_parameterDefinitions.size(); ++i) {
        QVariantMap paramDef = m_parameterDefinitions[i].toMap();
        QString paramName = paramDef["name"].toString();
        
        if (m_parameterValues.contains(paramName)) {
            if (!validateParameter(paramName, m_parameterValues[paramName], paramDef)) {
                QString displayName = paramDef["displayName"].toString();
                validationErrors << QString("%1 参数值无效").arg(displayName);
                allValid = false;
            }
        } else if (paramDef.contains("defaultValue")) {
            // 使用默认值
            m_parameterValues[paramName] = paramDef["defaultValue"];
        } else {
            QString displayName = paramDef["displayName"].toString();
            validationErrors << QString("%1 参数不能为空").arg(displayName);
            allValid = false;
        }
    }
    
    m_parametersValid = allValid;
    m_validationMessage = allValid ? "参数验证通过" : validationErrors.join("; ");
    
    emit parametersValidChanged();
    emit validationMessageChanged();
    
    if (!allValid) {
        qDebug() << "Parameter validation failed:" << m_validationMessage;
    }
}

// 创建因子
bool FactorParamController::createFactor()
{
    QMutexLocker locker(&m_mutex);
    
    // 验证参数
    validateParameters();
    if (!m_parametersValid) {
        emit factorCreated(false, m_validationMessage, "");
        return false;
    }
    
    try {
        // 创建C++因子对象
        auto factor = createBackendFactor();
        if (!factor) {
            emit factorCreated(false, "创建因子对象失败", "");
            return false;
        }
        
        // 设置参数值
        const auto& params = factor->getParams();
        for (size_t i = 0; i < params.size(); ++i) {
            const auto& param = params[i];
            QString paramName = QString::fromStdString(param.name);
            if (m_parameterValues.contains(paramName)) {
                // 将QVariant转换为JsonFacade
                std::string jsonValue = convertVariantToJsonValue(m_parameterValues[paramName], factorParamTypeToString(param.type));
                // 使用parse而不是fromString
                factor->setParamValue(param.name, foundation::json::JsonFacade::parse(jsonValue));
            }
        }
        
        // 生成因子ID
        QString factorId = QString("%1_%2").arg(factorTypeToString(m_currentFactorType)).arg(QDateTime::currentDateTime().toString("yyyyMMddhhmmss"));
        
        qDebug() << "Factor created successfully:" << m_currentFactorName << "ID:" << factorId;
        emit factorCreated(true, "因子创建成功", factorId);
        return true;
        
    } catch (const std::exception& e) {
        QString errorMsg = QString("创建因子时发生错误: %1").arg(e.what());
        updateError(errorMsg);
        emit factorCreated(false, errorMsg, "");
        return false;
    }
}

// 重置参数
void FactorParamController::resetParameters()
{
    QMutexLocker locker(&m_mutex);
    
    m_currentFactorName.clear();
    m_parameterValues.clear();
    m_parametersValid = false;
    m_validationMessage = "请设置参数";
    
    emit currentFactorNameChanged();
    emit parameterValuesChanged();
    emit parametersValidChanged();
    emit validationMessageChanged();
    
    qDebug() << "Parameters reset";
}

// 获取默认参数
QVariantMap FactorParamController::getDefaultParameters(FactorTypeUI factorType)
{
    QVariantMap defaults;
    
    // 根据因子类型设置默认参数
    switch (factorType) {
        case FactorTypeUI::MOMENTUM:
            defaults["window"] = 20;
            defaults["type"] = "return";
            defaults["smoothing"] = true;
            defaults["minMomentum"] = 0.0;
            break;
            
        case FactorTypeUI::VALUE:
            defaults["valuationType"] = "pe";
            defaults["usePercentile"] = true;
            defaults["industryNeutral"] = true;
            break;
            
        case FactorTypeUI::QUALITY:
            defaults["metric"] = "roe";
            defaults["timeframe"] = "annual";
            defaults["qualityThreshold"] = 0.10;
            break;
            
        case FactorTypeUI::GROWTH:
            defaults["growthType"] = "revenue";
            defaults["period"] = 3;
            defaults["growthMetric"] = "cagr";
            break;
            
        case FactorTypeUI::TECHNICAL:
            defaults["indicator"] = "ma";
            defaults["period"] = 20;
            defaults["signalLine"] = 9;
            break;
            
        case FactorTypeUI::SENTIMENT:
            defaults["sentimentSource"] = "news";
            defaults["lookbackDays"] = 30;
            defaults["sentimentMetric"] = "score";
            break;
            
        case FactorTypeUI::RISK:
            defaults["riskMeasure"] = "volatility";
            defaults["horizon"] = 30;
            defaults["confidenceLevel"] = 0.95;
            break;
            
        case FactorTypeUI::MACRO:
            defaults["macroVariable"] = "gdp";
            defaults["lag"] = 3;
            defaults["transformation"] = "yoy";
            break;
            
        case FactorTypeUI::CUSTOM:
            defaults["expression"] = "";
            defaults["variables"] = "";
            defaults["validation"] = true;
            break;
    }
    
    return defaults;
}

// 静态工具方法：因子类型转字符串
QString FactorParamController::factorTypeToString(FactorTypeUI type)
{
    switch (type) {
        case FactorTypeUI::MOMENTUM: return "动量类";
        case FactorTypeUI::VALUE: return "价值类";
        case FactorTypeUI::QUALITY: return "质量类";
        case FactorTypeUI::GROWTH: return "成长类";
        case FactorTypeUI::TECHNICAL: return "技术指标";
        case FactorTypeUI::SENTIMENT: return "情绪类";
        case FactorTypeUI::RISK: return "风险类";
        case FactorTypeUI::MACRO: return "宏观类";
        case FactorTypeUI::CUSTOM: return "自定义";
        default: return "未知类型";
    }
}

// 静态工具方法：字符串转因子类型
FactorParamController::FactorTypeUI FactorParamController::stringToFactorType(const QString& typeStr)
{
    if (typeStr == "动量类") return FactorTypeUI::MOMENTUM;
    if (typeStr == "价值类") return FactorTypeUI::VALUE;
    if (typeStr == "质量类") return FactorTypeUI::QUALITY;
    if (typeStr == "成长类") return FactorTypeUI::GROWTH;
    if (typeStr == "技术指标") return FactorTypeUI::TECHNICAL;
    if (typeStr == "情绪类") return FactorTypeUI::SENTIMENT;
    if (typeStr == "风险类") return FactorTypeUI::RISK;
    if (typeStr == "宏观类") return FactorTypeUI::MACRO;
    if (typeStr == "自定义") return FactorTypeUI::CUSTOM;
    
    // 尝试英文名称
    auto it = STRING_TO_FACTOR_TYPE.find(typeStr.toStdString());
    if (it != STRING_TO_FACTOR_TYPE.end()) {
        return it->second;
    }
    
    return FactorTypeUI::MOMENTUM; // 默认值
}

// 静态工具方法：获取参数名称列表
QStringList FactorParamController::getParameterNames(FactorTypeUI factorType)
{
    QStringList names;
    
    // 根据因子类型返回参数名称
    switch (factorType) {
        case FactorTypeUI::MOMENTUM:
            names << "window" << "type" << "smoothing" << "minMomentum";
            break;
            
        case FactorTypeUI::VALUE:
            names << "valuationType" << "usePercentile" << "industryNeutral";
            break;
            
        case FactorTypeUI::QUALITY:
            names << "metric" << "timeframe" << "qualityThreshold";
            break;
            
        case FactorTypeUI::GROWTH:
            names << "growthType" << "period" << "growthMetric";
            break;
            
        case FactorTypeUI::TECHNICAL:
            names << "indicator" << "period" << "signalLine";
            break;
            
        case FactorTypeUI::SENTIMENT:
            names << "sentimentSource" << "lookbackDays" << "sentimentMetric";
            break;
            
        case FactorTypeUI::RISK:
            names << "riskMeasure" << "horizon" << "confidenceLevel";
            break;
            
        case FactorTypeUI::MACRO:
            names << "macroVariable" << "lag" << "transformation";
            break;
            
        case FactorTypeUI::CUSTOM:
            names << "expression" << "variables" << "validation";
            break;
    }
    
    return names;
}

// 属性访问器
QString FactorParamController::currentFactorName() const
{
    QMutexLocker locker(const_cast<QMutex*>(&m_mutex));
    return m_currentFactorName;
}

void FactorParamController::setCurrentFactorName(const QString& name)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_currentFactorName != name) {
        m_currentFactorName = name;
        emit currentFactorNameChanged();
        validateParameters();
    }
}

FactorParamController::FactorTypeUI FactorParamController::currentFactorType() const
{
    QMutexLocker locker(const_cast<QMutex*>(&m_mutex));
    return m_currentFactorType;
}

void FactorParamController::setCurrentFactorType(FactorTypeUI type)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_currentFactorType != type) {
        m_currentFactorType = type;
        emit currentFactorTypeChanged();
        loadFactorTypeParameters(type);
    }
}

QVariantList FactorParamController::parameterDefinitions() const
{
    QMutexLocker locker(const_cast<QMutex*>(&m_mutex));
    return m_parameterDefinitions;
}

QVariantMap FactorParamController::parameterValues() const
{
    QMutexLocker locker(const_cast<QMutex*>(&m_mutex));
    return m_parameterValues;
}

void FactorParamController::setParameterValues(const QVariantMap& values)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_parameterValues != values) {
        m_parameterValues = values;
        emit parameterValuesChanged();
        validateParameters();
    }
}

bool FactorParamController::parametersValid() const
{
    QMutexLocker locker(const_cast<QMutex*>(&m_mutex));
    return m_parametersValid;
}

QString FactorParamController::validationMessage() const
{
    QMutexLocker locker(const_cast<QMutex*>(&m_mutex));
    return m_validationMessage;
}

QStringList FactorParamController::availableFactorTypes() const
{
    QMutexLocker locker(const_cast<QMutex*>(&m_mutex));
    return m_availableFactorTypes;
}

// 私有方法实现
bool FactorParamController::initializeBackend()
{
    try {
        // 初始化因子工厂
        m_factorFactory = std::make_shared<FactorFactory>();
        
        // 预创建各种类型的因子原型
        auto factorTypes = FactorFactory::getAvailableFactorTypes();
        for (size_t i = 0; i < factorTypes.size(); ++i) {
            const auto& typeName = factorTypes[i];
            auto it = STRING_TO_FACTOR_TYPE.find(typeName);
            if (it != STRING_TO_FACTOR_TYPE.end()) {
                FactorTypeUI uiType = it->second;
                auto factor = FactorFactory::createFactor(typeName, "prototype");
                if (factor) {
                    m_factorPrototypes[uiType] = factor;
                }
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        qDebug() << "Failed to initialize backend:" << e.what();
        return false;
    }
}

void FactorParamController::loadParameterDefinitionsFromBackend(FactorTypeUI factorType)
{
    m_parameterDefinitions.clear();
    
    auto it = m_factorPrototypes.find(factorType);
    if (it == m_factorPrototypes.end()) {
        // 尝试从工厂创建新实例
        auto typeMappingIt = FACTOR_TYPE_MAPPING.find(factorType);
        if (typeMappingIt != FACTOR_TYPE_MAPPING.end()) {
            try {
                auto factor = FactorFactory::createFactor(typeMappingIt->second, "temp");
                if (factor) {
                    m_factorPrototypes[factorType] = factor;
                    it = m_factorPrototypes.find(factorType);
                }
            } catch (...) {
                // 创建失败，使用模拟数据
            }
        }
    }
    
    if (it != m_factorPrototypes.end()) {
        // 从C++因子对象加载参数定义
        const auto& params = it->second->getParams();
        for (size_t i = 0; i < params.size(); ++i) {
            const auto& param = params[i];
            m_parameterDefinitions.append(convertParamToVariantMap(param));
        }
    } else {
        // C++后端不可用，抛出错误而不是使用模拟数据
        QString errorMsg = QString("C++后端不可用，无法加载%1因子的参数定义").arg(factorTypeToString(factorType));
        qWarning() << errorMsg;
        updateError(errorMsg);
        return;
    }
    
    emit parameterDefinitionsChanged();
}

bool FactorParamController::validateParameter(const QString& paramName, const QVariant& value, const QVariantMap& paramDef)
{
    QString type = paramDef["type"].toString();
    
    if (type == "int") {
        bool ok;
        int intValue = value.toInt(&ok);
        if (!ok) return false;
        
        if (paramDef.contains("minValue") && intValue < paramDef["minValue"].toInt()) return false;
        if (paramDef.contains("maxValue") && intValue > paramDef["maxValue"].toInt()) return false;
        
    } else if (type == "double") {
        bool ok;
        double doubleValue = value.toDouble(&ok);
        if (!ok) return false;
        
        if (paramDef.contains("minValue") && doubleValue < paramDef["minValue"].toDouble()) return false;
        if (paramDef.contains("maxValue") && doubleValue > paramDef["maxValue"].toDouble()) return false;
        
    } else if (type == "bool") {
        if (!value.canConvert<bool>()) return false;
        
    } else if (type == "string") {
        if (!value.canConvert<QString>()) return false;
        QString strValue = value.toString();
        if (strValue.isEmpty()) return false;
    }
    
    return true;
}

QVariantMap FactorParamController::convertParamToVariantMap(const FactorParam& param)
{
    QVariantMap paramMap;
    
    paramMap["name"] = QString::fromStdString(param.name);
    paramMap["displayName"] = QString::fromStdString(param.displayName);
    paramMap["description"] = QString::fromStdString(param.description);
    
    // 类型转换
    QString typeStr = factorParamTypeToString(param.type);
    paramMap["type"] = typeStr;
    
    // 默认值
    if (!param.defaultValue.isNull()) {
        paramMap["defaultValue"] = convertJsonValueToVariant(param.defaultValue.toString(), typeStr);
    }
    
    // 范围值
    if (!param.minValue.isNull()) {
        paramMap["minValue"] = convertJsonValueToVariant(param.minValue.toString(), typeStr);
    }
    
    if (!param.maxValue.isNull()) {
        paramMap["maxValue"] = convertJsonValueToVariant(param.maxValue.toString(), typeStr);
    }
    
    if (!param.stepValue.isNull()) {
        paramMap["stepValue"] = convertJsonValueToVariant(param.stepValue.toString(), typeStr);
    }
    
    // 枚举值
    if (!param.commonValues.empty()) {
        QVariantList enumValues;
        for (size_t i = 0; i < param.commonValues.size(); ++i) {
            const auto& enumValue = param.commonValues[i];
            enumValues.append(convertJsonValueToVariant(enumValue.toString(), typeStr));
        }
        paramMap["enumValues"] = enumValues;
    }
    
    return paramMap;
}

QVariant FactorParamController::convertJsonValueToVariant(const std::string& jsonValue, const QString& paramType)
{
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(jsonValue));
    if (doc.isNull()) {
        // 尝试直接解析
        QString strValue = QString::fromStdString(jsonValue);
        
        if (paramType == "int") {
            bool ok;
            int intValue = strValue.toInt(&ok);
            return ok ? QVariant(intValue) : QVariant(0);
        } else if (paramType == "double") {
            bool ok;
            double doubleValue = strValue.toDouble(&ok);
            return ok ? QVariant(doubleValue) : QVariant(0.0);
        } else if (paramType == "bool") {
            return QVariant(strValue.toLower() == "true" || strValue == "1");
        } else {
            return QVariant(strValue);
        }
    }
    
    if (doc.isObject()) {
        return doc.object().toVariantMap();
    } else if (doc.isArray()) {
        return doc.array().toVariantList();
    } else {
        return doc.toVariant();
    }
}

std::string FactorParamController::convertVariantToJsonValue(const QVariant& value, const QString& paramType)
{
    if (paramType == "int" || paramType == "double") {
        return value.toString().toStdString();
    } else if (paramType == "bool") {
        return value.toBool() ? "true" : "false";
    } else {
        return value.toString().toStdString();
    }
}

std::shared_ptr<Factor> FactorParamController::createBackendFactor()
{
    auto typeMappingIt = FACTOR_TYPE_MAPPING.find(m_currentFactorType);
    if (typeMappingIt == FACTOR_TYPE_MAPPING.end()) {
        throw std::runtime_error("Invalid factor type");
    }
    
    auto factor = FactorFactory::createFactor(typeMappingIt->second, m_currentFactorName.toStdString());
    if (!factor) {
        throw std::runtime_error("Failed to create factor object");
    }
    
    // 设置描述
    QString description = QString("通过参数配置界面创建的%1因子").arg(factorTypeToString(m_currentFactorType));
    factor->setDescription(description.toStdString());
    
    return factor;
}

void FactorParamController::updateError(const QString& error)
{
    m_validationMessage = error;
    m_parametersValid = false;
    
    emit validationMessageChanged();
    emit parametersValidChanged();
    emit errorOccurred(error);
}

void FactorParamController::cleanup()
{
    m_factorPrototypes.clear();
    m_factorFactory.reset();
}