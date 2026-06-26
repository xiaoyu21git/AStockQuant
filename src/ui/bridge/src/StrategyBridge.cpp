#include "../include/StrategyBridge.h"

#include "../include/StrategyLifecycleStatus.h"
#include "../include/StrategyListModel.h"

#include "database/MarketDataRepository.h"
#include "database/NativeMySQLConnectionPool.h"
#include "database/ISqlDatabase.h"
#include "database/NativePgDatabase.h"
#include "database/StrategyRepository.h"
#include "FactorService.h"


#include "../../domain/backtest/include/ResolvedStrategyBehavior.h"
#include "../../domain/trading/TradeExecutionEngine.h"
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
    if (request.factorIds().provided) target.metadata.factorIds = request.factorIds().values;
    if (request.ruleIds().provided) target.metadata.ruleIds = request.ruleIds().values;
    target.parameters = request.parameters();
    if (request.strategyType().valid) {
        const int typeIndex = static_cast<int>(request.strategyType().value);
        if (!isTypeIdxValid(typeIndex)) std::abort();
        target.strategyIdentity = domain::backtest::ResolvedStrategyIdentity{
            static_cast<domain::backtest::StrategyStoredType>(typeIndex),
            domain::backtest::ResolvedStrategyBehavior{
                static_cast<domain::backtest::StrategyBehaviorKind>(
                    static_cast<int>(target.metadata.behaviorKind)), true},
            true
        };
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
}

StrategyBridge::~StrategyBridge() = default;

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

    // ── 异步委托给 StrategyManager（工作线程：MySQL 查询 → 历史数据 → 启动实盘循环）──
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