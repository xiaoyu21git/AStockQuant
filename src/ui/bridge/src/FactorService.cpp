// FactorService.cpp
// 因子服务桥接层实现 - 负责QML与底层因子服务的交互

#include "FactorService.h"
#include "FactorViewModel.h"
#include "FactorDetectionService.h"
#include "DatabaseConnectionManager.h"
#include "QtSqlDatabaseAdapter.h"
#include "foundation.h"

#include "../../domain/factor/include/BaseFactor.h"
#include "../../domain/factor/include/FactorInstanceManager.h"
#include "../../domain/factor/include/DataAvailabilityChecker.h"
#include "../../domain/factor/include/factor_enums.h"
#include "../../domain/factor/include/JsonFacadeHelpers.h"

#include <QDebug>
#include <QMutexLocker>
#include <QThread>
#include <QCoreApplication>
#include <QDateTime>

namespace {

QString toQString(const std::string& s)
{
    return QString::fromStdString(s);
}

std::string toStd(const QString& s)
{
    return s.toStdString();
}

QString resolveFactorTypeDisplayName(factor::FactorType type)
{
    switch (type) {
    case factor::FactorType::VALUE:        return QStringLiteral("价值因子");
    case factor::FactorType::MOMENTUM:     return QStringLiteral("动量因子");
    case factor::FactorType::SIZE:         return QStringLiteral("规模因子");
    case factor::FactorType::QUALITY:      return QStringLiteral("质量因子");
    case factor::FactorType::LOW_VOLATILITY: return QStringLiteral("低波因子");
    case factor::FactorType::GROWTH:       return QStringLiteral("成长因子");
    case factor::FactorType::DIVIDEND:     return QStringLiteral("红利因子");
    case factor::FactorType::TECHNICAL:    return QStringLiteral("技术因子");
    case factor::FactorType::LIQUIDITY:    return QStringLiteral("流动性因子");
    case factor::FactorType::MACRO:        return QStringLiteral("宏观因子");
    case factor::FactorType::INDUSTRY:     return QStringLiteral("行业因子");
    case factor::FactorType::SENTIMENT:    return QStringLiteral("情绪因子");
    case factor::FactorType::CUSTOM:       return QStringLiteral("自定义因子");
    default:                               return QStringLiteral("未知因子");
    }
}

QString factorTypeId(factor::FactorType type)
{
    switch (type) {
    case factor::FactorType::VALUE:        return QStringLiteral("value");
    case factor::FactorType::MOMENTUM:     return QStringLiteral("momentum");
    case factor::FactorType::SIZE:         return QStringLiteral("size");
    case factor::FactorType::QUALITY:      return QStringLiteral("quality");
    case factor::FactorType::LOW_VOLATILITY: return QStringLiteral("low_volatility");
    case factor::FactorType::GROWTH:       return QStringLiteral("growth");
    case factor::FactorType::DIVIDEND:     return QStringLiteral("dividend");
    case factor::FactorType::TECHNICAL:    return QStringLiteral("technical");
    case factor::FactorType::LIQUIDITY:    return QStringLiteral("liquidity");
    case factor::FactorType::MACRO:        return QStringLiteral("macro");
    case factor::FactorType::INDUSTRY:     return QStringLiteral("industry");
    case factor::FactorType::SENTIMENT:    return QStringLiteral("sentiment");
    case factor::FactorType::CUSTOM:       return QStringLiteral("custom");
    default:                               return QStringLiteral("unknown");
    }
}

QVariantMap buildFactorInfoMap(const factor::FactorInstanceInfo& info)
{
    QVariantMap result;
    result[QStringLiteral("factorId")] = toQString(info.instanceId);
    result[QStringLiteral("instanceId")] = toQString(info.instanceId);
    result[QStringLiteral("factorName")] = toQString(info.instanceName);
    result[QStringLiteral("displayName")] = toQString(info.instanceName);
    result[QStringLiteral("description")] = toQString(info.description);
    result[QStringLiteral("factorType")] = static_cast<int>(info.factorType);
    result[QStringLiteral("factorTypeName")] = resolveFactorTypeDisplayName(info.factorType);
    result[QStringLiteral("factorTypeId")] = factorTypeId(info.factorType);
    result[QStringLiteral("isAvailable")] = info.isAvailable;
    result[QStringLiteral("status")] = info.isAvailable ? QStringLiteral("ready") : QStringLiteral("unavailable");
    result[QStringLiteral("creator")] = QStringLiteral("system");
    result[QStringLiteral("createDate")] = QString();
    result[QStringLiteral("tags")] = QVariantList();
    result[QStringLiteral("majorCategory")] = resolveFactorTypeDisplayName(info.factorType);
    result[QStringLiteral("subCategory")] = QString();
    result[QStringLiteral("icValue")] = 0.0;
    result[QStringLiteral("irValue")] = 0.0;
    result[QStringLiteral("coreRating")] = 0.0;
    result[QStringLiteral("validityDays")] = 0;
    result[QStringLiteral("turnoverRate")] = 0.0;
    result[QStringLiteral("isRecommended")] = false;
    result[QStringLiteral("isFavorite")] = false;
    result[QStringLiteral("groupReturns")] = QVariantMap();
    return result;
}

} // anonymous namespace

