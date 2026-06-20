// DataCacheAdapter.cpp — 纯 C++ DataCache 的轻量适配器
#include "DataCacheAdapter.h"
#include "AppStoragePaths.h"
#include "foundation/json/json_facade.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <cstdio>

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
        m_cache->initialize("persistent");
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

    // QVariantList → vector<JsonFacade> → Parquet（domain 层纯 C++）
    std::vector<foundation::json::JsonFacade> rows;
    rows.reserve(data.size());
    for (const QVariant& item : data) {
        QJsonDocument doc(QJsonObject::fromVariantMap(item.toMap()));
        auto j = foundation::json::JsonFacade::parse(doc.toJson(QJsonDocument::Compact).toStdString());
        rows.push_back(std::move(j));
    }
    m_cache->saveDataSetFile(dataId, rows);

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

    fprintf(stderr, "[DataCacheAdapter] stored dataset %d: %s (%d rows)\n",
            dataId, fullInfo.displayName.c_str(), static_cast<int>(data.size()));
    fflush(stderr);

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

    // 直写 Arrow，无 QVariant 中转
    m_cache->saveDataSetFile(dataId, rows);

    auto fullInfo = m_cache->getDataSetInfo(dataId);
    fullInfo.rowCount = static_cast<int>(rows.size());
    auto metaJson = fullInfo.toJson();
    std::string infoPath = m_cache->infoFilePath(dataId);
    FILE* fi = fopen(infoPath.c_str(), "wb");
    if (fi) {
        std::string metaStr = metaJson.toString();
        fwrite(metaStr.data(), 1, metaStr.size(), fi);
        fclose(fi);
    }

    fprintf(stderr, "[DataCacheAdapter] stored dataset %d (from rows): %s (%zu rows)\n",
            dataId, fullInfo.displayName.c_str(), rows.size());
    fflush(stderr);

    emit dataSetStored(dataId, cppInfoToMap(info));
    return dataId;
}

QVariantList DataCacheAdapter::getDataSetById(int dataId) {
    ensureInitialized();

    // Parquet → vector<JsonFacade> → QVariantList（domain 层纯 C++）
    auto rows = m_cache->loadDataSetFile(dataId);
    if (rows.empty()) return {};

    QVariantList result;
    result.reserve(static_cast<int>(rows.size()));
    for (const auto& row : rows) {
        QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(row.toString()));
        if (doc.isObject())
            result.append(doc.object().toVariantMap());
    }
    return result;
}

QVariantMap DataCacheAdapter::getDataSetInfo(int dataId) const {
    return cppInfoToMap(m_cache->getDataSetInfo(dataId));
}

QVector<QVariantMap> DataCacheAdapter::getAllDataSetInfos() const {
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
