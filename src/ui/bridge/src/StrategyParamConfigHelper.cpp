#include "StrategyParamConfigHelper.h"
#include <QDate>

StrategyParamConfigHelper::StrategyParamConfigHelper(QObject* parent) : QObject(parent)
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
        {"Invalid", -1}, {"Stock", 0}, {"ETF", 1}, {"Etf", 1}, {"Index", 2},
        {"Future", 3}, {"Futures", 3}, {"Option", 4}, {"Options", 4},
        {"Bond", 5}, {"Forex", 6}, {"Commodity", 7}, {"Cryptocurrency", 8}, {"MultiAsset", 9}
    });
    m_timeFrameIndex = makeEnum({
        {"Invalid", -1},
        {"Min1", 0}, {"OneMinute", 0}, {"Min5", 1}, {"FiveMinutes", 1},
        {"Min15", 2}, {"FifteenMinutes", 2}, {"Min30", 3}, {"Min60", 4},
        {"OneHour", 4}, {"Daily", 5}, {"Weekly", 6}, {"Monthly", 7}
    });
    m_riskLevelIndex = makeEnum({
        {"Invalid", -1},
        {"Conservative", 0}, {"Low", 0}, {"Moderate", 1}, {"Medium", 1},
        {"Aggressive", 2}, {"High", 2}, {"Extreme", 3}, {"CustomRisk", 4}
    });
}

QVariantMap StrategyParamConfigHelper::makeEnum(std::initializer_list<std::pair<const char*, int>> vals)
{
    QVariantMap m;
    for (auto& [k, v] : vals) m[QString::fromUtf8(k)] = v;
    return m;
}

// ── i18n ──
QString StrategyParamConfigHelper::tr(const QString& key) const { return key; }

// ── normalizeStrategyTypeIndex ──
int StrategyParamConfigHelper::normalizeStrategyTypeIndex(int v) const { return (v < 0 || v > 10) ? -1 : v; }

int StrategyParamConfigHelper::strategyBehaviorKindFromTypeIndex(int idx) const
{
    static const int map[] = {0,0,1,3,4,5,2,4,5,7,8,9};
    int n = normalizeStrategyTypeIndex(idx);
    return (n >= 0 && n <= 10) ? map[n] : -1;
}

int StrategyParamConfigHelper::strategyTypeIndexFromBehaviorKind(int bk) const
{
    static const int map[] = {-1,0,2,3,6,7,5,9,10,8};
    return (bk >= 0 && bk <= 8) ? map[bk] : -1;
}