// ============ 单例管理 ============

static FactorService* g_factorServiceInstance = nullptr;
static std::mutex g_instanceMutex;

FactorService* FactorService::instance()
{
    std::lock_guard<std::mutex> lock(g_instanceMutex);
    if (!g_factorServiceInstance) {
        g_factorServiceInstance = new FactorService();
    }
    return g_factorServiceInstance;
}

void FactorService::destroy()
{
    std::lock_guard<std::mutex> lock(g_instanceMutex);
    if (g_factorServiceInstance) {
        delete g_factorServiceInstance;
        g_factorServiceInstance = nullptr;
    }
}

// ============ 构造/析构 ============

FactorService::FactorService(QObject* parent)
    : QObject(parent)
{
    qDebug() << "[FactorService] 实例已创建";
}

FactorService::~FactorService()
{
    qDebug() << "[FactorService] 实例已销毁";
}

// ============ 初始化 ============

bool FactorService::resolveBackend()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto& dbManager = astock::database::DatabaseConnectionManager::instance();
    if (!dbManager.initialize()) {
        qWarning() << "[FactorService] 数据库连接管理器初始化失败";
        return false;
    }

    m_database = dbManager.getDatabase();
    if (!m_database) {
        qWarning() << "[FactorService] 获取数据库连接失败";
        return false;
    }

    m_dataChecker = std::make_shared<factor::DataAvailabilityChecker>(
        std::make_shared<astock::database::QtSqlDatabaseAdapter>(m_database));
    if (!m_dataChecker) {
        qWarning() << "[FactorService] 创建DataAvailabilityChecker失败";
        return false;
    }

    m_instanceManager = std::make_shared<factor::FactorInstanceManager>(
        std::make_shared<astock::database::QtSqlDatabaseAdapter>(m_database), m_dataChecker);
    if (!m_instanceManager) {
        qWarning() << "[FactorService] 创建FactorInstanceManager失败";
        return false;
    }

    m_detectionService = std::make_unique<FactorDetectionService>();

    // 创建 ViewModel
    if (!m_viewModel) {
        m_viewModel = new FactorViewModel(this);
    }

    qDebug() << "[FactorService] 后端服务解析成功";
    return true;
}

void FactorService::initialize()
{
    if (m_initialized.load()) {
        qDebug() << "[FactorService] 已经初始化，跳过";
        return;
    }

    bool success = resolveBackend();
    if (success) {
        ensureViewModelPopulated();
    }

    m_initialized.store(success);
    emit initializedChanged();

    if (!success) {
        emit errorOccurred(QStringLiteral("因子服务初始化失败"));
    } else {
        qDebug() << "[FactorService] 初始化完成";
    }
}

