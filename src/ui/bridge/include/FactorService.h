// FactorService.h
// 因子服务桥接层 - 负责QML与底层因子服务的交互
// 设计：单例QObject，桥接FactorInstanceManager/FactorDetectionService到QML
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>
#include <atomic>
#include <memory>
#include <mutex>

class FactorViewModel;
class FactorMetaService;
class FactorDetectionService;

// 前向声明域层类型
namespace factor {
struct FactorInstanceInfo;
class BaseFactor;
class FactorInstanceManager;
class DataAvailabilityChecker;
}
namespace astock { namespace database { class QtMySQLDatabase; } }

class FactorService : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool mutationInProgress READ mutationInProgress NOTIFY mutationInProgressChanged)
    Q_PROPERTY(QVariantMap lastOperationReport READ lastOperationReport NOTIFY lastOperationReportChanged)
    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)

public:
    static FactorService* instance();
    void destroy();

    explicit FactorService(QObject* parent = nullptr);
    ~FactorService() override;

    // ========== QML可调用的方法 ==========

    Q_INVOKABLE void initialize();

    // 获取ViewModel（用于QML ListView/Repeater绑定）
    Q_INVOKABLE FactorViewModel* getViewModel();

    // 因子CRUD
    Q_INVOKABLE QVariantList getAllFactors();
    Q_INVOKABLE QVariantMap getFactorById(const QString& factorId);
    Q_INVOKABLE QVariantMap getFactorByIdFromRepository(const QString& factorId);
    Q_INVOKABLE QString addFactor(const QVariantMap& factorData);
    Q_INVOKABLE bool updateFactor(const QString& factorId, const QVariantMap& factorData);
    Q_INVOKABLE bool deleteFactor(const QString& factorId);

    /// @brief 生成因子的跨表数据视图（收集 requiredFields → 按表分组 → 逐表加载 → 合并到 CachedMarketDataView）
    /// @param factorId 因子实例ID
    /// @return 操作结果描述
    Q_INVOKABLE QString generateFactorDataView(const QString& factorId);

    // 因子分析
    Q_INVOKABLE void analyzeFactor(const QString& factorId);

    /// @brief 获取底层 FactorInstanceManager（供回测引擎复用，避免重复创建）
    factor::FactorInstanceManager* instanceManager() const;

    /// @brief 因子上架检测：检查当前缓存数据集对每个因子的支持情况
    /// @return { factorId: { supported, reason, category, ... } }
    Q_INVOKABLE QVariantMap buildFactorSupportMap(
        const QStringList& factorIds,
        const QString& startDate, const QString& endDate,
        const QVariantMap& cacheSnapshot,
        const QString& dataSourceMode,
        int selectedDatasetId);

    // 属性访问器
    bool mutationInProgress() const;
    QVariantMap lastOperationReport() const;
    bool isInitialized() const;

signals:
    void mutationInProgressChanged();
    void lastOperationReportChanged();
    void initializedChanged();

    void factorAdded(const QString& factorId, const QVariantMap& factorData);
    void factorUpdated(const QString& factorId, const QVariantMap& factorData);
    void factorDeleted(const QString& factorId);
    void factorListRefreshed();
    void errorOccurred(const QString& error);
    void operationCompleted(const QString& operation, bool success, const QString& message);

private:
    // 内部帮助方法
    void beginMutation();
    void endMutation(bool success, const QString& message = QString());
    bool resolveBackend();
    void ensureViewModelPopulated();

    // 底层服务引用
    std::unique_ptr<FactorDetectionService> m_detectionService;
    std::shared_ptr<astock::database::QtMySQLDatabase> m_database;
    std::shared_ptr<factor::DataAvailabilityChecker> m_dataChecker;
    std::shared_ptr<factor::FactorInstanceManager> m_instanceManager;

    // ViewModel
    FactorViewModel* m_viewModel{nullptr};

    // 状态
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_mutationInProgress{false};
    std::atomic<bool> m_viewModelPopulating{false};  // 防重入：beginResetModel 期间 GridView 可能触发级联
    QVariantMap m_lastOperationReport;
    mutable std::mutex m_mutex;
};