// ── 字符串常量表 ──
static const char* kNames[] = {
    QT_TR_NOOP("双均线策略"), QT_TR_NOOP("海龟突破"), QT_TR_NOOP("布林带均值回归"),
    QT_TR_NOOP("RSI均值回归"), QT_TR_NOOP("多因子选股"), QT_TR_NOOP("业绩超预期"),
    QT_TR_NOOP("统计配对交易"), QT_TR_NOOP("风险平价配置"), QT_TR_NOOP("机器学习选股"),
    QT_TR_NOOP("订单流不平衡"), QT_TR_NOOP("波动率套利")
};
static const char* kDescs[] = {
    QT_TR_NOOP("基于双均线交叉信号进行交易"), QT_TR_NOOP("基于通道突破信号进行交易"),
    QT_TR_NOOP("利用布林带识别超买超卖进行均值回归"), QT_TR_NOOP("基于RSI指标识别超买超卖进行均值回归"),
    QT_TR_NOOP("综合多个因子评分,选择最优标的"), QT_TR_NOOP("基于业绩超预期事件进行交易"),
    QT_TR_NOOP("基于统计套利模型进行配对交易"), QT_TR_NOOP("基于风险平价理念配置资产组合"),
    QT_TR_NOOP("使用机器学习模型预测选股"), QT_TR_NOOP("基于订单流不平衡信号进行高频交易"),
    QT_TR_NOOP("基于期权波动率价差进行套利交易")
};
static const char* kIcons[] = {"📈","🐢","📊","📉","🧠","📰","🔗","⚖️","🤖","⚡","📐"};
static const char* kBriefs[] = {
    QT_TR_NOOP("趋势跟踪,均线交叉"), QT_TR_NOOP("突破通道,顺势而为"),
    QT_TR_NOOP("布林带超买超卖回归"), QT_TR_NOOP("RSI均值回归策略"),
    QT_TR_NOOP("多因子综合评分选股"), QT_TR_NOOP("业绩超预期事件驱动"),
    QT_TR_NOOP("统计配对套利"), QT_TR_NOOP("风险平价资产配置"),
    QT_TR_NOOP("机器学习智能选股"), QT_TR_NOOP("订单流高频交易"),
    QT_TR_NOOP("期权波动率套利")
};
static const char* kBehaviorLabels[] = {
    QT_TR_NOOP("趋势跟踪"), QT_TR_NOOP("均值回归"), QT_TR_NOOP("动量"), QT_TR_NOOP("套利"),
    QT_TR_NOOP("多因子"), QT_TR_NOOP("机器学习"), QT_TR_NOOP("事件驱动"), QT_TR_NOOP("高频交易"), QT_TR_NOOP("自定义")
};
static const char* kAssetNames[] = {
    QT_TR_NOOP("股票"), QT_TR_NOOP("ETF"), QT_TR_NOOP("指数"),
    QT_TR_NOOP("期货"), QT_TR_NOOP("期权"), QT_TR_NOOP("债券"), QT_TR_NOOP("外汇"),
    QT_TR_NOOP("大宗商品"), QT_TR_NOOP("加密货币"), QT_TR_NOOP("多资产")
};
static const char* kTimeFrameNames[] = {
    QT_TR_NOOP("1分钟"), QT_TR_NOOP("5分钟"), QT_TR_NOOP("15分钟"), QT_TR_NOOP("30分钟"),
    QT_TR_NOOP("60分钟"), QT_TR_NOOP("日线"), QT_TR_NOOP("周线"), QT_TR_NOOP("月线")
};
static const char* kRiskLevelNames[] = {
    QT_TR_NOOP("保守"), QT_TR_NOOP("稳健"), QT_TR_NOOP("激进"), QT_TR_NOOP("极端"), QT_TR_NOOP("自定义")
};
static const char* kRiskColors[] = {"#4ade80","#f59e0b","#ef4444","#dc2626","#a78bfa"};

// ── 内部 ──
static QString s(int idx, const char** tbl, int sz, const QString& fallback = {})
{
    if (idx >= 0 && idx < sz) {
        QString r = QString::fromUtf8(tbl[idx]);
        // QT_TR_NOOP 标记的字符串在此处直接用
        return r;
    }
    return fallback;
}

// ── 类型映射 ──
QString StrategyParamConfigHelper::getStrategyTypeNameFromIndex(int idx) const {
    return s(normalizeStrategyTypeIndex(idx), kNames, 11); }
QString StrategyParamConfigHelper::getStrategyTypeDescriptionFromIndex(int idx) const {
    return s(normalizeStrategyTypeIndex(idx), kDescs, 11); }
QString StrategyParamConfigHelper::getStrategyIconFromIndex(int idx) const {
    return s(normalizeStrategyTypeIndex(idx), kIcons, 11); }
QString StrategyParamConfigHelper::getBriefDescriptionFromIndex(int idx) const {
    return s(normalizeStrategyTypeIndex(idx), kBriefs, 11); }
QString StrategyParamConfigHelper::getDefaultStrategyDescriptionFromIndex(int idx) const {
    return s(normalizeStrategyTypeIndex(idx), kDescs, 11); }
QVariantList StrategyParamConfigHelper::getDefaultStrategyTagsFromIndex(int idx) const {
    QVariantList tags; tags.append(getStrategyTypeNameFromIndex(idx)); return tags; }
QString StrategyParamConfigHelper::strategyBehaviorKindLabel(int bk) const {
    return s(bk, kBehaviorLabels, 9); }
QString StrategyParamConfigHelper::getStrategyBehaviorKindLabelFromIndex(int idx) const {
    return strategyBehaviorKindLabel(strategyBehaviorKindFromTypeIndex(idx)); }

// ── 资产/时间/风险 ──
QString StrategyParamConfigHelper::getAssetTypeNameFromIndex(int idx) const { return s(idx, kAssetNames, 10); }
QString StrategyParamConfigHelper::getTimeFrameNameFromIndex(int idx) const { return s(idx, kTimeFrameNames, 8); }
QString StrategyParamConfigHelper::getRiskLevelNameFromIndex(int idx) const { return s(idx, kRiskLevelNames, 5); }
QString StrategyParamConfigHelper::getRiskLevelColorFromIndex(int idx) const { return s(idx, kRiskColors, 5); }