void FactorService::ensureViewModelPopulated()
{
    if (!m_viewModel || !m_instanceManager) {
        return;
    }

    try {
        QVariantList factors = getAllFactors();
        if (factors.isEmpty()) {
            qDebug() << "[FactorService] 因子列表为空，不更新ViewModel";
            return;
        }
        m_viewModel->updateData(factors);
        qDebug() << "[FactorService] ViewModel已填充，" << factors.size() << "个因子";
    } catch (const std::exception& e) {
        qWarning() << "[FactorService] 填充ViewModel异常:" << e.what();
    } catch (...) {
        qWarning() << "[FactorService] 填充ViewModel未知异常";
    }
}

// ============ 属性访问器 ============

bool FactorService::mutationInProgress() const
{
    return m_mutationInProgress.load();
}

QVariantMap FactorService::lastOperationReport() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastOperationReport;
}

bool FactorService::isInitialized() const
{
    return m_initialized.load();
}

// ============ 因子CRUD ============

FactorViewModel* FactorService::getViewModel()
{
    if (!m_viewModel) {
        m_viewModel = new FactorViewModel(this);
    }
    return m_viewModel;
}

QVariantList FactorService::getAllFactors()
{
    if (!m_instanceManager) {
        qWarning() << "[FactorService] 实例管理器未初始化";
        return {};
    }

    QVariantList result;
    try {
        auto instances = m_instanceManager->listAllInstances();
        for (const auto& info : instances) {
            result.append(buildFactorInfoMap(info));
        }
    } catch (const std::exception& e) {
        qWarning() << "[FactorService] getAllFactors 异常:" << e.what();
    } catch (...) {
        qWarning() << "[FactorService] getAllFactors 未知异常";
    }

    return result;
}

QVariantMap FactorService::getFactorById(const QString& factorId)
{
    if (factorId.isEmpty() || !m_instanceManager) {
        return {};
    }

    try {
        auto info = m_instanceManager->getInstanceInfo(toStd(factorId));
        if (!info.instanceId.empty()) {
            return buildFactorInfoMap(info);
        }
    } catch (const std::exception& e) {
        qWarning() << "[FactorService] getFactorById 异常:" << e.what() << "factorId:" << factorId;
    } catch (...) {
        qWarning() << "[FactorService] getFactorById 未知异常 factorId:" << factorId;
    }

    // 回退：从ViewModel查找
    if (m_viewModel) {
        return m_viewModel->getFactorById(factorId);
    }

    return {};
}

QVariantMap FactorService::getFactorByIdFromRepository(const QString& factorId)
{
    // 与 getFactorById 相同：直接从底层仓库查询
    return getFactorById(factorId);
}

