#include "StrategyParamConfigHelper.h"
#include <QDate>
#include <functional>

// ══════════════════════════════════════════════════════════════
StrategyParamConfigHelper::StrategyParamConfigHelper(QObject* parent)
    : QObject(parent)
{
    m_strategyTypeIndex = makeEnum({
        {"Invalid", -1},
        {"DoubleMovingAverage", 0}, {"TurtleBreakout", 1}, {"BollingerBandMeanReversion", 2},
        {"RsiMeanReversion", 3}, {"MultiFactorSelection", 4}, {"EarningsSurprise", 5},
        {"StatisticalPairTrading", 6}, {"RiskParityAllocation", 7}, {"MachineLearningSelection", 8},
        {"OrderFlowImbalance", 9}, {"VolatilitySpread", 10},
        {"TrendFollowing", 0}, {"TrendBreakout", 1}, {"MeanReversion", 2},
        {"Momentum", 3}, {"Arbitrage", 4}, {"MachineLearning", 5},
        {"MultiFactor", 6}, {"HighFrequency", 7}, {"EventDriven", 8}, {"Custom", 9},
        {"Common", 100}
    });

    m_strategyBehaviorKind = makeEnum({
        {"Invalid", -1}, {"TrendFollowing", 0}, {"MeanReversion", 1}, {"Momentum", 2},
        {"Arbitrage", 3}, {"MultiFactor", 4}, {"MachineLearning", 5}, {"EventDriven", 6},
        {"HighFrequency", 7}, {"Custom", 8}
    });

    m_assetTypeIndex = makeEnum({
        {"Invalid", -1}, {"Stock", 0}, {"ETF", 1}, {"Index", 2},
        {"Future", 3}, {"Option", 4}, {"Bond", 5}, {"Forex", 6}, {"Commodity", 7}, {"Cryptocurrency", 8}
    });

    m_timeFrameIndex = makeEnum({
        {"Invalid", -1}, {"Min1", 0}, {"Min5", 1}, {"Min15", 2}, {"Min30", 3},
        {"Min60", 4}, {"Daily", 5}, {"Weekly", 6}, {"Monthly", 7}
    });

    m_riskLevelIndex = makeEnum({
        {"Invalid", -1}, {"Conservative", 0}, {"Moderate", 1}, {"Aggressive", 2}
    });
}

QVariantMap StrategyParamConfigHelper::makeEnum(std::initializer_list<std::pair<const char*, int>> vals)
{
    QVariantMap m;
    for (auto& [k, v] : vals) m[QString::fromUtf8(k)] = v;
    return m;
}

// ── 类型映射 ──────────────────────────────────────────────────

int StrategyParamConfigHelper::normalizeStrategyTypeIndex(int v) const
{
    if (v < 0 || v > 10) return -1;  // Invalid
    return v;
}

int StrategyParamConfigHelper::strategyBehaviorKindFromTypeIndex(int typeIndex) const
{
    static const int map[] = {0,0,1,3,4,5,2,4,5,7,8,9}; // by typeIndex 0..11
    int n = normalizeStrategyTypeIndex(typeIndex);
    if (n < 0 || n > 10) return -1;
    return map[n];
}

int StrategyParamConfigHelper::strategyTypeIndexFromBehaviorKind(int bk) const
{
    static const int map[] = {-1, 0,2,3,6,7,5,9,10,8}; // by behaviorKind 0..9
    if (bk < 0 || bk > 8) return -1;
    return map[bk];
}

QString StrategyParamConfigHelper::strategyTypeName(int typeIndex) const
{
    static const char* names[] = {
        QT_TR_NOOP("双均线策略"), QT_TR_NOOP("海龟突破"), QT_TR_NOOP("布林带均值回归"),
        QT_TR_NOOP("RSI均值回归"), QT_TR_NOOP("多因子选股"), QT_TR_NOOP("业绩超预期"),
        QT_TR_NOOP("统计配对交易"), QT_TR_NOOP("风险平价配置"), QT_TR_NOOP("机器学习选股"),
        QT_TR_NOOP("订单流不平衡"), QT_TR_NOOP("波动率套利")
    };
    int n = normalizeStrategyTypeIndex(typeIndex);
    return (n >= 0 && n <= 10) ? tr(names[n]) : QString();
}