// ── buildParamConfigs ──
QVariantList StrategyParamConfigHelper::buildParamConfigs(int idx) const
{
    QVariantList c;
    auto sel = [&c](const QString& id, const QString& label, const QString& desc,
        QVariantList opts, QVariant def, const QString& cat) {
        QVariantMap m; m["type"]="select"; m["id"]=id; m["label"]=label;
        m["description"]=desc; m["options"]=opts; m["default"]=def; m["category"]=cat;
        c.append(m);
    };
    auto sld = [&c](const QString& id, const QString& label, const QString& desc,
        double def, double min, double max, double step, int decimals,
        const QString& unit, const QString& cat) {
        QVariantMap m; m["type"]="slider"; m["id"]=id; m["label"]=label;
        m["description"]=desc; m["default"]=def; m["min"]=min; m["max"]=max;
        m["step"]=step; m["decimals"]=decimals; m["unit"]=unit; m["category"]=cat;
        c.append(m);
    };
    auto cat = QStringLiteral("通用参数");
    sel("allowShort", QStringLiteral("允许做空"), QStringLiteral("是否允许空头仓位"), QVariantList{false,true}, false, cat);
    sld("maxPositions", QStringLiteral("最大持仓数"), QStringLiteral("组合允许的最大持仓标的数"), 100, 1, 500, 1, 0, QStringLiteral("只"), cat);
    sld("maxWeightPerStock", QStringLiteral("单票最大权重"), QStringLiteral("每个标的最大仓位权重"), 0.1, 0.01, 1.0, 0.01, 2, "", cat);
    sld("minWeightPerStock", QStringLiteral("单票最小权重"), QStringLiteral("每个标的最小仓位权重"), 0, 0, 0.5, 0.01, 2, "", cat);
    int n = normalizeStrategyTypeIndex(idx);
    auto scat = QStringLiteral("策略参数");
    switch(n){
    case 0: sld("fastPeriod",QStringLiteral("快线周期"),QStringLiteral("短期均线周期"),5,1,50,1,0,QStringLiteral("日"),scat); sld("slowPeriod",QStringLiteral("慢线周期"),QStringLiteral("长期均线周期"),20,5,200,1,0,QStringLiteral("日"),scat); break;
    case 1: sld("channelPeriod",QStringLiteral("通道周期"),QStringLiteral("通道计算周期"),20,5,100,1,0,QStringLiteral("日"),scat); sld("breakoutMultiplier",QStringLiteral("突破倍数"),QStringLiteral("ATR突破倍数"),2.0,0.5,5.0,0.1,1,"x",scat); sld("atrPeriod",QStringLiteral("ATR周期"),QStringLiteral("真实波幅计算周期"),14,5,50,1,0,QStringLiteral("日"),scat); break;
    case 2: sld("period",QStringLiteral("布林周期"),QStringLiteral("布林带计算周期"),20,5,100,1,0,QStringLiteral("日"),scat); sld("standardDeviationMultiplier",QStringLiteral("标准差倍数"),QStringLiteral("上下轨标准差倍数"),2.0,1.0,4.0,0.1,1,"σ",scat); sld("entryThreshold",QStringLiteral("入场阈值"),QStringLiteral("价格偏离均值的入场比例"),0.8,0.1,1.0,0.05,2,"",scat); sld("exitThreshold",QStringLiteral("出场阈值"),QStringLiteral("回归均线的出场比例"),0.2,0.0,0.5,0.05,2,"",scat); break;
    case 3: sld("period",QStringLiteral("RSI周期"),QStringLiteral("RSI计算周期"),14,5,50,1,0,QStringLiteral("日"),scat); sld("oversoldLevel",QStringLiteral("超卖线"),QStringLiteral("低于此值视为超卖"),30,10,40,1,0,"",scat); sld("overboughtLevel",QStringLiteral("超买线"),QStringLiteral("高于此值视为超买"),70,60,90,1,0,"",scat); break;
    case 4: sld("topN",QStringLiteral("Top N"),QStringLiteral("因子评分排名前N只标的"),20,5,100,1,0,QStringLiteral("只"),scat); sel("industryNeutral",QStringLiteral("行业中性"),QStringLiteral("是否启用行业中性化处理"),QVariantList{false,true},false,scat); break;
    case 5: sld("surpriseThreshold",QStringLiteral("超预期阈值"),QStringLiteral("盈利超预期的最小比例"),0.05,0.01,0.3,0.01,2,"",scat); sld("holdDays",QStringLiteral("持仓天数"),QStringLiteral("事件驱动策略的持仓天数"),5,1,30,1,0,QStringLiteral("天"),scat); break;
    case 6: sld("lookback",QStringLiteral("回看天数"),QStringLiteral("配对交易回看天数"),60,20,250,1,0,QStringLiteral("天"),scat); sld("entryZScore",QStringLiteral("入场Z分数"),QStringLiteral("价差偏离标准差的入场阈值"),2.0,1.0,4.0,0.1,1,"σ",scat); sld("exitZScore",QStringLiteral("出场Z分数"),QStringLiteral("价差回归标准差的出场阈值"),0.5,0.0,1.0,0.1,1,"σ",scat); sld("hedgeRatio",QStringLiteral("对冲比率"),QStringLiteral("配对交易的对冲比率"),1.0,0.1,3.0,0.1,1,"",scat); break;
    case 7: sld("volatilityLookback",QStringLiteral("波动率窗口"),QStringLiteral("波动率计算窗口"),60,20,250,1,0,QStringLiteral("天"),scat); sld("targetVolatility",QStringLiteral("目标波动率"),QStringLiteral("组合目标年化波动率"),0.15,0.05,0.4,0.01,2,"",scat); break;
    case 8: sld("topN",QStringLiteral("Top N"),QStringLiteral("模型预测排名前N只标的"),20,5,100,1,0,QStringLiteral("只"),scat); break;
    case 9: sld("depthLevels",QStringLiteral("盘口深度"),QStringLiteral("订单薄深度层数"),5,1,10,1,0,QStringLiteral("档"),scat); sld("imbalanceThreshold",QStringLiteral("不平衡阈值"),QStringLiteral("买卖力量不平衡阈值"),0.3,0.1,0.9,0.05,2,"",scat); sld("maxHoldSeconds",QStringLiteral("最大持仓秒数"),QStringLiteral("高频策略最大持仓时间"),60,10,600,5,0,QStringLiteral("秒"),scat); break;
    case 10: sld("historicalVolatilityWindow",QStringLiteral("历史波动率窗口"),QStringLiteral("历史波动率计算窗口"),30,10,120,1,0,QStringLiteral("天"),scat); sld("entrySpreadUpper",QStringLiteral("入场价差上限"),QStringLiteral("价差入场上限"),0.3,0.05,0.5,0.01,2,"",scat); sld("entrySpreadLower",QStringLiteral("入场价差下限"),QStringLiteral("价差入场下限"),0.1,0.01,0.3,0.01,2,"",scat); sel("deltaNeutral",QStringLiteral("Delta中性"),QStringLiteral("是否维持Delta中性"),QVariantList{false,true},false,scat); break;
    default: break;
    }
    return c;
}

