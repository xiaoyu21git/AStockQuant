#include "../include/StrategyBridge.h"

#include "../include/StrategyLifecycleStatus.h"
#include "../include/StrategyListModel.h"

#include "database/StrategyRepository.h"
#include "database/MarketDataRepository.h"
#include "database/NativeMySQLConnectionPool.h"
#include "FactorService.h"

#include "../../app/system/TradingSystem.h"
#include "../../domain/backtest/include/ResolvedStrategyBehavior.h"
#include "../../domain/factor/include/FactorInstanceManager.h"
#include "../../domain/factor/include/factor_compute/CachedMarketDataView.h"
#include "../../domain/strategies/include/StrategyDefinitionTypes.h"
#include "foundation/json/json_facade.h"
#include "../../domain/strategy/include/StrategyManager.h"

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

// ── 策略订单监听器：将策略引擎信号转发到 TradingSystem 执行管道 ──
namespace {

using SymbolResolver = std::function<std::string(std::uint32_t instrumentId)>;

class StrategyOrderForwarder final : public domain::strategy::IOrderListener {
public:
    explicit StrategyOrderForwarder(SymbolResolver resolver)
        : m_resolver(std::move(resolver)) {}

    void onOrders(const std::vector<domain::strategy::OrderRequest>& orders) override {
        for (const auto& req : orders) {
            if (!req.isValid()) continue;

            domain::trading::TradeOrder order;
            order.setSymbol(m_resolver(req.instrumentId().value));
            order.setSide(req.side() == domain::strategy::RuntimeOrderSide::Buy
                              ? domain::strategy::OrderDirection::Buy
                              : domain::strategy::OrderDirection::Sell);
            order.setQuantity(static_cast<std::int64_t>(req.quantity()));
            order.setStrategyId(std::to_string(req.strategyInstanceId()));
            order.setPrice(1.0);  // 实盘由券商网关确定实际成交价

            app::system::TradingSystem::instance().submitOrder(order);
        }
    }

private:
    SymbolResolver m_resolver;
};

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
}

StrategyBridge::~StrategyBridge() = default;

void StrategyBridge::init()
{
    if (m_inited) return;
    if (!m_repo || !m_repo->initialize()) {
        setErr(QStringLiteral("initialize strategy repository failed"));
        emit operationFailed(kRepositoryErrorCode, m_err);
        return;
    }
    m_inited = true;
    emit initedChanged();
    refreshModel();
}

