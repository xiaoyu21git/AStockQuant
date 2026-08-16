#include "../include/StrategyBridge.h"
#include "../include/TemplateInsights.h"
#include <QJsonDocument>

// 静态单例指针
StrategyBridge* StrategyBridge::s_instance = nullptr;

#include "../include/StrategyLifecycleStatus.h"
#include "../include/StrategyListModel.h"
#include "StockNameResolver.h"
#include "AppStoragePaths.h"

#include "database/MarketDataRepository.h"
#include "database/NativePgConnectionPool.h"
#include "database/ISqlDatabase.h"
#include "../../engine/include/AccountEngine.h"
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

#include "foundation/config/ConfigManager.hpp"

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
        QStringLiteral("strategyType"),  // 枚举名字符串 (如 "MACHINE_LEARNING_SELECTION"); 数字键已永久删除
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

StrategyBridge::StrategyTypeSpec StrategyBridge::readTypeSpec(const QVariantMap& payload) const
{
    StrategyTypeSpec spec;
    const QVariant rawValue = payload.value(QStringLiteral("strategyType"));
    if (!rawValue.isValid() || rawValue.isNull()) return spec;
    spec.provided = true;
    // 严格精确解析枚举名; 数字/旧键/模糊匹配一律拒绝, 无回退
    const auto parsed = domain::strategies::StrategyTypeRegistry::fromTypeId(
        rawValue.toString().trimmed().toStdString());
    if (!parsed.has_value()) return spec;
    spec.value = *parsed;
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
    target.status = request.status()
        ? strategy_view::StrategyLifecycleStatus::Active
        : strategy_view::StrategyLifecycleStatus::Inactive;
    target.metadata.enabled = request.status();
    if (request.ruleIds().provided) target.metadata.ruleIds = request.ruleIds().values;
    target.parameters = request.parameters();
    if (request.strategyType().valid) {
        target.strategyType = request.strategyType().value;
        // 行为类型一律由策略类型推导, 不再从载荷读取
        target.metadata.behaviorKind =
            domain::strategies::StrategyTypeRegistry::behaviorKindOf(target.strategyType.value());
        // identity 由注册表显式构建 (逐名对应, 禁止数值强转)
        target.strategyIdentity = domain::backtest::ResolvedStrategyIdentity{
            domain::strategies::StrategyTypeRegistry::storedTypeOf(target.strategyType.value()),
            domain::backtest::ResolvedStrategyBehavior{
                domain::strategies::StrategyTypeRegistry::backtestBehaviorKindOf(target.strategyType.value()),
                true},
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
    INTERNAL_INFO_STREAM << "[Bridge] 构造";
    s_instance = this;
}

StrategyBridge::~StrategyBridge()
{
    if (s_instance == this) s_instance = nullptr;
}

void StrategyBridge::init()
{
    if (m_inited) return;
    INTERNAL_INFO_STREAM << "[Bridge] 初始化开始";
    try {
        if (!m_repo || !m_repo->initialize()) {
            INTERNAL_ERROR_STREAM << "[Bridge] 初始化失败: 仓储初始化";
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

            // ── 实盘成交回报 → TradeJournal ──
            engine.setOnTradeFill([](const domain::trading::TradeFill& fill) {
                const std::string brokerId = fill.brokerOrderId().text();
                if (brokerId.empty()) return;
                auto order = domain::trading::TradeExecutionEngine::instance()
                    .findOrderByBrokerId(brokerId);
                if (!order.has_value()) return;
                auto* eng = domain::strategy::StrategyManager::instance()
                    .get(order->strategyId());
                if (!eng) return;
                std::string side = order->side() == domain::strategy::OrderDirection::Buy
                    ? "买入" : "卖出";
                eng->logExecutionFill(order->symbol(), side,
                    fill.price(), fill.quantity(), fill.commission(),
                    fill.tradeTime().to_string(), brokerId, order->traceId());
            });
        }

        // ── 半自动模式: 注册篮子拦截器 + 切换所有引擎为 SemiAuto ──
        {
            mgr.setDefaultBasketInterceptor(this);
            mgr.setBasketInterceptor(this);
            mgr.setExecutionMode(domain::strategy::EngineExecutionMode::SemiAuto);
        }

        INTERNAL_INFO_STREAM << "[Bridge] 初始化仓储成功, 调用刷新模型";
        m_inited = true;
        emit initedChanged();
        refreshModel();
        INTERNAL_INFO_STREAM << "[Bridge] 初始化完成";
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[Bridge] 初始化异常: " << e.what();
        const QString msg = QString::fromUtf8(e.what());
        INTERNAL_ERROR_STREAM << "[StrategyBridge] 初始化异常: " << msg.toStdString();
        setErr(QStringLiteral("strategy init failed: %1").arg(msg));
        emit operationFailed(kRepositoryErrorCode, m_err);
    } catch (...) {
        INTERNAL_ERROR_STREAM << "[Bridge] 初始化未知异常";
        INTERNAL_ERROR_STREAM << "[StrategyBridge] 初始化未知异常";
        setErr(QStringLiteral("strategy init failed: unknown error"));
        emit operationFailed(kRepositoryErrorCode, m_err);
    }
}

void StrategyBridge::initAsync()
{
    INTERNAL_INFO_STREAM << "[Bridge] initAsync 被调用";
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
    if (!isVariantMapObject(parameters, QString::fromLatin1(kRuleProfileKey))) {
        setErr(QStringLiteral("add: parameters 缺少 rule_profile 对象 (规则配置)"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }
    if (!isVariantMapObject(parameters, QString::fromLatin1(kRuleComposerStateKey))) {
        setErr(QStringLiteral("add: parameters 缺少 rule_composer_state 对象 (规则编辑器状态)"));
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
        setErr(QStringLiteral("add: factorIds 含无效值 (空字符串或非字符串元素)"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }
    if (!request.ruleIds().valid) {
        setErr(QStringLiteral("add: ruleIds 含无效值 (零值或非数字元素)"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }
    if (request.strategyName().empty()) {
        setErr(QStringLiteral("add strategyName is required"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return {};
    }
    if (!request.strategyType().valid) {
        setErr(QStringLiteral("add strategyType is required (枚举名字符串, 如 MACHINE_LEARNING_SELECTION)"));
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
    if (!isVariantMapObject(parameters, QString::fromLatin1(kRuleProfileKey))) {
        setErr(QStringLiteral("update: parameters 缺少 rule_profile 对象 (规则配置)"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    if (!isVariantMapObject(parameters, QString::fromLatin1(kRuleComposerStateKey))) {
        setErr(QStringLiteral("update: parameters 缺少 rule_composer_state 对象 (规则编辑器状态)"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    const BridgeUpsertRequest request = parseReq(payload);
    if (!request.factorIds().valid) {
        setErr(QStringLiteral("update: factorIds 含无效值 (空字符串或非字符串元素)"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    if (!request.ruleIds().valid) {
        setErr(QStringLiteral("update: ruleIds 含无效值 (零值或非数字元素)"));
        emit operationFailed(kInvalidArgumentCode, m_err);
        return false;
    }
    if (request.strategyType().provided && !request.strategyType().valid) {
        setErr(QStringLiteral("update strategyType is invalid (非法枚举名)"));
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

    // 实盘实时数据: runningDays从DB, dailyPnL/position从AccountEngine
    // 提升到循环外: positions() 与策略无关，所有策略共享同一账户汇总
    auto snap = engine::AccountEngine::instance().snapshot();
    double totalPnl = 0.0, totalMv = 0.0;
    int tradeCount = 0;
    for (const auto& p : snap.positions) {
        totalPnl += p.unrealizedPnl;
        totalMv += p.marketValue;
        if (p.quantity > 0) ++tradeCount;
    }

    for (const PersistedStrategyData& data : all) {
        auto map = data.toVariantMap();
        QString sid = QString::fromStdString(data.strategyId);
        map["displayStatus"] = m_runtimeStatus.value(sid, QStringLiteral("已停止"));
        map["dailyPnL"] = totalPnl;
        map["position"] = totalMv;
        map["trades"]   = tradeCount;  // 实盘持仓数作为交易活跃度指标
        INTERNAL_INFO_STREAM << "[StrategyBridge] 列表: " << sid.toStdString()
                             << " returns=" << map["returns"].toDouble()
                             << " runningDays=" << map["runningDays"].toInt()
                             << " holdings=" << tradeCount
                             << " dailyPnL=" << totalPnl
                             << " position=" << totalMv;
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
        INTERNAL_ERROR_STREAM << "[StrategyBridge] 初始化失败";
        return false;
    }

    INTERNAL_INFO_STREAM << "[Live] 启动策略: " << repositoryId.toStdString();

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
                INTERNAL_INFO_STREAM << "[Live] 引擎已启动: " << repositoryId.toStdString();
            }, Qt::QueuedConnection);

        } catch (const std::exception& e) {
            INTERNAL_ERROR_STREAM << "[StrategyBridge] 启动工作线程异常: " << e.what();
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
    std::shared_ptr<factor::compute::CachedMarketDataView> customView =
        factor::compute::CachedMarketDataView::fromJson(root);
    if (customView) {
        // P3 悬垂修复: shared_ptr 移交引擎持有 (m_injectedLiveView) — 局部对象析构后视图依然有效;
        // 引擎下次 prepareMarketData() 重建视图时由新 shared_ptr 接管
        engine->setLiveMarketView(customView);
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

// ── 策略类型枚举 (C++ 枚举唯一事实源, 替代 JS StrategyCreationUtils 的数字映射) ──

using ContractType = StrategyTypeContract::StrategyType;

// QML 契约枚举 → 域枚举 (StrategyTypeContract.h 的 static_assert 已保证逐值一致)
static domain::strategies::StrategyType toDomain(ContractType type)
{
    return static_cast<domain::strategies::StrategyType>(type);
}

// 契约枚举值合法性 (QML 可能传入任意 int)
static bool isContractTypeValid(ContractType type)
{
    return domain::strategies::isValidStrategyTypeIndex(static_cast<int>(type));
}

static constexpr int kInvalidBehaviorKind = -1;  // strategyBehaviorKindOfType 非法输入的返回值

using StrategyTypeMetaEntry = std::tuple<ContractType, QString, QString, QString>;

static const std::vector<StrategyTypeMetaEntry> kStrategyTypeMeta = {
    {ContractType::DoubleMovingAverage,      QStringLiteral("双均线趋势"), QStringLiteral("📈"), QStringLiteral("基于双均线金叉死叉的趋势跟踪策略")},
    {ContractType::TurtleBreakout,           QStringLiteral("海龟突破"),   QStringLiteral("🐢"), QStringLiteral("基于唐奇安通道突破的趋势跟踪策略")},
    {ContractType::BollingerBandMeanReversion, QStringLiteral("布林带回归"), QStringLiteral("📊"), QStringLiteral("基于布林带的均值回归策略")},
    {ContractType::RsiMeanReversion,         QStringLiteral("RSI回归"),    QStringLiteral("📉"), QStringLiteral("基于RSI超买超卖的均值回归策略")},
    {ContractType::MultiFactorSelection,     QStringLiteral("多因子选股"), QStringLiteral("🧩"), QStringLiteral("多因子加权综合排名选股")},
    {ContractType::EarningsSurprise,         QStringLiteral("财报超预期"), QStringLiteral("📰"), QStringLiteral("基于财报超预期事件的交易策略")},
    {ContractType::StatisticalPairTrading,   QStringLiteral("统计配对"),   QStringLiteral("⚖️"), QStringLiteral("基于价差偏离的统计套利配对交易")},
    {ContractType::RiskParityAllocation,     QStringLiteral("风险平价"),   QStringLiteral("🛡️"), QStringLiteral("基于风险平价的资产配置策略")},
    {ContractType::MachineLearningSelection, QStringLiteral("机器学习"),   QStringLiteral("🤖"), QStringLiteral("ML模型驱动的智能选股策略")},
    {ContractType::OrderFlowImbalance,       QStringLiteral("订单流"),     QStringLiteral("⚡"), QStringLiteral("基于订单流不平衡的高频交易策略")},
    {ContractType::VolatilitySpread,         QStringLiteral("波动率套利"), QStringLiteral("📐"), QStringLiteral("基于波动率价差的期权套利策略")},
};

static const std::vector<std::tuple<int, QString, QString>> kRiskLevelMeta = {
    {1, QStringLiteral("低风险"), QStringLiteral("#10B981")},
    {2, QStringLiteral("中风险"), QStringLiteral("#F59E0B")},
    {3, QStringLiteral("高风险"), QStringLiteral("#EF4444")},
    {4, QStringLiteral("激进"),   QStringLiteral("#8B5CF6")},
};

// 枚举 → 元数据; 非法枚举返回 nullptr (调用方自行处理, 禁止回退到其它类型)
static const StrategyTypeMetaEntry* findMeta(ContractType type)
{
    for (const auto& m : kStrategyTypeMeta)
        if (std::get<0>(m) == type) return &m;
    return nullptr;
}

QVariantList StrategyBridge::strategyTypeList() const
{
    QVariantList list;
    for (const auto& m : kStrategyTypeMeta) {
        QVariantMap item;
        item["type"] = static_cast<int>(std::get<0>(m));
        item["id"] = strategyTypeId(std::get<0>(m));
        item["name"] = std::get<1>(m);
        item["icon"] = std::get<2>(m);
        item["brief"] = std::get<3>(m);
        list.append(item);
    }
    return list;
}

QString StrategyBridge::strategyTypeId(ContractType type) const
{
    if (!isContractTypeValid(type)) return {};
    return QString::fromStdString(std::string(
        domain::strategies::StrategyTypeRegistry::typeId(toDomain(type))));
}

int StrategyBridge::strategyTypeFromId(const QString& id) const
{
    const auto parsed = domain::strategies::StrategyTypeRegistry::fromTypeId(id.trimmed().toStdString());
    return parsed.has_value() ? static_cast<int>(*parsed) : kInvalidStrategyType;
}

QString StrategyBridge::strategyTypeName(ContractType type) const
{
    const auto* m = findMeta(type);
    return m ? std::get<1>(*m) : QString();
}

QString StrategyBridge::strategyTypeIcon(ContractType type) const
{
    const auto* m = findMeta(type);
    return m ? std::get<2>(*m) : QString();
}

QString StrategyBridge::strategyTypeBrief(ContractType type) const
{
    const auto* m = findMeta(type);
    return m ? std::get<3>(*m) : QString();
}

int StrategyBridge::strategyBehaviorKindOfType(ContractType type) const
{
    if (!isContractTypeValid(type)) return kInvalidBehaviorKind;
    return static_cast<int>(
        domain::strategies::StrategyTypeRegistry::behaviorKindOf(toDomain(type)));
}

QString StrategyBridge::strategyBehaviorKindName(int behaviorKind) const
{
    switch (static_cast<domain::strategies::StrategyBehaviorKind>(behaviorKind)) {
    case domain::strategies::StrategyBehaviorKind::TrendFollowing:  return QStringLiteral("趋势跟随");
    case domain::strategies::StrategyBehaviorKind::MeanReversion:   return QStringLiteral("均值回归");
    case domain::strategies::StrategyBehaviorKind::Momentum:        return QStringLiteral("动量");
    case domain::strategies::StrategyBehaviorKind::Arbitrage:       return QStringLiteral("套利");
    case domain::strategies::StrategyBehaviorKind::MultiFactor:     return QStringLiteral("多因子");
    case domain::strategies::StrategyBehaviorKind::MachineLearning: return QStringLiteral("机器学习");
    case domain::strategies::StrategyBehaviorKind::EventDriven:     return QStringLiteral("事件驱动");
    case domain::strategies::StrategyBehaviorKind::HighFrequency:   return QStringLiteral("高频");
    case domain::strategies::StrategyBehaviorKind::Custom:          return QStringLiteral("自定义");
    }
    return {};
}

bool StrategyBridge::isPortfolioStrategyType(const QString& strategyTypeId) const
{
    const auto parsed = domain::strategies::StrategyTypeRegistry::fromTypeId(
        strategyTypeId.trimmed().toStdString());
    return parsed.has_value()
        && *parsed == domain::strategies::StrategyType::RISK_PARITY_ALLOCATION;
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

QVariantList StrategyBridge::buildParamConfigs(ContractType type) const {
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
    if (type == ContractType::DoubleMovingAverage || type == ContractType::TurtleBreakout) {
        configs << slider("fastPeriod", QStringLiteral("快线周期"), 5, 2, 60, 1, QStringLiteral("天"), 0, P);
        configs << slider("slowPeriod", QStringLiteral("慢线周期"), 30, 5, 120, 1, QStringLiteral("天"), 0, P);
    }
    if (type == ContractType::BollingerBandMeanReversion || type == ContractType::RsiMeanReversion) {
        configs << slider("period", QStringLiteral("RSI周期"), 14, 5, 50, 1, QStringLiteral("天"), 0, P);
    }
    if (type == ContractType::BollingerBandMeanReversion) {
        configs << slider("macdFast", QStringLiteral("MACD快线"), 12, 2, 60, 1, QStringLiteral("天"), 0, P);
        configs << slider("macdSlow", QStringLiteral("MACD慢线"), 26, 3, 120, 1, QStringLiteral("天"), 0, P);
        configs << slider("macdSignal", QStringLiteral("MACD信号"), 9, 2, 30, 1, QStringLiteral("天"), 0, P);
    }
    // 多因子 / 机器学习 / 业绩惊喜: 三者引擎同走 MultiFactorStrategy 子类,
    // sellThreshold/sellRankMultiplier/minCompositeScore 是引擎真实消费字段, topN 由 fromDb 直接读取
    if (type == ContractType::MultiFactorSelection
        || type == ContractType::MachineLearningSelection
        || type == ContractType::EarningsSurprise) {
        configs << slider("topN", QStringLiteral("TopN"), 50, 1, 500, 1, QStringLiteral(""), 0, P);
        configs << slider("sellThreshold", QStringLiteral("卖出阈值"), 0.2, -5.0, 5.0, 0.1, QStringLiteral("σ"), 1, P);
        configs << slider("sellRankMultiplier", QStringLiteral("排名卖出乘数"), 2.0, 1.0, 10.0, 0.5, QStringLiteral("x"), 1, P);
        configs << slider("minCompositeScore", QStringLiteral("最低综合分"), 0.0, -5.0, 5.0, 0.1, QStringLiteral("σ"), 1, P);
    }
    if (type == ContractType::StatisticalPairTrading) {
        configs << slider("bbPeriod", QStringLiteral("布林带周期"), 20, 5, 60, 1, QStringLiteral("天"), 0, P);
        configs << slider("bbStdDev", QStringLiteral("标准差倍数"), 2.0, 1.0, 4.0, 0.1, QStringLiteral("倍"), 1, P);
    }
    // 风险平价: id 与调优工厂 ParameterSpaceFactory 一致 (V1 不包含资产列表输入)
    if (type == ContractType::RiskParityAllocation) {
        configs << slider("volatilityLookback", QStringLiteral("波动率回看"), 60, 5, 500, 1, QStringLiteral(""), 0, P);
        configs << slider("targetVolatility", QStringLiteral("目标波动率"), 0.0, 0.0, 1.0, 0.01, QStringLiteral(""), 2, P);
    }
    // 订单流失衡 / 波动率套利: 依赖盘口/期权链外部数据, V1 仅通用参数 (与调优工厂裁决一致)
    return configs;
}

QVariantMap StrategyBridge::buildCompleteStrategyData(const QVariantMap& context) const {
    const auto type = static_cast<ContractType>(
        context.value(QStringLiteral("selectedStrategyType")).toInt());
    const QString typeId = strategyTypeId(type);

    auto params = context.value("strategyParameters", QVariantMap()).toMap();

    QVariantMap data;
    data["name"] = context.value("strategyName", QStringLiteral("新策略"));
    data["displayName"] = data["name"];
    // 类型只发枚举名字符串; 数字键与行为类字段已永久删除 (行为类一律服务端推导)
    data["strategyType"] = typeId;
    data["typeName"] = strategyTypeName(type);
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
    data["selectedStrategyType"] = static_cast<int>(ContractType::DoubleMovingAverage);
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

QString StrategyBridge::defaultStrategyDescription(ContractType type) const {
    return strategyTypeBrief(type);
}
QStringList StrategyBridge::defaultStrategyTags(ContractType) const {
    return {QStringLiteral("量化"), QStringLiteral("A股")};
}

// ── 规则编辑器 (基本实现) ──
QVariantMap StrategyBridge::buildDefaultStrategyProfile(ContractType type) const {
    QVariantMap p;
    // 类型只落枚举名字符串; 行为类字段已永久删除 (一律由类型推导)
    p["strategyType"] = strategyTypeId(type);
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
QString StrategyBridge::insightSectionTitle(const QString& phaseKey) const {
    auto nk = normalizePhaseKey(phaseKey);
    if (nk == "market") return QStringLiteral("当前市场风控详情");
    return QStringLiteral("当前模板语义详情");
}

QString StrategyBridge::getTemplateInsight(const QVariantMap& rule) const {
    // 从 rule 提取 templateId，查 kTemplateInsights 返回 insight JSON
    QString tid = rule.value("templateId").toString();
    if (tid.isEmpty()) tid = rule.value("template_id").toString();
    if (tid.isEmpty()) return QStringLiteral("{}");
    auto map = templateInsight(tid);
    if (map.isEmpty()) return QStringLiteral("{}");
    return QJsonDocument::fromVariant(map).toJson(QJsonDocument::Compact);
}

QString StrategyBridge::insightPrimaryTitle(const QString&) const {
    return QStringLiteral("触发要点");
}
QVariantList StrategyBridge::insightPrimaryItems(const QVariantMap& rule) const {
    QString tid = rule.value("templateId").toString();
    if (tid.isEmpty()) tid = rule.value("template_id").toString();
    auto map = templateInsight(tid);
    return map.value("primaryItems").toList();
}
QString StrategyBridge::insightSecondaryTitle(const QString&) const {
    return QStringLiteral("提醒");
}
QVariantList StrategyBridge::insightSecondaryItems(const QVariantMap& rule) const {
    QString tid = rule.value("templateId").toString();
    if (tid.isEmpty()) tid = rule.value("template_id").toString();
    auto map = templateInsight(tid);
    return map.value("secondaryItems").toList();
}

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

QString StrategyBridge::phaseShortName(const QString& phaseKey) const {
    if (phaseKey == "market") return QStringLiteral("市场");
    if (phaseKey == "eligibility") return QStringLiteral("准入");
    if (phaseKey == "signal") return QStringLiteral("入场/信号");
    if (phaseKey == "portfolio") return QStringLiteral("组合");
    if (phaseKey == "rebalance") return QStringLiteral("减仓/退出");
    if (phaseKey == "execution") return QStringLiteral("执行");
    if (phaseKey == "account_risk") return QStringLiteral("账户风控");
    return phaseKey.isEmpty() ? QStringLiteral("未分类") : phaseKey;
}

QString StrategyBridge::categoryDisplayName(const QString& category) const {
    auto key = category.toLower();
    if (key == "entry_pattern") return QStringLiteral("入场形态");
    if (key == "exit_pattern") return QStringLiteral("退出形态");
    if (key == "exit_management") return QStringLiteral("持仓管理");
    if (key == "market_gate") return QStringLiteral("市场放行");
    if (key == "market_risk") return QStringLiteral("市场风控");
    if (key == "watch_invalidation") return QStringLiteral("观察失效");
    return key;
}

QString StrategyBridge::actionDisplayName(const QString& action) const {
    auto key = action.toLower();
    if (key == "candidate_entry") return QStringLiteral("候选入场");
    if (key == "watch") return QStringLiteral("观察");
    if (key == "block") return QStringLiteral("阻断");
    if (key == "unwatch") return QStringLiteral("取消观察");
    if (key == "note") return QStringLiteral("备注");
    if (key == "reduce") return QStringLiteral("减仓");
    if (key == "exit") return QStringLiteral("退出");
    if (key == "cooldown") return QStringLiteral("冷却");
    if (key == "freeze") return QStringLiteral("冻结");
    if (key == "state_switch") return QStringLiteral("状态切换");
    if (key == "halt") return QStringLiteral("暂停");
    if (key == "open") return QStringLiteral("开仓");
    if (key == "score") return QStringLiteral("评分");
    if (key == "tag") return QStringLiteral("打标签");
    return action;
}

QVariantMap StrategyBridge::templateInsight(const QString& templateId) const {
    using astock::bridge::kTemplateInsights;
    QVariantMap result;
    auto it = kTemplateInsights.find(templateId);
    if (it != kTemplateInsights.end()) {
        result["summary"] = it->second.summary;
        result["primaryTitle"] = it->second.primaryTitle;
        result["secondaryTitle"] = it->second.secondaryTitle;
        result["primaryItems"] = it->second.primaryItems;
        result["secondaryItems"] = it->second.secondaryItems;
        result["templateId"] = templateId;
        return result;
    }
    return result;  // empty = caller uses fallback
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
    // 通用导航
    {"common.previous", QStringLiteral("上一步")},
    {"common.next", QStringLiteral("下一步")},
    {"common.create", QStringLiteral("创建")},
    {"common.saveChanges", QStringLiteral("保存修改")},
    // 策略创建补充
    {"strategyCreation.create", QStringLiteral("创建")},
    {"strategyCreation.validationPassed", QStringLiteral("验证通过")},
    {"strategyCreation.validationRequired", QStringLiteral("需完成验证")},
    {"strategyCreation.strategyCreatedSuccess", QStringLiteral("策略创建成功")},
    {"strategyCreation.strategyUpdatedSuccess", QStringLiteral("策略修改已保存")},
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

// ═══════════════════════════════════════════════════════════════════════════
// 半自动篮子确认 (v0.16.0)
// ═══════════════════════════════════════════════════════════════════════════

using OrderRequest = domain::strategy::OrderRequest;
using OrderSide    = domain::strategy::OrderSide;
using OrderType    = domain::strategy::OrderType;

// ── OrderRequest → QVariantList (引擎线程 → Qt 主线程) ──
QVariantList StrategyBridge::ordersToVariantList(const std::vector<OrderRequest>& orders)
{
    QVariantList list;
    list.reserve(static_cast<int>(orders.size()));
    for (size_t i = 0; i < orders.size(); ++i) {
        const auto& o = orders[i];
        QVariantMap item;
        item["symbol"]       = QString::fromStdString(o.symbol());
        item["stockName"]    = StockNameResolver::name(QString::fromStdString(o.symbol()));
        item["side"]         = o.side() == OrderSide::Buy ? QStringLiteral("买入") : QStringLiteral("卖出");
        item["sideRaw"]      = static_cast<int>(o.side());
        item["quantity"]     = static_cast<qlonglong>(o.quantity());
        item["price"]        = o.price();
        item["orderType"]    = static_cast<int>(o.orderType());
        item["targetWeight"] = o.extensionAs<double>(domain::trading::ExtKey::kTargetWeight, 0.0);
        item["signalScore"]  = o.extensionAs<double>(domain::trading::ExtKey::kSignalScore, 0.0);
        item["signalIntent"] = static_cast<int>(
            o.extensionAs<std::uint64_t>(domain::trading::ExtKey::kSignalIntent, 0));
        item["traceId"]      = QString::fromStdString(o.traceId());
        item["orderIndex"]   = static_cast<int>(i);

        // 最新价 (市价单估算资金占用用)
        auto& gse = engine::GmSessionEngine::instance();
        auto quote = gse.fetchQuote(o.symbol());
        item["latestPrice"] = (quote.has_value() && quote->valid) ? quote->price : 0.0;

        list.append(item);
    }
    return list;
}

// ── IBasketInterceptor 实现: 引擎线程 → Qt 主线程 ──
bool StrategyBridge::onBasketReady(std::uint64_t basketId,
                                   const std::vector<OrderRequest>& orders,
                                   const std::string& strategyId,
                                   const std::string& strategyName,
                                   const std::string& contextDescription)
{
    std::lock_guard<std::mutex> lock(m_basketMutex);
    if (m_pendingBasketId != 0) {
        INTERNAL_WARN_STREAM << "[Bridge] 篮子 " << m_pendingBasketId
                             << " 仍在等待确认, 拒绝新篮子 " << basketId;
        return false;  // 上一个篮子未确认, 拒绝
    }

    m_pendingBasketId = basketId;
    m_pendingStrategyId = QString::fromStdString(strategyId);
    m_pendingStrategyName = QString::fromStdString(strategyName);
    m_pendingContextDesc = QString::fromStdString(contextDescription);
    // 订单数据存引擎底层 (m_pendingBasket.orders), 桥接层只存储显示用 QVariantList

    // 转换为 QVariantList 并跨线程通知 QML
    QVariantList qmlOrders = ordersToVariantList(orders);

    QMetaObject::invokeMethod(this, [this, qmlOrders]() {
        m_pendingBasketOrders = qmlOrders;
        emit pendingBasketChanged();
    }, Qt::QueuedConnection);

    INTERNAL_INFO_STREAM << "[Bridge] 篮子已接收: basketId=" << basketId
                         << " strategy=" << strategyName
                         << " orders=" << orders.size();

    return true;
}

// ── QML 调用: 用户确认篮子 ──
void StrategyBridge::confirmBasket(const QVariantList& editedOrders)
{
    std::lock_guard<std::mutex> lock(m_basketMutex);
    if (m_pendingBasketId == 0) return;

    auto* engine = domain::strategy::StrategyManager::instance().get(
        m_pendingStrategyId.toStdString());
    if (!engine) {
        INTERNAL_WARN_STREAM << "[Bridge] 确认篮子: 未找到引擎 "
                             << m_pendingStrategyId.toStdString();
        m_pendingBasketId = 0;
        m_pendingStrategyId.clear();
        m_pendingBasketOrders.clear();
        return;
    }

    // QVariantList → BasketEdit[] → 引擎自己应用到 m_pendingBasket.orders
    std::vector<domain::strategy::BasketEdit> edits;
    edits.reserve(editedOrders.size());
    for (const auto& v : editedOrders) {
        QVariantMap item = v.toMap();
        domain::strategy::BasketEdit edit;
        edit.orderIndex = item.value("orderIndex").toInt();
        edit.quantity = static_cast<double>(item.value("quantity").toLongLong());
        if (edit.orderIndex >= 0 && edit.quantity > 0) {
            edits.push_back(edit);
        }
    }

    engine->confirmBasket(m_pendingBasketId, edits);

    INTERNAL_INFO_STREAM << "[Bridge] 篮子确认: basketId=" << m_pendingBasketId
                         << " edits=" << edits.size();

    m_pendingBasketId = 0;
    m_pendingStrategyId.clear();
    m_pendingBasketOrders.clear();
    emit pendingBasketChanged();
}

// ── QML 调用: 用户拒绝篮子 ──
void StrategyBridge::rejectBasket()
{
    std::lock_guard<std::mutex> lock(m_basketMutex);
    if (m_pendingBasketId == 0) return;

    auto* engine = domain::strategy::StrategyManager::instance().get(
        m_pendingStrategyId.toStdString());
    if (engine) {
        engine->rejectBasket(m_pendingBasketId);
    }

    INTERNAL_INFO_STREAM << "[Bridge] 篮子拒绝: basketId=" << m_pendingBasketId;

    m_pendingBasketId = 0;
    m_pendingStrategyId.clear();
    m_pendingBasketOrders.clear();
    emit pendingBasketChanged();
}

// ── QML 属性访问器 ──

QVariantList StrategyBridge::pendingBasketOrders() const
{
    return m_pendingBasketOrders;
}

QString StrategyBridge::pendingBasketStrategyName() const
{
    return m_pendingStrategyName;
}

QString StrategyBridge::pendingBasketContextDesc() const
{
    return m_pendingContextDesc;
}

bool StrategyBridge::hasPendingBasket() const
{
    return m_pendingBasketId != 0;
}