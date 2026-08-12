#include "database/StrategyRepository.h"
#include "database/ConnectionGuard.h"
#include "database/NativePgConnectionPool.h"
#include "foundation/log/logging.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <ctime>

namespace astock { namespace database {

static std::shared_ptr<ISqlDatabase> sdb() {
    try {
        return NativePgConnectionPool::instance().getConnection();
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[StrategyRepo] sdb() 异常: " << e.what();
        return nullptr;
    } catch (...) {
        INTERNAL_ERROR_STREAM << "[StrategyRepo] sdb() 未知异常";
        return nullptr;
    }
}
static std::string toS(const QString& v) { return v.toStdString(); }
static QString fromS(const std::string& v) { return QString::fromStdString(v); }
static std::string toJson(const QVariantMap& m) {
    return QJsonDocument(QJsonObject::fromVariantMap(m)).toJson(QJsonDocument::Compact).toStdString();
}
static QVariantMap fromJson(const std::string& j) {
    if (j.empty()) return {};
    auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(j));
    return doc.isObject() ? doc.object().toVariantMap() : QVariantMap{};
}

// metadata_json.strategyType 枚举名 → StrategyType (严格精确解析; 缺失/非法返回 nullopt, 无回退)
static std::optional<domain::strategies::StrategyType> readStrategyType(const QVariantMap& metaJson) {
    if (!metaJson.contains("strategyType")) return std::nullopt;
    return domain::strategies::StrategyTypeRegistry::fromTypeId(
        metaJson.value("strategyType").toString().toStdString());
}

// stub helpers for typed structs
bool PersistedStrategyData::isValid() const { return !strategyId.empty(); }
QVariantMap PersistedStrategyData::toVariantMap() const {
    QVariantMap m;
    m["strategyId"] = fromS(strategyId);
    m["strategyName"] = fromS(metadata.name);
    m["strategyCode"] = fromS(strategyCode);
    // 类型只发枚举名字符串; 行为类型由桥接层从 strategyType 推导
    m["strategyType"] = strategyType.has_value()
        ? fromS(std::string(domain::strategies::StrategyTypeRegistry::typeId(*strategyType)))
        : QString();
    m["description"] = fromS(metadata.description);
    m["version"] = fromS(version);
    m["author"] = fromS(author);
    m["language"] = static_cast<int>(language);
    m["status"] = static_cast<int>(status);
    m["createdAt"] = createdAt;
    m["updatedAt"] = updatedAt;
    m["parameters"] = parameters;
    m["performanceMetrics"] = performanceMetrics;
    // QML StrategyCard 直接属性
    m["returns"]       = performanceMetrics.value("returns", 0);
    m["sharpeRatio"]   = performanceMetrics.value("sharpeRatio", 0);
    m["maxDrawdown"]   = performanceMetrics.value("maxDrawdown", 0);
    m["winRate"]       = performanceMetrics.value("winRate", 0);
    m["runningDays"]   = createdAt.isValid() ? createdAt.daysTo(QDateTime::currentDateTime()) : 0;
    m["trades"]        = 0;  // 实盘统计由 StrategyBridge::list() 填充
    m["dailyPnL"]      = 0;
    m["position"]      = 0;
    return m;
}
PersistedStrategyData PersistedStrategyData::fromVariantMap(const QVariantMap& m) {
    PersistedStrategyData d;
    d.strategyId = m.value("strategyId").toString().toStdString();
    d.strategyCode = m.value("strategyCode").toString().toStdString();
    return d;
}

StrategyRepository::StrategyRepository() = default;
StrategyRepository::~StrategyRepository() = default;
bool StrategyRepository::initialize() { return true; }
bool StrategyRepository::clearAll() { auto db = sdb(); return db ? db->executeUpdate("DELETE FROM strategy") >= 0 : false; }

std::optional<PersistedStrategyData> StrategyRepository::findById(const QString& id) {
    auto db = sdb();
    if (!db) return {};
    auto r = db->executeQuery(
        "SELECT s.*, b.total_return, b.annualized_return, b.sharpe_ratio, b.max_drawdown, "
        "b.win_rate, b.total_trades "
        "FROM live.strategy s "
        "LEFT JOIN LATERAL ("
        "  SELECT * FROM live.strategy_backtest_results "
        "  WHERE strategy_id = s.strategy_id ORDER BY run_at DESC LIMIT 1"
        ") b ON true "
        "WHERE s.strategy_id=?", {SqlParam{toS(id)}});
    if (r.isEmpty()) return {};
    auto& row = r.getRow(0);
    PersistedStrategyData d;
    d.strategyId = row.getString("strategy_id");
    d.strategyCode = row.getString("strategy_code");
    d.version = row.getString("version");
    d.author = row.getString("author");
    d.language = static_cast<StrategyLanguageCode>(row.getInt("language"));
    d.status = strategy_view::StrategyLifecycleStatus::Active;
    auto metaJson = fromJson(row.getString("metadata_json"));
    d.metadata.name = metaJson.value("name").toString().toStdString();
    d.metadata.description = metaJson.value("description").toString().toStdString();
    d.metadata.enabled = metaJson.value("enabled").toBool();
    auto parsedType = readStrategyType(metaJson);
    if (!parsedType.has_value()) {
        INTERNAL_ERROR_STREAM << "[Repo] findById 拒绝: strategyType 缺失/非法 id="
                              << row.getString("strategy_id");
        return std::nullopt;
    }
    d.strategyType = parsedType;
    // 行为类型一律由策略类型推导, 不再从 JSON 读取
    d.metadata.behaviorKind = domain::strategies::StrategyTypeRegistry::behaviorKindOf(*parsedType);
    d.strategyIdentity = domain::backtest::ResolvedStrategyIdentity{};
    d.parameters = fromJson(row.getString("parameters"));
    QVariantMap perf;
    perf["totalReturn"]      = row.getDouble("total_return");
    perf["annualizedReturn"] = row.getDouble("annualized_return");
    perf["sharpeRatio"]      = row.getDouble("sharpe_ratio");
    perf["maxDrawdown"]      = row.getDouble("max_drawdown");
    perf["winRate"]          = row.getDouble("win_rate");
    perf["returns"]          = row.getDouble("total_return") * 100.0;
    perf["trades"]           = row.getInt("total_trades");
    d.performanceMetrics = perf;
    d.runtime = StrategyRuntimeProperties{};
    return d;
}

std::optional<PersistedStrategyData> StrategyRepository::findByCode(const QString& code) {
    auto db = sdb();
    if (!db) return {};
    auto r = db->executeQuery("SELECT * FROM strategy WHERE strategy_code=?", {SqlParam{toS(code)}});
    if (r.isEmpty()) return {};
    return findById(fromS(r.getRow(0).getString("strategy_id")));
}

std::vector<PersistedStrategyData> StrategyRepository::findAll() {
    try {
        INTERNAL_INFO_STREAM << "[Repo] findAll 开始";
        auto db = sdb();
        if (!db) { INTERNAL_ERROR_STREAM << "[Repo] findAll 失败: DB 不可用"; return {}; }
        auto r = db->executeQuery(
            "SELECT s.*, b.total_return, b.annualized_return, b.sharpe_ratio, b.max_drawdown, "
            "b.win_rate "
            "FROM live.strategy s "
            "LEFT JOIN LATERAL ("
            "  SELECT * FROM live.strategy_backtest_results "
            "  WHERE strategy_id = s.strategy_id ORDER BY run_at DESC LIMIT 1"
            ") b ON true "
            "ORDER BY s.created_at DESC");
        INTERNAL_INFO_STREAM << "[Repo] findAll 查询返回 " << static_cast<int>(r.rowCount()) << " 行";
        std::vector<PersistedStrategyData> v;
        for (auto& row : r.getRows()) {
            PersistedStrategyData d;
            d.strategyId = row.getString("strategy_id");
            d.strategyCode = row.getString("strategy_code");
            d.version = row.getString("version");
            d.author = row.getString("author");
            d.language = static_cast<StrategyLanguageCode>(row.getInt("language"));
            d.status = strategy_view::StrategyLifecycleStatus::Active;
            d.createdAt = QDateTime::fromString(fromS(row.getString("created_at")), Qt::ISODate);
            d.updatedAt = QDateTime::fromString(fromS(row.getString("updated_at")), Qt::ISODate);
            // 解析 metadata_json
            auto metaJson = fromJson(row.getString("metadata_json"));
            d.metadata.name = metaJson.value("name").toString().toStdString();
            d.metadata.description = metaJson.value("description").toString().toStdString();
            d.metadata.enabled = metaJson.value("enabled").toBool();
            auto parsedType = readStrategyType(metaJson);
            if (!parsedType.has_value()) {
                INTERNAL_ERROR_STREAM << "[Repo] findAll 跳过行: strategyType 缺失/非法 id="
                                      << row.getString("strategy_id");
                continue;
            }
            d.strategyType = parsedType;
            // 行为类型一律由策略类型推导, 不再从 JSON 读取
            d.metadata.behaviorKind = domain::strategies::StrategyTypeRegistry::behaviorKindOf(*parsedType);
            // 直接从最新回测记录取绩效 (LATERAL JOIN)
            QVariantMap perf;
            perf["totalReturn"]      = row.getDouble("total_return");
            perf["annualizedReturn"] = row.getDouble("annualized_return");
            perf["sharpeRatio"]      = row.getDouble("sharpe_ratio");
            perf["maxDrawdown"]      = row.getDouble("max_drawdown");
            perf["winRate"]          = row.getDouble("win_rate");
            perf["returns"]          = row.getDouble("total_return") * 100.0;
            d.performanceMetrics = perf;
            v.push_back(d);
        }
        return v;
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "[StrategyRepo] findAll 异常: " << e.what();
        return {};
    } catch (...) {
        INTERNAL_ERROR_STREAM << "[StrategyRepo] findAll 未知异常";
        return {};
    }
}

std::vector<PersistedStrategyData> StrategyRepository::findByType(domain::backtest::StrategyStoredType) { return {}; }
std::vector<PersistedStrategyData> StrategyRepository::findByStatus(strategy_view::StrategyLifecycleStatus) { return {}; }
std::vector<PersistedStrategyData> StrategyRepository::search(const QString&) { return {}; }
std::vector<PersistedStrategyData> StrategyRepository::findActiveStrategies() { return findAll(); }
std::vector<PersistedStrategyData> StrategyRepository::findDraftStrategies() { return {}; }

QString StrategyRepository::save(const PersistedStrategyData& d) {
    auto db = sdb();
    if (!db) return {};
    auto id = d.strategyId.empty() ? foundation::utils::Uuid::generate_v4().to_string() : d.strategyId;
    QString sid = fromS(id);

    if (!d.strategyType.has_value()) {
        INTERNAL_ERROR_STREAM << "[Repo] save 拒绝: strategyType 未设置 (行为类型不落库, 必须由类型推导)";
        return {};
    }
    QVariantMap metaJson;
    metaJson["name"] = fromS(d.metadata.name);
    metaJson["description"] = fromS(d.metadata.description);
    metaJson["enabled"] = d.metadata.enabled;
    // 类型只落枚举名字符串; behaviorKind 不再落库, 读取时由类型推导
    metaJson["strategyType"] = fromS(std::string(
        domain::strategies::StrategyTypeRegistry::typeId(*d.strategyType)));

    // 字符串参数以 text OID 绑定, jsonb 列必须显式 ::jsonb 转换 (否则 PG 42804)
    int affected = db->executeUpdate(
        "INSERT INTO live.strategy(strategy_id,strategy_code,metadata_json,strategy_identity_json,"
        "version,author,language,status,parameters,performance_metrics,runtime_json) "
        "VALUES(?,?,?::jsonb,?::jsonb,?,?,?,?,?::jsonb,?::jsonb,?::jsonb) ON CONFLICT(strategy_id) DO UPDATE SET "
        "strategy_code=EXCLUDED.strategy_code,metadata_json=EXCLUDED.metadata_json,"
        "strategy_identity_json=EXCLUDED.strategy_identity_json,version=EXCLUDED.version,"
        "author=EXCLUDED.author,language=EXCLUDED.language,status=EXCLUDED.status,"
        "parameters=EXCLUDED.parameters,performance_metrics=EXCLUDED.performance_metrics,"
        "runtime_json=EXCLUDED.runtime_json,updated_at=NOW()",
        {SqlParam{id},SqlParam{d.strategyCode},SqlParam{toJson(metaJson)},
         SqlParam{"{}"},SqlParam{d.version},SqlParam{d.author},
         SqlParam{std::to_string(static_cast<int>(d.language))},SqlParam{std::to_string(0)},
         SqlParam{toJson(d.parameters)},SqlParam{toJson(d.performanceMetrics)},SqlParam{"{}"}});
    if (affected <= 0) {
        INTERNAL_ERROR_STREAM << "[Repo] save INSERT 失败 affected=" << affected << " id=" << id;
        return {};
    }
    return sid;
}

QString StrategyRepository::saveStrategyInternal(const PersistedStrategyData& d, std::shared_ptr<ISqlDatabase>&, bool isUpdate) {
    return isUpdate ? (update(fromS(d.strategyId), d), fromS(d.strategyId)) : save(d);
}

bool StrategyRepository::update(const QString& id, const PersistedStrategyData& d) {
    auto db = sdb();
    if (!db) return false;

    if (!d.strategyType.has_value()) {
        INTERNAL_ERROR_STREAM << "[Repo] update 拒绝: strategyType 未设置 (行为类型不落库, 必须由类型推导)";
        return false;
    }
    QVariantMap metaJson;
    metaJson["name"] = fromS(d.metadata.name);
    metaJson["description"] = fromS(d.metadata.description);
    metaJson["enabled"] = d.metadata.enabled;
    // 类型只落枚举名字符串; behaviorKind 不再落库, 读取时由类型推导
    metaJson["strategyType"] = fromS(std::string(
        domain::strategies::StrategyTypeRegistry::typeId(*d.strategyType)));

    // 字符串参数以 text OID 绑定, jsonb 列必须显式 ::jsonb 转换,
    // 否则 PG 报 42804 (text→jsonb 无赋值转换) 导致更新失败
    const int affected = db->executeUpdate(
        "UPDATE strategy SET strategy_code=?,metadata_json=?::jsonb,strategy_identity_json=?::jsonb,"
        "version=?,author=?,language=?,status=?,parameters=?::jsonb,performance_metrics=?::jsonb,"
        "runtime_json=?::jsonb,updated_at=NOW() WHERE strategy_id=?",
        {SqlParam{d.strategyCode},SqlParam{toJson(metaJson)},SqlParam{"{}"},
         SqlParam{d.version},SqlParam{d.author},SqlParam{std::to_string(static_cast<int>(d.language))},
         SqlParam{std::to_string(0)},SqlParam{toJson(d.parameters)},SqlParam{toJson(d.performanceMetrics)},
         SqlParam{"{}"},SqlParam{toS(id)}});
    if (affected <= 0) {
        INTERNAL_ERROR_STREAM << "[StrategyRepo] update 失败 id=" << toS(id)
                              << " error=" << db->lastError();
        return false;
    }
    return true;
}

bool StrategyRepository::remove(const QString& id) {
    auto db = sdb();
    if (!db) return false;
    return db->executeUpdate("DELETE FROM strategy WHERE strategy_id=?", {SqlParam{toS(id)}}) > 0;
}

bool StrategyRepository::updateStatus(const QString& id, strategy_view::StrategyLifecycleStatus) {
    auto db = sdb();
    if (!db) return false;
    return db->executeUpdate("UPDATE strategy SET updated_at=NOW() WHERE strategy_id=?", {SqlParam{toS(id)}}) > 0;
}

bool StrategyRepository::updateParameters(const QString& id, const QVariantMap& p) {
    auto db = sdb();
    if (!db) return false;
    return db->executeUpdate("UPDATE strategy SET parameters=?::jsonb,updated_at=NOW() WHERE strategy_id=?",
        {SqlParam{toJson(p)},SqlParam{toS(id)}}) > 0;
}

bool StrategyRepository::updatePerformance(const QString& id, const QVariantMap& p) {
    auto db = sdb();
    if (!db) return false;
    return db->executeUpdate("UPDATE strategy SET performance_metrics=?::jsonb,updated_at=NOW() WHERE strategy_id=?",
        {SqlParam{toJson(p)},SqlParam{toS(id)}}) > 0;
}

size_t StrategyRepository::count() {
    auto db = sdb();
    if (!db) return 0;
    auto r = db->executeQuery("SELECT COUNT(*) FROM strategy");
    return r.isEmpty() ? 0 : r.getRow(0).getInt("count");
}

bool StrategyRepository::exists(const QString& id) {
    auto db = sdb();
    if (!db) return false;
    return !db->executeQuery("SELECT 1 FROM strategy WHERE strategy_id=?", {SqlParam{toS(id)}}).isEmpty();
}

bool StrategyRepository::existsByCode(const QString& code) {
    auto db = sdb();
    if (!db) return false;
    return !db->executeQuery("SELECT 1 FROM strategy WHERE strategy_code=?", {SqlParam{toS(code)}}).isEmpty();
}

QString StrategyRepository::generateStrategyCode(const PersistedStrategyData& d) const { return fromS(d.strategyCode); }
QVariantMap StrategyRepository::loadStrategyParameters(const QString& id, std::shared_ptr<ISqlDatabase>& db) {
    auto r = db->executeQuery("SELECT parameters FROM strategy WHERE strategy_id=?", {SqlParam{toS(id)}});
    return r.isEmpty() ? QVariantMap{} : fromJson(r.getRow(0).getString("parameters"));
}
PersistedStrategyData StrategyRepository::rowToStrategyData(const SqlQueryResultRow&) { return PersistedStrategyData{}; }

}} // namespaces
