// DataCacheAdapter.cpp — 纯 C++ DataCache 的轻量适配器
#include "DataCacheAdapter.h"
#include "AppStoragePaths.h"
#include "DataSourceRegistry.h"
#include "foundation/json/json_facade.h"
#include "database/MarketDataRepository.h"
#include "database/NativePgConnectionPool.h"
#include "foundation/market/AStockSymbol.h"
#include <arrow/api.h>

#include <cstdio>
#include <cstdlib>
#include <sstream>

DataCacheAdapter& DataCacheAdapter::instance() {
    static DataCacheAdapter s;
    return s;
}

DataCacheAdapter::DataCacheAdapter()
    : QObject(nullptr)
    , m_cache(&cleaning::DataCache::instance())
{}

void DataCacheAdapter::ensureInitialized() {
    if (!m_cache->isInitialized()) {
        std::string dir = bridge::storage::persistentDatasetRootDir().toStdString();
        m_cache->initialize(dir);
    }
}

// ── 数据集管理 ──

int DataCacheAdapter::storeDataSet(const QVariantList& data, const QVariantMap& infoMap,
                                    std::function<void(int,int)> progressCallback) {
    ensureInitialized();

    // 元数据写入纯 C++ DataCache
    auto info = mapToCppInfo(infoMap);
    int dataId = m_cache->storeDataSet({}, info, progressCallback);
    if (dataId <= 0) return -1;

    m_cache->saveDataSetFile(dataId, {});

    // 更新行数到元数据
    auto fullInfo = m_cache->getDataSetInfo(dataId);
    fullInfo.rowCount = static_cast<int>(data.size());
    auto metaJson = fullInfo.toJson();
    std::string infoPath = m_cache->infoFilePath(dataId);
    FILE* fi = fopen(infoPath.c_str(), "wb");
    if (fi) {
        std::string metaStr = metaJson.toString();
        fwrite(metaStr.data(), 1, metaStr.size(), fi);
        fclose(fi);
    }

    INTERNAL_INFO_STREAM << "[DataCacheAdapter] 已存储数据集 " << dataId << ": " << fullInfo.displayName << " (" << static_cast<int>(data.size()) << " rows)";

    emit dataSetStored(dataId, cppInfoToMap(info));
    return dataId;
}

int DataCacheAdapter::storeDataSetFromRows(const std::vector<foundation::json::JsonFacade>& rows,
                                             const QVariantMap& infoMap,
                                             std::function<void(int,int)> progressCallback) {
    ensureInitialized();

    auto info = mapToCppInfo(infoMap);
    int dataId = m_cache->storeDataSet({}, info, progressCallback);
    if (dataId <= 0) return -1;

    // 自动检测字段（输入行已含完整 Schema，无需硬编码）
    std::vector<std::string> fieldNames;
    std::unordered_set<std::string> numericFields;
    if (!rows.empty()) {
        // 从 cleaning 完整字段集获取数值类型标记
        auto klineSchema = cleaning::KlineDataSource().detectSchema(nullptr, {}, {});
        auto finSchema   = cleaning::FinancialDataSource().detectSchema(nullptr, {}, {});
        std::unordered_set<std::string> knownNumeric;
        for (auto& f : klineSchema.numeric) knownNumeric.insert(f);
        for (auto& f : finSchema.numeric)   knownNumeric.insert(f);
        // 按首行字段顺序排列（symbol, trade_date 优先）
        const char* head[] = {"symbol","trade_date","report_date"};
        for (auto* h : head) { if (rows[0].has(h)) fieldNames.push_back(h); }
        for (const auto& row : rows) {
            if (!row.isObject()) continue;
            for (const auto& key : row.keys()) {
                if (std::find(fieldNames.begin(), fieldNames.end(), key) == fieldNames.end())
                    fieldNames.push_back(key);
            }
            break;
        }
        for (auto& f : fieldNames) {
            if (knownNumeric.count(f)) numericFields.insert(f);
            else { double d = 0; try { d = std::stod(rows[0].get(f).asString()); } catch(...) {} if (d != 0 || rows[0].get(f).isNumber()) numericFields.insert(f); }
        }
    }
    m_cache->saveDataSetFile(dataId, rows, fieldNames, numericFields);
    m_cache->updateDataSetRowCount(dataId, static_cast<int>(rows.size()));

    INTERNAL_INFO_STREAM << "[DataCacheAdapter] 已存储数据集 " << dataId << " (来自rows): " << info.displayName << " (" << rows.size() << " rows)";

    emit dataSetStored(dataId, cppInfoToMap(info));
    return dataId;
}