void StrategyBridge::initAsync()
{
    QTimer::singleShot(0, this, [this]() { init(); });
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

    // 不设中间状态，等 worker 完成后直接切"运行中"
    m_repo->updateStatus(repositoryId, strategy_view::StrategyLifecycleStatus::Active);

    // ── 异步执行引擎创建（工作线程：MySQL 查询 → 启动实盘循环）──
    if (!m_startupPool) {
        m_startupPool = std::make_unique<foundation::thread::ThreadPoolExecutor>(
            1, 4, std::chrono::seconds(120), "StrategyBridgeStartup");
    }
    m_startupPool->post([this, repositoryId]() {
        try {
        auto& mgr = domain::strategy::StrategyManager::instance();
        if (!mgr.get(repositoryId.toStdString())) {
            std::unique_ptr<domain::strategy::IRuntimeFactorService> factorSvc;
            auto* factorSvcBridge = FactorService::instance();
            if (factorSvcBridge && factorSvcBridge->isInitialized()) {
                auto* instanceMgr = factorSvcBridge->instanceManager();
                if (instanceMgr) {
                    auto symbolResolver = [](std::uint32_t id) -> std::string {
                        char buf[16];
                        const char* suffix = (id >= 600000 && id < 700000) ? ".SH" : ".SZ";
                        std::snprintf(buf, sizeof(buf), "%06u%s", id, suffix);
                        return buf;
                    };
                    auto factorNameResolver = [](std::uint64_t fid) -> std::string {
                        return std::to_string(fid);
                    };
                    factorSvc = std::make_unique<domain::strategy::RuntimeFactorSvc>(
                        *instanceMgr,
                        std::move(symbolResolver),
                        std::move(factorNameResolver));
                }
            }
            mgr.createEngine(repositoryId.toStdString(), std::move(factorSvc));
        }
        auto* engine = mgr.get(repositoryId.toStdString());
        if (!engine) {
            QMetaObject::invokeMethod(this, [this]() {
                setErr(QStringLiteral("引擎创建失败"));
                emit operationFailed(kRepositoryErrorCode, m_err);
            }, Qt::QueuedConnection);
            return;
        }

        const auto result = engine->start();
        if (!result.isOk()) {
            QMetaObject::invokeMethod(this, [this]() {
                setErr(QStringLiteral("引擎启动失败"));
                emit operationFailed(kRepositoryErrorCode, m_err);
            }, Qt::QueuedConnection);
            return;
        }

        // 加载历史 MarketView（因子策略必需，非因子策略无害）
        {
            auto& pool = astock::database::NativeMySQLConnectionPool::instance();
            if (pool.isInitialized()) {
                auto db = pool.getConnection();
                if (db && db->isOpen()) {
                    auto repo = std::make_unique<astock::infrastructure::database::MarketDataRepository>(db);
                    // 获取当前日期 YYYY-MM-DD
                    auto now = std::chrono::system_clock::now();
                    auto tt = std::chrono::system_clock::to_time_t(now);
                    char endBuf[16];
                    std::strftime(endBuf, sizeof(endBuf), "%Y-%m-%d", std::localtime(&tt));
                    // 往前推足够交易日 (MySQL DATE_SUB 用日历日近似)
                    constexpr int kLookbackDays = 365;  // 约 250 个交易日
                    char startBuf[16];
                    {
                        auto tp = std::chrono::system_clock::now() - std::chrono::hours(24 * kLookbackDays);
                        auto t = std::chrono::system_clock::to_time_t(tp);
                        std::strftime(startBuf, sizeof(startBuf), "%Y-%m-%d", std::localtime(&t));
                    }
                    INTERNAL_INFO_STREAM << "[StrategyBridge] loading live market data: " << startBuf << " ~ " << endBuf;
                    auto rows = repo->queryDailyBarWithFields({}, startBuf, endBuf, {});
                    if (!rows.empty()) {
                        // 转 QVariantList → JSON → CachedMarketDataView
                        QVariantList data;
                        for (auto& r : rows) {
                            QVariantMap row;
                            row["symbol"] = QString::fromStdString(r.symbol);
                            row["trade_date"] = QString::fromStdString(r.tradeDate);
                            row["open"] = r.open;
                            row["high"] = r.high;
                            row["low"] = r.low;
                            row["close"] = r.close;
                            row["volume"] = r.volume;
                            data.append(row);
                        }
                        QJsonDocument doc(QJsonArray::fromVariantList(data));
                        auto root = foundation::json::JsonFacade::parse(doc.toJson(QJsonDocument::Compact).toStdString());
                        m_liveMarketView = factor::compute::CachedMarketDataView::fromJson(root);
                        engine->setLiveMarketView(m_liveMarketView.get());
                        INTERNAL_INFO_STREAM << "[Live] history loaded: " << rows.size() << " rows, "
                                         << m_liveMarketView->instruments().size() << " 标的";
                    }
                }
            }
        }

        // 先注册订单监听器，再启动 loop，避免漏单
        if (!m_orderListener) {
            auto symResolver = [](std::uint32_t id) -> std::string {
                char buf[16];
                const char* suffix = (id >= 600000 && id < 700000) ? ".SH" : ".SZ";
                std::snprintf(buf, sizeof(buf), "%06u%s", id, suffix);
                return buf;
            };
            m_orderListener = std::make_unique<StrategyOrderForwarder>(std::move(symResolver));
        }
        mgr.setOrderListener(m_orderListener.get());

        engine->startLiveLoop();

        QMetaObject::invokeMethod(this, [this, repositoryId]() {
            m_runtimeStatus[repositoryId] = QStringLiteral("运行中");
            if (m_listModel) m_listModel->updateDisplayStatus(repositoryId, QStringLiteral("运行中"));
            emit strategiesChanged();
            emit started(repositoryId);
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
        auto& mgr = domain::strategy::StrategyManager::instance();
        auto* engine = mgr.get(repositoryId.toStdString());
        if (engine) {
            engine->stopLiveLoop();
            engine->stop();
        }
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

    // 从 JSON 构建 CachedMarketDataView（公用 fromJson 工厂）
    auto root = foundation::json::JsonFacade::parse(datasetJson.toStdString());
    m_liveMarketView = factor::compute::CachedMarketDataView::fromJson(root);
    engine->setLiveMarketView(m_liveMarketView.get());
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