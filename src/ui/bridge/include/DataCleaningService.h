// DataCleaningService.h
// 专门负责数据清洗的服务类
// 职责：封装现有的DataCleaningEngine，提供同步预览和异步清洗接口

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QDateTime>
#include <memory>
#include <functional>

// 清洗统计信息（兼容DataCleaningEngine）
struct CleaningStats {
    int totalRecords{0};
    int cleanedRecords{0};
    int removedRecords{0};
    qint64 durationMs{0};
    QDateTime startTime;
    QDateTime endTime;
    QVariantMap ruleStats;
    
    QString toString() const {
        return QString("清洗统计: 原始=%1, 清洗后=%2, 移除=%3, 耗时=%4ms")
               .arg(totalRecords).arg(cleanedRecords).arg(removedRecords).arg(durationMs);
    }
    
    QVariantMap toVariantMap() const {
        QVariantMap map;
        map["totalRecords"] = totalRecords;
        map["cleanedRecords"] = cleanedRecords;
        map["removedRecords"] = removedRecords;
        map["durationMs"] = durationMs;
        map["startTime"] = startTime;
        map["endTime"] = endTime;
        map["ruleStats"] = ruleStats;
        return map;
    }
};

class DataCleaningService : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(DataCleaningService)
    
public:
    explicit DataCleaningService(QObject* parent = nullptr);
    ~DataCleaningService();
    
    // ============ 初始化 ============
    Q_INVOKABLE bool initialize();
    Q_INVOKABLE bool isInitialized() const { return m_initialized; }
    
    // ============ 核心接口 ============
    
    // 1. 同步预览清洗效果（适合小数据量）
    // 返回清洗后的数据，不修改原始数据
    QVariantList previewCleaning(const QVariantList& data, 
                                const QVariantMap& rules);
    
    // 2. 异步执行完整清洗（适合大数据量）
    Q_INVOKABLE void executeCleaningAsync(const QString& requestId,
                                         const QVariantList& data,
                                         const QVariantMap& rules);
    
    // 3. 批量清洗（多个数据集）
    void batchCleanAsync(const QStringList& requestIds,
                        const QVector<QVariantList>& dataList,
                        const QVariantMap& rules);
    
    // 4. 带缓存的清洗（自动使用缓存）
    QVariantList cleanWithCache(const QString& requestId,
                               const QVariantList& data,
                               const QVariantMap& rules);
    
    // 5. 取消当前清洗操作
    void cancelCleaning(const QString& requestId = QString());
    
    // ============ 规则管理 ============
    
    // 获取预定义规则集
    QVariantMap getDefaultRules() const;
    QVariantMap getTechnicalAnalysisRules() const;
    QVariantMap getFundamentalAnalysisRules() const;
    
    // 自定义规则
    void addCustomRule(const QString& ruleName, 
                      const QVariantMap& ruleConfig);
    void removeCustomRule(const QString& ruleName);
    QVariantMap getCustomRules() const;
    
    // ============ 统计和状态 ============
    
    CleaningStats getLastCleaningStats(const QString& requestId = QString()) const;
    QVariantMap getAllCleaningStats() const;
    void resetStats();
    
    // 检查清洗操作状态
    bool isCleaningInProgress() const;
    QStringList getActiveCleaningRequests() const;
    
    // ============ 配置 ============
    
    void setCacheEnabled(bool enabled);
    bool isCacheEnabled() const;
    
    void setMaxPreviewRecords(int maxRecords);
    int getMaxPreviewRecords() const;
    
    void setAsyncThreadCount(int count);
    int getAsyncThreadCount() const;
    
signals:
    // 清洗进度信号
    void cleaningProgress(const QString& requestId, int progress, const QString& message);
    void cleaningStarted(const QString& requestId, const QString& description);
    void cleaningCompleted(const QString& requestId, bool success, 
                          const QString& message, const QVariantList& cleanedData);
    
    // 统计信号
    void cleaningStatsUpdated(const QString& requestId, const CleaningStats& stats);
    
    // 错误信号
    void cleaningError(const QString& requestId, const QString& error);
    
    // 缓存信号
    void cacheHit(const QString& requestId, const QString& cacheKey);
    void cacheMiss(const QString& requestId, const QString& cacheKey);
    
    // 规则更新信号
    void rulesUpdated();
    
private:
    // 内部实现细节
    class Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_initialized{false};
};

// 工厂函数：创建DataCleaningService实例
std::shared_ptr<DataCleaningService> createDataCleaningService(QObject* parent = nullptr);