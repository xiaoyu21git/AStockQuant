#include "StrategyBacktestController.h"
#include "../../domain/backtest/include/StrategyBacktestService.h"
#include "../../domain/backtest/include/DatabaseStockDataProvider.h"
#include "../../domain/backtest/include/DatabaseFactorDataProvider.h"
#include "../include/DatabaseConnectionManager.h"
#include "../include/FactorService.h"
#include "../include/StrategyStructureResolvers.h"
#include "RiskConfigService.h"

#include <QDebug>
#include <QDate>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QFile>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include "foundation.h"
#include <algorithm>
#include <chrono>
#include <string>
#include <memory>
#include <stdexcept>

namespace {

QString normalizeDataSourceMode(const QString& rawMode) {
    const QString mode = rawMode.trimmed().toLower();
    if (mode == "cleaned" || mode == "cleaned_table" || mode == "cleaned_daily_bar") {
        return "cleaned";
    }
    if (mode == "cache" || mode == "dataset") {
        return "cache";
    }
    return "raw";
}

bool isPortfolioStrategyContext(const QVariantMap& strategyParams)
{
    const QString strategyType = strategyParams.value("selectedStrategyType").toString().trimmed().toUpper();
    const QString strategySubtype = strategyParams.value("selectedStrategySubtype").toString().trimmed().toLower();
    const QString portfolioSource = strategyParams.value("portfolio_source").toString().trimmed().toLower();
    return strategyType == "PORTFOLIO"
        || strategySubtype == "portfolio_builder"
        || portfolioSource == "portfolio_builder";
}

struct BacktestStageInfo {
    QString key;
    QString label;
    bool collectingData;
};

BacktestStageInfo resolveBacktestStageInfo(const QString& rawStatus, bool isRunning)
{
    const QString status = rawStatus.trimmed();
    if (status.isEmpty()) {
        return {QStringLiteral("idle"), QStringLiteral("等待开始"), false};
    }

    if (status.contains(QStringLiteral("校验回测参数"))) {
        return {QStringLiteral("validating"), QStringLiteral("校验配置"), false};
    }
    if (status.contains(QStringLiteral("准备股票池"))) {
        return {QStringLiteral("prepare_universe"), QStringLiteral("准备股票池"), true};
    }
    if (status.contains(QStringLiteral("解析指数成分股"))) {
        return {QStringLiteral("resolve_universe"), QStringLiteral("解析指数成分股"), true};
    }
    if (status.contains(QStringLiteral("应用股票池筛选"))) {
        return {QStringLiteral("filter_symbols"), QStringLiteral("筛选股票池"), true};
    }
    if (status.contains(QStringLiteral("连接数据源"))) {
        return {QStringLiteral("connect_data_source"), QStringLiteral("连接数据源"), true};
    }
    if (status.contains(QStringLiteral("加载行情数据"))) {
        return {QStringLiteral("load_market_data"), QStringLiteral("加载行情数据"), true};
    }
    if (status.contains(QStringLiteral("加载组合因子数据"))) {
        return {QStringLiteral("load_factor_data"), QStringLiteral("加载因子数据"), true};
    }
    if (status.contains(QStringLiteral("执行组合调仓回测"))) {
        return {QStringLiteral("run_portfolio_backtest"), QStringLiteral("执行组合回测"), false};
    }
    if (status.contains(QStringLiteral("执行策略撮合回测"))) {
        return {QStringLiteral("run_strategy_backtest"), QStringLiteral("执行策略回测"), false};
    }
    if (status.contains(QStringLiteral("汇总绩效指标"))) {
        return {QStringLiteral("summarize_metrics"), QStringLiteral("汇总回测结果"), false};
    }
    if (status.contains(QStringLiteral("回测完成")) || status.contains(QStringLiteral("已加载缓存回测结果"))) {
        return {QStringLiteral("completed"), QStringLiteral("回测完成"), false};
    }
    if (status.contains(QStringLiteral("已取消"))) {
        return {QStringLiteral("cancelled"), QStringLiteral("已取消"), false};
    }
    if (status.contains(QStringLiteral("准备提交回测"))
        || status.contains(QStringLiteral("回测启动中"))
        || status.contains(QStringLiteral("准备中"))) {
        return {QStringLiteral("queued"), QStringLiteral("准备提交回测"), false};
    }
    if (isRunning) {
        return {QStringLiteral("running"), QStringLiteral("处理中"), false};
    }
    return {QStringLiteral("failed"), QStringLiteral("回测失败"), false};
}

QVariantList parsePortfolioAllocations(const QVariant& rawAllocations)
{
    if (rawAllocations.typeId() == QMetaType::QVariantList) {
        return rawAllocations.toList();
    }

    const QString jsonText = rawAllocations.toString().trimmed();
    if (jsonText.isEmpty()) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        qWarning() << "StrategyBacktestController: failed to parse portfolio allocations json:" << parseError.errorString();
        return {};
    }

    return document.array().toVariantList();
}

double normalizedPercentRate(const QVariant& value, double fallback = 0.0) {
    bool ok = false;
    double numeric = value.toDouble(&ok);
    if (!ok) {
        return fallback;
    }
    return numeric > 1.0 ? numeric / 100.0 : numeric;
}

double normalizedTradingCostRate(const QVariant& value, double fallback = 0.0) {
    bool ok = false;
    double numeric = value.toDouble(&ok);
    if (!ok) {
        return fallback;
    }

    // 兼容历史脏数据：旧页面/旧配置里可能把 0.2 当成 0.2% 保存，
    // 但回测内需要的是比例值 0.002。
    if (numeric > 0.01) {
        return numeric / 100.0;
    }

    return numeric;
}

int positiveIntegerFromVariant(const QVariant& value, int fallback = 0)
{
    if (!value.isValid() || value.isNull()) {
        return fallback;
    }

    bool ok = false;
    int parsedValue = value.toInt(&ok);
    if (!ok) {
        const double parsedDouble = value.toDouble(&ok);
        if (ok) {
            parsedValue = qRound(parsedDouble);
        }
    }
    return ok && parsedValue > 0 ? parsedValue : fallback;
}

QVariantMap loadAppliedRiskConfiguration()
{
    if (auto* service = RiskConfigService::instance()) {
        return service->loadAppliedConfiguration();
    }
    return {};
}

QVariantMap variantMapValue(const QVariant& value)
{
    return value.canConvert<QVariantMap>() ? value.toMap() : QVariantMap{};
}

QVariantList normalizeRuleTemplateBindings(const QVariant& value)
{
    QVariantList bindings;
    const QVariantList list = value.toList();
    for (const QVariant& item : list) {
        const QVariantMap binding = item.toMap();
        if (!binding.isEmpty()) {
            bindings.append(binding);
        }
    }
    if (!bindings.isEmpty()) {
        return bindings;
    }

    const QVariantMap map = value.toMap();
    if (!map.isEmpty()) {
        const QStringList phaseOrder{QStringLiteral("market"), QStringLiteral("signal"), QStringLiteral("entry"), QStringLiteral("rebalance"), QStringLiteral("exit"), QStringLiteral("risk"), QStringLiteral("watch")};
        for (const QString& phase : phaseOrder) {
            const QVariantMap binding = map.value(phase).toMap();
            if (!binding.isEmpty()) {
                bindings.append(binding);
            }
        }
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            const QVariantMap binding = it.value().toMap();
            if (binding.isEmpty()) {
                continue;
            }
            bool exists = false;
            for (const QVariant& existing : bindings) {
                if (existing.toMap() == binding) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                bindings.append(binding);
            }
        }
        if (!bindings.isEmpty()) {
            return bindings;
        }
    }

    const QVariantMap singleBinding = value.toMap();
    if (!singleBinding.isEmpty()) {
        bindings.append(singleBinding);
    }
    return bindings;
}

