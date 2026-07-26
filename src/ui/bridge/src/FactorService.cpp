// FactorService.cpp
// 因子服务桥接层实现 - 负责QML与底层因子服务的交互

#include "FactorService.h"
#include "FactorViewModel.h"
#include "FactorDetectionService.h"
#include "database/NativePgConnectionPool.h"
#include "foundation.h"
#include "foundation/thread/ThreadPoolExecutor.h"

#include "../../domain/factor/include/BaseFactor.h"
#include "../../domain/factor/include/FactorInstanceManager.h"
#include "../../domain/factor/include/DataAvailabilityChecker.h"
#include "../../domain/factor/include/FactorConfigAccess.h"
#include "../../domain/factor/include/factor_enums.h"
#include "../../domain/factor/include/JsonFacadeHelpers.h"

#include <QMutexLocker>
#include <QThread>
#include <QCoreApplication>
#include <QDateTime>
#include <fstream>
#include <ctime>

namespace {
void fsDiag(const std::string& msg) {
    std::ofstream ofs("factor_resolve_diag.log", std::ios::app);
    if (ofs.is_open()) {
        std::time_t now = std::time(nullptr);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&now));
        ofs << "[" << buf << "] " << msg << std::endl;
    }
}
}

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
    case factor::FactorType::COMPOSITE:    return QStringLiteral("组合因子");
    case factor::FactorType::REVERSAL:     return QStringLiteral("反转因子");
    case factor::FactorType::HIGH_FREQ:    return QStringLiteral("高频因子");
    case factor::FactorType::DL:           return QStringLiteral("AI因子");
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
    case factor::FactorType::COMPOSITE:    return QStringLiteral("composite");
    case factor::FactorType::REVERSAL:     return QStringLiteral("reversal");
    case factor::FactorType::HIGH_FREQ:    return QStringLiteral("high_freq");
    case factor::FactorType::DL:           return QStringLiteral("dl");
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

    // ── 回测指标 (从 full_config.backtest_metrics 提取) ──
    if (info.config.has("backtest_metrics")) {
        auto bt = info.config.get("backtest_metrics");
        if (bt.has("icValue"))            result[QStringLiteral("icValue")]            = bt.get("icValue").asDouble();
        if (bt.has("irValue"))            result[QStringLiteral("irValue")]            = bt.get("irValue").asDouble();
        if (bt.has("coreRating"))         result[QStringLiteral("coreRating")]         = bt.get("coreRating").asInt();
        if (bt.has("coreRatingLabel"))    result[QStringLiteral("coreRatingLabel")]    = toQString(bt.get("coreRatingLabel").asString());
        if (bt.has("turnoverRate"))       result[QStringLiteral("turnoverRate")]       = bt.get("turnoverRate").asDouble();
        if (bt.has("effectiveStartDate")) result[QStringLiteral("backtestStartDate")]  = toQString(bt.get("effectiveStartDate").asString());
        if (bt.has("effectiveEndDate"))   result[QStringLiteral("backtestEndDate")]    = toQString(bt.get("effectiveEndDate").asString());
    } else {
        result[QStringLiteral("icValue")]     = 0.0;
        result[QStringLiteral("irValue")]     = 0.0;
        result[QStringLiteral("coreRating")]  = 0.0;
        result[QStringLiteral("turnoverRate")]= 0.0;
    }
    result[QStringLiteral("majorCategory")] = resolveFactorTypeDisplayName(info.factorType);
    result[QStringLiteral("subCategory")] = QString();
    result[QStringLiteral("validityDays")] = 0;
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
        g_factorServiceInstance->initialize();
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
    // FactorInstanceManager 在 initialize() → resolveBackend() 中延迟创建
}

FactorService::~FactorService()
{
    INTERNAL_DEBUG_STREAM << "[FactorService] 实例已销毁";
}

// ============ 初始化 ============