QString StrategyParamConfigHelper::strategyTypeDescription(int typeIndex) const
{
    static const char* descs[] = {
        QT_TR_NOOP("基于双均线交叉信号进行交易"),
        QT_TR_NOOP("基于通道突破信号进行交易"),
        QT_TR_NOOP("利用布林带识别超买超卖进行均值回归"),
        QT_TR_NOOP("基于RSI指标识别超买超卖进行均值回归"),
        QT_TR_NOOP("综合多个因子评分,选择最优标的"),
        QT_TR_NOOP("基于业绩超预期事件进行交易"),
        QT_TR_NOOP("基于统计套利模型进行配对交易"),
        QT_TR_NOOP("基于风险平价理念配置资产组合"),
        QT_TR_NOOP("使用机器学习模型预测选股"),
        QT_TR_NOOP("基于订单流不平衡信号进行高频交易"),
        QT_TR_NOOP("基于期权波动率价差进行套利交易")
    };
    int n = normalizeStrategyTypeIndex(typeIndex);
    return (n >= 0 && n <= 10) ? tr(descs[n]) : QString();
}

QString StrategyParamConfigHelper::strategyIcon(int typeIndex) const
{
    static const char* icons[] = {"📈","🐢","📊","📉","🧠","📰","🔗","⚖️","🤖","⚡","📐"};
    int n = normalizeStrategyTypeIndex(typeIndex);
    return (n >= 0 && n <= 10) ? QString::fromUtf8(icons[n]) : QString();
}

QString StrategyParamConfigHelper::strategyBehaviorKindLabel(int bk) const
{
    static const char* labels[] = {
        QT_TR_NOOP("趋势跟踪"), QT_TR_NOOP("均值回归"), QT_TR_NOOP("动量"), QT_TR_NOOP("套利"),
        QT_TR_NOOP("多因子"), QT_TR_NOOP("机器学习"), QT_TR_NOOP("事件驱动"), QT_TR_NOOP("高频交易"), QT_TR_NOOP("自定义")
    };
    return (bk >= 0 && bk <= 8) ? tr(labels[bk]) : QString();
}

QString StrategyParamConfigHelper::defaultStrategyDescription(int typeIndex) const
{
    return strategyTypeDescription(typeIndex);
}

QVariantList StrategyParamConfigHelper::defaultStrategyTags(int typeIndex) const
{
    int n = normalizeStrategyTypeIndex(typeIndex);
    QVariantList tags;
    if (n >= 0 && n <= 10)
        tags.append(strategyTypeName(n));
    return tags;
}

// ── 资产/时间/风险 ────────────────────────────────────────────

QString StrategyParamConfigHelper::assetTypeName(int idx) const
{
    static const char* names[] = {QT_TR_NOOP("股票"), QT_TR_NOOP("ETF"), QT_TR_NOOP("指数"),
        QT_TR_NOOP("期货"), QT_TR_NOOP("期权"), QT_TR_NOOP("债券"), QT_TR_NOOP("外汇"),
        QT_TR_NOOP("大宗商品"), QT_TR_NOOP("加密货币")};
    return (idx >= 0 && idx <= 8) ? tr(names[idx]) : QString();
}

QString StrategyParamConfigHelper::timeFrameName(int idx) const
{
    static const char* names[] = {QT_TR_NOOP("1分钟"), QT_TR_NOOP("5分钟"), QT_TR_NOOP("15分钟"),
        QT_TR_NOOP("30分钟"), QT_TR_NOOP("60分钟"), QT_TR_NOOP("日线"), QT_TR_NOOP("周线"), QT_TR_NOOP("月线")};
    return (idx >= 0 && idx <= 7) ? tr(names[idx]) : QString();
}

QString StrategyParamConfigHelper::riskLevelName(int idx) const
{
    static const char* names[] = {QT_TR_NOOP("保守"), QT_TR_NOOP("稳健"), QT_TR_NOOP("激进")};
    return (idx >= 0 && idx <= 2) ? tr(names[idx]) : QString();
}

QString StrategyParamConfigHelper::riskLevelColor(int idx) const
{
    static const char* colors[] = {"#4ade80", "#f59e0b", "#ef4444"};
    return (idx >= 0 && idx <= 2) ? QString::fromUtf8(colors[idx]) : QStringLiteral("#888888");
}

