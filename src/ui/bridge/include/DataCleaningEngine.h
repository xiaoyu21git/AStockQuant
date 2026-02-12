#ifndef DATACLEANINGENGINE_H
#define DATACLEANINGENGINE_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QDateTime>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <memory>
#include <vector>
#include <functional>

/**
 * @brief 数据清洗引擎 - 核心清洗功能模块
 * 
 * 功能特点：
 * 1. 支持多种清洗规则
 * 2. 可扩展的规则系统
 * 3. 批量清洗和实时清洗
 * 4. 清洗结果统计
 * 5. 与规则设置模块集成
 */
class DataCleaningEngine : public QObject
{
    Q_OBJECT

public:
    explicit DataCleaningEngine(QObject *parent = nullptr);
    ~DataCleaningEngine();

    // 清洗规则类型
    enum CleaningRuleType {
        RULE_TIME_RANGE = 0,      // 时间范围过滤
        RULE_PRICE_FILTER,        // 价格过滤
        RULE_VOLUME_FILTER,       // 成交量过滤
        RULE_COMPLETENESS_CHECK,  // 完整性检查
        RULE_OUTLIER_DETECTION,   // 异常值检测
        RULE_DUPLICATE_REMOVAL,   // 重复数据删除
        RULE_FORMAT_VALIDATION,   // 格式验证
        RULE_CUSTOM_FILTER        // 自定义过滤
    };

    // 清洗规则定义
    struct CleaningRule {
        CleaningRuleType type;
        QString name;
        QString description;
        QVariantMap parameters;
        bool enabled;
        
        CleaningRule(CleaningRuleType t, const QString& n, const QString& desc = "")
            : type(t), name(n), description(desc), enabled(true) {}
    };

    // 清洗结果统计
    struct CleaningStats {
        int totalRecords;          // 总记录数
        int cleanedRecords;        // 清洗后记录数
        int removedRecords;        // 移除记录数
        QVariantMap ruleStats;     // 各规则统计
        QDateTime startTime;       // 清洗开始时间
        QDateTime endTime;         // 清洗结束时间
        qint64 durationMs;         // 清洗耗时(毫秒)
        
        CleaningStats() : totalRecords(0), cleanedRecords(0), removedRecords(0), durationMs(0) {}
    };

    /**
     * @brief 添加清洗规则
     */
    void addRule(const CleaningRule& rule);
    
    /**
     * @brief 移除清洗规则
     */
    void removeRule(const QString& ruleName);
    
    /**
     * @brief 启用/禁用规则
     */
    void setRuleEnabled(const QString& ruleName, bool enabled);
    
    /**
     * @brief 获取所有规则
     */
    QVector<CleaningRule> getRules() const;
    
    /**
     * @brief 执行数据清洗
     * @param data 原始数据
     * @param rules 清洗规则（可选，为空时使用已配置的规则）
     * @return 清洗后的数据
     */
    QVariantList cleanData(const QVariantList& data, 
                          const QVector<CleaningRule>& rules = QVector<CleaningRule>());
    
    /**
     * @brief 批量清洗数据
     * @param dataList 原始数据列表
     * @param rules 清洗规则
     * @return 清洗后的数据列表
     */
    QVector<QVariantList> batchCleanData(const QVector<QVariantList>& dataList,
                                        const QVector<CleaningRule>& rules);
    
    /**
     * @brief 获取上次清洗的统计信息
     */
    CleaningStats getLastCleaningStats() const;
    
    /**
     * @brief 创建预定义规则集
     */
    QVector<CleaningRule> createDefaultRuleSet();
    
    /**
     * @brief 创建技术分析规则集
     */
    QVector<CleaningRule> createTechnicalAnalysisRuleSet();
    
    /**
     * @brief 创建基本面分析规则集
     */
    QVector<CleaningRule> createFundamentalAnalysisRuleSet();
    
    /**
     * @brief 验证数据格式
     */
    bool validateDataFormat(const QVariantMap& data) const;
    
    /**
     * @brief 导出清洗规则到JSON
     */
    QVariantMap exportRulesToJson() const;
    
    /**
     * @brief 从JSON导入清洗规则
     */
    bool importRulesFromJson(const QVariantMap& json);

signals:
    /**
     * @brief 清洗进度信号
     * @param progress 进度百分比
     * @param message 进度消息
     */
    void cleaningProgress(int progress, const QString& message);
    
    /**
     * @brief 清洗完成信号
     * @param stats 清洗统计信息
     */
    void cleaningCompleted(const CleaningStats& stats);
    
    /**
     * @brief 清洗错误信号
     * @param error 错误信息
     */
    void cleaningError(const QString& error);
    
    /**
     * @brief 规则更新信号
     */
    void rulesUpdated();

private:
    /**
     * @brief 应用时间范围过滤
     */
    bool applyTimeRangeFilter(const QVariantMap& data, const QVariantMap& params);
    
    /**
     * @brief 应用价格过滤
     */
    bool applyPriceFilter(const QVariantMap& data, const QVariantMap& params);
    
    /**
     * @brief 应用成交量过滤
     */
    bool applyVolumeFilter(const QVariantMap& data, const QVariantMap& params);
    
    /**
     * @brief 应用完整性检查
     */
    bool applyCompletenessCheck(const QVariantMap& data, const QVariantMap& params);
    
    /**
     * @brief 应用异常值检测
     */
    bool applyOutlierDetection(const QVariantMap& data, const QVariantMap& params);
    
    /**
     * @brief 应用重复数据删除
     */
    bool applyDuplicateRemoval(const QVariantMap& data, const QVariantMap& params, 
                              QVector<QString>& seenKeys);
    
    /**
     * @brief 应用格式验证
     */
    bool applyFormatValidation(const QVariantMap& data, const QVariantMap& params);
    
    /**
     * @brief 应用自定义过滤
     */
    bool applyCustomFilter(const QVariantMap& data, const QVariantMap& params);
    
    /**
     * @brief 执行单条规则
     */
    bool executeRule(const CleaningRule& rule, const QVariantMap& data, 
                    QVariantMap& ruleContext);
    
    /**
     * @brief 更新清洗统计
     */
    void updateCleaningStats(const CleaningRule& rule, bool passed);
    
    /**
     * @brief 重置清洗统计
     */
    void resetCleaningStats();
    
    /**
     * @brief 验证规则参数
     */
    bool validateRuleParameters(const CleaningRule& rule) const;
    
    QVector<CleaningRule> m_rules;
    CleaningStats m_lastStats;
    mutable QMutex m_mutex;
    
    // 清洗上下文
    QVector<QString> m_seenKeys;  // 用于重复检测
    QVariantMap m_cleaningContext; // 清洗上下文信息
};

#endif // DATACLEANINGENGINE_H