cleaning::DataCache::ArrowWriteToken DataCacheAdapter::beginArrowWrite(int dataId) {
    ensureInitialized();
    return m_cache->beginArrowWrite(dataId);
}

cleaning::DataCache::ArrowWriteToken DataCacheAdapter::beginArrowWrite(int dataId,
    const std::vector<std::string>& fieldNames,
    const std::unordered_set<std::string>& numericFields) {
    ensureInitialized();
    return m_cache->beginArrowWrite(dataId, fieldNames, numericFields);
}

void DataCacheAdapter::appendArrowBatch(const cleaning::DataCache::ArrowWriteToken& token,
                                         const std::vector<foundation::json::JsonFacade>& rows) {
    m_cache->appendArrowBatch(token, rows);
}

void DataCacheAdapter::appendArrowTable(const cleaning::DataCache::ArrowWriteToken& token,
                                         const std::shared_ptr<arrow::Table>& table) {
    m_cache->appendArrowTable(token, table);
}

void DataCacheAdapter::finishArrowWrite(cleaning::DataCache::ArrowWriteToken token, int rowCount) {
    int dataId = token ? token->dataId : -1;
    m_cache->finishArrowWrite(std::move(token));
    if (dataId > 0) {
        m_cache->updateDataSetRowCount(dataId, rowCount);
        auto updated = m_cache->getDataSetInfo(dataId);
        emit dataSetStored(dataId, cppInfoToMap(updated));
    }
    INTERNAL_INFO_STREAM << "[DataCacheAdapter] 批量写入完成: dataSetId=" << dataId << " rows=" << rowCount;
}

QVariantList DataCacheAdapter::getDataSetById(int dataId) {
    ensureInitialized();
    auto rows = m_cache->loadDataSetFile(dataId);
    if (rows.empty()) return {};
    QVariantList result;
    result.reserve(static_cast<int>(rows.size()));
    for (const auto& row : rows) {
        QVariantMap m;
        for (const auto& key : row.keys()) {
            auto v = row.get(key);
            if (v.isNumber()) m[QString::fromStdString(key)] = v.asDouble();
            else if (v.isString()) m[QString::fromStdString(key)] = QString::fromStdString(v.asString());
            else if (v.isBool()) m[QString::fromStdString(key)] = v.asBool();
        }
        result.append(m);
    }
    return result;
}

QVariantMap DataCacheAdapter::getDataSetInfo(int dataId) const {
    return cppInfoToMap(m_cache->getDataSetInfo(dataId));
}

QVariantList DataCacheAdapter::loadRowsBySymbol(int dataId, const QString& symbol) {
    ensureInitialized();
    auto rows = m_cache->loadDataSetRowsBySymbol(dataId, symbol.toStdString());
    QVariantList result;
    result.reserve(static_cast<int>(rows.size()));
    for (const auto& row : rows) {
        QVariantMap m;
        for (const auto& key : row.keys()) {
            auto v = row.get(key);
            if (v.isNumber()) m[QString::fromStdString(key)] = v.asDouble();
            else if (v.isString()) m[QString::fromStdString(key)] = QString::fromStdString(v.asString());
            else if (v.isBool()) m[QString::fromStdString(key)] = v.asBool();
        }
        result.append(m);
    }
    return result;
}