QVariantList extractRuleTemplateBindingsFromComposerState(const QVariantMap& ruleProfile)
{
    QVariantMap composerState = ruleProfile.value(QStringLiteral("ruleComposerState")).toMap();
    if (composerState.isEmpty()) {
        composerState = ruleProfile.value(QStringLiteral("rule_composer_state")).toMap();
    }
    if (composerState.isEmpty()) {
        return {};
    }

    QVariantList bindings;
    const QVariantList stages = composerState.value(QStringLiteral("stages")).toList();
    for (const QVariant& stageValue : stages) {
        const QVariantMap stage = stageValue.toMap();
        const QString stageId = stage.value(QStringLiteral("stageId")).toString().trimmed().toLower();
        const QVariantList groups = stage.value(QStringLiteral("groups")).toList();
        for (const QVariant& groupValue : groups) {
            const QVariantMap group = groupValue.toMap();
            const QString groupId = group.value(QStringLiteral("groupId")).toString().trimmed();
            const QString groupTitle = group.value(QStringLiteral("title")).toString().trimmed();
            const QString groupRole = group.value(QStringLiteral("role")).toString().trimmed().toLower();
            const QString groupOperator = group.value(QStringLiteral("operator")).toString().trimmed().toLower();
            const int groupMinMatchCount = positiveIntegerFromVariant(
                group.value(QStringLiteral("groupMinMatchCount")),
                positiveIntegerFromVariant(
                    group.value(QStringLiteral("minMatchCount")),
                    positiveIntegerFromVariant(
                        group.value(QStringLiteral("minimumMatches")),
                        positiveIntegerFromVariant(group.value(QStringLiteral("atLeastCount"))))));
            const QVariantList rules = group.value(QStringLiteral("rules")).toList();
            for (const QVariant& ruleValue : rules) {
                const QVariantMap rule = ruleValue.toMap();
                const QString filePath = rule.value(QStringLiteral("filePath")).toString().trimmed();
                const QString fileName = rule.value(QStringLiteral("fileName")).toString().trimmed();
                const QString templateId = rule.value(QStringLiteral("templateId")).toString().trimmed();
                if (filePath.isEmpty() && fileName.isEmpty() && templateId.isEmpty()) {
                    continue;
                }

                QVariantMap binding;
                const QString phase = rule.value(QStringLiteral("phase")).toString().trimmed().toLower();
                binding.insert(QStringLiteral("phase"), phase.isEmpty() ? stageId : phase);
                if (!fileName.isEmpty()) {
                    binding.insert(QStringLiteral("file_name"), fileName);
                }
                if (!filePath.isEmpty()) {
                    binding.insert(QStringLiteral("file_path"), filePath);
                }
                if (!templateId.isEmpty()) {
                    binding.insert(QStringLiteral("template_id"), templateId);
                }

                const QString templateName = rule.value(QStringLiteral("templateName")).toString().trimmed();
                if (!templateName.isEmpty()) {
                    binding.insert(QStringLiteral("template_display_name"), templateName);
                }
                const QString summary = rule.value(QStringLiteral("summary")).toString().trimmed();
                if (!summary.isEmpty()) {
                    binding.insert(QStringLiteral("summary"), summary);
                }
                const QString category = rule.value(QStringLiteral("category")).toString().trimmed();
                if (!category.isEmpty()) {
                    binding.insert(QStringLiteral("category"), category);
                }
                const QString termId = rule.value(QStringLiteral("termId")).toString().trimmed();
                if (!termId.isEmpty()) {
                    binding.insert(QStringLiteral("term_id"), termId);
                }
                const QString termName = rule.value(QStringLiteral("termName")).toString().trimmed();
                if (!termName.isEmpty()) {
                    binding.insert(QStringLiteral("term_display_name"), termName);
                }
                if (!groupId.isEmpty()) {
                    binding.insert(QStringLiteral("group_id"), groupId);
                }
                if (!groupTitle.isEmpty()) {
                    binding.insert(QStringLiteral("group_title"), groupTitle);
                }
                if (!groupRole.isEmpty()) {
                    binding.insert(QStringLiteral("group_role"), groupRole);
                }
                if (!groupOperator.isEmpty()) {
                    binding.insert(QStringLiteral("group_operator"), groupOperator);
                }
                if (groupMinMatchCount > 0) {
                    binding.insert(QStringLiteral("group_min_match_count"), groupMinMatchCount);
                }

                bool exists = false;
                for (const QVariant& existing : bindings) {
                    if (existing.toMap() == binding) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    bindings.append(binding);
                }
            }
        }
    }

    return bindings;
}

QVariantMap primaryRuleTemplateBinding(const QVariantList& bindings)
{
    QVariantMap fallback;
    for (const QVariant& bindingValue : bindings) {
        const QVariantMap binding = bindingValue.toMap();
        if (binding.isEmpty()) {
            continue;
        }
        const QString phase = binding.value(QStringLiteral("phase")).toString().trimmed().toLower();
        if (phase == QStringLiteral("signal") || phase == QStringLiteral("entry")) {
            return binding;
        }
        if (fallback.isEmpty()) {
            fallback = binding;
        }
    }
    return fallback;
}

std::string serializeVariantJson(const QVariant& value)
{
    QJsonValue jsonValue = QJsonValue::fromVariant(value);
    if (jsonValue.isObject()) {
        return QJsonDocument(jsonValue.toObject()).toJson(QJsonDocument::Compact).toStdString();
    }
    if (jsonValue.isArray()) {
        return QJsonDocument(jsonValue.toArray()).toJson(QJsonDocument::Compact).toStdString();
    }
    return {};
}

void mergeConfiguredValues(QVariantMap& target, const QVariantMap& source)
{
    for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
        const QVariant& value = it.value();
        if (!value.isValid() || value.isNull()) {
            continue;
        }
        if (value.typeId() == QMetaType::QString && value.toString().trimmed().isEmpty()) {
            continue;
        }
        target.insert(it.key(), value);
    }
}

QVariant firstConfiguredValue(const QVariantMap& map, const QStringList& keys)
{
    for (const QString& key : keys) {
        if (!map.contains(key)) {
            continue;
        }

        const QVariant value = map.value(key);
        if (!value.isValid() || value.isNull()) {
            continue;
        }
        if (value.typeId() == QMetaType::QString && value.toString().trimmed().isEmpty()) {
            continue;
        }
        return value;
    }

    return {};
}

QStringList parseBacktestStringList(const QVariant& rawValue)
{
    QStringList values;
    QSet<QString> seenValues;

    auto appendValue = [&values, &seenValues](const QVariant& value) {
        const QString text = value.toString().trimmed();
        if (text.isEmpty() || seenValues.contains(text)) {
            return;
        }
        seenValues.insert(text);
        values.append(text);
    };

    if (!rawValue.isValid() || rawValue.isNull()) {
        return values;
    }

    if (rawValue.typeId() == QMetaType::QVariantList) {
        const QVariantList items = rawValue.toList();
        for (const QVariant& item : items) {
            appendValue(item);
        }
        return values;
    }

    const QString rawText = rawValue.toString().trimmed();
    if (rawText.isEmpty()) {
        return values;
    }

    if (rawText.startsWith('[')) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(rawText.toUtf8(), &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isArray()) {
            const QJsonArray array = document.array();
            for (const QJsonValue& value : array) {
                appendValue(value.toVariant());
            }
            return values;
        }
    }

    const QStringList parts = rawText.split(QRegularExpression(QString::fromUtf8(R"([,;\s，；]+)")), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        appendValue(part);
    }

    return values;
}

