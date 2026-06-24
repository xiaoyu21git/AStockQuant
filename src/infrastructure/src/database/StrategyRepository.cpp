#include "database/StrategyRepository.h"
#include "database/NativeMySQLConnectionPool.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <ctime>

namespace astock { namespace database {

static auto sdb() { return NativeMySQLConnectionPool::instance().getConnection(); }
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

// stub helpers for typed structs
bool PersistedStrategyData::isValid() const { return !strategyId.empty(); }
QVariantMap PersistedStrategyData::toVariantMap() const {
    QVariantMap m;
    m["strategyId"] = fromS(strategyId);
    m["strategyName"] = fromS(metadata.name);
    m["strategyCode"] = fromS(strategyCode);
    m["behaviorKind"] = static_cast<int>(metadata.behaviorKind);
    m["description"] = fromS(metadata.description);
    m["version"] = fromS(version);
    m["author"] = fromS(author);
    m["language"] = static_cast<int>(language);
    m["status"] = static_cast<int>(status);
    m["createdAt"] = createdAt;
    m["updatedAt"] = updatedAt;
    m["parameters"] = parameters;
    m["performanceMetrics"] = performanceMetrics;
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
bool StrategyRepository::clearAll() { return sdb()->executeUpdate("DELETE FROM strategy") >= 0; }

std::optional<PersistedStrategyData> StrategyRepository::findById(const QString& id) {
    auto r = sdb()->executeQuery("SELECT * FROM strategy WHERE strategy_id=?", {SqlParam{toS(id)}});
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
    d.metadata.behaviorKind = static_cast<domain::strategies::StrategyBehaviorKind>(
        metaJson.value("behaviorKind").toInt());
    d.metadata.enabled = metaJson.value("enabled").toBool();
    d.strategyIdentity = domain::backtest::ResolvedStrategyIdentity{};
    d.parameters = fromJson(row.getString("parameters"));
    d.performanceMetrics = fromJson(row.getString("performance_metrics"));
    d.runtime = StrategyRuntimeProperties{};
    return d;
}

std::optional<PersistedStrategyData> StrategyRepository::findByCode(const QString& code) {
    auto r = sdb()->executeQuery("SELECT * FROM strategy WHERE strategy_code=?", {SqlParam{toS(code)}});
    if (r.isEmpty()) return {};
    return findById(fromS(r.getRow(0).getString("strategy_id")));
}

std::vector<PersistedStrategyData> StrategyRepository::findAll() {
    auto r = sdb()->executeQuery("SELECT * FROM strategy ORDER BY created_at DESC");
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
        d.metadata.behaviorKind = static_cast<domain::strategies::StrategyBehaviorKind>(
            metaJson.value("behaviorKind").toInt());
        d.metadata.enabled = metaJson.value("enabled").toBool();
        v.push_back(d);
    }
    return v;
}

std::vector<PersistedStrategyData> StrategyRepository::findByType(domain::backtest::StrategyStoredType) { return {}; }
std::vector<PersistedStrategyData> StrategyRepository::findByStatus(strategy_view::StrategyLifecycleStatus) { return {}; }
std::vector<PersistedStrategyData> StrategyRepository::search(const QString&) { return {}; }
std::vector<PersistedStrategyData> StrategyRepository::findActiveStrategies() { return findAll(); }
std::vector<PersistedStrategyData> StrategyRepository::findDraftStrategies() { return {}; }

QString StrategyRepository::save(const PersistedStrategyData& d) {
    auto db = sdb();
    auto id = d.strategyId.empty() ? "s_" + std::to_string(std::time(nullptr)) : d.strategyId;
    QString sid = fromS(id);
    db->executeUpdate(
        "INSERT INTO strategy(strategy_id,strategy_code,metadata_json,strategy_identity_json,"
        "version,author,language,status,parameters,performance_metrics,runtime_json) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(strategy_id) DO UPDATE SET "
        "strategy_code=EXCLUDED.strategy_code,metadata_json=EXCLUDED.metadata_json,"
        "strategy_identity_json=EXCLUDED.strategy_identity_json,version=EXCLUDED.version,"
        "author=EXCLUDED.author,language=EXCLUDED.language,status=EXCLUDED.status,"
        "parameters=EXCLUDED.parameters,performance_metrics=EXCLUDED.performance_metrics,"
        "runtime_json=EXCLUDED.runtime_json,updated_at=NOW()",
        {SqlParam{id},SqlParam{d.strategyCode},SqlParam{"{}"},
         SqlParam{"{}"},SqlParam{d.version},SqlParam{d.author},
         SqlParam{std::to_string(static_cast<int>(d.language))},SqlParam{std::to_string(0)},
         SqlParam{toJson(d.parameters)},SqlParam{toJson(d.performanceMetrics)},SqlParam{"{}"}});
    return sid;
}

QString StrategyRepository::saveStrategyInternal(const PersistedStrategyData& d, std::shared_ptr<ISqlDatabase>&, bool isUpdate) {
    return isUpdate ? (update(fromS(d.strategyId), d), fromS(d.strategyId)) : save(d);
}

bool StrategyRepository::update(const QString& id, const PersistedStrategyData& d) {
    return sdb()->executeUpdate(
        "UPDATE strategy SET strategy_code=?,metadata_json=?,strategy_identity_json=?,"
        "version=?,author=?,language=?,status=?,parameters=?,performance_metrics=?,"
        "runtime_json=?,updated_at=NOW() WHERE strategy_id=?",
        {SqlParam{d.strategyCode},SqlParam{"{}"},SqlParam{"{}"},
         SqlParam{d.version},SqlParam{d.author},SqlParam{std::to_string(static_cast<int>(d.language))},
         SqlParam{std::to_string(0)},SqlParam{toJson(d.parameters)},SqlParam{toJson(d.performanceMetrics)},
         SqlParam{"{}"},SqlParam{toS(id)}}) > 0;
}

bool StrategyRepository::remove(const QString& id) {
    return sdb()->executeUpdate("DELETE FROM strategy WHERE strategy_id=?", {SqlParam{toS(id)}}) > 0;
}

bool StrategyRepository::updateStatus(const QString& id, strategy_view::StrategyLifecycleStatus) {
    return sdb()->executeUpdate("UPDATE strategy SET updated_at=NOW() WHERE strategy_id=?", {SqlParam{toS(id)}}) > 0;
}

bool StrategyRepository::updateParameters(const QString& id, const QVariantMap& p) {
    return sdb()->executeUpdate("UPDATE strategy SET parameters=?,updated_at=NOW() WHERE strategy_id=?",
        {SqlParam{toJson(p)},SqlParam{toS(id)}}) > 0;
}

bool StrategyRepository::updatePerformance(const QString& id, const QVariantMap& p) {
    return sdb()->executeUpdate("UPDATE strategy SET performance_metrics=?,updated_at=NOW() WHERE strategy_id=?",
        {SqlParam{toJson(p)},SqlParam{toS(id)}}) > 0;
}

size_t StrategyRepository::count() {
    auto r = sdb()->executeQuery("SELECT COUNT(*) FROM strategy");
    return r.isEmpty() ? 0 : r.getRow(0).getInt("count");
}

bool StrategyRepository::exists(const QString& id) {
    return !sdb()->executeQuery("SELECT 1 FROM strategy WHERE strategy_id=?", {SqlParam{toS(id)}}).isEmpty();
}

bool StrategyRepository::existsByCode(const QString& code) {
    return !sdb()->executeQuery("SELECT 1 FROM strategy WHERE strategy_code=?", {SqlParam{toS(code)}}).isEmpty();
}

QString StrategyRepository::generateStrategyCode(const PersistedStrategyData& d) const { return fromS(d.strategyCode); }
QVariantMap StrategyRepository::loadStrategyParameters(const QString& id, std::shared_ptr<ISqlDatabase>& db) {
    auto r = db->executeQuery("SELECT parameters FROM strategy WHERE strategy_id=?", {SqlParam{toS(id)}});
    return r.isEmpty() ? QVariantMap{} : fromJson(r.getRow(0).getString("parameters"));
}
PersistedStrategyData StrategyRepository::rowToStrategyData(const SqlQueryResultRow&) { return PersistedStrategyData{}; }

}} // namespaces
