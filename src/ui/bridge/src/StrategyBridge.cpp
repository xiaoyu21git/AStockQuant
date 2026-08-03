#include "../include/StrategyBridge.h"

// 静态单例指针
StrategyBridge* StrategyBridge::s_instance = nullptr;

#include "../include/StrategyLifecycleStatus.h"
#include "../include/StrategyListModel.h"
#include "StockNameResolver.h"
#include "AppStoragePaths.h"

#include "database/MarketDataRepository.h"
#include "database/NativePgConnectionPool.h"
#include "database/ISqlDatabase.h"
#include "database/NativePgDatabase.h"
#include "database/StrategyRepository.h"
#include "FactorService.h"


#include "../../domain/backtest/include/ResolvedStrategyBehavior.h"
#include "../../domain/trading/TradeExecutionEngine.h"
#include "../../domain/trading/include/TradingSystem.h"
#include "../../domain/factor/include/FactorInstanceManager.h"
#include "../../domain/factor/include/factor_compute/CachedMarketDataView.h"
#include "../../domain/strategies/include/StrategyDefinitionTypes.h"
#include "foundation/json/json_facade.h"
#include "../../domain/strategy/include/StrategyManager.h"
#include "../include/TradingRuntimeStatusService.h"

#include <chrono>
#include <ctime>
#include <exception>
#include "foundation/log/logging.hpp"
#include "../../domain/strategy/include/RuntimeFactorSvc.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QTimer>

#include <cstdlib>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

using astock::database::PersistedStrategyData;
using astock::database::StrategyRepository;

namespace {

constexpr const char* kRuleProfileKey = "rule_profile";
constexpr const char* kRuleComposerStateKey = "rule_composer_state";

bool isVariantMapObject(const QVariantMap& payload, const QString& key)
{
    if (!payload.contains(key)) return false;
    const QVariant value = payload.value(key);
    return value.isValid() && !value.isNull() && value.canConvert<QVariantMap>();
}

bool isRequiredStrategyParametersShapeValid(const QVariantMap& parameters)
{
    return isVariantMapObject(parameters, QString::fromLatin1(kRuleProfileKey))
        && isVariantMapObject(parameters, QString::fromLatin1(kRuleComposerStateKey));
}

bool hasLegacyParameterKeys(const QVariantMap& parameters)
{
    return parameters.contains(QStringLiteral("commonConfig"))
        || parameters.contains(QStringLiteral("strategySpec"))
        || parameters.contains(QStringLiteral("rule_template_bindings"))
        || parameters.contains(QStringLiteral("advanced_options"))
        || parameters.contains(QStringLiteral("factorOverlaySnapshot"));
}

bool hasUnexpectedPayloadKeys(const QVariantMap& payload,
                              const QStringList& allowedKeys,
                              QString* firstUnexpectedKey)
{
    for (auto it = payload.constBegin(); it != payload.constEnd(); ++it) {
        if (allowedKeys.contains(it.key())) continue;
        if (firstUnexpectedKey) *firstUnexpectedKey = it.key();
        return true;
    }
    return false;
}

const QStringList& frozenStrategyUpsertPayloadKeys()
{
    static const QStringList keys = {
        QStringLiteral("strategyId"),
        QStringLiteral("strategyName"),
        QStringLiteral("strategyTypeIndex"),
        QStringLiteral("strategyBehaviorKind"),
        QStringLiteral("description"),
        QStringLiteral("assetTypeIndex"),
        QStringLiteral("timeFrameIndex"),
        QStringLiteral("riskLevelIndex"),
        QStringLiteral("optimization_method"),
        QStringLiteral("parameters"),
        QStringLiteral("tags"),
        QStringLiteral("status"),
        QStringLiteral("factorIds"),
        QStringLiteral("ruleIds")
    };
    return keys;
}

template <typename TValue, typename TConverter>
TValue readScalarByKeys(const QVariantMap& payload,
                        std::initializer_list<const char*> keys,
                        const TValue& fallback,
                        TConverter&& converter)
{
    for (const char* key : keys) {
        const QVariant rawValue = payload.value(QString::fromLatin1(key));
        if (!rawValue.isValid() || rawValue.isNull()) continue;
        bool ok = false;
        const TValue parsed = converter(rawValue, ok);
        if (ok) return parsed;
    }
    return fallback;
}

template <typename TValue>
TValue readScalarByKeys(const QVariantMap& payload,
                        std::initializer_list<const char*> keys,
                        const TValue& fallback)
{
    if constexpr (std::is_same_v<TValue, bool>) {
        return readScalarByKeys<TValue>(payload, keys, fallback,
            [](const QVariant& v, bool& ok) { ok = true; return v.toBool(); });
    } else if constexpr (std::is_same_v<TValue, int>) {
        return readScalarByKeys<TValue>(payload, keys, fallback,
            [](const QVariant& v, bool& ok) { return v.toInt(&ok); });
    } else if constexpr (std::is_same_v<TValue, double>) {
        return readScalarByKeys<TValue>(payload, keys, fallback,
            [](const QVariant& v, bool& ok) { return v.toDouble(&ok); });
    } else {
        static_assert(std::is_same_v<TValue, void>, "unsupported scalar read type");
    }
}

} // namespace

QString StrategyBridge::readText(const QVariantMap& payload,
                                 std::initializer_list<const char*> keys) const
{
    for (const char* key : keys) {
        const QVariant rawValue = payload.value(QString::fromLatin1(key));
        if (!rawValue.isValid() || rawValue.isNull()) continue;
        const QString value = rawValue.toString().trimmed();
        if (!value.isEmpty()) return value;
    }
    return {};
}

bool StrategyBridge::isTypeIdxValid(const int index) const
{
    return domain::strategies::isValidStrategyTypeIndex(index);
}

StrategyBridge::StrategyTypeSpec StrategyBridge::readTypeSpec(const QVariantMap& payload) const
{
    StrategyTypeSpec spec;
    const int typeIndex = readScalarByKeys<int>(payload, {"strategyTypeIndex"}, -1);
    if (!isTypeIdxValid(typeIndex)) return spec;
    spec.value = static_cast<domain::strategies::StrategyType>(typeIndex);
    spec.valid = true;
    return spec;
}

StrategyBridge::StrategyBehaviorKindSpec StrategyBridge::readBehaviorKindSpec(const QVariantMap& payload) const
{
    StrategyBehaviorKindSpec spec;
    const int behaviorIndex = readScalarByKeys<int>(payload, {"strategyBehaviorKind"}, -1);
    if (!domain::strategies::isValidStrategyBehaviorKindIndex(behaviorIndex)) return spec;
    spec.value = static_cast<domain::strategies::StrategyBehaviorKind>(behaviorIndex);
    spec.valid = true;
    return spec;
}

StrategyBridge::FactorIdListSpec StrategyBridge::readFactorIds(const QVariantMap& payload) const
{
    FactorIdListSpec spec;
    const QVariant rawValue = payload.value(QStringLiteral("factorIds"));
    if (!rawValue.isValid() || rawValue.isNull()) return spec;
    spec.provided = true;
    const QVariantList rawList = rawValue.toList();
    spec.values.reserve(static_cast<size_t>(rawList.size()));
    for (const QVariant& item : rawList) {
        const QString str = item.toString().trimmed();
        if (str.isEmpty()) { spec.valid = false; return spec; }
        spec.values.push_back(str.toStdString());
    }
    return spec;
}

