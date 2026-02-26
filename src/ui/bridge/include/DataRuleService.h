// DataRuleService.h - 规则服务，负责规则配置和管理
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

class DataRuleService : public QObject {
    Q_OBJECT
    
    // QML属性
    Q_PROPERTY(QVariantMap currentRules READ currentRules NOTIFY currentRulesChanged)
    Q_PROPERTY(QVariantList availableRuleTemplates READ availableRuleTemplates NOTIFY availableRuleTemplatesChanged)
    Q_PROPERTY(bool isSavingRules READ isSaving NOTIFY isSavingChanged)
    
public:
    explicit DataRuleService(QObject* parent = nullptr);
    ~DataRuleService();
    
    // QML可调用的方法
    Q_INVOKABLE void saveRules(const QVariantMap& rules);
    Q_INVOKABLE QVariantMap loadRules(const QString& ruleId = QString());
    Q_INVOKABLE void deleteRules(const QString& ruleId);
    Q_INVOKABLE QVariantList getRuleHistory(const QString& ruleId);
    Q_INVOKABLE void applyRules(const QVariantMap& rules, const QVariantList& data);
    Q_INVOKABLE QVariantMap validateRules(const QVariantMap& rules);
    Q_INVOKABLE void importRulesFromFile(const QString& filePath);
    Q_INVOKABLE void exportRulesToFile(const QString& filePath, const QString& ruleId);
    Q_INVOKABLE void createRuleTemplate(const QString& templateName, const QVariantMap& rules);
    Q_INVOKABLE void updateRuleTemplate(const QString& templateName, const QVariantMap& rules);
    
    // 属性getter
    QVariantMap currentRules() const;
    QVariantList availableRuleTemplates() const;
    bool isSaving() const;
    
signals:
    // 属性变化信号
    void currentRulesChanged();
    void availableRuleTemplatesChanged();
    void isSavingChanged();
    
    // 操作结果信号
    void rulesSaved(bool success, const QString& message, const QString& ruleId);
    void rulesLoaded(bool success, const QString& message, const QVariantMap& rules);
    void rulesDeleted(bool success, const QString& message, const QString& ruleId);
    void rulesApplied(bool success, const QString& message, const QVariantList& result);
    void rulesValidated(bool success, const QString& message, const QVariantMap& validationResult);
    void ruleTemplateCreated(bool success, const QString& message, const QString& templateName);
    void ruleTemplateUpdated(bool success, const QString& message, const QString& templateName);
    void rulesImported(bool success, const QString& message, const QString& filePath);
    void rulesExported(bool success, const QString& message, const QString& filePath);
    
    // 进度信号
    void progress(int progress, const QString& message);
    void error(const QString& errorMessage);
    
private:
    DataRuleService(const DataRuleService&) = delete;
    DataRuleService& operator=(const DataRuleService&) = delete;
    
    // 私有实现方法
    class Impl;
    std::unique_ptr<Impl> m_impl;
};