// ── normalizeStrategyParameters ──
static void pick(const QVariantMap& src, QVariantMap& dst, const QString& key)
{
    if (src.contains(key)) {
        QVariant v = src[key];
        if (!v.isNull() && !(v.type()==QVariant::String && v.toString().isEmpty()))
            dst[key] = v;
    }
}
template<typename F>
static void pickT(const QVariantMap& src, QVariantMap& dst, const QString& key, F f)
{
    if (src.contains(key)) {
        QVariant v = src[key];
        if (!v.isNull() && !(v.type()==QVariant::String && v.toString().isEmpty()))
            dst[key] = f(v);
    }
}

QVariantMap StrategyParamConfigHelper::normalizeStrategyParameters(int idx, const QVariantMap& src) const
{
    QVariantMap out;
    int n = normalizeStrategyTypeIndex(idx);
    auto asDbl = [](const QVariant& v){ return v.toDouble(); };
    auto asBool= [](const QVariant& v){ return v.toBool(); };
    auto asInt = [](const QVariant& v){ return v.toInt(); };

    pickT(src, out, "allowShort", asBool);
    pickT(src, out, "maxPositions", asInt);
    pickT(src, out, "maxWeightPerStock", asDbl);
    pickT(src, out, "minWeightPerStock", asDbl);
    pickT(src, out, "weightScheme", asInt);
    pickT(src, out, "rebalanceFrequency", asInt);

    switch(n){
    case 0: pickT(src,out,"fastPeriod",asInt); pickT(src,out,"slowPeriod",asInt); pick(src,out,"priceField"); break;
    case 1: pickT(src,out,"channelPeriod",asInt); pickT(src,out,"breakoutMultiplier",asDbl); pickT(src,out,"atrPeriod",asInt); break;
    case 2: pickT(src,out,"period",asInt); pickT(src,out,"standardDeviationMultiplier",asDbl); pickT(src,out,"entryThreshold",asDbl); pickT(src,out,"exitThreshold",asDbl); break;
    case 3: pickT(src,out,"period",asInt); pickT(src,out,"oversoldLevel",asInt); pickT(src,out,"overboughtLevel",asInt); break;
    case 4: pick(src,out,"factorWeights"); pickT(src,out,"topN",asInt); pickT(src,out,"industryNeutral",asBool); break;
    case 5: pickT(src,out,"surpriseThreshold",asDbl); pickT(src,out,"holdDays",asInt); pick(src,out,"eventSources"); break;
    case 6: pick(src,out,"tradingPair"); pickT(src,out,"hedgeRatio",asDbl); pickT(src,out,"lookback",asInt); pickT(src,out,"entryZScore",asDbl); pickT(src,out,"exitZScore",asDbl); break;
    case 7: pick(src,out,"assets"); pickT(src,out,"volatilityLookback",asInt); pickT(src,out,"targetVolatility",asDbl); break;
    case 8: pickT(src,out,"modelId",asInt); pick(src,out,"featureIds"); pickT(src,out,"topN",asInt); break;
    case 9: pickT(src,out,"depthLevels",asInt); pickT(src,out,"imbalanceThreshold",asDbl); pickT(src,out,"maxHoldSeconds",asInt); break;
    case 10: pick(src,out,"underlying"); pick(src,out,"optionChainFilter"); pickT(src,out,"historicalVolatilityWindow",asInt); pickT(src,out,"entrySpreadUpper",asDbl); pickT(src,out,"entrySpreadLower",asDbl); pickT(src,out,"deltaNeutral",asBool); break;
    default: break;
    }
    pick(src,out,"rule_profile"); pick(src,out,"rule_composer_state");
    pick(src,out,"execution_policy"); pick(src,out,"backtest_assumptions");
    pick(src,out,"strategy_scope_context"); pick(src,out,"factor_overlay");
    return out;
}