QStringList DataCacheAdapter::getDataSetSchemaFields(int dataId) const {
    QStringList fields;
    for (const auto& f : m_cache->loadDataSetSchemaFields(dataId))
        fields.append(QString::fromStdString(f));
    return fields;
}

QVector<QVariantMap> DataCacheAdapter::getAllDataSetInfos() const {
    const_cast<DataCacheAdapter*>(this)->ensureInitialized();
    QVector<QVariantMap> result;
    for (const auto& info : m_cache->listDataSets())
        result.append(cppInfoToMap(info));
    return result;
}

bool DataCacheAdapter::removeDataSet(int dataId) {
    bool ok = m_cache->removeDataSet(dataId);
    if (ok) emit dataSetRemoved(dataId);
    return ok;
}

int DataCacheAdapter::removeDataSetsBySourceType(const std::string& sourceType) {
    int n = m_cache->removeDataSetsBySourceType(sourceType);
    INTERNAL_INFO_STREAM << "[DataCacheAdapter] 已移除 " << n << " 个数据集 sourceType=" << sourceType;
    return n;
}

bool DataCacheAdapter::initialize(const QString& persistentDir) {
    return m_cache->initialize(persistentDir.toStdString());
}

bool DataCacheAdapter::isInitialized() const {
    return m_cache->isInitialized();
}

// ── 类型转换（仅元数据） ──

cleaning::DataSetInfo DataCacheAdapter::mapToCppInfo(const QVariantMap& m) {
    cleaning::DataSetInfo info;
    info.id = m.value("id", -1).toInt();
    info.displayName = m.value("displayName").toString().toStdString();
    info.description = m.value("description").toString().toStdString();
    info.sourceType = m.value("sourceType").toString().toStdString();
    info.createdAt = m.value("createdAt", 0).toLongLong();
    info.rowCount = m.value("rowCount", 0).toInt();
    info.schemaVersion = m.value("schemaVersion", 2).toInt();
    info.sourceDataSetId = m.value("sourceDataSetId", -1).toInt();
    for (const auto& f : m.value("availableFields").toStringList())
        info.availableFields.push_back(f.toStdString());
    for (const auto& s : m.value("stockCodes").toStringList())
        info.stockCodes.push_back(s.toStdString());
    for (const auto& t : m.value("tags").toStringList())
        info.tags.push_back(t.toStdString());
    info.startDate = m.value("startDate").toString().toStdString();
    info.endDate = m.value("endDate").toString().toStdString();
    info.isBacktestReady = m.value("isBacktestReady", false).toBool();
    return info;
}

QVariantMap DataCacheAdapter::cppInfoToMap(const cleaning::DataSetInfo& info) {
    QVariantMap m;
    m["id"] = info.id;
    m["displayName"] = QString::fromStdString(info.displayName);
    m["description"] = QString::fromStdString(info.description);
    m["sourceType"] = QString::fromStdString(info.sourceType);
    m["createdAt"] = QVariant::fromValue(qint64(info.createdAt));
    m["rowCount"] = info.rowCount;
    m["schemaVersion"] = info.schemaVersion;
    if (info.sourceDataSetId > 0) m["sourceDataSetId"] = info.sourceDataSetId;
    QStringList fields, codes, tgs;
    for (const auto& f : info.availableFields) fields.append(QString::fromStdString(f));
    for (const auto& s : info.stockCodes) codes.append(QString::fromStdString(s));
    for (const auto& t : info.tags) tgs.append(QString::fromStdString(t));
    m["availableFields"] = fields;
    m["stockCodes"] = codes;
    m["tags"] = tgs;
    m["startDate"] = QString::fromStdString(info.startDate);
    m["endDate"] = QString::fromStdString(info.endDate);
    m["isBacktestReady"] = info.isBacktestReady;
    return m;
}