QVariantMap resolveStrategyParamView(const QVariantMap& strategyParams)
{
    QVariantMap resolved = variantMapValue(strategyParams.value("parameters"));
    mergeConfiguredValues(resolved, strategyParams);
    return resolved;
}

QVariantMap buildResolvedRuntimeView(const bridge::config::StrategyStructureResolution& resolution)
{
    QVariantMap runtimeParams;
    mergeConfiguredValues(runtimeParams, resolution.backtestAssumptions);
    mergeConfiguredValues(runtimeParams, resolution.executionPolicy);
    mergeConfiguredValues(runtimeParams, resolution.ruleProfile);
    mergeConfiguredValues(runtimeParams, resolution.strategyScopeContext);
    mergeConfiguredValues(runtimeParams, resolution.factorOverlay);
    return runtimeParams;
}

QStringList resolveConfiguredSymbolPool(const QVariantMap& strategyParams,
                                        const QVariantMap& resolvedStrategyParams,
                                        const QVariantMap& scopeContext = QVariantMap())
{
    QStringList resolvedSymbols;
    QSet<QString> seenSymbols;

    auto appendSymbols = [&resolvedSymbols, &seenSymbols](const QVariant& rawValue) {
        if (!rawValue.isValid() || rawValue.isNull()) {
            return;
        }

        auto appendSingle = [&resolvedSymbols, &seenSymbols](const QString& rawSymbol) {
            const QString symbol = rawSymbol.trimmed();
            if (symbol.isEmpty() || seenSymbols.contains(symbol)) {
                return;
            }
            seenSymbols.insert(symbol);
            resolvedSymbols.append(symbol);
        };

        if (rawValue.canConvert<QVariantList>()) {
            const QVariantList items = rawValue.toList();
            for (const QVariant& item : items) {
                appendSingle(item.toString());
            }
            return;
        }

        const QString rawText = rawValue.toString().trimmed();
        if (rawText.isEmpty()) {
            return;
        }

        if (rawText.startsWith('[')) {
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(rawText.toUtf8(), &parseError);
            if (parseError.error == QJsonParseError::NoError && document.isArray()) {
                const QJsonArray array = document.array();
                for (const QJsonValue& value : array) {
                    appendSingle(value.toString());
                }
                return;
            }
        }

        const QStringList parts = rawText.split(QRegularExpression(QStringLiteral("[,;\\s，；]+")), Qt::SkipEmptyParts);
        for (const QString& part : parts) {
            appendSingle(part);
        }
    };

    appendSymbols(firstConfiguredValue(scopeContext, {QStringLiteral("symbol_pool"), QStringLiteral("symbolPool")}));
    appendSymbols(firstConfiguredValue(resolvedStrategyParams, {QStringLiteral("symbol_pool"), QStringLiteral("symbolPool")}));
    appendSymbols(firstConfiguredValue(strategyParams, {QStringLiteral("symbol_pool"), QStringLiteral("symbolPool")}));
    return resolvedSymbols;
}

QString normalizeBacktestSymbolText(const QString& rawSymbol)
{
    return rawSymbol.trimmed().toUpper();
}

void appendUniqueBacktestSymbol(std::vector<std::string>& target,
                                QSet<QString>& seenSymbols,
                                const QString& rawSymbol)
{
    const QString normalizedSymbol = normalizeBacktestSymbolText(rawSymbol);
    if (normalizedSymbol.isEmpty() || seenSymbols.contains(normalizedSymbol)) {
        return;
    }

    seenSymbols.insert(normalizedSymbol);
    target.push_back(normalizedSymbol.toStdString());
}

template <typename Func>
bool submitToFoundationThreadPool(StrategyBacktestController* controller, Func&& func, QString* errorMessage = nullptr)
{
    QPointer<StrategyBacktestController> safeController(controller);
    try {
        foundation::Foundation::instance().thread_pool().post(
            [safeController, fn = std::forward<Func>(func)]() mutable {
                if (safeController) {
                    fn(safeController.data());
                }
            }
        );
        return true;
    } catch (const std::exception& e) {
        if (errorMessage) {
            *errorMessage = QString::fromUtf8(e.what());
        }
    } catch (...) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未知线程池错误");
        }
    }
    return false;
}

} // namespace

StrategyBacktestController::StrategyBacktestController(QObject *parent)
    : QObject(parent)
    , m_isRunning(false)
    , m_progress(0)
    , m_initialCapital(1000000.0)
    , m_startDate(QDate::currentDate().addYears(-1).toString("yyyy-MM-dd"))
    , m_endDate(QDate::currentDate().toString("yyyy-MM-dd"))
{
    qDebug() << "StrategyBacktestController constructed";

    try {
        m_service = std::make_unique<domain::backtest::StrategyBacktestService>();
        m_stockDataProvider = std::make_shared<domain::backtest::DatabaseStockDataProvider>(nullptr);
        m_stockDataProvider->setDataSourceContext(m_dataSourceMode.toStdString(), m_selectedDatasetId);
        m_service->setDataProvider(m_stockDataProvider);

        if (FactorService* factorService = FactorService::instance()) {
            auto sharedFactorService = std::shared_ptr<FactorService>(factorService, [](FactorService*) {});
            m_factorDataProvider = std::make_shared<domain::backtest::DatabaseFactorDataProvider>(sharedFactorService);
            m_service->setFactorProvider(m_factorDataProvider);
        }
        qDebug() << "StrategyBacktestService initialized successfully";
    } catch (const std::exception& e) {
        qWarning() << "Failed to initialize StrategyBacktestService:" << e.what();
    }
}

StrategyBacktestController::~StrategyBacktestController()
{
    qDebug() << "StrategyBacktestController destroyed";
}

domain::backtest::StrategyBacktestConfig StrategyBacktestController::resolveConfigForTesting(
    const QString& strategyId,
    const QVariantMap& strategyParams,
    const QVariantList& symbols,
    const QString& startDate,
    const QString& endDate)
{
    StrategyBacktestController controller;
    return controller.createConfig(strategyId, strategyParams, symbols, startDate, endDate);
}

double StrategyBacktestController::normalizeInitialCapitalValue(const QVariant& value, double fallback)
{
    bool ok = false;
    const double numeric = value.toDouble(&ok);
    if (!ok) {
        return fallback;
    }

    // 兼容旧版 QML 将初始资金按“万元”保存的历史数据。
    if (numeric > 0.0 && numeric < 10000.0) {
        return numeric * 10000.0;
    }

    return numeric;
}