// ── buildCompleteStrategyData ──
QVariantMap StrategyParamConfigHelper::buildCompleteStrategyData(const QVariantMap& ctx) const
{
    int ti = normalizeStrategyTypeIndex(ctx.value("selectedStrategyTypeIndex").toInt());
    int bk = strategyBehaviorKindFromTypeIndex(ti);
    auto norm = normalizeStrategyParameters(ti, ctx.value("strategyParameters").toMap());
    QVariantMap d;
    d["name"]=ctx.value("strategyName"); d["displayName"]=ctx.value("strategyName");
    d["strategyBehaviorKind"]=bk; d["strategyTypeIndex"]=ti;
    d["typeName"]=getStrategyTypeNameFromIndex(ti); d["description"]=ctx.value("strategyDescription");
    d["assetTypeIndex"]=ctx.value("assetTypeIndex"); d["timeFrameIndex"]=ctx.value("timeFrameIndex");
    d["riskLevelIndex"]=ctx.value("riskLevelIndex"); d["optimizationMethod"]=ctx.value("optimizationMethod");
    d["enableAdvancedOptions"]=ctx.value("enableAdvancedOptions");
    d["statusIndex"]=false; d["createdDate"]=QDate::currentDate().toString("yyyy-MM-dd");
    d["returns"]="+0.0%"; d["maxDrawdown"]="-0.0%"; d["sharpeRatio"]="0.0"; d["winRate"]="0.0%";
    d["tags"]=ctx.value("strategyTags"); d["parameters"]=norm; d["parameterCount"]=norm.size();
    return d;
}

// ── buildDefaultStrategyProfile ──
QVariantMap StrategyParamConfigHelper::buildDefaultStrategyProfile(int idx) const
{
    int ti = normalizeStrategyTypeIndex(idx);
    QVariantMap p;
    p["strategyTypeIndex"]=ti;
    p["strategyBehaviorKind"]=strategyBehaviorKindFromTypeIndex(ti);
    p["name"]=getStrategyTypeNameFromIndex(ti);
    p["description"]=getDefaultStrategyDescriptionFromIndex(ti);
    p["tags"]=QStringList{getStrategyTypeNameFromIndex(ti)};
    return p;
}

