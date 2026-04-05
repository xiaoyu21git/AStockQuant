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
#include <QTimer>
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
        RULE_TIME_RANGE = 0,
        RULE_PRICE_FILTER,
        RULE_VOLUME_FILTER,
        RULE_COMPLETENESS_CHECK,
        RULE_OUTLIER_DETECTION,
        RULE_DUPLICATE_REMOVAL,
        RULE_FORMAT_VALIDATION,
        RULE_CUSTOM_FILTER,
        RULE_SURVIVOR_BIAS,
        RULE_REPORT_DATE_ALIGNMENT,
        RULE_ADJUSTED_PRICE,
        RULE_NEW_STOCK_FILTER,
        RULE_ST_FILTER,
        RULE_LIMIT_MOVE_TAG,
        RULE_SUSPENSION_FILL,
        RULE_WINSORIZATION,
        RULE_MISSING_VALUE_FILL,
        RULE_MARKET_CAP_FILTER,
        RULE_NEUTRALIZATION,
        RULE_STANDARDIZATION,
        RULE_INDEX_MEMBERSHIP_ALIGNMENT,
        RULE_CONTINUOUS_SUSPENSION_FILTER
    };

    enum CleaningRuleLevel {
        RULE_LEVEL_MANDATORY = 0,
        RULE_LEVEL_RECOMMENDED,
        RULE_LEVEL_OPTIONAL
    };

    enum CleaningRuleMode {
        RULE_MODE_SINGLE_POINT = 0,
        RULE_MODE_TEMPORAL,
        RULE_MODE_CROSS_SECTIONAL,
        RULE_MODE_TAG_GENERATION
    };

    // 清洗规则定义
    struct CleaningRule {
        CleaningRuleType type;
        QString id;
        QString name;
        QString description;
        QVariantMap parameters;
        bool enabled;
        CleaningRuleLevel level;
        CleaningRuleMode mode;
        int executionOrder;
        
        CleaningRule(CleaningRuleType t, const QString& n, const QString& desc = "")
            : type(t)
            , id(n)
            , name(n)
            , description(desc)
            , enabled(true)
            , level(RULE_LEVEL_OPTIONAL)
            , mode(RULE_MODE_SINGLE_POINT)
            , executionOrder(0) {}
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

    /**
     * @brief 清洗数据并保存到数据库
     * @param data 原始数据
     * @param rules 清洗规则
     * @param autoSave 是否自动保存到数据库
     * @return 清洗后的数据
     */
    QVariantList cleanDataWithPersistence(const QVariantList& data,
                                         const QVector<CleaningRule>& rules = QVector<CleaningRule>(),
                                         bool autoSave = true);

    /**
     * @brief 保存清洗结果到数据库
     * @param cleanedData 清洗后的数据
     * @return 是否保存成功
     */
    bool saveCleaningResult(const QVariantList& cleanedData);

    /**
     * @brief 从数据库加载清洗结果
     * @param taskId 任务ID
     * @return 清洗后的数据
     */
    QVariantList loadCleanedData(const QString& taskId);

    /**
     * @brief 计算数据质量评分
     * @param stats 清洗统计信息
     * @return 质量评分(0-100)
     */
    double calculateQualityScore(const CleaningStats& stats);

signals:
    /**
     * @brief 清洗进度信号
     * @param progress 进度百分比
     * @param message 进度消息
     */
    void cleaningProgress(int progress, const QString& message);

    /**
     * @brief 清洗进度详情信号
     * @param progress 进度百分比
     * @param message 进度消息
     * @param currentStock 当前处理股票
     */
    void cleaningProgressDetail(int progress, const QString& message, const QString& currentStock);
    
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

    /**
     * @brief 数据保存完成信号
     * @param taskId 任务ID
     */
    void dataSaved(const QString& taskId);
    
    /**
     * @brief 数据加载完成信号
     * @param taskId 任务ID
     * @param data 加载的数据
     */
    void dataLoaded(const QString& taskId, const QVariantList& data);

private:
    struct RuntimeContext;

    bool isCrossSectionalRule(const CleaningRule& rule) const;
    bool executeRule(const CleaningRule& rule, QVariantMap& data, RuntimeContext& context);
    bool executeCrossSectionalRule(const CleaningRule& rule, QVariantList& records, RuntimeContext& context);

    void updateCleaningStats(const CleaningRule& rule, int totalEvaluated, int passedCount);
    
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

    QVector<QString> m_seenKeys;
    QVariantMap m_cleaningContext;
};

#endif // DATACLEANINGENGINE_H