void StrategyBacktestController::updateBacktestState(int progress, const QString& status)
{
    const int boundedProgress = (std::max)(0, (std::min)(100, progress));
    const QString normalizedStatus = status.trimmed().isEmpty() ? m_status : status.trimmed();
    const BacktestStageInfo stageInfo = resolveBacktestStageInfo(normalizedStatus, m_isRunning);

    const bool progressDirty = m_progress != boundedProgress;
    const bool statusDirty = m_status != normalizedStatus;
    const bool stageKeyDirty = m_currentStageKey != stageInfo.key;
    const bool stageLabelDirty = m_currentStageLabel != stageInfo.label;
    const bool collectingDirty = m_collectingData != stageInfo.collectingData;

    m_progress = boundedProgress;
    m_status = normalizedStatus;
    m_currentStageKey = stageInfo.key;
    m_currentStageLabel = stageInfo.label;
    m_collectingData = stageInfo.collectingData;

    if (stageKeyDirty || statusDirty) {
        qDebug() << "StrategyBacktestController: stage update"
                 << "strategyId=" << m_selectedStrategyId
                 << "stageKey=" << m_currentStageKey
                 << "stageLabel=" << m_currentStageLabel
                 << "collectingData=" << m_collectingData
                 << "progress=" << m_progress
                 << "status=" << m_status;
    }

    if (progressDirty) {
        emit progressChanged(m_progress);
    }
    if (statusDirty) {
        emit statusChanged(m_status);
    }
    if (stageKeyDirty) {
        emit currentStageKeyChanged(m_currentStageKey);
    }
    if (stageLabelDirty) {
        emit currentStageLabelChanged(m_currentStageLabel);
    }
    if (collectingDirty) {
        emit collectingDataChanged(m_collectingData);
    }

    emit backtestProgress(m_progress, m_status);
}

void StrategyBacktestController::resetTransientRunState(bool clearResult)
{
    m_currentTaskId.clear();

    if (!clearResult || m_backtestResult.isEmpty()) {
        return;
    }

    m_backtestResult.clear();
    emit backtestResultChanged(m_backtestResult);
}

void StrategyBacktestController::setSelectedStrategyId(const QString& strategyId)
{
    if (m_selectedStrategyId == strategyId)
        return;
    
    m_selectedStrategyId = strategyId;
    emit selectedStrategyIdChanged(strategyId);
}

void StrategyBacktestController::setStrategyParams(const QVariantMap& params)
{
    if (m_strategyParams == params)
        return;
    
    m_strategyParams = params;
    emit strategyParamsChanged(params);
}

void StrategyBacktestController::setInitialCapital(double capital)
{
    if (qFuzzyCompare(m_initialCapital, capital))
        return;
    
    m_initialCapital = capital;
    emit initialCapitalChanged(capital);
}

void StrategyBacktestController::setStartDate(const QString& date)
{
    if (m_startDate == date)
        return;
    
    m_startDate = date;
    emit startDateChanged(date);
}

void StrategyBacktestController::setEndDate(const QString& date)
{
    if (m_endDate == date)
        return;
    
    m_endDate = date;
    emit endDateChanged(date);
}

void StrategyBacktestController::setSelectedSymbols(const QVariantList& symbols)
{
    if (m_selectedSymbols == symbols)
        return;
    
    m_selectedSymbols = symbols;
    emit selectedSymbolsChanged(symbols);
}

void StrategyBacktestController::setDataSourceMode(const QString& dataSourceMode)
{
    const QString normalizedMode = normalizeDataSourceMode(dataSourceMode);
    if (m_dataSourceMode == normalizedMode)
        return;

    m_dataSourceMode = normalizedMode;
    if (m_stockDataProvider) {
        m_stockDataProvider->setDataSourceContext(m_dataSourceMode.toStdString(), m_selectedDatasetId);
    }
    emit dataSourceModeChanged(m_dataSourceMode);
}

void StrategyBacktestController::setSelectedDatasetId(int datasetId)
{
    if (m_selectedDatasetId == datasetId)
        return;

    m_selectedDatasetId = datasetId;
    if (m_stockDataProvider) {
        m_stockDataProvider->setDataSourceContext(m_dataSourceMode.toStdString(), m_selectedDatasetId);
    }
    emit selectedDatasetIdChanged(datasetId);
}

void StrategyBacktestController::startStrategyBacktest(
    const QString& strategyId,
    const QVariantMap& strategyParams,
    const QVariantList& symbols,
    const QString& startDate,
    const QString& endDate)
{
    if (m_isRunning) {
        qWarning() << "Strategy backtest already running";
        return;
    }

    if (!m_service || !m_stockDataProvider) {
        emit backtestFailed("策略回测服务未初始化");
        return;
    }
    
    qDebug() << "Starting strategy backtest for strategy:" << strategyId;
    qDebug() << "Params:" << strategyParams;
    qDebug() << "Symbols:" << symbols;
    qDebug() << "Date range:" << startDate << "to" << endDate;
    qDebug() << "Data source mode:" << m_dataSourceMode << "datasetId:" << m_selectedDatasetId;

    resetTransientRunState(true);
    
    m_isRunning = true;
    emit isRunningChanged(true);
    updateBacktestState(0, QStringLiteral("准备提交回测..."));
    emit backtestStarted(strategyId);
    
    // 生成任务ID
    m_currentTaskId = QString("strategy_%1_%2").arg(strategyId).arg(QDateTime::currentMSecsSinceEpoch());

    const auto config = createConfig(strategyId, strategyParams, symbols, startDate, endDate);
    QString submitError;
    if (!submitToFoundationThreadPool(this, [config](StrategyBacktestController* controller) {
        try {
            qDebug() << "StrategyBacktestController: background task started"
                     << "strategyId=" << QString::fromStdString(config.strategyId)
                     << "universeId=" << QString::fromStdString(config.universeId)
                     << "dataSourceMode=" << QString::fromStdString(config.dataSourceMode)
                     << "datasetId=" << config.datasetId;

            qDebug() << "StrategyBacktestController: invoking StrategyBacktestService::runStrategyBacktestSync";
            auto result = controller->m_service->runStrategyBacktestSyncWithProgress(
                config,
                [controller](int progress, const std::string& message) {
                    const QString qMessage = QString::fromStdString(message);
                    QMetaObject::invokeMethod(controller, [controller, progress, qMessage]() {
                        controller->updateBacktestState(progress, qMessage);
                    }, Qt::QueuedConnection);
                });
            QVariantMap qmlResult = controller->convertResultToQml(result);

            QMetaObject::invokeMethod(controller, [controller, qmlResult]() {
                controller->m_backtestResult = qmlResult;
                controller->m_isRunning = false;
                controller->updateBacktestState(100, QStringLiteral("回测完成"));
                controller->resetTransientRunState(false);

                emit controller->backtestResultChanged(qmlResult);
                emit controller->isRunningChanged(false);
                emit controller->backtestCompleted(qmlResult);
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            const QString error = QString::fromUtf8(e.what());
            QMetaObject::invokeMethod(controller, [controller, error]() {
                controller->m_isRunning = false;
                controller->updateBacktestState(0, error);
                controller->resetTransientRunState(false);
                emit controller->isRunningChanged(false);
                emit controller->backtestFailed(error);
            }, Qt::QueuedConnection);
        }
    }, &submitError)) {
        m_isRunning = false;
        updateBacktestState(0, QString("线程池不可用，无法启动回测: %1").arg(submitError));
        resetTransientRunState(false);
        emit isRunningChanged(false);
        emit backtestFailed(m_status);
    }
}

void StrategyBacktestController::startBatchStrategyBacktest(const QVariantList& strategies)
{
    if (m_isRunning) {
        qWarning() << "Strategy backtest already running";
        return;
    }
    
    qDebug() << "Starting batch strategy backtest for" << strategies.size() << "strategies";
    
    m_isRunning = true;
    m_progress = 0;
    m_status = "准备批量回测...";
    emit isRunningChanged(true);
    emit progressChanged(0);
    emit statusChanged(m_status);

    Q_UNUSED(strategies)
    m_isRunning = false;
    m_progress = 0;
    m_status = "批量回测尚未接入真实实现";

    emit isRunningChanged(false);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);
    emit backtestFailed(m_status);
}

void StrategyBacktestController::optimizeStrategyParameters(
    const QString& baseStrategyId,
    const QVariantMap& paramRanges,
    const QString& objectiveFunction,
    int maxIterations)
{
    if (m_isRunning) {
        qWarning() << "Strategy optimization already running";
        return;
    }
    
    qDebug() << "Starting strategy parameter optimization for:" << baseStrategyId;
    qDebug() << "Parameter ranges:" << paramRanges;
    
    m_isRunning = true;
    m_progress = 0;
    m_status = "准备参数优化...";
    emit isRunningChanged(true);
    emit progressChanged(0);
    emit statusChanged(m_status);
    emit optimizationStarted();
    
    Q_UNUSED(baseStrategyId)
    Q_UNUSED(paramRanges)
    Q_UNUSED(objectiveFunction)
    Q_UNUSED(maxIterations)

    m_isRunning = false;
    m_progress = 0;
    m_status = "参数优化尚未接入真实实现";

    emit isRunningChanged(false);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);
    emit optimizationFailed(m_status);
}

