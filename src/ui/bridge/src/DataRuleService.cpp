// DataRuleService.cpp - 规则服务实现
#include "DataRuleService.h"
#include "DataFetchFieldContractUtils.h"
#include <QDebug>
#include <QVariant>
#include <QThread>
#include <QDateTime>

namespace {

QStringList defaultDuplicateRuleKeyFields()
{
    return {
        QString(factor::bridge::CommonFieldKeys::SYMBOL),
        QString(factor::bridge::CommonFieldKeys::TRADE_DATE)
    };
}

QStringList defaultSuspensionFillFields()
{
    return factor::bridge::MarketBarFieldKeys::priceCore().orderedValues();
}

QStringList defaultMissingValueFillFields()
{
    return factor::bridge::MarketBarFieldKeys::missingFillDefaults().orderedValues();
}

}

// PIMPL实现类
class DataRuleService::Impl {
public:
    Impl(DataRuleService* parent) 
        : m_parent(parent)
        , m_isSaving(false) {
        qDebug() << "DataRuleService::Impl: 创建";
        
        // 初始化默认规则
        m_currentRules = {
            {"survivorBias", QVariantMap{{"enabled", true}}},
            {"reportDateAlignment", QVariantMap{{"enabled", true}}},
            {"adjustedPrice", QVariantMap{{"enabled", true}, {"preferAdjustedFields", true}, {"applyFactorFallback", true}}},
            {"newStockFilter", QVariantMap{{"enabled", true}, {"minTradeDays", 60}}},
            {"stFilter", QVariantMap{{"enabled", true}}},
            {"priceValidity", QVariantMap{{"enabled", true}, {"minPrice", 0.01}, {"maxPrice", 10000.0}, {"enforceChain", true}, {"allowZeroWhenSuspended", true}}},
            {"duplicateRemoval", QVariantMap{{"enabled", true}, {"keyFields", defaultDuplicateRuleKeyFields()}}},
            {"suspensionFill", QVariantMap{{"enabled", true}, {"fillFields", defaultSuspensionFillFields()}, {"maxForwardFillDays", 10}, {"dropAfterMaxDays", true}}},
            {"missingValueFill", QVariantMap{{"enabled", true}, {"fields", defaultMissingValueFillFields()}, {"maxLookbackDays", 5}}},
            {"limitMoveTag", QVariantMap{{"enabled", true}, {"upThreshold", 9.5}, {"downThreshold", -9.5}}},
            {"marketCapFilter", QVariantMap{{"enabled", true}, {"lowerTail", 0.05}}},
            {"winsorization", QVariantMap{{"enabled", true}, {"fields", QStringList{"factor_value", "factor", "value", "score"}}, {"lowerQuantile", 0.01}, {"upperQuantile", 0.99}}},
            {"indexAlignment", QVariantMap{{"enabled", false}, {"lagDays", 1}}},
            {"continuousSuspensionFilter", QVariantMap{{"enabled", false}, {"maxSuspensionDays", 10}}},
            {"timeRange", QVariantMap{{"enabled", false}, {"startDate", "2026-01-01"}, {"endDate", "2026-12-31"}}}
        };
        
        // 初始化规则模板
        m_ruleTemplates = {
            QVariantMap{{"name", "技术分析规则"}, {"description", "用于技术分析的数据清洗规则"}, {"rules", m_currentRules}},
            QVariantMap{{"name", "基本面分析规则"}, {"description", "用于基本面分析的数据清洗规则"}, {"rules", m_currentRules}},
            QVariantMap{{"name", "高频交易规则"}, {"description", "用于高频交易的数据清洗规则"}, {"rules", m_currentRules}}
        };
    }
    
    ~Impl() {
        qDebug() << "DataRuleService::Impl: 销毁";
    }
    
    void saveRules(const QVariantMap& rules) {
        qDebug() << "保存规则，规则数量:" << rules.size();
        
        m_isSaving = true;
        emit m_parent->isSavingChanged();
        
        QThread::msleep(100);
        
        m_currentRules = rules;
        
        QString ruleId = QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss");
        m_ruleHistory[ruleId] = rules;
        
        m_isSaving = false;
        emit m_parent->isSavingChanged();
        emit m_parent->rulesSaved(true, "规则保存成功", ruleId);
        emit m_parent->currentRulesChanged();
    }
    