StrategyBridge::RuleIdListSpec StrategyBridge::readRuleIds(const QVariantMap& payload) const
{
    RuleIdListSpec spec;
    const QVariant rawValue = payload.value(QStringLiteral("ruleIds"));
    if (!rawValue.isValid() || rawValue.isNull()) return spec;
    spec.provided = true;
    const QVariantList rawList = rawValue.toList();
    spec.values.reserve(static_cast<size_t>(rawList.size()));
    for (const QVariant& item : rawList) {
        bool ok = false;
        const qulonglong value = item.toULongLong(&ok);
        if (!ok || value == 0) { spec.valid = false; return spec; }
        spec.values.push_back(static_cast<domain::strategies::RuleId>(value));
    }
    return spec;
}

bool StrategyBridge::hasForbiddenFields(const QVariantMap& payload) const
{
    const QStringList forbiddenKeys = {
        QStringLiteral("strategyCode"), QStringLiteral("version"),
        QStringLiteral("author"), QStringLiteral("performanceMetrics"),
        QStringLiteral("engineStrategyId")
    };
    for (const QString& key : forbiddenKeys) {
        if (payload.contains(key) && payload.value(key).isValid() && !payload.value(key).isNull())
            return true;
    }
    return false;
}

QVariant StrategyBridge::readValue(const QVariantMap& payload,
                                   std::initializer_list<const char*> keys) const
{
    for (const char* key : keys) {
        const QVariant value = payload.value(QString::fromLatin1(key));
        if (value.isValid() && !value.isNull()) return value;
    }
    return {};
}

QVariantMap StrategyBridge::readMap(const QVariantMap& payload,
                                    std::initializer_list<const char*> keys) const
{
    const QVariant value = readValue(payload, keys);
    return value.isValid() && value.canConvert<QVariantMap>() ? value.toMap() : QVariantMap{};
}

std::optional<domain::strategies::StrategyUuid> StrategyBridge::readId(const QVariantMap& payload) const
{
    const std::string rawValue = payload.value(QString::fromLatin1(kStrategyIdKey)).toString().trimmed().toStdString();
    if (!foundation::utils::Uuid::is_valid_uuid(rawValue)) return std::nullopt;
    return domain::strategies::StrategyUuid::from_string(rawValue);
}

StrategyBridge::BridgeUpsertRequest StrategyBridge::parseReq(const QVariantMap& payload) const
{
    BridgeUpsertRequest request;
    const QVariantMap parameters = readMap(payload, {"parameters"});
    const std::optional<domain::strategies::StrategyUuid> strategyId = readId(payload);
    if (strategyId.has_value()) request.setStrategyId(*strategyId);
    request.setStrategyName(readText(payload, {"strategyName"}).toStdString());
    request.setDescription(readText(payload, {"description"}).toStdString());
    request.setStrategyType(readTypeSpec(payload));
    request.setBehaviorKind(readBehaviorKindSpec(payload));
    request.setFactorIds(readFactorIds(payload));
    request.setRuleIds(readRuleIds(payload));
    request.setStatus(readScalarByKeys<bool>(payload, {"status"}, request.status()));
    request.setParameters(parameters);
    return request;
}

void StrategyBridge::applyReq(const BridgeUpsertRequest& request,
                              PersistedStrategyData& target) const
{
    if (request.hasStrategyId()) target.strategyId = request.strategyId().to_string();
    if (!request.strategyName().empty()) target.metadata.name = request.strategyName();
    if (!request.description().empty()) target.metadata.description = request.description();
    if (request.behaviorKind().valid) target.metadata.behaviorKind = request.behaviorKind().value;
    target.status = request.status()
        ? strategy_view::StrategyLifecycleStatus::Active
        : strategy_view::StrategyLifecycleStatus::Inactive;
    target.metadata.enabled = request.status();
    if (request.ruleIds().provided) target.metadata.ruleIds = request.ruleIds().values;
    target.parameters = request.parameters();
    if (request.strategyType().valid) {
        const int typeIndex = static_cast<int>(request.strategyType().value);
        if (!isTypeIdxValid(typeIndex)) std::abort();
        target.strategyTypeIndex = typeIndex;
        target.strategyIdentity = domain::backtest::ResolvedStrategyIdentity{
            static_cast<domain::backtest::StrategyStoredType>(typeIndex),
            domain::backtest::ResolvedStrategyBehavior{
                static_cast<domain::backtest::StrategyBehaviorKind>(
                    static_cast<int>(target.metadata.behaviorKind)), true},
            true
        };
    }
    // strategy_code 有 UNIQUE 约束，不能为空
    if (target.strategyCode.empty()) {
        auto ts = std::chrono::system_clock::now().time_since_epoch().count();
        target.strategyCode = "SPT_" + std::to_string(ts);
    }
}

std::optional<domain::strategies::StrategyUuid> StrategyBridge::parseId(const QString& input) const
{
    const std::string rawValue = input.trimmed().toStdString();
    if (!foundation::utils::Uuid::is_valid_uuid(rawValue)) return std::nullopt;
    return domain::strategies::StrategyUuid::from_string(rawValue);
}

QString StrategyBridge::clearedMsg() const
{
    return QStringLiteral("StrategyBridge bridge typed persistence is not implemented");
}

StrategyBridge::StrategyBridge(QObject* parent)
    : QObject(parent)
    , m_repo(std::make_unique<StrategyRepository>())
    , m_listModel(new StrategyListModel(this))
{
    INTERNAL_INFO_STREAM << "[Bridge] CTOR";
    s_instance = this;
}

StrategyBridge::~StrategyBridge()
{
    if (s_instance == this) s_instance = nullptr;
}

void StrategyBridge::init()
{
    if (m_inited) return;
    INTERNAL_INFO_STREAM << "[Bridge] init START";
    try {
        if (!m_repo || !m_repo->initialize()) {
            INTERNAL_ERROR_STREAM << "[Bridge] init FAILED: repo init";
            setErr(QStringLiteral("initialize strategy repository failed"));
            emit operationFailed(kRepositoryErrorCode, m_err);
            return;
        }

        // ── 向 StrategyManager 注入领域依赖（一次初始化，所有策略共享）──
        auto& mgr = domain::strategy::StrategyManager::instance();

        // 注入 FactorInstanceManager
        auto* factorSvcBridge = FactorService::instance();
        if (factorSvcBridge && factorSvcBridge->isInitialized()) {
            mgr.setFactorInstanceManager(factorSvcBridge->instanceManager());
        }

        // 注入实盘数据持久化目录 (lastEvalDay JSON 等)
        {
            QString livePath = bridge::storage::absolutePathInAppDir("files/live");
            bridge::storage::ensureDirectoryExists(livePath);
            mgr.setLiveDataPath(livePath.toStdString());
            INTERNAL_INFO_STREAM << "[Bridge] liveDataPath=" << livePath.toStdString();
        }

        // 初始化 TradingSystem（交易 facade）
        domain::trading::TradingSystem::instance().initialize();

        // 注册 TradeExecutionEngine 为策略订单监听器
        {
            auto& engine = domain::trading::TradeExecutionEngine::instance();
            mgr.setOrderListener(&engine);
            mgr.setDefaultOrderListener(&engine);
        }

        INTERNAL_INFO_STREAM << "[Bridge] init repo OK, calling refreshModel";
        m_inited = true;
        emit initedChanged();
        refreshModel();
        INTERNAL_INFO_STREAM << "[Bridge] init COMPLETE";
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[Bridge] init EXCEPTION: " << e.what();
        const QString msg = QString::fromUtf8(e.what());
        INTERNAL_ERROR_STREAM << "[StrategyBridge] init exception: " << msg.toStdString();
        setErr(QStringLiteral("strategy init failed: %1").arg(msg));
        emit operationFailed(kRepositoryErrorCode, m_err);
    } catch (...) {
        INTERNAL_ERROR_STREAM << "[Bridge] init UNKNOWN EXCEPTION";
        INTERNAL_ERROR_STREAM << "[StrategyBridge] init unknown exception";
        setErr(QStringLiteral("strategy init failed: unknown error"));
        emit operationFailed(kRepositoryErrorCode, m_err);
    }
}

