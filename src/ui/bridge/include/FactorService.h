// FactorService.h
// 因子服务层 - 负责业务逻辑：数据库操作、缓存、因子管理
#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QMutex>
#include <memory>

namespace astock {
namespace database {
    class IFactorRepository;
}
}

class FactorService : public QObject {
    Q_OBJECT
    
public:
    explicit FactorService(QObject* parent = nullptr);
    ~FactorService();
    
    // 初始化方法
    Q_INVOKABLE void initialize();
    
    // 因子管理方法
    Q_INVOKABLE QString addFactor(const QVariantMap& factorData);
    Q_INVOKABLE bool updateFactor(const QString& factorId, const QVariantMap& factorData);
    Q_INVOKABLE bool deleteFactor(const QString& factorId);
    Q_INVOKABLE QVariantMap getFactorById(const QString& factorId);
    Q_INVOKABLE QVariantList getAllFactors();
    Q_INVOKABLE QVariantList getFactorsByType(const QString& type);
    
    // 搜索和过滤
    Q_INVOKABLE QVariantList searchFactors(const QString& keyword);
    Q_INVOKABLE QVariantList filterFactorsByCategory(const QString& category);
    Q_INVOKABLE QVariantList filterFactorsByTags(const QStringList& tags);
    
    // 批量操作
    Q_INVOKABLE bool importFactors(const QVariantList& factors);
    Q_INVOKABLE bool exportFactors(const QString& format, const QString& filePath);
    
    // 收藏管理
    Q_INVOKABLE bool toggleFavorite(const QString& factorId);
    
    // 数据同步
    Q_INVOKABLE void syncWithDatabase();
    Q_INVOKABLE void clearCache();
    
signals:
    // 业务操作信号
    void factorAdded(const QString& factorId, const QVariantMap& factorData);
    void factorUpdated(const QString& factorId, const QVariantMap& factorData);
    void factorDeleted(const QString& factorId);
    void factorsLoaded(const QVariantList& factors);
    void errorOccurred(const QString& error);
    
    // 数据变更信号 - 通知视图层更新
    void dataChanged();
    
private:
    // 初始化仓储
    void initializeRepository();
    
    // 数据库操作方法
    bool saveFactorToDatabase(const QVariantMap& factorData);
    bool updateFactorInDatabase(const QString& factorId, const QVariantMap& factorData);
    bool deleteFactorFromDatabase(const QString& factorId);
    QVariantList loadFactorsFromDatabase();
    
    // 缓存操作方法
    void saveFactorToCache(const QString& factorId, const QVariantMap& factorData);
    QVariantMap loadFactorFromCache(const QString& factorId);
    void removeFactorFromCache(const QString& factorId);
    void clearAllCache();
    
    // 数据验证
    bool validateFactorData(const QVariantMap& factorData, QString& errorMessage);
    
    // 生成因子ID
    QString generateFactorId(const QString& factorName);
    
private:
    std::shared_ptr<astock::database::IFactorRepository> m_repository;
    mutable QMutex m_mutex;
    bool m_initialized;
    
    // 内存缓存
    QMap<QString, QVariantMap> m_memoryCache;
};