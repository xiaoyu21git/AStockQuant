// DataCacheAdapter.h — 纯 C++ DataCache 的轻量适配器
// 职责: 管理文件路径 + 数据集元数据索引
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <functional>

#include "DataCache.h"

namespace arrow { class Table; }

class DataCacheAdapter : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(DataCacheAdapter)

public:
    static DataCacheAdapter& instance();

    bool initialize(const QString& persistentDir);
    bool isInitialized() const;

    // ── 数据集管理 ──
    int storeDataSet(const QVariantList& data, const QVariantMap& infoMap,
                     std::function<void(int,int)> progressCallback = {});
    int storeDataSetFromRows(const std::vector<foundation::json::JsonFacade>& rows,
                              const QVariantMap& infoMap,
                              std::function<void(int,int)> progressCallback = {});
    cleaning::DataCache::ArrowWriteToken beginArrowWrite(int dataId);
    cleaning::DataCache::ArrowWriteToken beginArrowWrite(int dataId,
        const std::vector<std::string>& fieldNames,
        const std::unordered_set<std::string>& numericFields);
    void appendArrowBatch(cleaning::DataCache::ArrowWriteToken token,
                           const std::vector<foundation::json::JsonFacade>& rows);
    void appendArrowTable(cleaning::DataCache::ArrowWriteToken token,
                           const std::shared_ptr<arrow::Table>& table);
    void finishArrowWrite(cleaning::DataCache::ArrowWriteToken token, int rowCount);
    QVariantList getDataSetById(int dataId);
    QVariantMap getDataSetInfo(int dataId) const;
    /// @brief 仅读取 Arrow schema 字段名列表（零数据行加载）
    QStringList getDataSetSchemaFields(int dataId) const;
    QVector<QVariantMap> getAllDataSetInfos() const;
    bool removeDataSet(int dataId);
    int removeDataSetsBySourceType(const std::string& sourceType);

signals:
    void dataSetStored(int dataId, QVariantMap info);
    void dataSetRemoved(int dataId);

private:
    DataCacheAdapter();
    void ensureInitialized();

    static cleaning::DataSetInfo mapToCppInfo(const QVariantMap& m);
    static QVariantMap cppInfoToMap(const cleaning::DataSetInfo& info);

    cleaning::DataCache* m_cache;
};