void StrategyBridge::initAsync()
{
    INTERNAL_INFO_STREAM << "[Bridge] initAsync called";
    init();
}

bool StrategyBridge::inited() const { return m_inited; }
bool StrategyBridge::cacheOk() const { return m_cacheOk; }

QString StrategyBridge::add(const QVariantMap& payload)
{
    QString unexpectedKey;
    if (hasUnexpectedPayloadKeys(payload, frozenStrategyUpsertPayloadKeys(), &unexpectedKey)) {
        setErr(QStringLiteral("add payload contains non-frozen field: %1").arg(unexpectedKey));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }
    if (hasForbiddenFields(payload)) {
        setErr(QStringLiteral("add payload contains forbidden fields"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }
    init();
    if (!m_inited) return {};
    const QVariantMap parameters = readMap(payload, {"parameters"});
    if (parameters.isEmpty()) {
        setErr(QStringLiteral("add parameters is required"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }
    if (hasLegacyParameterKeys(parameters)) {
        setErr(QStringLiteral("add parameters contains legacy fields"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }
    if (!isRequiredStrategyParametersShapeValid(parameters)) {
        setErr(QStringLiteral("add parameters must include rule_profile and rule_composer_state objects"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }
    if (payload.contains(QStringLiteral("strategyId"))) {
        const QString payloadStrategyId = payload.value(QStringLiteral("strategyId")).toString().trimmed();
        if (!payloadStrategyId.isEmpty() && !parseId(payloadStrategyId).has_value()) {
            setErr(QStringLiteral("add strategyId is not a valid UUID"));
            emit operationFailed(kInvalidArgumentCode, m_err);
            return {};
        }
    }
    const BridgeUpsertRequest request = parseReq(payload);
    if (!request.factorIds().valid) {
        setErr(QStringLiteral("add factorIds contains invalid value"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }
    if (!request.ruleIds().valid) {
        setErr(QStringLiteral("add ruleIds contains invalid value"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }
    if (request.strategyName().empty()) {
        setErr(QStringLiteral("add strategyName is required"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }
    if (!request.strategyType().valid) {
        setErr(QStringLiteral("add strategyTypeIndex is required"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }
    if (!request.behaviorKind().valid) {
        setErr(QStringLiteral("add strategyBehaviorKind is required"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }
    PersistedStrategyData strategy;
    applyReq(request, strategy);
    const QString strategyId = m_repo->save(strategy);
    if (strategyId.trimmed().isEmpty()) {
        setErr(QStringLiteral("add repository save failed"));
        emit operationFailed(kRepositoryErrorCode, m_err);
        return {};
    }
    if (!parseId(strategyId).has_value()) {
        m_repo->remove(strategyId);
        setErr(QStringLiteral("add repository returned non-UUID strategyId"));
        emit operationFailed(kRepositoryErrorCode, m_err);
        return {};
    }
    refreshModel();
    emit created(strategyId, get(strategyId));
    return strategyId;
}

bool StrategyBridge::update(const QVariantMap& payload)
{
    QString unexpectedKey;
    if (hasUnexpectedPayloadKeys(payload, frozenStrategyUpsertPayloadKeys(), &unexpectedKey)) {
        setErr(QStringLiteral("update payload contains non-frozen field: %1").arg(unexpectedKey));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    if (hasForbiddenFields(payload)) {
        setErr(QStringLiteral("update payload contains forbidden fields"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    init();
    if (!m_inited) return false;
    const QString payloadStrategyId = payload.value(QStringLiteral("strategyId")).toString().trimmed();
    if (payloadStrategyId.isEmpty()) {
        setErr(QStringLiteral("update strategyId is required"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    if (!parseId(payloadStrategyId).has_value()) {
        setErr(QStringLiteral("update strategyId is not a valid UUID"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    const QVariantMap parameters = readMap(payload, {"parameters"});
    if (parameters.isEmpty()) {
        setErr(QStringLiteral("update parameters is required"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    if (hasLegacyParameterKeys(parameters)) {
        setErr(QStringLiteral("update parameters contains legacy fields"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    if (!isRequiredStrategyParametersShapeValid(parameters)) {
        setErr(QStringLiteral("update parameters must include rule_profile and rule_composer_state objects"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    const BridgeUpsertRequest request = parseReq(payload);
    if (!request.factorIds().valid) {
        setErr(QStringLiteral("update factorIds contains invalid value"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    if (!request.ruleIds().valid) {
        setErr(QStringLiteral("update ruleIds contains invalid value"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    if (!request.behaviorKind().valid) {
        setErr(QStringLiteral("update strategyBehaviorKind is required"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    const QString strategyId = payloadStrategyId;
    const auto existing = m_repo->findById(strategyId);
    if (!existing.has_value()) {
        setErr(QStringLiteral("update strategyId not found"));
        emit operationFailed(kRepositoryErrorCode, m_err);
        return false;
    }
    PersistedStrategyData strategy = *existing;
    applyReq(request, strategy);
    const bool ok = m_repo->update(strategyId, strategy);
    if (!ok) {
        setErr(QStringLiteral("update repository update failed"));
        emit operationFailed(kRepositoryErrorCode, m_err);
        return false;
    }
    refreshModel();
    emit updated(strategyId);
    return true;
}

bool StrategyBridge::remove(const QString& strategyId)
{
    const QString repositoryId = strategyId.trimmed();
    if (repositoryId.isEmpty() || !parseId(repositoryId).has_value()) {
        setErr(QStringLiteral("deleteStrategy strategyId is invalid"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    init();
    if (!m_inited) return false;
    setBusy(true);
    const bool ok = m_repo->remove(repositoryId);
    setBusy(false);
    if (!ok) {
        setErr(QStringLiteral("deleteStrategy repository remove failed"));
        emit operationFailed(kRepositoryErrorCode, m_err);
        return false;
    }
    refreshModel();
    emit deleted(repositoryId);
    return true;
}

QVariantMap StrategyBridge::get(const QString& strategyId)
{
    const QString repositoryId = strategyId.trimmed();
    if (repositoryId.isEmpty() || !parseId(repositoryId).has_value()) return {};
    init();
    if (!m_inited) return {};
    const auto strategy = m_repo->findById(repositoryId);
    if (!strategy.has_value()) return {};
    auto map = strategy->toVariantMap();
    map["displayStatus"] = m_runtimeStatus.value(repositoryId, QStringLiteral("已停止"));
    return map;
}

QVariantList StrategyBridge::list()
{
    init();
    if (!m_inited) return {};
    QVariantList list;
    const std::vector<PersistedStrategyData> all = m_repo->findAll();
    list.reserve(static_cast<int>(all.size()));
    for (const PersistedStrategyData& data : all) {
        auto map = data.toVariantMap();
        QString sid = QString::fromStdString(data.strategyId);
        map["displayStatus"] = m_runtimeStatus.value(sid, QStringLiteral("已停止"));
        list.push_back(map);
    }
    return list;
}

bool StrategyBridge::start(const QString& strategyId)
{
    const QString repositoryId = strategyId.trimmed();
    if (repositoryId.isEmpty() || !parseId(repositoryId).has_value()) {
        setErr(QStringLiteral("startStrategy strategyId is invalid"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    init();
    if (!m_inited) {
        INTERNAL_ERROR_STREAM << "[StrategyBridge] init FAILED";
        return false;
    }

    INTERNAL_INFO_STREAM << "[Live] start strategy: " << repositoryId.toStdString();

    m_repo->updateStatus(repositoryId, strategy_view::StrategyLifecycleStatus::Active);

    // ── 异步委托给 StrategyManager（工作线程：DB 查询 → 历史数据 → 启动实盘循环）──
    if (!m_startupPool) {
        m_startupPool = std::make_unique<foundation::thread::ThreadPoolExecutor>(
            1, 4, std::chrono::seconds(120), "StrategyBridgeStartup");
    }
    m_startupPool->post([this, repositoryId]() {
        try {
            auto& mgr = domain::strategy::StrategyManager::instance();
            mgr.startStrategy(repositoryId.toStdString());

            QMetaObject::invokeMethod(this, [this, repositoryId]() {
                m_runtimeStatus[repositoryId] = QStringLiteral("运行中");
                if (m_listModel) m_listModel->updateDisplayStatus(repositoryId, QStringLiteral("运行中"));
                emit strategiesChanged();
                emit started(repositoryId);
                bridge::TradingRuntimeStatusService::instance()->refresh();
                INTERNAL_INFO_STREAM << "[Live] engine started: " << repositoryId.toStdString();
            }, Qt::QueuedConnection);

        } catch (const std::exception& e) {
            INTERNAL_ERROR_STREAM << "[StrategyBridge] start worker exception: " << e.what();
            QMetaObject::invokeMethod(this, [this, repositoryId, msg = QString::fromUtf8(e.what())]() {
                m_runtimeStatus[repositoryId] = QStringLiteral("启动失败");
                emit strategiesChanged();
                setErr(QStringLiteral("引擎启动异常: %1").arg(msg));
                emit operationFailed(kRepositoryErrorCode, m_err);
            }, Qt::QueuedConnection);
        }
    });

    return true;
}

bool StrategyBridge::stop(const QString& strategyId)
{
    const QString repositoryId = strategyId.trimmed();
    if (repositoryId.isEmpty() || !parseId(repositoryId).has_value()) {
        setErr(QStringLiteral("stopStrategy strategyId is invalid"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    init();
    if (!m_inited) return false;

    INTERNAL_INFO_STREAM << "[Live] 停止策略: " << repositoryId.toStdString();

    // 立即显示"停止中"，然后异步执行 stop 避免阻塞 UI
    m_runtimeStatus[repositoryId] = QStringLiteral("停止中");
    if (m_listModel) m_listModel->updateDisplayStatus(repositoryId, QStringLiteral("停止中"));
    emit strategiesChanged();
    m_repo->updateStatus(repositoryId, strategy_view::StrategyLifecycleStatus::Inactive);

    // 用 QueuedConnection 让 QML 先渲染"停止中"，再执行阻塞的 stopLiveLoop
    QMetaObject::invokeMethod(this, [this, repositoryId]() {
        domain::strategy::StrategyManager::instance().stopStrategy(repositoryId.toStdString());
        m_runtimeStatus[repositoryId] = QStringLiteral("已停止");
        if (m_listModel) m_listModel->updateDisplayStatus(repositoryId, QStringLiteral("已停止"));
        emit strategiesChanged();
        emit stopped(repositoryId);
    }, Qt::QueuedConnection);

    return true;
}

int StrategyBridge::liquidateAll(const QString& strategyId)
{
    const QString repositoryId = strategyId.trimmed();
    if (repositoryId.isEmpty()) {
        setErr(QStringLiteral("liquidateAll strategyId is invalid"));
        return -1;
    }
    init();
    if (!m_inited) return -1;

    auto* engine = domain::strategy::StrategyManager::instance().get(repositoryId.toStdString());
    if (!engine) {
        setErr(QStringLiteral("策略未找到"));
        return -1;
    }

    INTERNAL_WARN_STREAM << "[Live] 一键清仓: " << repositoryId.toStdString();
    return engine->liquidateAll();
}

void StrategyBridge::setupLiveMarketView(const QString& strategyId, const QString& datasetJson)
{
    const std::string id = strategyId.trimmed().toStdString();
    if (id.empty() || datasetJson.isEmpty()) return;

    auto* engine = domain::strategy::StrategyManager::instance().get(id);
    if (!engine) return;

    // QML 手动注入自定义数据集（用于调试/回放）
    auto root = foundation::json::JsonFacade::parse(datasetJson.toStdString());
    auto customView = factor::compute::CachedMarketDataView::fromJson(root);
    if (customView) {
        engine->setLiveMarketView(customView.get());
        // 视图生命周期由 prepareMarketData() 统一管理；
        // 手动注入的视图在 engine 下次 prepareMarketData() 时被覆盖
    }
}

bool StrategyBridge::saveViewCfg(const QString& strategyId, const QVariantMap& visualConfig)
{
    Q_UNUSED(strategyId); Q_UNUSED(visualConfig);
    setErr(clearedMsg());
    emit operationFailed(kInvalidArgumentCode, m_err);
    return false;
}

domain::strategy::StrategyEngine* StrategyBridge::backtestEngineProvider(const QString& strategyId)
{
    return domain::strategy::StrategyManager::instance().get(strategyId.trimmed().toStdString());
}

bool StrategyBridge::busy() const { return m_busy; }
QString StrategyBridge::errMsg() const { return m_err; }
QString StrategyBridge::selId() const { return m_selId; }
QAbstractListModel* StrategyBridge::listModel() const { return m_listModel; }

void StrategyBridge::setSelId(const QString& strategyId)
{
    const QString normalized = strategyId.trimmed();
    if (m_selId == normalized) return;
    m_selId = normalized;
    emit selIdChanged();
}

void StrategyBridge::setBusy(bool busy)
{
    if (m_busy == busy) return;
    m_busy = busy;
    emit busyChanged();
}

void StrategyBridge::setErr(const QString& message)
{
    if (m_err == message) return;
    m_err = message;
    emit errMsgChanged();
}

void StrategyBridge::refreshModel()
{
    const QVariantList strategies = list();
    if (m_listModel != nullptr) m_listModel->replaceAll(strategies);
    if (!m_cacheOk) { m_cacheOk = true; emit cacheOkChanged(); }
    emit strategiesChanged();
}

StrategyBridge* StrategyBridge::instance()
{
    return s_instance;
}

void StrategyBridge::refreshSingleStrategy(const QString& strategyId)
{
    init();  // 确保 m_inited，不等 initAsync 的延迟队列
    if (!m_inited || !m_listModel || strategyId.isEmpty()) return;

    auto data = m_repo->findById(strategyId);
    if (!data.has_value()) return;

    auto map = data->toVariantMap();
    QString sid = QString::fromStdString(data->strategyId);
    map["displayStatus"] = m_runtimeStatus.value(sid, QStringLiteral("已停止"));
    m_listModel->upsertOne(map);
    emit strategiesChanged();
}

QString StrategyBridge::stockDisplayName(const QString& symbol) const
{
    return StockNameResolver::displayName(symbol);
}

// ── 策略类型枚举 (替代 JS StrategyCreationUtils 数字映射) ──

using SBK = ::domain::strategies::StrategyBehaviorKind;

static const std::vector<std::tuple<int, QString, QString, QString>> kStrategyTypeMeta = {
    {0, QStringLiteral("趋势跟随"), QStringLiteral("📈"), QStringLiteral("基于双均线金叉死叉的趋势跟踪策略")},
    {1, QStringLiteral("均值回归"), QStringLiteral("🔄"), QStringLiteral("捕捉超买超卖后的价格回归机会")},
    {2, QStringLiteral("动量"),       QStringLiteral("🚀"), QStringLiteral("追踪强势股的持续上涨趋势")},
    {3, QStringLiteral("套利"),       QStringLiteral("⚖️"), QStringLiteral("利用价差偏离进行统计套利")},
    {4, QStringLiteral("多因子"),     QStringLiteral("🧩"), QStringLiteral("多因子加权综合排名选股")},
    {5, QStringLiteral("机器学习"),   QStringLiteral("🤖"), QStringLiteral("ML模型驱动的智能选股")},
    {6, QStringLiteral("多因子"),     QStringLiteral("🧩"), QStringLiteral("多因子加权综合排名选股")},
    {7, QStringLiteral("事件驱动"),   QStringLiteral("📰"), QStringLiteral("基于财报/公告事件的交易策略")},
    {8, QStringLiteral("高频"),       QStringLiteral("⚡"), QStringLiteral("分钟级别高频交易策略")},
    {9, QStringLiteral("自定义"),     QStringLiteral("🛠"), QStringLiteral("用户自定义参数的灵活策略")},
};

static const std::vector<std::tuple<int, QString, QString>> kRiskLevelMeta = {
    {1, QStringLiteral("低风险"), QStringLiteral("#10B981")},
    {2, QStringLiteral("中风险"), QStringLiteral("#F59E0B")},
    {3, QStringLiteral("高风险"), QStringLiteral("#EF4444")},
    {4, QStringLiteral("激进"),   QStringLiteral("#8B5CF6")},
};

static int toBk(int typeIndex) {
    switch (typeIndex) {
    case 0: return (int)SBK::TrendFollowing;
    case 1: return (int)SBK::MeanReversion;
    case 2: return (int)SBK::Momentum;
    case 3: return (int)SBK::Arbitrage;
    case 4: return (int)SBK::MultiFactor;
    case 5: return (int)SBK::MachineLearning;
    case 6: return (int)SBK::MultiFactor;    // 兼容旧JS: StrategyTypeIndex.MultiFactor=6
    case 7: return (int)SBK::EventDriven;
    case 8: return (int)SBK::HighFrequency;
    case 9: return (int)SBK::Custom;
    default: return (int)SBK::Custom;
    }
}

static int fromBk(int bk) {
    switch (bk) {
    case (int)SBK::TrendFollowing:  return 0;
    case (int)SBK::MeanReversion:   return 1;
    case (int)SBK::Momentum:        return 2;
    case (int)SBK::Arbitrage:       return 3;
    case (int)SBK::MultiFactor:     return 4;
    case (int)SBK::MachineLearning: return 5;
    case (int)SBK::EventDriven:     return 6;
    case (int)SBK::HighFrequency:   return 7;
    case (int)SBK::Custom:          return 8;
    default: return 0;
    }
}

static const auto* findMeta(int typeIndex) {
    for (auto& m : kStrategyTypeMeta)
        if (std::get<0>(m) == typeIndex) return &m;
    return &kStrategyTypeMeta[0];  // fallback TrendFollowing
}

int StrategyBridge::normalizeStrategyTypeIndex(int raw) const {
    if (raw >= 0 && raw <= 10) return raw;           // display range 0-10
    if (raw == 100) return 0;                        // Common → TrendFollowing
    // Try behaviorKind range
    int ti = fromBk(raw);
    if (ti >= 0) return ti;
    return 0;  // fallback
}

QString StrategyBridge::strategyTypeName(int typeIndex) const {
    return std::get<1>(*findMeta(normalizeStrategyTypeIndex(typeIndex)));
}

QString StrategyBridge::strategyTypeIcon(int typeIndex) const {
    return std::get<2>(*findMeta(normalizeStrategyTypeIndex(typeIndex)));
}

QString StrategyBridge::strategyTypeBrief(int typeIndex) const {
    return std::get<3>(*findMeta(normalizeStrategyTypeIndex(typeIndex)));
}

int StrategyBridge::strategyBehaviorKindFromTypeIndex(int typeIndex) const {
    return toBk(normalizeStrategyTypeIndex(typeIndex));
}

int StrategyBridge::strategyTypeIndexFromBehaviorKind(int behaviorKind) const {
    return fromBk(behaviorKind);
}

QString StrategyBridge::riskLevelName(int index) const {
    for (auto& m : kRiskLevelMeta)
        if (std::get<0>(m) == index) return std::get<1>(m);
    return QStringLiteral("中风险");
}

QString StrategyBridge::riskLevelColor(int index) const {
    for (auto& m : kRiskLevelMeta)
        if (std::get<0>(m) == index) return std::get<2>(m);
    return QStringLiteral("#F59E0B");
}

// ── 策略参数配置 + 数据组装 (替代 JS buildParamConfigs / buildCompleteStrategyData / resetFormData) ──

static auto slider(const QString& id, const QString& label, double def, double min, double max,
                    double step, const QString& unit, int decimals = 0,
                    const QString& category = QStringLiteral("通用参数")) {
    QVariantMap m;
    m["id"] = id; m["label"] = label; m["type"] = "slider";
    m["default"] = def; m["min"] = min; m["max"] = max; m["step"] = step;
    m["unit"] = unit; m["decimals"] = decimals;
    m["category"] = category;
    return m;
}

static auto select(const QString& id, const QString& label, int def, const QVariantList& options,
                   const QString& category = QStringLiteral("通用参数")) {
    QVariantMap m;
    m["id"] = id; m["label"] = label; m["type"] = "select";
    m["default"] = def; m["options"] = options;
    m["category"] = category;
    return m;
}

static auto toggle(const QString& id, const QString& label, bool def,
                   const QString& category = QStringLiteral("通用参数")) {
    QVariantMap m;
    m["id"] = id; m["label"] = label; m["type"] = "toggle";
    m["default"] = def;
    m["category"] = category;
    return m;
}

static auto input(const QString& id, const QString& label, const QString& def,
                  const QString& placeholder, bool multiline = false) {
    QVariantMap m;
    m["id"] = id; m["label"] = label; m["type"] = "input";
    m["default"] = def; m["placeholder"] = placeholder; m["multiline"] = multiline;
    return m;
}

static auto option(int val, const QString& label) {
    QVariantMap m;
    m["value"] = val; m["label"] = label;
    return m;
}

QVariantList StrategyBridge::buildParamConfigs(int typeIndex) const {
    int ti = normalizeStrategyTypeIndex(typeIndex);
    QVariantList configs;

    // ── 公共参数 ──
    configs << slider("maxPositions", QStringLiteral("最大持仓数"), 20, 1, 100, 1, QStringLiteral("只"), 0);
    configs << select("weightScheme", QStringLiteral("权重方案"), 0, QVariantList{
        option(0, QStringLiteral("等权")),
        option(1, QStringLiteral("市值加权")),
        option(2, QStringLiteral("信号强度")),
        option(3, QStringLiteral("风险平价"))
    });
    configs << slider("maxWeightPerStock", QStringLiteral("单票最大权重"), 0.1, 0.01, 0.5, 0.01, QStringLiteral(""), 2);
    configs << slider("minWeightPerStock", QStringLiteral("单票最小权重"), 0.01, 0, 0.05, 0.005, QStringLiteral(""), 3);
    configs << slider("stopLossPercent", QStringLiteral("止损线(%)"), 10, 0, 30, 1, QStringLiteral("%"), 0);
    configs << slider("takeProfitPercent", QStringLiteral("止盈线(%)"), 20, 5, 100, 1, QStringLiteral("%"), 0);
    configs << slider("maxDrawdownLimit", QStringLiteral("最大回撤限制(%)"), 99, 5, 99, 1, QStringLiteral("%"), 0);
    configs << select("rebalanceFrequency", QStringLiteral("调仓频率"), 0, QVariantList{
        option(0, QStringLiteral("每日")), option(1, QStringLiteral("每周")),
        option(2, QStringLiteral("每月")), option(3, QStringLiteral("每季度"))
    });
    configs << toggle("allowShort", QStringLiteral("允许做空"), false);

    // ── 类型专属参数 ──
    auto P = QStringLiteral("个性化参数");
    if (ti == 0 || ti == 1) {  // TrendFollowing / TrendBreakout
        configs << slider("fastPeriod", QStringLiteral("快线周期"), 5, 2, 60, 1, QStringLiteral("天"), 0, P);
        configs << slider("slowPeriod", QStringLiteral("慢线周期"), 30, 5, 120, 1, QStringLiteral("天"), 0, P);
    }
    if (ti == 2 || ti == 3) {  // MeanReversion / RsiMeanReversion
        configs << slider("period", QStringLiteral("RSI周期"), 14, 5, 50, 1, QStringLiteral("天"), 0, P);
    }
    if (ti == 2) {  // Momentum
        configs << slider("macdFast", QStringLiteral("MACD快线"), 12, 2, 60, 1, QStringLiteral("天"), 0, P);
        configs << slider("macdSlow", QStringLiteral("MACD慢线"), 26, 3, 120, 1, QStringLiteral("天"), 0, P);
        configs << slider("macdSignal", QStringLiteral("MACD信号"), 9, 2, 30, 1, QStringLiteral("天"), 0, P);
    }
    if (ti == 4 || ti == 5) {  // MultiFactor / MachineLearning
        configs << slider("sellThreshold", QStringLiteral("卖出阈值"), 0.2, -5.0, 5.0, 0.1, QStringLiteral("σ"), 1, P);
        configs << slider("sellRankMultiplier", QStringLiteral("排名卖出乘数"), 2.0, 1.0, 10.0, 0.5, QStringLiteral("x"), 1, P);
        configs << slider("minCompositeScore", QStringLiteral("最低综合分"), 0.0, -5.0, 5.0, 0.1, QStringLiteral("σ"), 1, P);
    }
    if (ti == 6) {  // Arbitrage/BollingerBand
        configs << slider("bbPeriod", QStringLiteral("布林带周期"), 20, 5, 60, 1, QStringLiteral("天"), 0, P);
        configs << slider("bbStdDev", QStringLiteral("标准差倍数"), 2.0, 1.0, 4.0, 0.1, QStringLiteral("倍"), 1, P);
    }
    return configs;
}

QVariantMap StrategyBridge::buildCompleteStrategyData(const QVariantMap& context) const {
    int ti = normalizeStrategyTypeIndex(context.value("selectedStrategyTypeIndex", 0).toInt());
    int bk = strategyBehaviorKindFromTypeIndex(ti);

    // 有因子配置时 → 强制 MultiFactor (4)
    auto params = context.value("strategyParameters", QVariantMap()).toMap();
    auto overlay = params.value("factor_overlay", QVariantMap()).toMap();
    bool hasFactorOverlay = overlay.value("enabled", false).toBool()
                         && !overlay.value("allocations", QVariantList()).toList().isEmpty();
    if (hasFactorOverlay || !params.value("factorIds", QVariantList()).toList().isEmpty()) {
        bk = 4;  // StrategyBehaviorKind::MultiFactor
        ti = 4;  // strategyTypeIndex 同步到多因子
    }

    QVariantMap data;
    data["name"] = context.value("strategyName", QStringLiteral("新策略"));
    data["displayName"] = data["name"];
    data["strategyBehaviorKind"] = bk;
    data["strategyTypeIndex"] = ti;
    data["typeName"] = strategyTypeName(ti);
    data["description"] = context.value("strategyDescription", "");

    data["assetTypeIndex"] = context.value("assetTypeIndex", 1);
    data["timeFrameIndex"] = context.value("timeFrameIndex", 7);
    data["riskLevelIndex"] = context.value("riskLevelIndex", 2);
    data["optimizationMethod"] = context.value("optimizationMethod", "genetic");
    data["enableAdvancedOptions"] = context.value("enableAdvancedOptions", false);

    data["maxPositions"] = 20;
    data["maxWeightPerStock"] = 0.1;
    data["minWeightPerStock"] = 0.01;
    data["weightScheme"] = 0;
    data["rebalanceFrequency"] = 0;
    data["allowShort"] = false;
    data["stopLossPercent"] = 10.0;
    data["takeProfitPercent"] = 20.0;
    data["maxDrawdownLimit"] = 99.0;

    data["parameters"] = params;
    data["rule_profile"] = params.value("rule_profile", QVariantMap());
    return data;
}

QVariantMap StrategyBridge::resetFormData() const {
    QVariantMap data;
    data["strategyName"] = "";
    data["strategyDescription"] = "";
    data["selectedStrategyTypeIndex"] = 0;
    data["strategyTags"] = QVariantList();
    data["assetType"] = "stock";
    data["timeFrame"] = "daily";
    data["riskLevel"] = "medium";
    data["optimizationMethod"] = "genetic";
    data["enableAdvancedOptions"] = false;
    data["strategyParameters"] = QVariantMap();
    data["parametersValid"] = false;
    data["validationMessage"] = "";
    return data;
}

QString StrategyBridge::defaultStrategyDescription(int typeIndex) const {
    return strategyTypeBrief(typeIndex);
}
QStringList StrategyBridge::defaultStrategyTags(int) const {
    return {QStringLiteral("量化"), QStringLiteral("A股")};
}

// ── 规则编辑器 (基本实现) ──
QVariantMap StrategyBridge::buildDefaultStrategyProfile(int typeIndex) const {
    int bk = strategyBehaviorKindFromTypeIndex(typeIndex);
    QVariantMap p;
    p["strategyTypeIndex"] = typeIndex;
    p["strategyBehaviorKind"] = bk;
    p["horizon"] = "swing";
    p["tradingFrequency"] = "low_frequency";
    p["marketScope"] = "a_share";
    p["executionStyle"] = "close_confirmed";
    return p;
}

QVariantList StrategyBridge::buildDefaultBaseRuleBindings(const QVariantMap&) const {
    QVariantList bindings;
    // 默认: 趋势模板 → 承接走弱退出 + 分批止盈
    auto makeRule = [](const QString& tid, const QString& phase, const QString& group) {
        QVariantMap r;
        r["templateId"] = tid; r["bindingPhase"] = phase.toInt();
        r["groupId"] = group; r["defaultInjected"] = true;
        return r;
    };
    bindings << makeRule("template_exit_acceptance_breakdown_v1", "4", "rebalance_exit");
    bindings << makeRule("template_exit_scale_out_take_profit_v1", "4", "rebalance_scale");
    return bindings;
}

QVariantList StrategyBridge::buildDefaultMarketRuleBindings(const QVariantMap&) const {
    QVariantList bindings;
    auto makeRule = [](const QString& tid, const QString& phase, const QString& group) {
        QVariantMap r;
        r["templateId"] = tid; r["bindingPhase"] = phase.toInt();
        r["groupId"] = group; r["defaultInjected"] = true;
        return r;
    };
    bindings << makeRule("template_risk_market_bear_freeze_entry_v1", "2", "market_gate");
    bindings << makeRule("template_risk_market_bull_trend_allow_entry_v1", "2", "market_gate");
    bindings << makeRule("template_risk_market_trend_neutral_allow_entry_v1", "2", "market_gate");
    return bindings;
}

QVariantList StrategyBridge::buildDefaultRuleComposerSkeleton(const QVariantMap& profile, const QVariantList& bindings) const {
    QVariantList stages;
    auto findGroup = [&](const QString& groupId) -> QVariantList {
        QVariantList rules;
        for (auto& b : bindings) {
            auto m = b.toMap();
            if (m["groupId"].toString() == groupId) {
                QVariantMap r;
                r["templateId"] = m["templateId"];
                r["bindingPhase"] = m["bindingPhase"];
                r["defaultInjected"] = true;
                rules << r;
            }
        }
        return rules;
    };

    // Market stage
    QVariantMap marketStage;
    marketStage["stageId"] = "market"; marketStage["stageTitle"] = QStringLiteral("市场闸门");
    marketStage["bindingPhase"] = 0;
    QVariantList marketGroups;
    QVariantMap gateGroup;
    gateGroup["groupId"] = "market_gate"; gateGroup["groupTitle"] = QStringLiteral("市场放行组");
    gateGroup["groupRole"] = "must_pass"; gateGroup["groupOperator"] = "all";
    gateGroup["rules"] = findGroup("market_gate");
    marketGroups << gateGroup;
    marketStage["groups"] = marketGroups;
    stages << marketStage;

    // Rebalance stage
    QVariantMap rebalanceStage;
    rebalanceStage["stageId"] = "rebalance"; rebalanceStage["stageTitle"] = QStringLiteral("调仓管理");
    rebalanceStage["bindingPhase"] = 3;
    QVariantList rebalanceGroups;
    QVariantMap exitGroup;
    exitGroup["groupId"] = "rebalance_exit"; exitGroup["groupTitle"] = QStringLiteral("退出触发组");
    exitGroup["groupRole"] = "any_pass"; exitGroup["groupOperator"] = "any";
    exitGroup["rules"] = findGroup("rebalance_exit");
    rebalanceGroups << exitGroup;
    QVariantMap scaleGroup;
    scaleGroup["groupId"] = "rebalance_scale"; scaleGroup["groupTitle"] = QStringLiteral("分批管理组");
    scaleGroup["groupRole"] = "position_management"; scaleGroup["groupOperator"] = "all";
    scaleGroup["rules"] = findGroup("rebalance_scale");
    rebalanceGroups << scaleGroup;
    rebalanceStage["groups"] = rebalanceGroups;
    stages << rebalanceStage;

    return stages;
}

QVariantMap StrategyBridge::validateRuleComposerConfiguration(const QVariantMap&, const QVariantList&) const {
    QVariantMap result;
    result["valid"] = true;
    result["errorCount"] = 0;
    result["warningCount"] = 0;
    result["errors"] = QVariantList();
    result["warnings"] = QVariantList();
    result["suggestions"] = QVariantList();
    result["summaryText"] = QStringLiteral("配置有效");
    return result;
}

QString StrategyBridge::resolveRuleTemplateFileName(const QString& templateId) const {
    return templateId + ".yaml";
}

// ── 模板洞察 ──
QString StrategyBridge::getTemplateInsight(const QVariantMap& rule) const {
    auto s = rule.value("summary").toString();
    if (!s.isEmpty()) return s;
    s = rule.value("templateDisplayName").toString();
    if (!s.isEmpty()) return s;
    return rule.value("templateId").toString();
}
QString StrategyBridge::insightSectionTitle(const QString& phaseKey) const {
    return phaseDisplayName(phaseKey);
}
QString StrategyBridge::insightPrimaryTitle(const QString&) const {
    return QStringLiteral("主要信号");
}
QVariantList StrategyBridge::insightPrimaryItems(const QVariantMap&) const { return {}; }
QString StrategyBridge::insightSecondaryTitle(const QString&) const {
    return QStringLiteral("辅助条件");
}
QVariantList StrategyBridge::insightSecondaryItems(const QVariantMap&) const { return {}; }

QString StrategyBridge::normalizePhaseKey(const QString& raw) const {
    if (raw == "market" || raw == "0") return "market";
    if (raw == "signal" || raw == "1") return "signal";
    if (raw == "eligibility" || raw == "2") return "eligibility";
    if (raw == "rebalance" || raw == "3") return "rebalance";
    if (raw == "portfolio" || raw == "4") return "portfolio";
    if (raw == "execution" || raw == "5") return "execution";
    if (raw == "account_risk" || raw == "6") return "account_risk";
    return raw;
}

QString StrategyBridge::phaseDisplayName(const QString& phaseKey) const {
    if (phaseKey == "market") return QStringLiteral("市场闸门");
    if (phaseKey == "signal") return QStringLiteral("信号审核");
    if (phaseKey == "eligibility") return QStringLiteral("资格检查");
    if (phaseKey == "rebalance") return QStringLiteral("调仓管理");
    if (phaseKey == "portfolio") return QStringLiteral("组合构建");
    if (phaseKey == "execution") return QStringLiteral("执行管理");
    if (phaseKey == "account_risk") return QStringLiteral("账户风控");
    return phaseKey;
}

// ── 翻译 ──
static const std::vector<std::pair<QString, QString>> kTranslations = {
    // 策略创建
    {"strategyCreation.selectStrategyType", QStringLiteral("选择策略类型")},
    {"strategyCreation.strategyBasicInfo", QStringLiteral("基本信息")},
    {"strategyCreation.basicInfo", QStringLiteral("基本信息")},
    {"strategyCreation.paramConfig", QStringLiteral("参数配置")},
    {"strategyCreation.reviewConfirm", QStringLiteral("审核确认")},
    {"strategyCreation.strategyName", QStringLiteral("策略名称")},
    {"strategyCreation.strategyNamePlaceholder", QStringLiteral("输入策略名称，如：AI择时策略")},
    {"strategyCreation.strategyNameError", QStringLiteral("策略名称至少需要2个字符")},
    {"strategyCreation.strategyDescription", QStringLiteral("策略描述")},
    {"strategyCreation.strategyDescriptionPlaceholder", QStringLiteral("输入策略描述（可选）")},
    {"strategyCreation.optimizationMethod", QStringLiteral("优化方式")},
    {"strategyCreation.assetType", QStringLiteral("资产类型")},
    {"strategyCreation.assetTypes", QStringLiteral("股票,ETF,可转债")},
    {"strategyCreation.timeFrame", QStringLiteral("时间周期")},
    {"strategyCreation.timeFrames", QStringLiteral("日线,周线,月线")},
    {"strategyCreation.createStrategy", QStringLiteral("创建策略")},
    {"strategyCreation.back", QStringLiteral("返回")},
    {"strategyCreation.next", QStringLiteral("下一步")},
    {"strategyCreation.complete", QStringLiteral("完成")},
    {"strategyCreation.maxPositions", QStringLiteral("最大持仓数")},
    {"strategyCreation.weightScheme", QStringLiteral("权重方案")},
    {"strategyCreation.stopLoss", QStringLiteral("止损线")},
    {"strategyCreation.takeProfit", QStringLiteral("止盈线")},
    {"strategyCreation.maxDrawdown", QStringLiteral("最大回撤")},
    // 策略类型
    {"strategyCreation.strategyTypes.trend_following", QStringLiteral("趋势跟随")},
    {"strategyCreation.strategyTypes.trend_breakout", QStringLiteral("趋势突破")},
    {"strategyCreation.strategyTypes.mean_reversion", QStringLiteral("均值回归")},
    {"strategyCreation.strategyTypes.momentum", QStringLiteral("动量")},
    {"strategyCreation.strategyTypes.multi_factor", QStringLiteral("多因子选股")},
    {"strategyCreation.strategyTypes.machine_learning", QStringLiteral("机器学习")},
    {"strategyCreation.strategyTypes.arbitrage", QStringLiteral("统计套利")},
    {"strategyCreation.strategyTypes.event_driven", QStringLiteral("事件驱动")},
    {"strategyCreation.strategyTypes.high_frequency", QStringLiteral("高频交易")},
    {"strategyCreation.strategyTypes.custom", QStringLiteral("自定义策略")},
    // 风险等级
    {"risk.low", QStringLiteral("低风险")},
    {"risk.medium", QStringLiteral("中风险")},
    {"risk.high", QStringLiteral("高风险")},
    {"risk.aggressive", QStringLiteral("激进")},
    // 通用
    {"common.confirm", QStringLiteral("确认")},
    {"common.cancel", QStringLiteral("取消")},
    {"common.save", QStringLiteral("保存")},
    {"common.delete", QStringLiteral("删除")},
    // 规则
    {"rules.market", QStringLiteral("市场闸门")},
    {"rules.signal", QStringLiteral("信号审核")},
    {"rules.rebalance", QStringLiteral("调仓管理")},
    // 描述
    {"strategyCreation.strategyTypeDescriptions.trend_following",
     QStringLiteral("基于双均线金叉死叉的趋势跟踪策略，顺势而为")},
    {"strategyCreation.strategyTypeDescriptions.mean_reversion",
     QStringLiteral("捕捉超买超卖后的价格回归机会")},
    {"strategyCreation.strategyTypeDescriptions.momentum",
     QStringLiteral("追踪强势股的持续上涨趋势")},
    {"strategyCreation.strategyTypeDescriptions.multi_factor",
     QStringLiteral("多因子加权综合排名选股，分散单因子风险")},
    {"strategyCreation.strategyTypeDescriptions.machine_learning",
     QStringLiteral("机器学习模型驱动的智能选股策略")},
    {"strategyCreation.strategyTypeDescriptions.arbitrage",
     QStringLiteral("利用价差偏离进行统计套利")},
    {"strategyCreation.strategyTypeDescriptions.event_driven",
     QStringLiteral("基于财报、公告等事件的交易策略")},
    {"strategyCreation.strategyTypeDescriptions.high_frequency",
     QStringLiteral("分钟级别高频交易策略")},
    {"strategyCreation.strategyTypeDescriptions.custom",
     QStringLiteral("用户自定义参数的灵活策略")},
    // ── 步骤2: 参数配置页 (StrategyParamConfig) ──
    {"strategyCreation.step2Title", QStringLiteral("参数配置")},
    {"strategyCreation.step2Description", QStringLiteral("配置策略的运行参数和因子权重")},
    {"strategyCreation.commonParameters", QStringLiteral("通用参数")},
    {"strategyCreation.personalizedParameters", QStringLiteral("个性化参数")},
    {"strategyCreation.parameterConfigPanel", QStringLiteral("参数配置面板")},
    {"strategyCreation.configuredParameters", QStringLiteral("已配置参数")},
    {"strategyCreation.parameterValidationPassed", QStringLiteral("参数校验通过")},
    {"strategyCreation.parameterValidationRequired", QStringLiteral("请完成参数配置")},
    {"strategyCreation.parameterOptimizationRange", QStringLiteral("参数优化范围")},
    {"strategyCreation.parameterOptimizationRangeOptions", QStringLiteral("默认范围,自定义范围,全范围搜索")},
    {"strategyCreation.sensitivityAnalysis", QStringLiteral("敏感性分析")},
    {"strategyCreation.sensitivityAnalysisOptions", QStringLiteral("不启用,单参数分析,多参数分析")},
    {"strategyCreation.parameterConstraints", QStringLiteral("参数约束")},
    {"strategyCreation.parameterConstraintOptions", QStringLiteral("无约束,正整数,正浮点,自定义范围")},
    {"strategyCreation.parameterInitializationMethod", QStringLiteral("参数初始化方式")},
    {"strategyCreation.parameterInitializationMethods", QStringLiteral("默认值,随机采样,网格搜索,遗传算法")},
    {"strategyCreation.customParameterScript", QStringLiteral("自定义参数脚本")},
    {"strategyCreation.customParameterScriptPlaceholder", QStringLiteral("输入自定义参数脚本（可选）")},
    // ── 步骤1: 基本信息 (StrategyBasicInfo) ──
    {"strategyCreation.riskLevel", QStringLiteral("风险等级")},
    {"strategyCreation.optimizationMethods", QStringLiteral("遗传算法,网格搜索,贝叶斯优化,随机搜索")},
    {"strategyCreation.tags", QStringLiteral("标签")},
    {"strategyCreation.tagsPlaceholder", QStringLiteral("输入标签，按回车添加（可选）")},
    {"strategyCreation.optimizationMethodValues", QStringLiteral("genetic,grid_search,bayesian,random")},
};

QString StrategyBridge::tr(const QString& key, const QString&) const {
    for (auto& [k, v] : kTranslations)
        if (k == key) return v;
    // 策略类型名: 尝试从 key 推断
    if (key.contains("TrendFollowing") || key.contains("trend_following"))
        return QStringLiteral("趋势跟随");
    if (key.contains("MeanReversion") || key.contains("mean_reversion"))
        return QStringLiteral("均值回归");
    if (key.contains("Momentum") || key.contains("momentum"))
        return QStringLiteral("动量");
    if (key.contains("MultiFactor") || key.contains("multi_factor"))
        return QStringLiteral("多因子选股");
    if (key.contains("Arbitrage") || key.contains("arbitrage"))
        return QStringLiteral("套利");
    // fallback
    int dot = key.lastIndexOf('.');
    return dot >= 0 ? key.mid(dot + 1) : key;
}