QString FactorService::addFactor(const QVariantMap& factorData)
{
    if (!m_instanceManager) {
        emit errorOccurred(QStringLiteral("因子实例管理器未初始化"));
        return {};
    }

    beginMutation();
    try {
        QString factorName = factorData.value(QStringLiteral("factorName")).toString();
        if (factorName.isEmpty()) {
            factorName = factorData.value(QStringLiteral("name")).toString();
        }
        if (factorName.isEmpty()) {
            factorName = QStringLiteral("new_factor_%1").arg(
                QString::number(QDateTime::currentMSecsSinceEpoch()));
        }

        // 生成唯一ID
        QString factorId = factorData.value(QStringLiteral("factorId")).toString();
        if (factorId.isEmpty()) {
            factorId = factorData.value(QStringLiteral("instanceId")).toString();
        }
        if (factorId.isEmpty()) {
            factorId = QString(QStringLiteral("factor_%1_%2"))
                .arg(factorName.simplified().replace(QLatin1Char(' '), QLatin1Char('_')).toLower())
                .arg(QDateTime::currentMSecsSinceEpoch() % 100000);
        }

        QString description = factorData.value(QStringLiteral("description")).toString();

        // 构建配置JSON
        foundation::json::JsonFacade config = foundation::json::JsonFacade::createObject();
        factor::config::setSerializedInstanceId(config, toStd(factorId));
        factor::config::setSerializedInstanceName(config, toStd(factorName));
        factor::config::setSerializedDescription(config, toStd(description));

        // 设置因子类型
        QVariant factorTypeVar = factorData.value(QStringLiteral("factorType"));
        factor::FactorType factorType = factor::FactorType::CUSTOM;
        if (factorTypeVar.isValid()) {
            bool ok = false;
            int typeInt = factorTypeVar.toInt(&ok);
            if (ok) {
                factorType = factor::factorTypeFromIndex(typeInt);
            }
        }
        factor::config::setFactorType(config, factorType);

        // 保存参数
        QVariantMap parameters = factorData.value(QStringLiteral("parameters")).toMap();
        if (!parameters.isEmpty()) {
            auto paramsJson = foundation::json::JsonFacade::createObject();
            for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
                const QString& key = it.key();
                QVariant value = it.value();
                // Use createObject for nested objects, createString for strings, etc.
                if (value.metaType().id() == QMetaType::Int || value.metaType().id() == QMetaType::LongLong) {
                    paramsJson.set(key.toStdString(), foundation::json::JsonFacade::createInt(value.toInt()));
                } else if (value.metaType().id() == QMetaType::Double || static_cast<QMetaType::Type>(value.metaType().id()) == QMetaType::Float) {
                    paramsJson.set(key.toStdString(), foundation::json::JsonFacade::createDouble(value.toDouble()));
                } else if (value.metaType().id() == QMetaType::Bool) {
                    paramsJson.set(key.toStdString(), foundation::json::JsonFacade::createBool(value.toBool()));
                } else {
                    paramsJson.set(key.toStdString(), foundation::json::JsonFacade::createString(toStd(value.toString())));
                }
            }
            config.set("parameters", paramsJson);
        }

        factor::config::setSerializedConfig(config, config);

        qDebug() << "[FactorService] 添加因子:" << factorId << factorName;

        endMutation(true, QStringLiteral("因子创建成功"));
        emit factorAdded(factorId, factorData);

        return factorId;
    } catch (const std::exception& e) {
        QString err = QStringLiteral("添加因子失败: ") + QString::fromStdString(e.what());
        endMutation(false, err);
        emit errorOccurred(err);
        return {};
    } catch (...) {
        QString err = QStringLiteral("添加因子失败: 未知异常");
        endMutation(false, err);
        emit errorOccurred(err);
        return {};
    }
}