// ── 步骤 ──
QString StrategyParamConfigHelper::stepLabel(int s) const
{ static const char* v[]={"①","②","③"}; return (s>=1&&s<=3)?QString::fromUtf8(v[s-1]):QString(); }
QString StrategyParamConfigHelper::stepTitle(int s) const
{ static const char* v[]={"策略类型与信息","参数与规则模板","确认与创建"}; return (s>=1&&s<=3)?QString::fromUtf8(v[s-1]):QString(); }
QString StrategyParamConfigHelper::stepDescription(int s) const
{ static const char* v[]={"选择策略类型,填写基本信息","配置策略参数,应用规则模板","确认配置,保存并创建策略"}; return (s>=1&&s<=3)?QString::fromUtf8(v[s-1]):QString(); }
bool StrategyParamConfigHelper::isStepValid(int s, const QVariantMap& d) const
{
    if(s==1){int ti=d.value("selectedStrategyTypeIndex").toInt(); return normalizeStrategyTypeIndex(ti)>=0&&!d.value("strategyName").toString().trimmed().isEmpty();}
    if(s==2) return d.value("parametersValid").toBool();
    return true;
}

// ── 规则模板 ──
QVariantMap StrategyParamConfigHelper::getRuleComposerStageDefinitions() const
{
    QVariantMap defs;
    QVariantList stages;
    struct { const char* id; const char* label; const char* description; } s[] = {
        {"market","市场环境","市场趋势/波动率/流动性/情绪等前置判断"},
        {"eligibility","标的选择","股票池/ETF/期货等可交易标的筛选"},
        {"signal","信号生成","买入/卖出/平仓信号的生成条件"},
        {"portfolio","组合管理","仓位调整/再平衡/权重优化"},
        {"rebalance","再平衡","定期或条件触发的组合再平衡"},
        {"execution","执行","订单拆分/滑点控制/执行算法"},
        {"account_risk","账户风控","止损/止盈/最大回撤/集中度"}
    };
    for (auto& si : s) {
        QVariantMap stage;
        stage["id"]=si.id; stage["label"]=QString::fromUtf8(si.label);
        stage["description"]=QString::fromUtf8(si.description);
        stages.append(stage);
    }
    defs["stages"]=stages;
    return defs;
}

QString StrategyParamConfigHelper::resolveRuleTemplateFileName(const QString& name) const
{
    static QHash<QString,QString> map = {
        {"双均线","double_ma"},{"海龟","turtle"},{"布林带","bollinger"},{"RSI","rsi"},
        {"多因子","multi_factor"},{"业绩超预期","earnings"},{"配对交易","pairs"},
        {"风险平价","risk_parity"},{"机器学习","ml"},{"订单流","order_flow"},{"波动率套利","vol_spread"}
    };
    for (auto it=map.begin(); it!=map.end(); ++it)
        if (name.contains(it.key())) return it.value();
    return QStringLiteral("custom");
}

// ── 缺失方法兼容 ──
QString StrategyParamConfigHelper::getRiskLevelName(int idx) const { return getRiskLevelNameFromIndex(idx); }
QString StrategyParamConfigHelper::getDefaultStrategyDescription(int idx) const { return getDefaultStrategyDescriptionFromIndex(idx); }
QVariantList StrategyParamConfigHelper::getDefaultStrategyTags(int idx) const { return getDefaultStrategyTagsFromIndex(idx); }

QVariantMap StrategyParamConfigHelper::resetFormData() const {
    QVariantMap d;
    d["selectedStrategyTypeIndex"]=0;
    d["strategyName"]="";
    d["strategyDescription"]="";
    d["strategyTags"]=QVariantList();
    d["optimizationMethod"]="genetic";
    d["enableAdvancedOptions"]=false;
    d["strategyParameters"]=QVariantMap();
    d["parametersValid"]=false;
    return d;
}

QVariantMap StrategyParamConfigHelper::validateRuleComposerConfiguration(const QVariantMap&) const {
    QVariantMap r;
    r["valid"]=true; r["errors"]=QVariantList(); r["warnings"]=QVariantList();
    return r;
}

QVariantMap StrategyParamConfigHelper::buildDefaultBaseRuleBindings(int) const {
    return QVariantMap();
}

QVariantMap StrategyParamConfigHelper::buildDefaultMarketRuleBindings(int) const {
    return QVariantMap();
}

QVariantMap StrategyParamConfigHelper::buildDefaultRuleComposerSkeleton(int) const {
    return QVariantMap();
}

#include "moc_StrategyParamConfigHelper.cpp"