bool FactorService::resolveBackend()
{
    try {
        INTERNAL_INFO_STREAM << "[FS] resolveBackend START";
        std::lock_guard<std::mutex> lock(m_mutex);

        // 统一使用 NativePgConnectionPool（基础设施层唯一 DB 连接入口）
        auto nativeDb = astock::database::NativePgConnectionPool::instance().getConnection();
        if (!nativeDb) {
            INTERNAL_ERROR_STREAM << "[FS] nativeDb=null";
            emit errorOccurred(QStringLiteral("FactorService: 数据库连接池初始化失败"));
            return false;
        }
        INTERNAL_INFO_STREAM << "[FS] nativeDb OK";

        m_dataChecker = std::make_shared<factor::DataAvailabilityChecker>(nativeDb);
        INTERNAL_INFO_STREAM << "[FS] DataAvailabilityChecker OK";

        INTERNAL_INFO_STREAM << "[FS] creating FIM...";
        m_instanceManager.reset(new factor::FactorInstanceManager(nativeDb, m_dataChecker));
        INTERNAL_INFO_STREAM << "[FS] FactorInstanceManager created";

        m_detectionService = std::make_unique<FactorDetectionService>();
        INTERNAL_INFO_STREAM << "[FS] DetectionService OK";

        if (!m_viewModel) {
            m_viewModel = new FactorViewModel(this);
        }
        INTERNAL_INFO_STREAM << "[FS] ViewModel OK";

        INTERNAL_INFO_STREAM << "[FS] resolveBackend COMPLETE";
        return true;
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[FS] EXCEPTION: " << e.what();
        emit errorOccurred(QStringLiteral("FactorService 异常: %1").arg(QString::fromStdString(e.what())));
        return false;
    } catch (...) {
        INTERNAL_ERROR_STREAM << "[FS] UNKNOWN EXCEPTION crash";
        emit errorOccurred(QStringLiteral("FactorService 未知异常"));
        return false;
    }
}

void FactorService::initialize()
{
    if (m_initialized.load()) {
        INTERNAL_DEBUG_STREAM << "[FactorService] 已经初始化，跳过";
        return;
    }

    bool success = resolveBackend();
    // ViewModel 惰性填充 — QML 因子选择器打开时才加载, 启动时不预加载全部因子

    m_initialized.store(success);
    emit initializedChanged();

    if (!success) {
        emit errorOccurred(QStringLiteral("因子服务初始化失败"));
    } else {
        INTERNAL_DEBUG_STREAM << "[FactorService] 初始化完成";
    }
}

void FactorService::ensureViewModelPopulated()
{
    if (!m_viewModel || !m_instanceManager) {
        return;
    }

    // 防重入：beginResetModel() 期间若 GridView 回调触发了再次 getViewModel()，
    // m_viewModelPopulating 为 true 时直接跳过，避免 beginResetModel/endResetModel 嵌套崩溃
    bool expected = false;
    if (!m_viewModelPopulating.compare_exchange_strong(expected, true)) {
        INTERNAL_DEBUG_STREAM << "[FactorService] ViewModel 填充进行中，跳过重入";
        return;
    }

    try {
        QVariantList factors = getAllFactors();
        if (factors.isEmpty()) {
            INTERNAL_DEBUG_STREAM << "[FactorService] 因子列表为空，不更新ViewModel";
            m_viewModelPopulating.store(false);
            return;
        }
        m_viewModel->updateData(factors);
        INTERNAL_DEBUG_STREAM << "[FactorService] ViewModel已填充，" << factors.size() << "个因子";
    } catch (const std::exception& e) {
        INTERNAL_WARN_STREAM << "[FactorService] 填充ViewModel异常:" << e.what();
    } catch (...) {
        INTERNAL_WARN_STREAM << "[FactorService] 填充ViewModel未知异常";
    }

    m_viewModelPopulating.store(false);
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
    ensureViewModelPopulated();
    return m_viewModel;
}