// ── assignIfPresent ───────────────────────────────────────────

void StrategyParamConfigHelper::assignIfPresent(const QVariantMap& src, QVariantMap& dest,
    const QString& targetKey, const QStringList& sourceKeys,
    std::function<QVariant(const QVariant&)> transform) const
{
    for (const auto& sk : sourceKeys) {
        if (!src.contains(sk)) continue;
        QVariant v = src[sk];
        if (v.isNull() || (v.type() == QVariant::String && v.toString().isEmpty()))
            continue;
        dest[targetKey] = transform ? transform(v) : v;
        return;
    }
}

static QVariant toNumber(const QVariant& v)   { return v.toDouble(); }
static QVariant toBool(const QVariant& v)     { return v.toBool(); }
static QVariant toInt(const QVariant& v)      { return v.toInt(); }

// ── buildParamConfigs ─────────────────────────────────────────

QVariantList StrategyParamConfigHelper::buildParamConfigs(int strategyTypeIndex) const
{
    QVariantList configs;

    auto addSelect = [&](const QString& id, const QString& label, const QString& desc,
                         QVariantList opts, QVariant def, const QString& cat) {
        QVariantMap m; m["id"]=id; m["type"]="select"; m["label"]=label;
        m["description"]=desc; m["options"]=opts; m["default"]=def; m["category"]=cat;
        configs.append(m);
    };
    auto addSlider = [&](const QString& id, const QString& label, const QString& desc,
                         double def, double min, double max, double step, int decimals,
                         const QString& unit, const QString& cat) {
        QVariantMap m; m["id"]=id; m["type"]="slider"; m["label"]=label;
        m["description"]=desc; m["default"]=def; m["min"]=min; m["max"]=max;
        m["step"]=step; m["decimals"]=decimals; m["unit"]=unit; m["category"]=cat;
        configs.append(m);
    };
    auto cat = tr("通用参数");
    addSelect("allowShort", tr("允许做空"), tr("是否允许空头仓位"), QVariantList{false,true}, false, cat);
    addSlider("maxPositions", tr("最大持仓数"), tr("组合允许的最大持仓标的数"), 100, 1, 500, 1, 0, tr("只"), cat);
    addSlider("maxWeightPerStock", tr("单票最大权重"), tr("每个标的最大仓位权重"), 0.1, 0.01, 1.0, 0.01, 2, "", cat);
    addSlider("minWeightPerStock", tr("单票最小权重"), tr("每个标的最小仓位权重"), 0.0, 0.0, 0.5, 0.01, 2, "", cat);

    int n = normalizeStrategyTypeIndex(strategyTypeIndex);
    auto scat = tr("策略参数");

    switch (n) {
    case 0: // DoubleMovingAverage
        addSlider("fastPeriod", tr("快线周期"), tr("短期均线周期"), 5, 1, 50, 1, 0, tr("日"), scat);
        addSlider("slowPeriod", tr("慢线周期"), tr("长期均线周期"), 20, 5, 200, 1, 0, tr("日"), scat);
        break;
    case 1: // TurtleBreakout
        addSlider("channelPeriod", tr("通道周期"), tr("通道计算周期"), 20, 5, 100, 1, 0, tr("日"), scat);
        addSlider("breakoutMultiplier", tr("突破倍数"), tr("ATR突破倍数"), 2.0, 0.5, 5.0, 0.1, 1, "x", scat);
        addSlider("atrPeriod", tr("ATR周期"), tr("真实波幅计算周期"), 14, 5, 50, 1, 0, tr("日"), scat);
        break;
    case 2: // BollingerBand
        addSlider("period", tr("布林周期"), tr("布林带计算周期"), 20, 5, 100, 1, 0, tr("日"), scat);
        addSlider("standardDeviationMultiplier", tr("标准差倍数"), tr("上下轨标准差倍数"), 2.0, 1.0, 4.0, 0.1, 1, "σ", scat);
        addSlider("entryThreshold", tr("入场阈值"), tr("价格偏离均值的入场比例"), 0.8, 0.1, 1.0, 0.05, 2, "", scat);
        addSlider("exitThreshold", tr("出场阈值"), tr("回归均线的出场比例"), 0.2, 0.0, 0.5, 0.05, 2, "", scat);
        break;
    case 3: // RSI
        addSlider("period", tr("RSI周期"), tr("RSI计算周期"), 14, 5, 50, 1, 0, tr("日"), scat);
        addSlider("oversoldLevel", tr("超卖线"), tr("低于此值视为超卖"), 30, 10, 40, 1, 0, "", scat);
        addSlider("overboughtLevel", tr("超买线"), tr("高于此值视为超买"), 70, 60, 90, 1, 0, "", scat);
        break;
    case 4: // MultiFactor
        addSlider("topN", tr("Top N"), tr("因子评分排名前N只标的"), 20, 5, 100, 1, 0, tr("只"), scat);
        addSelect("industryNeutral", tr("行业中忄"), tr("是否启用行业中性化处理"), QVariantList{false,true}, false, scat);
        break;
    case 5: // EarningsSurprise
        addSlider("surpriseThreshold", tr("超预期阈值"), tr("盈利超预期的最小比例"), 0.05, 0.01, 0.3, 0.01, 2, "", scat);
        addSlider("holdDays", tr("持仓天数"), tr("事件驱动策略的持仓天数"), 5, 1, 30, 1, 0, tr("天"), scat);
        break;
    case 6: // StatisticalPairTrading
        addSlider("lookback", tr("回看天数"), tr("配对交易回看天数"), 60, 20, 250, 1, 0, tr("天"), scat);
        addSlider("entryZScore", tr("入场Z分数"), tr("价差偏离标准差的入场阈值"), 2.0, 1.0, 4.0, 0.1, 1, "σ", scat);
        addSlider("exitZScore", tr("出场Z分数"), tr("价差回归标准差的出场阈值"), 0.5, 0.0, 1.0, 0.1, 1, "σ", scat);
        addSlider("hedgeRatio", tr("对冲比率"), tr("配对交易的对冲比率"), 1.0, 0.1, 3.0, 0.1, 1, "", scat);
        break;
    case 7: // RiskParity
        addSlider("volatilityLookback", tr("波动率窗口"), tr("波动率计算窗口"), 60, 20, 250, 1, 0, tr("天"), scat);
        addSlider("targetVolatility", tr("目标波动率"), tr("组合目标年化波动率"), 0.15, 0.05, 0.4, 0.01, 2, "", scat);
        break;
    case 8: // MachineLearning
        addSlider("topN", tr("Top N"), tr("模型预测排名前N只标的"), 20, 5, 100, 1, 0, tr("只"), scat);
        break;
    case 9: // OrderFlowImbalance
        addSlider("depthLevels", tr("盘口深度"), tr("订单薄深度层数"), 5, 1, 10, 1, 0, tr("档"), scat);
        addSlider("imbalanceThreshold", tr("不平衡阈值"), tr("买卖力量不平衡阈值"), 0.3, 0.1, 0.9, 0.05, 2, "", scat);
        addSlider("maxHoldSeconds", tr("最大持仓秒数"), tr("高频策略最大持仓时间"), 60, 10, 600, 5, 0, tr("秒"), scat);
        break;
    case 10: // VolatilitySpread
        addSlider("historicalVolatilityWindow", tr("历史波动率窗口"), tr("历史波动率计算窗口"), 30, 10, 120, 1, 0, tr("天"), scat);
        addSlider("entrySpreadUpper", tr("入场价差上限"), tr("价差入场上限"), 0.3, 0.05, 0.5, 0.01, 2, "", scat);
        addSlider("entrySpreadLower", tr("入场价差下限"), tr("价差入场下限"), 0.1, 0.01, 0.3, 0.01, 2, "", scat);
        addSelect("deltaNeutral", tr("Delta中性"), tr("是否维持Delta中性"), QVariantList{false,true}, false, scat);
        break;
    default: break;
    }
    return configs;
}