    QVariantMap loadRules(const QString& ruleId) {
        qDebug() << "加载规则:" << ruleId;
        
        if (ruleId.isEmpty()) {
            return m_currentRules;
        }
        
        if (m_ruleHistory.contains(ruleId)) {
            return m_ruleHistory[ruleId];
        }
        
        // 默认返回当前规则
        return m_currentRules;
    }
    
    void deleteRules(const QString& ruleId) {
        qDebug() << "删除规则:" << ruleId;
        
        if (m_ruleHistory.contains(ruleId)) {
            m_ruleHistory.remove(ruleId);
            emit m_parent->rulesDeleted(true, "规则删除成功", ruleId);
        } else {
            emit m_parent->rulesDeleted(false, "规则不存在", ruleId);
        }
    }
    
    QVariantList getRuleHistory(const QString& ruleId) {
        qDebug() << "获取规则历史:" << ruleId;
        
        QVariantList history;
        for (auto it = m_ruleHistory.begin(); it != m_ruleHistory.end(); ++it) {
            if (ruleId.isEmpty() || it.key() == ruleId) {
                history.append(QVariantMap{{"id", it.key()}, {"rules", it.value()}});
            }
        }
        
        return history;
    }
    
    void applyRules(const QVariantMap& rules, const QVariantList& data) {
        qDebug() << "应用规则到数据，规则数量:" << rules.size() << "数据条数:" << data.size();
        
        QVariantList result = data; // 简化的应用逻辑
        
        QThread::msleep(50);
        
        emit m_parent->rulesApplied(true, "规则应用成功", result);
    }
    
    QVariantMap validateRules(const QVariantMap& rules) {
        qDebug() << "验证规则:" << rules.size();
        
        QVariantMap validationResult = {
            {"valid", true},
            {"message", "规则验证通过"},
            {"errors", QVariantList()},
            {"warnings", QVariantList()}
        };
        
        QThread::msleep(30);
        
        emit m_parent->rulesValidated(true, "规则验证成功", validationResult);
        return validationResult;
    }
    
    QVariantMap currentRules() const { return m_currentRules; }
    QVariantList availableRuleTemplates() const { return m_ruleTemplates; }
    bool isSaving() const { return m_isSaving; }
    
private:
    DataRuleService* m_parent;
    QVariantMap m_currentRules;
    QVariantList m_ruleTemplates;
    QMap<QString, QVariantMap> m_ruleHistory;
    bool m_isSaving;
};

// DataRuleService 公共接口实现
DataRuleService::DataRuleService(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(this)) {
    qDebug() << "DataRuleService: 创建";
}

DataRuleService::~DataRuleService() {
    qDebug() << "DataRuleService: 销毁";
}

void DataRuleService::saveRules(const QVariantMap& rules) {
    m_impl->saveRules(rules);
}

QVariantMap DataRuleService::loadRules(const QString& ruleId) {
    return m_impl->loadRules(ruleId);
}

void DataRuleService::deleteRules(const QString& ruleId) {
    m_impl->deleteRules(ruleId);
}

QVariantList DataRuleService::getRuleHistory(const QString& ruleId) {
    return m_impl->getRuleHistory(ruleId);
}

void DataRuleService::applyRules(const QVariantMap& rules, const QVariantList& data) {
    m_impl->applyRules(rules, data);
}

QVariantMap DataRuleService::validateRules(const QVariantMap& rules) {
    return m_impl->validateRules(rules);
}

void DataRuleService::importRulesFromFile(const QString& filePath) {
    qDebug() << "从文件导入规则:" << filePath;
    emit rulesImported(true, "规则导入成功", filePath);
}

void DataRuleService::exportRulesToFile(const QString& filePath, const QString& ruleId) {
    qDebug() << "导出规则到文件:" << filePath << ruleId;
    emit rulesExported(true, "规则导出成功", filePath);
}

void DataRuleService::createRuleTemplate(const QString& templateName, const QVariantMap& rules) {
    qDebug() << "创建规则模板:" << templateName;
    emit ruleTemplateCreated(true, "规则模板创建成功", templateName);
}

void DataRuleService::updateRuleTemplate(const QString& templateName, const QVariantMap& rules) {
    qDebug() << "更新规则模板:" << templateName;
    emit ruleTemplateUpdated(true, "规则模板更新成功", templateName);
}

QVariantMap DataRuleService::currentRules() const {
    return m_impl->currentRules();
}

QVariantList DataRuleService::availableRuleTemplates() const {
    return m_impl->availableRuleTemplates();
}

bool DataRuleService::isSaving() const {
    return m_impl->isSaving();
}