void StrategyBacktestController::compareStrategies(const QVariantList& strategies)
{
    if (m_isRunning) {
        qWarning() << "Strategy comparison already running";
        return;
    }
    
    qDebug() << "Starting strategy comparison for" << strategies.size() << "strategies";
    
    m_isRunning = true;
    m_progress = 0;
    m_status = "准备策略对比...";
    emit isRunningChanged(true);
    emit progressChanged(0);
    emit statusChanged(m_status);
    emit comparisonStarted();
    
    Q_UNUSED(strategies)
    m_isRunning = false;
    m_progress = 0;
    m_status = "策略对比尚未接入真实实现";

    emit isRunningChanged(false);
    emit progressChanged(m_progress);
    emit statusChanged(m_status);
    emit comparisonFailed(m_status);
}

void StrategyBacktestController::cancelBacktest()
{
    if (!m_isRunning) {
        qWarning() << "No backtest running to cancel";
        return;
    }
    
    qDebug() << "Cancelling current backtest";
    
    // 由于编译依赖问题，暂时简化实现
    // 实际集成时需使用m_service->cancelBacktest(m_currentTaskId.toStdString());
    
    m_isRunning = false;
    emit isRunningChanged(false);
    updateBacktestState(0, QStringLiteral("已取消"));
    resetTransientRunState(false);
    emit backtestCancelled();
}

void StrategyBacktestController::prepareForNextRun(bool clearResult)
{
    if (m_isRunning) {
        qWarning() << "Strategy backtest is running, skip prepareForNextRun";
        return;
    }

    resetTransientRunState(clearResult);
}

QVariantList StrategyBacktestController::getAvailableStrategies() const
{
    return QVariantList();
}

QVariantMap StrategyBacktestController::getDefaultStrategyParams(const QString& strategyId) const
{
    Q_UNUSED(strategyId)
    return QVariantMap();
}

QVariantMap StrategyBacktestController::getDefaultDateRange() const
{
    QVariantMap range;
    range["startDate"] = QDate::currentDate().addYears(-1).toString("yyyy-MM-dd");
    range["endDate"] = QDate::currentDate().toString("yyyy-MM-dd");
    return range;
}

QVariantList StrategyBacktestController::getAvailableSymbols(const QString& universeId) const
{
    Q_UNUSED(universeId)
    QVariantList symbols;
    if (!m_stockDataProvider) {
        return symbols;
    }

    const_cast<StrategyBacktestController*>(this)->m_stockDataProvider->setDataSourceContext(
        m_dataSourceMode.toStdString(), m_selectedDatasetId);
    for (const auto& symbol : m_stockDataProvider->getAvailableSymbols()) {
        symbols.append(QString::fromStdString(symbol));
    }

    return symbols;
}

QVariantList StrategyBacktestController::getAvailableIndustries() const
{
    QVariantList industries;

    try {
        auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
        if (!database) {
            return industries;
        }

        const auto result = database->executeQuery(
            QStringLiteral(
                "SELECT DISTINCT industry "
                "FROM symbol_info "
                "WHERE industry IS NOT NULL AND TRIM(industry) != '' "
                "ORDER BY industry ASC"));

        for (size_t rowIndex = 0; rowIndex < result.rowCount(); ++rowIndex) {
            const QString industry = result.getRow(rowIndex).getString(QStringLiteral("industry")).trimmed();
            if (!industry.isEmpty()) {
                industries.append(industry);
            }
        }
    } catch (const std::exception& e) {
        qWarning() << "StrategyBacktestController: failed to load industries:" << e.what();
    }

    return industries;
}

QVariantList StrategyBacktestController::getIndexConstituentSymbols(const QString& indexSymbol,
                                                                    const QString& snapshotDate) const
{
    QVariantList symbols;
    if (!m_stockDataProvider) {
        return symbols;
    }

    const auto indexSymbols = m_stockDataProvider->getIndexConstituentSymbols(indexSymbol, snapshotDate);
    for (const auto& symbol : indexSymbols) {
        symbols.append(QString::fromStdString(symbol));
    }
    return symbols;
}

bool StrategyBacktestController::saveResultToFile(const QString& filePath) const
{
    if (m_backtestResult.isEmpty()) {
        qWarning() << "No backtest result to save";
        return false;
    }
    
    QJsonDocument doc(QJsonObject::fromVariantMap(m_backtestResult));
    QFile file(filePath);
    
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open file for writing:" << filePath;
        return false;
    }
    
    file.write(doc.toJson());
    file.close();
    
    qDebug() << "Backtest result saved to:" << filePath;
    return true;
}

bool StrategyBacktestController::loadResultFromFile(const QString& filePath)
{
    QFile file(filePath);
    
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open file for reading:" << filePath;
        return false;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        qWarning() << "Invalid JSON data in file:" << filePath;
        return false;
    }
    
    m_backtestResult = doc.object().toVariantMap();
    emit backtestResultChanged(m_backtestResult);
    
    qDebug() << "Backtest result loaded from:" << filePath;
    return true;
}

std::map<std::string, std::pair<double, double>> StrategyBacktestController::parseParamRanges(const QVariantMap& qmlRanges) const
{
    std::map<std::string, std::pair<double, double>> ranges;
    
    for (auto it = qmlRanges.begin(); it != qmlRanges.end(); ++it) {
        QString paramName = it.key();
        QVariantList rangeList = it.value().toList();
        
        if (rangeList.size() >= 2) {
            double minVal = rangeList[0].toDouble();
            double maxVal = rangeList[1].toDouble();
            ranges[paramName.toStdString()] = {minVal, maxVal};
        }
    }
    
    return ranges;
}