// ═══════════════════════════════════════════════════════════════════
// 资金流列增量追加 — PG 查询 + 委托 DataCache 做 Arrow 操作
// ═══════════════════════════════════════════════════════════════════

int DataCacheAdapter::augmentMoneyFlow(int dataId) {
    ensureInitialized();
    auto dsInfo = m_cache->getDataSetInfo(dataId);
    if (dsInfo.id <= 0) {
        INTERNAL_ERROR_STREAM << "[DataCacheAdapter] augmentMF: 数据集 " << dataId << " 不存在";
        return -1;
    }

    const auto& symbols = dsInfo.stockCodes;
    const auto& startDate = dsInfo.startDate;
    const auto& endDate = dsInfo.endDate;
    if (symbols.empty() || startDate.empty() || endDate.empty()) {
        INTERNAL_ERROR_STREAM << "[DataCacheAdapter] augmentMF: 无标的或日期范围";
        return -1;
    }
    INTERNAL_INFO_STREAM << "[DataCacheAdapter] augmentMF: id=" << dataId
        << " symbols=" << symbols.size() << " range=" << startDate << "~" << endDate;

    // 1. 查询 PG：批量拉取所有资金流数据
    cleaning::MoneyFlowMap mfMap;
    mfMap.reserve(symbols.size() * 500); // 估计每个 symbol ~500 个交易日

    {
        using namespace astock::database;
        auto db = NativePgConnectionPool::instance().getConnection();
        if (!db || !db->isOpen()) {
            INTERNAL_ERROR_STREAM << "[DataCacheAdapter] augmentMF: DB连接不可用";
            return -1;
        }

        astock::infrastructure::database::MarketDataRepository repo(db);
        auto mfRows = repo.queryMoneyFlow(symbols, startDate, endDate);
        for (const auto& row : mfRows) {
            const auto& vals = row.getValues();
            auto sit = vals.find("symbol");
            auto tit = vals.find("trade_date");
            if (sit == vals.end() || tit == vals.end()) continue;
            std::string key = foundation::market::AStockSymbol::normalizeToFullSymbol(sit->second) + "|" + tit->second;

            cleaning::MoneyFlowRow mr;
            auto getVal = [&](const char* col) -> double {
                auto it = vals.find(col);
                if (it != vals.end() && !it->second.empty())
                    return std::strtod(it->second.c_str(), nullptr);
                return std::numeric_limits<double>::quiet_NaN();
            };
            mr.main_net_in = getVal("money_main_net_in");
            mr.main_net_in_rate = getVal("money_main_net_in_rate");
            mr.main_in = getVal("money_main_in");
            mr.main_out = getVal("money_main_out");
            mr.super_net_in = getVal("money_super_net_in");
            mr.super_net_in_rate = getVal("money_super_net_in_rate");
            mr.super_in = getVal("money_super_in");
            mr.super_out = getVal("money_super_out");
            mr.large_net_in = getVal("money_large_net_in");
            mr.large_net_in_rate = getVal("money_large_net_in_rate");
            mr.large_in = getVal("money_large_in");
            mr.large_out = getVal("money_large_out");
            mr.mid_net_in = getVal("money_mid_net_in");
            mr.mid_net_in_rate = getVal("money_mid_net_in_rate");
            mr.mid_in = getVal("money_mid_in");
            mr.mid_out = getVal("money_mid_out");
            mr.small_net_in = getVal("money_small_net_in");
            mr.small_net_in_rate = getVal("money_small_net_in_rate");
            mr.small_in = getVal("money_small_in");
            mr.small_out = getVal("money_small_out");
            mfMap[std::move(key)] = mr;
        }
    }
    INTERNAL_INFO_STREAM << "[DataCacheAdapter] augmentMF: PG查询完成, mfMap.size=" << mfMap.size();

    // 2. 委托 DataCache 做 Arrow 列追加
    return m_cache->augmentMoneyFlowColumns(dataId, mfMap);
}
