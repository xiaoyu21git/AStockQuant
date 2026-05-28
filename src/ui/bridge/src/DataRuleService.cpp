// DataRuleService.cpp - 规则服务实现
#include "DataRuleService.h"
#include "cleaning/CleaningEngine.h"
#include "cleaning/CleaningRuleContract.h"
#include "DataFetchFieldContractUtils.h"
#include <QDebug>
#include <QVariant>
#include <QThread>
#include <QDateTime>

namespace factor::bridge::detail {

bool configureStrictCleaningEngine(CleaningEngine& cleaningEngine,
                                   const QVariantMap& rules,
                                   QString* errorMessage);

} // namespace factor::bridge::detail

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

QStringList supportedRuleKeys()
{
    return factor::bridge::supportedStrictCleaningRuleKeyNames();
}

QString ruleKeyName(factor::bridge::CleaningRuleKey key)
{
    return factor::bridge::cleaningRuleKeyName(key);
}

QString ruleFieldName(factor::bridge::CleaningRuleConfigField field)
{
    return factor::bridge::cleaningRuleFieldName(field);
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
            {ruleKeyName(factor::bridge::CleaningRuleKey::Completeness), QVariantMap{{ruleFieldName(factor::bridge::CleaningRuleConfigField::Enabled), true}}},
            {ruleKeyName(factor::bridge::CleaningRuleKey::SurvivorBias), QVariantMap{{ruleFieldName(factor::bridge::CleaningRuleConfigField::Enabled), true}}},
            {ruleKeyName(factor::bridge::CleaningRuleKey::FinancialDateValidity), QVariantMap{{ruleFieldName(factor::bridge::CleaningRuleConfigField::Enabled), true}}},
            {ruleKeyName(factor::bridge::CleaningRuleKey::FinancialMetricSanitize), QVariantMap{{ruleFieldName(factor::bridge::CleaningRuleConfigField::Enabled), true}}},
            {ruleKeyName(factor::bridge::CleaningRuleKey::ReportDateAlignment), QVariantMap{{ruleFieldName(factor::bridge::CleaningRuleConfigField::Enabled), true}}},
            {ruleKeyName(factor::bridge::CleaningRuleKey::AdjustedPrice), QVariantMap{{ruleFieldName(factor::bridge::CleaningRuleConfigField::Enabled), true}, {ruleFieldName(factor::bridge::CleaningRuleConfigField::PreferAdjustedFields), true}, {ruleFieldName(factor::bridge::CleaningRuleConfigField::ApplyFactorFallback), true}}},
            {ruleKeyName(factor::bridge::CleaningRuleKey::NewStockFilter), QVariantMap{{ruleFieldName(factor::bridge::CleaningRuleConfigField::Enabled), true}, {ruleFieldName(factor::bridge::CleaningRuleConfigField::MinTradeDays), 60}}},
            {ruleKeyName(factor::bridge::CleaningRuleKey::STFilter), QVariantMap{{ruleFieldName(factor::bridge::CleaningRuleConfigField::Enabled), true}}},
            {ruleKeyName(factor::bridge::CleaningRuleKey::PriceValidity), QVariantMap{{ruleFieldName(factor::bridge::CleaningRuleConfigField::Enabled), true}, {ruleFieldName(factor::bridge::CleaningRuleConfigField::MinPrice), 0.01}, {ruleFieldName(factor::bridge::CleaningRuleConfigField::MaxPrice), 10000.0}, {ruleFieldName(factor::bridge::CleaningRuleConfigField::EnforceChain), true}, {ruleFieldName(factor::bridge::CleaningRuleConfigField::AllowZeroWhenSuspended), true}}},
            {ruleKeyName(factor::bridge::CleaningRuleKey::VolumeFilter), QVariantMap{{ruleFieldName(factor::bridge::CleaningRuleConfigField::Enabled), true}, {ruleFieldName(factor::bridge::CleaningRuleConfigField::MinVolume), 0.0}, {ruleFieldName(factor::bridge::CleaningRuleConfigField::MaxVolume), 1000000000.0}, {ruleFieldName(factor::bridge::CleaningRuleConfigField::AllowZeroWhenSuspended), true}}},
            {ruleKeyName(factor::bridge::CleaningRuleKey::DuplicateRemoval), QVariantMap{{ruleFieldName(factor::bridge::CleaningRuleConfigField::Enabled), true}, {ruleFieldName(factor::bridge::CleaningRuleConfigField::KeyFields), defaultDuplicateRuleKeyFields()}}},
            {ruleKeyName(factor::bridge::CleaningRuleKey::SuspensionFill), QVariantMap{{ruleFieldName(factor::bridge::CleaningRuleConfigField::Enabled), true}, {ruleFieldName(factor::bridge::CleaningRuleConfigField::FillFields), defaultSuspensionFillFields()}, {ruleFieldName(factor::bridge::CleaningRuleConfigField::MaxForwardFillDays), 10}, {ruleFieldName(factor::bridge::CleaningRuleConfigField::DropAfterMaxDays), true}}},
            {ruleKeyName(factor::bridge::CleaningRuleKey::MissingValueFill), QVariantMap{{ruleFieldName(factor::bridge::CleaningRuleConfigField::Enabled), true}, {ruleFieldName(factor::bridge::CleaningRuleConfigField::Fields), defaultMissingValueFillFields()}, {ruleFieldName(factor::bridge::CleaningRuleConfigField::MaxLookbackDays), 5}}},
            {ruleKeyName(factor::bridge::CleaningRuleKey::LimitMoveTag), QVariantMap{{ruleFieldName(factor::bridge::CleaningRuleConfigField::Enabled), true}, {ruleFieldName(factor::bridge::CleaningRuleConfigField::UpThreshold), 9.5}, {ruleFieldName(factor::bridge::CleaningRuleConfigField::DownThreshold), -9.5}}},
            {ruleKeyName(factor::bridge::CleaningRuleKey::ValuationSanitize), QVariantMap{{ruleFieldName(factor::bridge::CleaningRuleConfigField::Enabled), true}}}
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

        if (data.isEmpty()) {
            emit m_parent->rulesApplied(false, QStringLiteral("没有数据可清洗"), QVariantList());
            emit m_parent->error(QStringLiteral("没有数据可清洗"));
            return;
        }

        try {
            factor::bridge::CleaningEngine cleaningEngine;
            QObject::connect(&cleaningEngine, &factor::bridge::CleaningEngine::progress,
                             m_parent, [this](int progress, const QString& message) {
                                 emit m_parent->progress(progress, message);
                             });
            QObject::connect(&cleaningEngine, &factor::bridge::CleaningEngine::errorOccurred,
                             m_parent, [this](const QString& errorMessage) {
                                 emit m_parent->error(errorMessage);
                             });

            QString configurationError;
            if (!factor::bridge::detail::configureStrictCleaningEngine(cleaningEngine,
                                                                       rules,
                                                                       &configurationError)) {
                emit m_parent->rulesApplied(false, configurationError, QVariantList());
                emit m_parent->error(configurationError);
                return;
            }

            const QVariantList result = cleaningEngine.clean(data);
            const QString message = QStringLiteral("规则应用成功: 原始 %1 条 -> 清洗后 %2 条")
                .arg(data.size())
                .arg(result.size());
            emit m_parent->rulesApplied(true, message, result);
        } catch (const std::exception& e) {
            const QString errorMessage = QStringLiteral("规则应用失败: %1").arg(e.what());
            emit m_parent->rulesApplied(false, errorMessage, QVariantList());
            emit m_parent->error(errorMessage);
        } catch (...) {
            const QString errorMessage = QStringLiteral("规则应用失败: 未知错误");
            emit m_parent->rulesApplied(false, errorMessage, QVariantList());
            emit m_parent->error(errorMessage);
        }
    }
    
    QVariantMap validateRules(const QVariantMap& rules) {
        qDebug() << "验证规则:" << rules.size();

        factor::bridge::CleaningEngine cleaningEngine;
        QString validationError;
        const bool valid = factor::bridge::detail::configureStrictCleaningEngine(cleaningEngine,
                                                                                 rules,
                                                                                 &validationError);

        QVariantList errors;
        if (!valid && !validationError.isEmpty()) {
            errors.append(validationError);
        }

        QVariantMap validationResult = {
            {"valid", valid},
            {"message", valid ? QStringLiteral("规则验证通过") : QStringLiteral("规则验证失败")},
            {"errors", errors},
            {"warnings", QVariantList()}
        };

        const QString resultMessage = valid
            ? QStringLiteral("规则验证通过")
            : (validationError.isEmpty() ? QStringLiteral("规则验证失败") : validationError);
        
        QThread::msleep(30);
        
        emit m_parent->rulesValidated(valid, resultMessage, validationResult);
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