QVariantList FactorService::getAllFactors()
{
    if (!m_instanceManager) {
        INTERNAL_WARN_STREAM << "[FactorService] 实例管理器未初始化";
        return {};
    }

    QVariantList result;
    try {
        auto instances = m_instanceManager->listAllInstances();
        for (const auto& info : instances) {
            result.append(buildFactorInfoMap(info));
        }
    } catch (const std::exception& e) {
        INTERNAL_WARN_STREAM << "[FactorService] getAllFactors 异常:" << e.what();
    } catch (...) {
        INTERNAL_WARN_STREAM << "[FactorService] getAllFactors 未知异常";
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
        INTERNAL_WARN_STREAM << "[FactorService] getFactorById 异常:" << e.what() << "factorId:" << factorId.toStdString();
    } catch (...) {
        INTERNAL_WARN_STREAM << "[FactorService] getFactorById 未知异常 factorId:" << factorId.toStdString();
    }

    if (m_viewModel) {
        return m_viewModel->getFactorById(factorId);
    }

    return {};
}

QVariantMap FactorService::getFactorByIdFromRepository(const QString& factorId)
{
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

        foundation::json::JsonFacade config = foundation::json::JsonFacade::createObject();
        factor::config::setSerializedInstanceId(config, toStd(factorId));
        factor::config::setSerializedInstanceName(config, toStd(factorName));
        factor::config::setSerializedDescription(config, toStd(description));

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

        QVariantMap parameters = factorData.value(QStringLiteral("parameters")).toMap();
        if (!parameters.isEmpty()) {
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

        factor::config::setSerializedConfig(config, config);

        INTERNAL_DEBUG_STREAM << "[FactorService] 添加因子:" << factorId.toStdString() << factorName.toStdString();

        bool persisted = m_instanceManager->updateInstanceConfig(toStd(factorId), config);
        if (!persisted) {
            endMutation(false, QStringLiteral("因子持久化失败"));
            return {};
        }

        if (m_viewModel) {
            ensureViewModelPopulated();
        }
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
        auto info = m_instanceManager->getInstanceInfo(toStd(factorId));
        if (info.instanceId.empty()) {
            endMutation(false, QStringLiteral("因子 '%1' 不存在").arg(factorId));
            return false;
        }

        foundation::json::JsonFacade config = info.config;
        if (config.empty()) {
            config = foundation::json::JsonFacade::createObject();
        }

        if (factorData.contains(QStringLiteral("factorName"))) {
            factor::config::setSerializedInstanceName(config, toStd(factorData.value(QStringLiteral("factorName")).toString()));
        } else if (factorData.contains(QStringLiteral("name"))) {
            factor::config::setSerializedInstanceName(config, toStd(factorData.value(QStringLiteral("name")).toString()));
        }

        if (factorData.contains(QStringLiteral("description"))) {
            factor::config::setSerializedDescription(config, toStd(factorData.value(QStringLiteral("description")).toString()));
        }

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

        // ── 回测指标写入 (QML 回测完成后回调) ──
        auto btMetrics = foundation::json::JsonFacade::createObject();
        if (factorData.contains(QStringLiteral("icValue"))) {
            btMetrics.set("icValue", foundation::json::JsonFacade::createDouble(
                factorData.value(QStringLiteral("icValue")).toDouble()));
        }
        if (factorData.contains(QStringLiteral("irValue"))) {
            btMetrics.set("irValue", foundation::json::JsonFacade::createDouble(
                factorData.value(QStringLiteral("irValue")).toDouble()));
        }
        if (factorData.contains(QStringLiteral("coreRating"))) {
            int rating = factorData.value(QStringLiteral("coreRating")).toInt();
            btMetrics.set("coreRating", foundation::json::JsonFacade::createInt(rating));
            // 同时写入中文标签
            const char* label = (rating == 3) ? "优秀" : (rating == 2) ? "良好"
                              : (rating == 1) ? "合格" : "不合格";
            btMetrics.set("coreRatingLabel", foundation::json::JsonFacade::createString(label));
        }
        if (factorData.contains(QStringLiteral("turnoverRate"))) {
            btMetrics.set("turnoverRate", foundation::json::JsonFacade::createDouble(
                factorData.value(QStringLiteral("turnoverRate")).toDouble()));
        }
        if (factorData.contains(QStringLiteral("effectiveStartDate"))) {
            btMetrics.set("effectiveStartDate", foundation::json::JsonFacade::createString(
                toStd(factorData.value(QStringLiteral("effectiveStartDate")).toString())));
        }
        if (factorData.contains(QStringLiteral("effectiveEndDate"))) {
            btMetrics.set("effectiveEndDate", foundation::json::JsonFacade::createString(
                toStd(factorData.value(QStringLiteral("effectiveEndDate")).toString())));
        }
        if (factorData.contains(QStringLiteral("warmupTrimmedTradingDays"))) {
            btMetrics.set("warmupTrimmedTradingDays", foundation::json::JsonFacade::createInt(
                factorData.value(QStringLiteral("warmupTrimmedTradingDays")).toInt()));
        }
        config.set("backtest_metrics", btMetrics);

        bool updated = m_instanceManager->updateInstanceConfig(toStd(factorId), config);

        if (updated) {
            INTERNAL_DEBUG_STREAM << "[FactorService] 更新因子:" << factorId.toStdString();
            if (m_viewModel) {
                ensureViewModelPopulated();
            }
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

bool FactorService::writeBacktestMetrics(const QString& factorId, const QVariantMap& report)
{
    auto metrics = report.value("metrics").toMap();
    auto ic = metrics.value("ic").toMap();
    auto factorQuality = metrics.value("factorQuality").toMap();
    auto exec = metrics.value("execution").toMap();
    auto config = report.value("config").toMap();

    QVariantMap data;
    data["factorId"]            = factorId;
    data["icValue"]             = ic.value("value");
    data["irValue"]             = ic.value("ir");
    data["coreRating"]          = factorQuality.value("coreRating");
    data["turnoverRate"]        = exec.value("turnoverRate");
    data["effectiveStartDate"]  = config.value("startDate");
    data["effectiveEndDate"]    = config.value("endDate");

    return updateFactor(factorId, data);
}

bool FactorService::deleteFactor(const QString& factorId)
{
    if (factorId.isEmpty()) {
        emit errorOccurred(QStringLiteral("因子ID不能为空"));
        return false;
    }

    beginMutation();
    try {
        INTERNAL_DEBUG_STREAM << "[FactorService] 删除因子:" << factorId.toStdString();

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

// ============ 跨表因子数据视图生成 ============

QString FactorService::generateFactorDataView(const QString& factorId)
{
    if (factorId.isEmpty() || !m_initialized.load() || !m_instanceManager) {
        QString err = QStringLiteral("因子服务未就绪或ID为空");
        emit errorOccurred(err);
        return err;
    }

    try {
        auto info = m_instanceManager->getInstanceInfo(toStd(factorId));
        if (info.instanceId.empty()) {
            QString err = QStringLiteral("因子 '%1' 不存在").arg(factorId);
            emit errorOccurred(err);
            return err;
        }

        const auto& config = info.config;
        if (!factor::config::hasDataRequirementsConfig(config)) {
            QString err = QStringLiteral("因子 '%1' 缺少 dataRequirements 配置").arg(factorId);
            emit errorOccurred(err);
            return err;
        }

        auto dataReq = factor::config::dataRequirementsConfig(config);
        if (!dataReq.isObject() || !dataReq.has("required")) {
            QString err = QStringLiteral("因子 '%1' dataRequirements 缺少 required 字段").arg(factorId);
            emit errorOccurred(err);
            return err;
        }

        auto requiredFields = dataReq.get("required");
        std::vector<std::string> fields;
        for (size_t i = 0; i < requiredFields.size(); ++i)
            fields.push_back(requiredFields.at(i).asString());

        if (fields.empty()) {
            QString msg = QStringLiteral("因子 '%1' 无必需字段").arg(factorId);
            endMutation(true, msg);
            return msg;
        }

        auto groups = factor::DataAvailabilityChecker::groupFieldsByTable(fields);

        QString report;
        report += QStringLiteral("因子: %1\n").arg(factorId);
        report += QStringLiteral("必需字段数: %1\n").arg(fields.size());

        if (groups.empty()) {
            report += QStringLiteral("警告: 所有字段均无法匹配到已知数据表");
        } else {
            report += QStringLiteral("涉及数据表数: %1\n").arg(groups.size());
            for (const auto& [table, tableFields] : groups) {
                report += QStringLiteral("  表 %1: %2 个字段\n")
                    .arg(QString::fromStdString(table))
                    .arg(tableFields.size());
                for (const auto& f : tableFields) {
                    report += QStringLiteral("    - %1\n").arg(QString::fromStdString(f));
                }
            }

            if (groups.size() == 1U) {
                report += QStringLiteral("\n所有字段集中在单表，可通旧路径加载\n");
            } else {
                report += QStringLiteral("\n跨表因子：需要从 %1 张表分别加载数据后合并\n").arg(groups.size());
            }
        }

        endMutation(true, report);
        return report;

    } catch (const std::exception& e) {
        QString err = QStringLiteral("生成因子数据视图失败: %1").arg(QString::fromStdString(e.what()));
        endMutation(false, err);
        emit errorOccurred(err);
        return err;
    } catch (...) {
        QString err = QStringLiteral("生成因子数据视图失败: 未知异常");
        endMutation(false, err);
        emit errorOccurred(err);
        return err;
    }
}

// ============ 因子分析 ============

void FactorService::analyzeFactor(const QString& factorId)
{
    if (factorId.isEmpty() || !m_initialized.load()) {
        INTERNAL_WARN_STREAM << "[FactorService] analyzeFactor 无法执行: factorId为空或未初始化";
        return;
    }

    INTERNAL_DEBUG_STREAM << "[FactorService] 请求分析因子:" << factorId.toStdString();

    emit operationCompleted(QStringLiteral("analyzeFactor"), true,
                           QStringLiteral("因子 '%1' 分析请求已提交").arg(factorId));
}

// ============ 内部帮助方法 ============

void FactorService::beginMutation()
{
    m_mutationInProgress.store(true);
    emit mutationInProgressChanged();
}

factor::FactorInstanceManager* FactorService::instanceManager() const
{
    return m_instanceManager.get();
}

QVariantMap FactorService::buildFactorSupportMap(
    const QStringList& factorIds,
    const QString& startDate, const QString& endDate,
    const QVariantMap& cacheSnapshot,
    const QString& dataSourceMode,
    int selectedDatasetId)
{
    if (!m_detectionService || !m_instanceManager) {
        // 服务未就绪时返回 unsupported，不允许 UI 显示"可回测"
        QVariantMap map;
        for (const QString& id : factorIds) {
            QVariantMap info;
            info["supported"] = false;
            info["reason"] = QStringLiteral("因子服务未就绪，请稍后重试");
            info["category"] = QStringLiteral("runtime-init-failed");
            map[id] = info;
        }
        return map;
    }

    FactorDetectionService::Request request;
    request.factorIds = factorIds;
    request.startDate = startDate;
    request.endDate = endDate;
    request.cacheSnapshot = cacheSnapshot;
    request.dataSourceMode = dataSourceMode;
    request.selectedDatasetId = selectedDatasetId;

    auto runtimeContext = m_detectionService->resolveRuntimeContext(
        m_database, m_dataChecker, m_instanceManager, false);

    FactorDetectionService::Overrides overrides; // 无测试覆写

    auto result = m_detectionService->buildSupportMap(request, runtimeContext, overrides);
    return result.supportMap;
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