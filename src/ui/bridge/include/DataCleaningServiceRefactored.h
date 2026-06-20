// DataCleaningServiceRefactored.h
// 重构后的数据清洗服务，使用foundation线程池而不是Qt线程
// 桥接部分只负责转发，不包含复杂逻辑

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QDateTime>
#include <memory>
#include <functional>
#include <QTimer>

// 清洗统计信息
struct RefactoredCleaningStats {
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

class DataCleaningServiceRefactored : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY(DataCleaningServiceRefactored)
    
public:
    explicit DataCleaningServiceRefactored(QObject* parent = nullptr);
    ~DataCleaningServiceRefactored();
    
    // ============ 初始化 ============
    Q_INVOKABLE bool initialize();
    Q_INVOKABLE bool isInitialized() const { return m_initialized; }
    
    // ============ 核心接口 ============
    
    // 异步清洗——从 DataSet 加载数据，清洗后写入新 DataSet，清洗后写入新 DataSet
    //    全程纯 C++ 类型，零 QVariant
    Q_INVOKABLE void cleanDataFromDataSet(int dataSetId,
                                          const QVariantMap& rules);

    // 3. 取消当前清洗操作
    void cancelCleaning(const QString& requestId = QString());
    
    // ============ 规则管理 ============
    
    // 获取预定义规则集
    QVariantMap getDefaultRules() const;
    
    // 自定义规则
    void addCustomRule(const QString& ruleName, 
                      const QVariantMap& ruleConfig);
    void removeCustomRule(const QString& ruleName);
    QVariantMap getCustomRules() const;
    
    // ============ 统计和状态 ============
    
    RefactoredCleaningStats getLastCleaningStats(const QString& requestId = QString()) const;
    QVariantMap getAllCleaningStats() const;
    void resetStats();
    
    // 检查清洗操作状态
    bool isCleaningInProgress() const;
    QStringList getActiveCleaningRequests() const;
    
    // ============ 配置 ============
    
    void setMaxPreviewRecords(int maxRecords);
    int getMaxPreviewRecords() const;
    
    void setAsyncThreadCount(int count);
    int getAsyncThreadCount() const;
    
signals:
    // 清洗进度信号 - 只转发，不包含逻辑
    void cleaningProgress(const QString& requestId, int progress, const QString& message);
    void cleaningStarted(const QString& requestId, const QString& description);
    void dataSetCleaned(int dataSetId, int resultDataSetId,
                        const QString& message, int inputRows, int outputRows);

    // 统计信号
    void cleaningStatsUpdated(const QString& requestId, const RefactoredCleaningStats& stats);
    
    // 错误信号
    void cleaningError(const QString& requestId, const QString& error);
    
    // 规则更新信号
    void rulesUpdated();
    
private:
    // 内部实现细节
    class Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_initialized{false};
};

Q_DECLARE_METATYPE(RefactoredCleaningStats)

// 工厂函数：创建DataCleaningServiceRefactored实例
std::shared_ptr<DataCleaningServiceRefactored> createDataCleaningServiceRefactored(QObject* parent = nullptr);