// ── normalizeStrategyParameters ───────────────────────────────

QVariantMap StrategyParamConfigHelper::normalizeStrategyParameters(
    int strategyTypeIndex, const QVariantMap& raw) const
{
    QVariantMap out;
    int n = normalizeStrategyTypeIndex(strategyTypeIndex);

    assignIfPresent(raw, out, "allowShort", {"allowShort"}, toBool);
    assignIfPresent(raw, out, "maxPositions", {"maxPositions"}, toInt);
    assignIfPresent(raw, out, "maxWeightPerStock", {"maxWeightPerStock"}, toNumber);
    assignIfPresent(raw, out, "minWeightPerStock", {"minWeightPerStock"}, toNumber);
    assignIfPresent(raw, out, "weightScheme", {"weightScheme"}, toInt);
    assignIfPresent(raw, out, "rebalanceFrequency", {"rebalanceFrequency"}, toInt);

    switch (n) {
    case 0: // DoubleMovingAverage
        assignIfPresent(raw, out, "fastPeriod", {"fastPeriod"}, toInt);
        assignIfPresent(raw, out, "slowPeriod", {"slowPeriod"}, toInt);
        assignIfPresent(raw, out, "priceField", {"priceField"});
        break;
    case 1: // TurtleBreakout
        assignIfPresent(raw, out, "channelPeriod", {"channelPeriod"}, toInt);
        assignIfPresent(raw, out, "breakoutMultiplier", {"breakoutMultiplier"}, toNumber);
        assignIfPresent(raw, out, "atrPeriod", {"atrPeriod"}, toInt);
        break;
    case 2: // BollingerBand
        assignIfPresent(raw, out, "period", {"period"}, toInt);
        assignIfPresent(raw, out, "standardDeviationMultiplier", {"standardDeviationMultiplier"}, toNumber);
        assignIfPresent(raw, out, "entryThreshold", {"entryThreshold"}, toNumber);
        assignIfPresent(raw, out, "exitThreshold", {"exitThreshold"}, toNumber);
        break;
    case 3: // RSI
        assignIfPresent(raw, out, "period", {"period"}, toInt);
        assignIfPresent(raw, out, "oversoldLevel", {"oversoldLevel"}, toInt);
        assignIfPresent(raw, out, "overboughtLevel", {"overboughtLevel"}, toInt);
        break;
    case 4: // MultiFactor
        assignIfPresent(raw, out, "factorWeights", {"factorWeights"});
        assignIfPresent(raw, out, "topN", {"topN"}, toInt);
        assignIfPresent(raw, out, "industryNeutral", {"industryNeutral"}, toBool);
        break;
    case 5: // EarningsSurprise
        assignIfPresent(raw, out, "surpriseThreshold", {"surpriseThreshold"}, toNumber);
        assignIfPresent(raw, out, "holdDays", {"holdDays"}, toInt);
        assignIfPresent(raw, out, "eventSources", {"eventSources"});
        break;
    case 6: // StatisticalPairTrading
        assignIfPresent(raw, out, "tradingPair", {"tradingPair"});
        assignIfPresent(raw, out, "hedgeRatio", {"hedgeRatio"}, toNumber);
        assignIfPresent(raw, out, "lookback", {"lookback"}, toInt);
        assignIfPresent(raw, out, "entryZScore", {"entryZScore"}, toNumber);
        assignIfPresent(raw, out, "exitZScore", {"exitZScore"}, toNumber);
        break;
    case 7: // RiskParity
        assignIfPresent(raw, out, "assets", {"assets"});
        assignIfPresent(raw, out, "volatilityLookback", {"volatilityLookback"}, toInt);
        assignIfPresent(raw, out, "targetVolatility", {"targetVolatility"}, toNumber);
        break;
    case 8: // MachineLearning
        assignIfPresent(raw, out, "modelId", {"modelId"}, toInt);
        assignIfPresent(raw, out, "featureIds", {"featureIds"});
        assignIfPresent(raw, out, "topN", {"topN"}, toInt);
        break;
    case 9: // OrderFlowImbalance
        assignIfPresent(raw, out, "depthLevels", {"depthLevels"}, toInt);
        assignIfPresent(raw, out, "imbalanceThreshold", {"imbalanceThreshold"}, toNumber);
        assignIfPresent(raw, out, "maxHoldSeconds", {"maxHoldSeconds"}, toInt);
        break;
    case 10: // VolatilitySpread
        assignIfPresent(raw, out, "underlying", {"underlying"});
        assignIfPresent(raw, out, "optionChainFilter", {"optionChainFilter"});
        assignIfPresent(raw, out, "historicalVolatilityWindow", {"historicalVolatilityWindow"}, toInt);
        assignIfPresent(raw, out, "entrySpreadUpper", {"entrySpreadUpper"}, toNumber);
        assignIfPresent(raw, out, "entrySpreadLower", {"entrySpreadLower"}, toNumber);
        assignIfPresent(raw, out, "deltaNeutral", {"deltaNeutral"}, toBool);
        break;
    default: break;
    }

    assignIfPresent(raw, out, "rule_profile", {"rule_profile"});
    assignIfPresent(raw, out, "rule_composer_state", {"rule_composer_state"});
    assignIfPresent(raw, out, "execution_policy", {"execution_policy"});
    assignIfPresent(raw, out, "backtest_assumptions", {"backtest_assumptions"});
    assignIfPresent(raw, out, "strategy_scope_context", {"strategy_scope_context"});
    assignIfPresent(raw, out, "factor_overlay", {"factor_overlay"});

    return out;
}