domain::backtest::StrategyBacktestConfig StrategyBacktestController::createConfig(
    const QString& strategyId,
    const QVariantMap& strategyParams,
    const QVariantList& symbols,
    const QString& startDate,
    const QString& endDate) const
{
    domain::backtest::StrategyBacktestConfig config;
    config.strategyId = strategyId.toStdString();
    const QVariantMap appliedRiskConfig = loadAppliedRiskConfiguration();
    const bridge::config::StrategyStructureResolverSet resolverSet;
    const bridge::config::StrategyStructureResolution resolvedStructures = resolverSet.resolve(strategyParams, appliedRiskConfig);
    const QVariantMap resolvedStrategyParams = resolvedStructures.strategyView;
    QVariantMap strategyContextView = resolvedStrategyParams;
    mergeConfiguredValues(strategyContextView, resolvedStructures.strategyScopeContext);
    config.strategyName = firstConfiguredValue(
        strategyContextView,
        {QStringLiteral("selectedStrategyName"), QStringLiteral("strategy_name"), QStringLiteral("strategyName")}
    ).toString().toStdString();
    if (config.strategyName.empty()) {
        config.strategyName = strategyId.toStdString();
    }
    config.startDate = startDate.toStdString();
    config.endDate = endDate.toStdString();
    config.initialCapital = m_initialCapital;
    config.dataSourceMode = m_dataSourceMode.toStdString();
    config.datasetId = m_selectedDatasetId;

    const QVariantMap runtimeParams = buildResolvedRuntimeView(resolvedStructures);
    const QVariantMap resolvedFactorOverlay = resolvedStructures.factorOverlay;
    const bool portfolioContext = isPortfolioStrategyContext(strategyContextView);
    const QVariantList portfolioAllocations = parsePortfolioAllocations(
        firstConfiguredValue(strategyContextView,
            {QStringLiteral("portfolio_allocations_json"), QStringLiteral("factor_allocations"), QStringLiteral("allocations")})
    );
    const QString universeType = firstConfiguredValue(strategyContextView, {QStringLiteral("universeType")}).toString().trimmed().toLower();
    auto resolveParamValue = [&runtimeParams, &resolvedStrategyParams, &appliedRiskConfig](const QStringList& keys,
                                                                                    const QVariant& defaultValue) -> QVariant {
        for (const QString& key : keys) {
            if (runtimeParams.contains(key)) {
                return runtimeParams.value(key);
            }
        }
        for (const QString& key : keys) {
            if (resolvedStrategyParams.contains(key)) {
                return resolvedStrategyParams.value(key);
            }
        }
        for (const QString& key : keys) {
            if (appliedRiskConfig.contains(key)) {
                return appliedRiskConfig.value(key);
            }
        }
        return defaultValue;
    };

    auto applyConfigValues = [&config](const QVariantMap& values, bool overwriteExisting) {
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (it.key() == "backtest_runtime"
                || it.key() == "selectedStrategyId"
                || it.key() == "selectedStrategyName"
                || it.key() == "parameters"
                || it.key() == "advanced_options") {
                continue;
            }

            bool numericOk = false;
            const double numericValue = it.value().toDouble(&numericOk);
            if (numericOk) {
                if (overwriteExisting || config.strategyParams.count(it.key().toStdString()) == 0) {
                    config.strategyParams[it.key().toStdString()] = numericValue;
                }
                continue;
            }

            if (it.value().typeId() == QMetaType::Bool) {
                if (overwriteExisting || config.strategyOptions.count(it.key().toStdString()) == 0) {
                    config.strategyOptions[it.key().toStdString()] = it.value().toBool() ? "true" : "false";
                }
                continue;
            }

            if (overwriteExisting || config.strategyOptions.count(it.key().toStdString()) == 0) {
                config.strategyOptions[it.key().toStdString()] = it.value().toString().toStdString();
            }
        }
    };

    applyConfigValues(resolvedStrategyParams, false);
    applyConfigValues(runtimeParams, true);

    QVariantList resolvedRuleTemplateBindings = extractRuleTemplateBindingsFromComposerState(
        variantMapValue(firstConfiguredValue(
            runtimeParams,
            {QStringLiteral("rule_profile")}
        )));
    if (resolvedRuleTemplateBindings.isEmpty()) {
        resolvedRuleTemplateBindings = extractRuleTemplateBindingsFromComposerState(
            variantMapValue(firstConfiguredValue(
                resolvedStrategyParams,
                {QStringLiteral("rule_profile")}
            )));
    }
    if (resolvedRuleTemplateBindings.isEmpty()) {
        QVariantList ruleTemplateBindings = normalizeRuleTemplateBindings(firstConfiguredValue(
            runtimeParams,
            {QStringLiteral("rule_template_bindings"), QStringLiteral("rule_template_binding")}
        ));
        resolvedRuleTemplateBindings = ruleTemplateBindings.isEmpty()
            ? normalizeRuleTemplateBindings(firstConfiguredValue(
                  resolvedStrategyParams,
                  {QStringLiteral("rule_template_bindings"), QStringLiteral("rule_template_binding")}
              ))
            : ruleTemplateBindings;
    }
    if (!resolvedRuleTemplateBindings.isEmpty()) {
        const std::string bindingJson = serializeVariantJson(resolvedRuleTemplateBindings);
        if (!bindingJson.empty()) {
            config.strategyOptions["rule_template_bindings_json"] = bindingJson;
        }

        const QVariantMap ruleTemplateBinding = primaryRuleTemplateBinding(resolvedRuleTemplateBindings);
        const std::string primaryBindingJson = serializeVariantJson(ruleTemplateBinding);
        if (!primaryBindingJson.empty()) {
            config.strategyOptions["rule_template_binding_json"] = primaryBindingJson;
        }

        const QString templateFilePath = firstConfiguredValue(
            ruleTemplateBinding,
            {QStringLiteral("file_path"), QStringLiteral("filePath"), QStringLiteral("template_file_path"), QStringLiteral("templateFilePath")}
        ).toString().trimmed();
        if (!templateFilePath.isEmpty()) {
            config.strategyOptions["rule_template_file_path"] = templateFilePath.toStdString();
        }

        const QString templateFileName = firstConfiguredValue(
            ruleTemplateBinding,
            {QStringLiteral("file_name"), QStringLiteral("fileName"), QStringLiteral("template_file_name"), QStringLiteral("templateFileName")}
        ).toString().trimmed();
        if (!templateFileName.isEmpty()) {
            config.strategyOptions["rule_template_file_name"] = templateFileName.toStdString();
        }
    }

    if (!resolvedFactorOverlay.isEmpty()) {
        const std::string factorOverlayJson = serializeVariantJson(resolvedFactorOverlay);
        if (!factorOverlayJson.empty()) {
            config.strategyOptions["factor_overlay_json"] = factorOverlayJson;
        }

        const bool factorOverlayEnabled = resolvedFactorOverlay.value(QStringLiteral("enabled")).toBool();
        config.strategyOptions["factor_overlay_enabled"] = factorOverlayEnabled ? "true" : "false";

        const QVariantList factorAllocations = resolvedFactorOverlay.value(QStringLiteral("allocations")).toList();
        if (!factorAllocations.isEmpty()) {
            const std::string allocationJson = serializeVariantJson(factorAllocations);
            if (!allocationJson.empty()) {
                config.strategyOptions["factor_overlay_allocations_json"] = allocationJson;
            }
        }
    }

    const QString selectedStrategySubtype = strategyContextView.value("selectedStrategySubtype").toString().trimmed();
    if (!selectedStrategySubtype.isEmpty()) {
        config.strategyOptions["strategy_subtype"] = selectedStrategySubtype.toStdString();
        config.strategyOptions["sub_type"] = selectedStrategySubtype.toStdString();
    }

    const QString selectedStrategyType = strategyContextView.value("selectedStrategyType").toString().trimmed();
    if (!selectedStrategyType.isEmpty()) {
        config.strategyOptions["strategy_type"] = selectedStrategyType.toStdString();
    }

    if (portfolioContext) {
        config.strategyOptions["portfolio_source"] = strategyContextView.value("portfolio_source", QStringLiteral("portfolio_builder")).toString().toStdString();
        config.strategyOptions["portfolio_name"] = strategyContextView.value("portfolio_name").toString().toStdString();
        config.strategyOptions["portfolio_strategy_subtype"] = config.strategyOptions["strategy_subtype"];

        const double allocationCount = static_cast<double>(portfolioAllocations.size());
        if (allocationCount > 0.0) {
            config.strategyParams["portfolio_factor_count"] = allocationCount;
            const auto topNIt = config.strategyParams.find("top_n");
            if (topNIt == config.strategyParams.end() || !std::isfinite(topNIt->second) || topNIt->second <= 0.0) {
                config.strategyParams["top_n"] = 10.0;
            }
        }

        double maxWeight = 0.0;
        double totalWeight = 0.0;
        QStringList factorIds;
        for (const QVariant& allocationVariant : portfolioAllocations) {
            const QVariantMap allocation = allocationVariant.toMap();
            const double weightPercent = allocation.value("weight").toDouble();
            const double weightRatio = weightPercent > 1.0 ? weightPercent / 100.0 : weightPercent;
            maxWeight = (std::max)(maxWeight, weightRatio);
            totalWeight += weightRatio;

            const QString factorId = allocation.value("factor_id", allocation.value("factorId")).toString().trimmed();
            if (!factorId.isEmpty()) {
                factorIds.push_back(factorId);
            }
        }

        if (maxWeight > 0.0) {
            config.strategyParams["position_size"] = maxWeight;
        }
        if (totalWeight > 0.0) {
            config.strategyParams["portfolio_total_weight"] = totalWeight;
        }
        if (!factorIds.isEmpty()) {
            config.strategyOptions["portfolio_factor_ids"] = factorIds.join(',').toStdString();
        }
    }

    QSet<QString> seenSymbols;
    for (const QVariant& symbol : symbols) {
        appendUniqueBacktestSymbol(config.symbols, seenSymbols, symbol.toString());
    }

    if (config.symbols.empty() && universeType != "index") {
        const QStringList configuredSymbolPool = resolveConfiguredSymbolPool(strategyParams,
                                                                            strategyContextView,
                                                                            resolvedStructures.strategyScopeContext);
        for (const QString& symbol : configuredSymbolPool) {
            appendUniqueBacktestSymbol(config.symbols, seenSymbols, symbol);
        }
        if (!configuredSymbolPool.isEmpty()) {
            config.strategyOptions["universeType"] = "stock_pool";
        }
    }

    if (universeType == "index") {
        const QString universeId = strategyContextView.value("universeId",
                                 strategyContextView.value("indexSymbol")).toString().trimmed();
        config.universeId = universeId.toStdString();
        config.strategyOptions["universeType"] = "index";
    } else if (!universeType.isEmpty()) {
        config.strategyOptions["universeType"] = universeType.toStdString();
    }

    const QStringList sectorFilters = parseBacktestStringList(resolveParamValue(
        {"sectorFilters", "sector_filters", "industryFilters", "industry_filters"},
        QVariant()));
    for (const QString& sectorFilter : sectorFilters) {
        config.sectorFilters.push_back(sectorFilter.toStdString());
    }

    const QStringList marketFilters = parseBacktestStringList(resolveParamValue(
        {"marketFilters", "market_filters"},
        QVariant()));
    for (const QString& marketFilter : marketFilters) {
        const QString normalizedFilter = normalizeBacktestSymbolText(marketFilter);
        if (!normalizedFilter.isEmpty()) {
            config.marketFilters.push_back(normalizedFilter.toStdString());
        }
    }

    const QVariant defaultTotalExposure = appliedRiskConfig.value("maxTotalExposure", 100);
    const QVariant defaultSinglePosition = appliedRiskConfig.value("maxPositionPercent", defaultTotalExposure);

    config.commissionRate = normalizedTradingCostRate(resolveParamValue({"commissionRate", "commission", "transactionCost"}, 0.0003), 0.0003);
    config.slippageRate = normalizedTradingCostRate(resolveParamValue({"slippageRate", "slippage", "slippageCost"}, 0.0002), 0.0002);
    config.maxPositionRatio = normalizedPercentRate(
        resolveParamValue({"maxTotalExposure", "maxPositionRatio", "maxPositionPercent", "positionPercent", "position_size", "positionSize"}, defaultTotalExposure),
        1.0
    );
    config.maxSinglePositionRatio = normalizedPercentRate(
        resolveParamValue({"maxPositionPercent", "maxSinglePositionRatio", "positionPercent", "position_size", "positionSize"}, defaultSinglePosition),
        config.maxPositionRatio
    );
    config.maxDrawdownLimit = normalizedPercentRate(resolveParamValue({"maxDrawdownLimit"}, 20), 0.2);
    const bool autoStopEnabled = resolveParamValue({"autoStopEnabled"}, true).toBool();
    config.stopLossRate = autoStopEnabled
        ? normalizedPercentRate(resolveParamValue({"stopLossPercent", "stop_loss", "stopLoss"}, 5), 0.05)
        : 0.0;
    const double resolvedTakeProfitRate = normalizedPercentRate(
        resolveParamValue({"takeProfitPercent", "take_profit", "takeProfit"}, 15),
        0.15
    );
    config.strategyParams["stop_loss"] = config.stopLossRate;
    config.strategyParams["stopLoss"] = config.stopLossRate;
    config.strategyParams["stopLossPercent"] = config.stopLossRate;
    config.strategyOptions["autoStopEnabled"] = autoStopEnabled ? "true" : "false";
    config.strategyParams["take_profit"] = resolvedTakeProfitRate;
    config.strategyParams["takeProfit"] = resolvedTakeProfitRate;
    config.strategyParams["takeProfitPercent"] = resolvedTakeProfitRate;
    config.strategyParams["maxTotalExposure"] = config.maxPositionRatio;
    config.strategyParams["maxPositionRatio"] = config.maxPositionRatio;
    config.strategyParams["maxPositionPercent"] = config.maxSinglePositionRatio;
    config.strategyParams["maxSinglePositionRatio"] = config.maxSinglePositionRatio;
    config.strategyParams["maxDrawdownLimit"] = config.maxDrawdownLimit;
    const QVariant varWarningPercent = resolveParamValue({"varWarningPercent"}, QVariant());
    if (varWarningPercent.isValid()) {
        config.strategyParams["varWarningPercent"] = varWarningPercent.toDouble();
    }
    const QVariant orderSizeLimit = resolveParamValue({"orderSizeLimit"}, QVariant());
    if (orderSizeLimit.isValid()) {
        config.strategyParams["orderSizeLimit"] = orderSizeLimit.toDouble();
    }
    const QVariant turnoverLimit = resolveParamValue({"turnoverLimit"}, QVariant());
    if (turnoverLimit.isValid()) {
        config.strategyParams["turnoverLimit"] = turnoverLimit.toDouble();
    }
    const QVariant slippageLimit = resolveParamValue({"slippageLimit"}, QVariant());
    if (slippageLimit.isValid()) {
        config.strategyParams["slippageLimit"] = slippageLimit.toDouble();
    }
    const QVariant level1Breaker = resolveParamValue({"level1Breaker"}, QVariant());
    if (level1Breaker.isValid()) {
        config.strategyParams["level1Breaker"] = level1Breaker.toDouble();
    }
    const QVariant level2Breaker = resolveParamValue({"level2Breaker"}, QVariant());
    if (level2Breaker.isValid()) {
        config.strategyParams["level2Breaker"] = level2Breaker.toDouble();
    }
    const QVariant level3Breaker = resolveParamValue({"level3Breaker"}, QVariant());
    if (level3Breaker.isValid()) {
        config.strategyParams["level3Breaker"] = level3Breaker.toDouble();
    }
    config.strategyParams["autoStopEnabled"] = autoStopEnabled ? 1.0 : 0.0;
    if (runtimeParams.contains("initialCapital")) {
        config.initialCapital = normalizeInitialCapitalValue(runtimeParams.value("initialCapital"), config.initialCapital);
    }
    if (runtimeParams.contains("dataSourceMode")) {
        config.dataSourceMode = runtimeParams.value("dataSourceMode").toString().toStdString();
    }
    if (resolveParamValue({"rebalanceDays", "rebalancingPeriod", "rebalance_days"}, QVariant()).isValid()) {
        config.rebalanceFrequency = (std::max)(
            1,
            resolveParamValue({"rebalanceDays", "rebalancingPeriod", "rebalance_days"}, 5).toInt()
        );
    }
    config.strategyParams["rebalanceDays"] = static_cast<double>(config.rebalanceFrequency);
    config.strategyParams["rebalance_days"] = static_cast<double>(config.rebalanceFrequency);
    config.strategyParams["rebalancingPeriod"] = static_cast<double>(config.rebalanceFrequency);

    qDebug() << "StrategyBacktestController: resolved config"
             << "strategyId=" << strategyId
             << "strategyName=" << QString::fromStdString(config.strategyName)
             << "portfolioContext=" << portfolioContext
             << "portfolioAllocations=" << portfolioAllocations.size()
             << "initialCapital=" << config.initialCapital
             << "commissionRate=" << config.commissionRate
             << "slippageRate=" << config.slippageRate
             << "maxPositionRatio=" << config.maxPositionRatio
             << "dataSourceMode=" << QString::fromStdString(config.dataSourceMode)
             << "datasetId=" << config.datasetId;

    return config;
}