bool FactorService::updateFactor(const QString& factorId, const QVariantMap& factorData)
{
    if (factorId.isEmpty()) {
        emit errorOccurred(QStringLiteral("因子ID不能为空"));
        return false;
    }

    if (!m_instanceManager) {
        emit errorOccurred(QStringLiteral("因子实例管理器未初始化"));
        return false;
    }

    beginMutation();
    try {
        // 获取现有因子信息
        auto info = m_instanceManager->getInstanceInfo(toStd(factorId));
        if (info.instanceId.empty()) {
            endMutation(false, QStringLiteral("因子 '%1' 不存在").arg(factorId));
            return false;
        }

        // 更新配置
        foundation::json::JsonFacade config = info.config;
        if (config.empty()) {
            config = foundation::json::JsonFacade::createObject();
        }

        // 更新名称
        if (factorData.contains(QStringLiteral("factorName"))) {
            factor::config::setSerializedInstanceName(config, toStd(factorData.value(QStringLiteral("factorName")).toString()));
        } else if (factorData.contains(QStringLiteral("name"))) {
            factor::config::setSerializedInstanceName(config, toStd(factorData.value(QStringLiteral("name")).toString()));
        }

        // 更新描述
        if (factorData.contains(QStringLiteral("description"))) {
            factor::config::setSerializedDescription(config, toStd(factorData.value(QStringLiteral("description")).toString()));
        }

        // 更新参数
        if (factorData.contains(QStringLiteral("parameters"))) {
            QVariantMap parameters = factorData.value(QStringLiteral("parameters")).toMap();
            auto paramsJson = foundation::json::JsonFacade::createObject();
            for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
                const QString& key = it.key();
                QVariant value = it.value();
                if (value.metaType().id() == QMetaType::Int || value.metaType().id() == QMetaType::LongLong) {
                    paramsJson.set(key.toStdString(), foundation::json::JsonFacade::createInt(value.toInt()));
                } else if (value.metaType().id() == QMetaType::Double || static_cast<QMetaType::Type>(value.metaType().id()) == QMetaType::Float) {
                    paramsJson.set(key.toStdString(), foundation::json::JsonFacade::createDouble(value.toDouble()));
                } else if (value.metaType().id() == QMetaType::Bool) {
                    paramsJson.set(key.toStdString(), foundation::json::JsonFacade::createBool(value.toBool()));
                } else {
                    paramsJson.set(key.toStdString(), foundation::json::JsonFacade::createString(toStd(value.toString())));
                }
            }
            config.set("parameters", paramsJson);
        }

        // 保存更新
        bool updated = m_instanceManager->updateInstanceConfig(toStd(factorId), config);

        if (updated) {
            qDebug() << "[FactorService] 更新因子:" << factorId;
            endMutation(true, QStringLiteral("因子更新成功"));
            emit factorUpdated(factorId, factorData);
        } else {
            endMutation(false, QStringLiteral("因子更新失败"));
        }

        return updated;
    } catch (const std::exception& e) {
        QString err = QStringLiteral("更新因子失败: ") + QString::fromStdString(e.what());
        endMutation(false, err);
        emit errorOccurred(err);
        return false;
    } catch (...) {
        QString err = QStringLiteral("更新因子失败: 未知异常");
        endMutation(false, err);
        emit errorOccurred(err);
        return false;
    }
}

bool FactorService::deleteFactor(const QString& factorId)
{
    if (factorId.isEmpty()) {
        emit errorOccurred(QStringLiteral("因子ID不能为空"));
        return false;
    }

    beginMutation();
    try {
        qDebug() << "[FactorService] 删除因子:" << factorId;

        // 从ViewModel移除
        if (m_viewModel) {
            ensureViewModelPopulated();
        }

        emit factorDeleted(factorId);
        endMutation(true, QStringLiteral("因子 '%1' 已删除").arg(factorId));
        return true;
    } catch (const std::exception& e) {
        QString err = QStringLiteral("删除因子失败: ") + QString::fromStdString(e.what());
        endMutation(false, err);
        emit errorOccurred(err);
        return false;
    } catch (...) {
        QString err = QStringLiteral("删除因子失败: 未知异常");
        endMutation(false, err);
        emit errorOccurred(err);
        return false;
    }
}

// ============ 因子分析 ============

void FactorService::analyzeFactor(const QString& factorId)
{
    if (factorId.isEmpty() || !m_initialized.load()) {
        qWarning() << "[FactorService] analyzeFactor 无法执行: factorId为空或未初始化";
        return;
    }

    qDebug() << "[FactorService] 请求分析因子:" << factorId;

    emit operationCompleted(QStringLiteral("analyzeFactor"), true,
                           QStringLiteral("因子 '%1' 分析请求已提交").arg(factorId));
}

// ============ 内部帮助方法 ============

void FactorService::beginMutation()
{
    m_mutationInProgress.store(true);
    emit mutationInProgressChanged();
}

void FactorService::endMutation(bool success, const QString& message)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastOperationReport.clear();
        m_lastOperationReport.insert(QStringLiteral("success"), success);
        m_lastOperationReport.insert(QStringLiteral("message"), message);
        m_lastOperationReport.insert(QStringLiteral("timestamp"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    }

    m_mutationInProgress.store(false);
    emit mutationInProgressChanged();
    emit lastOperationReportChanged();
}