// ── buildCompleteStrategyData ─────────────────────────────────

QVariantMap StrategyParamConfigHelper::buildCompleteStrategyData(const QVariantMap& ctx) const
{
    int typeIndex = normalizeStrategyTypeIndex(ctx.value("selectedStrategyTypeIndex").toInt());
    int behaviorKind = strategyBehaviorKindFromTypeIndex(typeIndex);
    QVariantMap normParams = normalizeStrategyParameters(typeIndex,
        ctx.value("strategyParameters").toMap());

    QVariantMap data;
    data["name"] = ctx.value("strategyName");
    data["displayName"] = ctx.value("strategyName");
    data["strategyBehaviorKind"] = behaviorKind;
    data["strategyTypeIndex"] = typeIndex;
    data["typeName"] = strategyTypeName(typeIndex);
    data["description"] = ctx.value("strategyDescription");
    data["assetTypeIndex"] = ctx.value("assetTypeIndex");
    data["timeFrameIndex"] = ctx.value("timeFrameIndex");
    data["riskLevelIndex"] = ctx.value("riskLevelIndex");
    data["optimizationMethod"] = ctx.value("optimizationMethod");
    data["enableAdvancedOptions"] = ctx.value("enableAdvancedOptions");
    data["statusIndex"] = false;
    data["createdDate"] = QDate::currentDate().toString("yyyy-MM-dd");
    data["returns"] = "+0.0%";
    data["maxDrawdown"] = "-0.0%";
    data["sharpeRatio"] = "0.0";
    data["winRate"] = "0.0%";
    data["tags"] = ctx.value("strategyTags");
    data["parameters"] = normParams;
    data["parameterCount"] = normParams.size();
    return data;
}

