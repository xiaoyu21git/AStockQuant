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
    void appendArrowBatch(cleaning::DataCache::ArrowWriteToken token,
                           const std::vector<foundation::json::JsonFacade>& rows);
    void finishArrowWrite(cleaning::DataCache::ArrowWriteToken token, int rowCount);
    QVariantList getDataSetById(int dataId);
    QVariantMap getDataSetInfo(int dataId) const;
    QVector<QVariantMap> getAllDataSetInfos() const;
    bool removeDataSet(int dataId);

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