QVariantMap StrategyBacktestController::convertResultToQml(const domain::backtest::StrategyBacktestResult& result) const
{
    const auto jsonText = QString::fromStdString(result.toJson());
    const auto document = QJsonDocument::fromJson(jsonText.toUtf8());
    if (!document.isObject()) {
        return QVariantMap();
    }
    QVariantMap resultMap = document.object().toVariantMap();
    QVariantMap configMap = resultMap.value("config").toMap();
    if (configMap.isEmpty()) {
        const QString configJsonText = resultMap.value("config").toString().trimmed();
        if (!configJsonText.isEmpty()) {
            QJsonParseError configParseError;
            const QJsonDocument configDocument = QJsonDocument::fromJson(configJsonText.toUtf8(), &configParseError);
            if (configParseError.error == QJsonParseError::NoError && configDocument.isObject()) {
                configMap = configDocument.object().toVariantMap();
            } else {
                qWarning() << "StrategyBacktestController: failed to parse result config json:" << configParseError.errorString();
            }
        }
    }
    QVariantMap strategyParamsMap = configMap.value("strategyParams").toMap();
    strategyParamsMap.insert("maxTotalExposure", configMap.value("maxPositionRatio"));
    strategyParamsMap.insert("maxPositionRatio", configMap.value("maxPositionRatio"));
    strategyParamsMap.insert("maxPositionPercent", configMap.value("maxSinglePositionRatio"));
    strategyParamsMap.insert("maxSinglePositionRatio", configMap.value("maxSinglePositionRatio"));
    strategyParamsMap.insert("maxDrawdownLimit", configMap.value("maxDrawdownLimit"));
    if (strategyParamsMap.contains("varWarningPercent")) {
        configMap.insert("varWarningPercent", strategyParamsMap.value("varWarningPercent"));
    }
    if (strategyParamsMap.contains("orderSizeLimit")) {
        configMap.insert("orderSizeLimit", strategyParamsMap.value("orderSizeLimit"));
    }
    if (strategyParamsMap.contains("turnoverLimit")) {
        configMap.insert("turnoverLimit", strategyParamsMap.value("turnoverLimit"));
    }
    if (strategyParamsMap.contains("slippageLimit")) {
        configMap.insert("slippageLimit", strategyParamsMap.value("slippageLimit"));
    }
    if (strategyParamsMap.contains("level1Breaker")) {
        configMap.insert("level1Breaker", strategyParamsMap.value("level1Breaker"));
    }
    if (strategyParamsMap.contains("level2Breaker")) {
        configMap.insert("level2Breaker", strategyParamsMap.value("level2Breaker"));
    }
    if (strategyParamsMap.contains("level3Breaker")) {
        configMap.insert("level3Breaker", strategyParamsMap.value("level3Breaker"));
    }
    if (strategyParamsMap.contains("autoStopEnabled")) {
        configMap.insert("autoStopEnabled", strategyParamsMap.value("autoStopEnabled"));
    }
    strategyParamsMap.insert("stop_loss", configMap.value("stopLossRate"));
    strategyParamsMap.insert("stopLoss", configMap.value("stopLossRate"));
    strategyParamsMap.insert("stopLossPercent", configMap.value("stopLossRate"));
    if (!strategyParamsMap.contains("take_profit") && configMap.contains("takeProfitRate")) {
        strategyParamsMap.insert("take_profit", configMap.value("takeProfitRate"));
    }
    if (!strategyParamsMap.contains("takeProfit") && configMap.contains("takeProfitRate")) {
        strategyParamsMap.insert("takeProfit", configMap.value("takeProfitRate"));
    }
    if (!strategyParamsMap.contains("takeProfitPercent") && configMap.contains("takeProfitRate")) {
        strategyParamsMap.insert("takeProfitPercent", configMap.value("takeProfitRate"));
    }
    strategyParamsMap.insert("rebalanceDays", configMap.value("rebalanceFrequency"));
    strategyParamsMap.insert("rebalance_days", configMap.value("rebalanceFrequency"));
    strategyParamsMap.insert("rebalancingPeriod", configMap.value("rebalanceFrequency"));
    configMap.insert("strategyParams", strategyParamsMap);
    configMap.insert("maxTotalExposure", configMap.value("maxPositionRatio"));
    configMap.insert("maxPositionPercent", configMap.value("maxSinglePositionRatio"));
    configMap.insert("rebalanceDays", configMap.value("rebalanceFrequency"));
    configMap.insert("stopLossPercent", configMap.value("stopLossRate"));
    if (!configMap.contains("takeProfitRate")) {
        configMap.insert("takeProfitRate", strategyParamsMap.value("take_profit"));
    }
    configMap.insert("takeProfitPercent", strategyParamsMap.value("takeProfitPercent", configMap.value("takeProfitRate")));
    configMap["dataSourceMode"] = m_dataSourceMode;
    configMap["selectedDatasetId"] = m_selectedDatasetId;
    QVariantMap structureSource = configMap;
    structureSource.insert("parameters", strategyParamsMap);
    const bridge::config::StrategyStructureResolverSet resolverSet;
    const bridge::config::StrategyStructureResolution resolvedStructures = resolverSet.resolve(structureSource);
    configMap.insert("ruleProfileSnapshot", resolvedStructures.ruleProfile);
    configMap.insert("executionPolicySnapshot", resolvedStructures.executionPolicy);
    configMap.insert("backtestAssumptionsSnapshot", resolvedStructures.backtestAssumptions);
    configMap.insert("strategyScopeContextSnapshot", resolvedStructures.strategyScopeContext);
    configMap.insert("factorOverlaySnapshot", resolvedStructures.factorOverlay);
    resultMap["config"] = configMap;
    return resultMap;
}

QVariantList StrategyBacktestController::convertHistoryToQml(
    const QVariantList& history) const
{
    // 返回传入的历史记录
    return history;
}