// ── 步骤 ────────────────────────────────────────────────────────

QString StrategyParamConfigHelper::stepLabel(int step) const
{
    static const char* labels[] = {"①","②","③"};
    return (step >= 1 && step <= 3) ? tr(labels[step-1]) : QString();
}

QString StrategyParamConfigHelper::stepTitle(int step) const
{
    static const char* titles[] = {
        QT_TR_NOOP("策略类型与信息"), QT_TR_NOOP("参数与规则模板"), QT_TR_NOOP("确认与创建")
    };
    return (step >= 1 && step <= 3) ? tr(titles[step-1]) : QString();
}

QString StrategyParamConfigHelper::stepDescription(int step) const
{
    static const char* descs[] = {
        QT_TR_NOOP("选择策略类型,填写基本信息"),
        QT_TR_NOOP("配置策略参数,应用规则模板"),
        QT_TR_NOOP("确认配置,保存并创建策略")
    };
    return (step >= 1 && step <= 3) ? tr(descs[step-1]) : QString();
}

bool StrategyParamConfigHelper::isStepValid(int step, const QVariantMap& data) const
{
    if (step == 1) {
        int ti = data.value("selectedStrategyTypeIndex").toInt();
        return normalizeStrategyTypeIndex(ti) >= 0 &&
               !data.value("strategyName").toString().trimmed().isEmpty();
    }
    if (step == 2) {
        return data.value("parametersValid").toBool();
    }
    return true;
}

#include "moc_StrategyParamConfigHelper.cpp"
