// FactorMetaService.cpp
// 因子元数据服务实现 - 专门处理因子参数元数据的加载和查询

#include "FactorMetaService.h"
#include "foundation.h"

#include "../../../domain/factor/include/BaseFactor.h"
#include "../../../domain/factor/include/FactorMetricConfig.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDir>
#include <QCoreApplication>
#include <QStandardPaths>
#include <memory>
#include <stdexcept>

// 静态成员初始化
const QMap<factor::FactorType, QString> FactorMetaService::FACTOR_TYPE_TO_ID = {
    {factor::FactorType::VALUE, "value"},
    {factor::FactorType::MOMENTUM, "momentum"},
    {factor::FactorType::SIZE, "size"},
    {factor::FactorType::QUALITY, "quality"},
    {factor::FactorType::LOW_VOLATILITY, "low_volatility"},
    {factor::FactorType::GROWTH, "growth"},
    {factor::FactorType::DIVIDEND, "dividend"},
    {factor::FactorType::TECHNICAL, "technical"},
    {factor::FactorType::LIQUIDITY, "liquidity"},
    {factor::FactorType::MACRO, "macro"},
    {factor::FactorType::INDUSTRY, "industry"},
    {factor::FactorType::SENTIMENT, "sentiment"},
    {factor::FactorType::CUSTOM, "custom"},
    {factor::FactorType::REVERSAL, "reversal"},
    {factor::FactorType::HIGH_FREQ, "high_freq"},
    {factor::FactorType::DL, "dl"}
};

const QMap<QString, factor::FactorType> FactorMetaService::ID_TO_FACTOR_TYPE = {
    {"value", factor::FactorType::VALUE},
    {"momentum", factor::FactorType::MOMENTUM},
    {"size", factor::FactorType::SIZE},
    {"quality", factor::FactorType::QUALITY},
    {"low_volatility", factor::FactorType::LOW_VOLATILITY},
    {"growth", factor::FactorType::GROWTH},
    {"dividend", factor::FactorType::DIVIDEND},
    {"technical", factor::FactorType::TECHNICAL},
    {"liquidity", factor::FactorType::LIQUIDITY},
    {"macro", factor::FactorType::MACRO},
    {"industry", factor::FactorType::INDUSTRY},
    {"sentiment", factor::FactorType::SENTIMENT},
    {"custom", factor::FactorType::CUSTOM},
    {"reversal", factor::FactorType::REVERSAL},
    {"high_freq", factor::FactorType::HIGH_FREQ},
    {"dl", factor::FactorType::DL}
};

const QMap<factor::FactorType, QString> FactorMetaService::FACTOR_TYPE_TO_DISPLAY_NAME = {
    {factor::FactorType::VALUE, "价值因子"},
    {factor::FactorType::MOMENTUM, "动量因子"},
    {factor::FactorType::SIZE, "规模因子"},
    {factor::FactorType::QUALITY, "质量因子"},
    {factor::FactorType::LOW_VOLATILITY, "低波因子"},
    {factor::FactorType::GROWTH, "成长因子"},
    {factor::FactorType::DIVIDEND, "红利因子"},
    {factor::FactorType::TECHNICAL, "技术因子"},
    {factor::FactorType::LIQUIDITY, "流动性因子"},
    {factor::FactorType::MACRO, "宏观因子"},
    {factor::FactorType::INDUSTRY, "行业因子"},
    {factor::FactorType::SENTIMENT, "情绪因子"},
    {factor::FactorType::CUSTOM, "自定义因子"},
    {factor::FactorType::REVERSAL, "反转因子"},
    {factor::FactorType::HIGH_FREQ, "高频因子"},
    {factor::FactorType::DL, "AI因子"}
};

namespace {

std::string toStdString(const QString& value)
{
    return value.toStdString();
}

QVariantMap buildFactorUiMeta(const QString& id,
                              int factorType,
                              const QString& displayName,
                              const QString& cardDescription,
                              const QString& description,
                              const QString& placeholderName,
                              const QString& placeholderDesc,
                              const QString& color,
                              const QString& icon,
                              const QString& subCategory)
{
    return QVariantMap{
        {QStringLiteral("id"), id},
        {QStringLiteral("factorType"), factorType},
        {QStringLiteral("displayName"), displayName},
        {QStringLiteral("cardDescription"), cardDescription},
        {QStringLiteral("description"), description},
        {QStringLiteral("placeholderName"), placeholderName},
        {QStringLiteral("placeholderDesc"), placeholderDesc},
        {QStringLiteral("color"), color},
        {QStringLiteral("icon"), icon},
        {QStringLiteral("subCategory"), subCategory}
    };
}

enum class ParamWidgetType {
    Slider,
    Select,
    MultiSelect,
    Toggle,
    Input
};

struct ParamConfigSpec {
    QString id;
    QString label;
    QString description;
    ParamWidgetType widgetType{ParamWidgetType::Input};
    QVariant defaultValue;
    bool required{false};
    QVariantMap extras;
};

QString paramWidgetTypeKey(ParamWidgetType widgetType)
{
    switch (widgetType) {
    case ParamWidgetType::Slider:
        return QStringLiteral("slider");
    case ParamWidgetType::Select:
        return QStringLiteral("select");
    case ParamWidgetType::MultiSelect:
        return QStringLiteral("multiselect");
    case ParamWidgetType::Toggle:
        return QStringLiteral("toggle");
    case ParamWidgetType::Input:
        return QStringLiteral("input");
    }

    return QStringLiteral("input");
}

ParamConfigSpec applyExtraFields(ParamConfigSpec config, const QVariantMap& extras = QVariantMap())
{
    for (auto it = extras.constBegin(); it != extras.constEnd(); ++it) {
        config.extras.insert(it.key(), it.value());
    }
    return config;
}

QVariantMap toVariantMap(const ParamConfigSpec& config)
{
    QVariantMap map{
        {QStringLiteral("id"), config.id},
        {QStringLiteral("label"), config.label},
        {QStringLiteral("description"), config.description},
        {QStringLiteral("type"), paramWidgetTypeKey(config.widgetType)},
        {QStringLiteral("default"), config.defaultValue},
        {QStringLiteral("required"), config.required}
    };

    for (auto it = config.extras.constBegin(); it != config.extras.constEnd(); ++it) {
        map.insert(it.key(), it.value());
    }

    return map;
}

QVariantList toVariantList(const QList<ParamConfigSpec>& configs)
{
    QVariantList list;
    for (const ParamConfigSpec& config : configs) {
        list.append(toVariantMap(config));
    }
    return list;
}

QVariantList buildIntList(std::initializer_list<int> values)
{
    QVariantList list;
    for (int value : values) {
        list.append(value);
    }
    return list;
}

template <typename EnumType>
constexpr int enumValue(EnumType value)
{
    return static_cast<int>(value);
}

template <typename EnumType>
QVariantList buildEnumList(std::initializer_list<EnumType> values)
{
    QVariantList list;
    for (EnumType value : values) {
        list.append(enumValue(value));
    }
    return list;
}

QVariantList buildOptionList(std::initializer_list<QVariantMap> options)
{
    QVariantList list;
    for (const QVariantMap& option : options) {
        list.append(option);
    }
    return list;
}

QVariantMap buildOption(int value, const QString& label)
{
    return QVariantMap{
        {QStringLiteral("value"), value},
        {QStringLiteral("label"), label}
    };
}

ParamConfigSpec buildBaseConfig(const QString& id,
                                const QString& label,
                                const QString& description,
                                ParamWidgetType widgetType,
                                const QVariant& defaultValue,
                                bool required = false)
{
    return ParamConfigSpec{id, label, description, widgetType, defaultValue, required, QVariantMap()};
}

ParamConfigSpec buildSliderConfig(const QString& id,
                                  const QString& label,
                                  const QString& description,
                                  const QVariant& defaultValue,
                                  const QVariant& minValue,
                                  const QVariant& maxValue,
                                  const QVariant& step,
                                  const QString& unit,
                                  int decimals,
                                  const QVariantList& presets = QVariantList(),
                                  bool required = false,
                                  const QVariantMap& extras = QVariantMap())
{
    ParamConfigSpec config = buildBaseConfig(id, label, description, ParamWidgetType::Slider, defaultValue, required);
    config.extras.insert(QStringLiteral("min"), minValue);
    config.extras.insert(QStringLiteral("max"), maxValue);
    config.extras.insert(QStringLiteral("step"), step);
    config.extras.insert(QStringLiteral("unit"), unit);
    config.extras.insert(QStringLiteral("decimals"), decimals);
    config.extras.insert(QStringLiteral("showPresets"), !presets.isEmpty());
    if (!presets.isEmpty()) {
        config.extras.insert(QStringLiteral("presets"), presets);
    }
    return applyExtraFields(config, extras);
}

ParamConfigSpec buildSelectConfig(const QString& id,
                                  const QString& label,
                                  const QString& description,
                                  const QVariant& defaultValue,
                                  const QVariantList& options,
                                  bool required = false,
                                  const QVariantMap& extras = QVariantMap())
{
    ParamConfigSpec config = buildBaseConfig(id, label, description, ParamWidgetType::Select, defaultValue, required);
    config.extras.insert(QStringLiteral("options"), options);
    return applyExtraFields(config, extras);
}

ParamConfigSpec buildMultiSelectConfig(const QString& id,
                                       const QString& label,
                                       const QString& description,
                                       const QVariantList& defaultValue,
                                       const QVariantList& options,
                                       bool required = false,
                                       const QVariantMap& extras = QVariantMap())
{
    ParamConfigSpec config = buildBaseConfig(id, label, description, ParamWidgetType::MultiSelect, defaultValue, required);
    config.extras.insert(QStringLiteral("options"), options);
    return applyExtraFields(config, extras);
}

ParamConfigSpec buildToggleConfig(const QString& id,
                                  const QString& label,
                                  const QString& description,
                                  bool defaultValue,
                                  const QString& trueLabel,
                                  const QString& falseLabel,
                                  bool required = false,
                                  const QVariantMap& extras = QVariantMap())
{
    ParamConfigSpec config = buildBaseConfig(id, label, description, ParamWidgetType::Toggle, defaultValue, required);
    config.extras.insert(QStringLiteral("trueLabel"), trueLabel);
    config.extras.insert(QStringLiteral("falseLabel"), falseLabel);
    return applyExtraFields(config, extras);
}

ParamConfigSpec buildInputConfig(const QString& id,
                                 const QString& label,
                                 const QString& description,
                                 const QVariant& defaultValue,
                                 const QString& placeholder,
                                 bool multiline = false,
                                 int maxLength = 255,
                                 bool required = false,
                                 const QVariantMap& extras = QVariantMap())
{
    ParamConfigSpec config = buildBaseConfig(id, label, description, ParamWidgetType::Input, defaultValue, required);
    config.extras.insert(QStringLiteral("placeholder"), placeholder);
    config.extras.insert(QStringLiteral("multiline"), multiline);
    config.extras.insert(QStringLiteral("maxLength"), maxLength);
    return applyExtraFields(config, extras);
}

QList<ParamConfigSpec> appendConfigs(const QList<ParamConfigSpec>& first, const QList<ParamConfigSpec>& second)
{
    QList<ParamConfigSpec> merged = first;
    for (const ParamConfigSpec& item : second) {
        merged.append(item);
    }
    return merged;
}

QVariantList commonFrequencyOptions()
{
    return buildOptionList({
        buildOption(enumValue(factor::DataFrequency::Daily), QStringLiteral("日频")),
        buildOption(enumValue(factor::DataFrequency::Weekly), QStringLiteral("周频")),
        buildOption(enumValue(factor::DataFrequency::Monthly), QStringLiteral("月频")),
        buildOption(enumValue(factor::DataFrequency::Quarterly), QStringLiteral("季频")),
        buildOption(enumValue(factor::DataFrequency::Yearly), QStringLiteral("年频"))
    });
}

QVariantList standardizationOptions()
{
    return buildOptionList({
        buildOption(enumValue(factor::StandardizationMethod::Rank), QStringLiteral("排序")),
        buildOption(enumValue(factor::StandardizationMethod::ZScore), QStringLiteral("标准分标准化（Z-Score）")),
        buildOption(enumValue(factor::StandardizationMethod::MinMax), QStringLiteral("区间缩放标准化（Min-Max）")),
        buildOption(enumValue(factor::StandardizationMethod::Percentile), QStringLiteral("分位数")),
        buildOption(enumValue(factor::StandardizationMethod::None), QStringLiteral("不处理"))
    });
}

QList<ParamConfigSpec> buildCommonConfigs()
{
    return QList<ParamConfigSpec>{
        buildSelectConfig(QStringLiteral("frequency"),
                          QStringLiteral("数据频率"),
                          QStringLiteral("因子计算的数据频率"),
                          enumValue(factor::DataFrequency::Daily),
                          commonFrequencyOptions()),
        buildSliderConfig(QStringLiteral("lookbackWindow"),
                          QStringLiteral("回溯窗口"),
                          QStringLiteral("计算因子值所需的通用历史数据长度（与各因子专属观察窗口独立）"),
                          252,
                          1,
                          1000,
                          1,
                          QStringLiteral("天"),
                          0,
                          buildIntList({20, 60, 120, 252})),
        buildToggleConfig(QStringLiteral("laggedEnabled"),
                          QStringLiteral("滞后处理开关"),
                          QStringLiteral("是否启用滞后处理（防止未来函数）"),
                          true,
                          QStringLiteral("启用"),
                          QStringLiteral("禁用")),
        buildSelectConfig(QStringLiteral("standardization"),
                          QStringLiteral("标准化方法"),
                          QStringLiteral("因子值的标准化处理方法"),
                          enumValue(factor::StandardizationMethod::ZScore),
                          standardizationOptions()),
        buildToggleConfig(QStringLiteral("neutralizationEnabled"),
                          QStringLiteral("中性化开关"),
                          QStringLiteral("是否消除行业/市值影响"),
                          true,
                          QStringLiteral("启用"),
                          QStringLiteral("禁用"))
    };
}

QList<ParamConfigSpec> momentumConfigs()
{
    return QList<ParamConfigSpec>{
        buildSliderConfig(QStringLiteral("window"),
                          QStringLiteral("动量窗口"),
                          QStringLiteral("计算动量的时间窗口（天数）"),
                          60,
                          5,
                          250,
                          1,
                          QStringLiteral("天"),
                          0,
                          buildIntList({20, 60, 120, 250})),
        buildSelectConfig(QStringLiteral("type"),
                          QStringLiteral("计算方法"),
                          QStringLiteral("动量计算方法"),
                          enumValue(factor::MomentumCalculationType::SIMPLE),
                          buildOptionList({
                              buildOption(enumValue(factor::MomentumCalculationType::SIMPLE), QStringLiteral("简单动量")),
                              buildOption(enumValue(factor::MomentumCalculationType::EXPONENTIAL), QStringLiteral("指数加权动量")),
                              buildOption(enumValue(factor::MomentumCalculationType::RANK), QStringLiteral("排序动量"))
                          }))
    };
}

QList<ParamConfigSpec> valueConfigs()
{
    const QVariantMap weightExtras = QVariantMap{
        {QStringLiteral("linkedWeightGroup"), QStringLiteral("value")},
        {QStringLiteral("linkedWeightTotal"), 100},
        {QStringLiteral("linkedWeightDecimals"), 0}
    };

    return QList<ParamConfigSpec>{
        buildMultiSelectConfig(QStringLiteral("valuationMetrics"),
                               QStringLiteral("价值指标"),
                               QStringLiteral("选择价值因子代表指标"),
                               buildEnumList({factor::ValuationMetric::BP, factor::ValuationMetric::EP}),
                               buildOptionList({
                                   buildOption(enumValue(factor::ValuationMetric::BP), QStringLiteral("市净率倒数（BP）")),
                                   buildOption(enumValue(factor::ValuationMetric::EP), QStringLiteral("市盈率倒数（EP）")),
                                   buildOption(enumValue(factor::ValuationMetric::DIVIDEND_YIELD), QStringLiteral("股息率（过去12个月）")),
                                   buildOption(enumValue(factor::ValuationMetric::CFP), QStringLiteral("现金流市值比（CF/P）"))
                               })),
        buildSliderConfig(QStringLiteral("bpWeight"),
                          QStringLiteral("市净率倒数权重（BP）"),
                          QStringLiteral("市净率倒数（BP）在价值组合中的权重"),
                          25,
                          0,
                          100,
                          1,
                          QStringLiteral("%"),
                          0,
                          QVariantList(),
                          false,
                          weightExtras),
        buildSliderConfig(QStringLiteral("epWeight"),
                          QStringLiteral("市盈率倒数权重（EP）"),
                          QStringLiteral("市盈率倒数（EP）在价值组合中的权重"),
                          25,
                          0,
                          100,
                          1,
                          QStringLiteral("%"),
                          0,
                          QVariantList(),
                          false,
                          weightExtras),
        buildSliderConfig(QStringLiteral("dividendYieldWeight"),
                          QStringLiteral("股息率权重"),
                          QStringLiteral("股息率（过去12个月）在价值组合中的权重"),
                          25,
                          0,
                          100,
                          1,
                          QStringLiteral("%"),
                          0,
                          QVariantList(),
                          false,
                          weightExtras),
        buildSliderConfig(QStringLiteral("cfPWeight"),
                          QStringLiteral("现金流市值比权重（CF/P）"),
                          QStringLiteral("现金流市值比（CF/P）在价值组合中的权重"),
                          25,
                          0,
                          100,
                          1,
                          QStringLiteral("%"),
                          0,
                          QVariantList(),
                          false,
                          weightExtras)
    };
}

QList<ParamConfigSpec> qualityConfigs()
{
    return QList<ParamConfigSpec>{
        buildSelectConfig(QStringLiteral("metric"),
                          QStringLiteral("质量指标"),
                          QStringLiteral("使用的质量指标"),
                          enumValue(factor::QualityMetric::ROE),
                          buildOptionList({
                              buildOption(enumValue(factor::QualityMetric::ROE), QStringLiteral("净资产收益率（ROE）")),
                              buildOption(enumValue(factor::QualityMetric::ROA), QStringLiteral("总资产收益率（ROA）")),
                              buildOption(enumValue(factor::QualityMetric::GROSS_MARGIN), QStringLiteral("毛利率")),
                              buildOption(enumValue(factor::QualityMetric::OPERATING_MARGIN), QStringLiteral("营业利润率"))
                          })),
        buildSliderConfig(QStringLiteral("qualityThreshold"),
                          QStringLiteral("质量阈值"),
                          QStringLiteral("保留的质量因子最小值，输入 10 表示 10%"),
                          0,
                          0,
                          100,
                          1,
                          QStringLiteral("%"),
                          0)
    };
}

QList<ParamConfigSpec> growthConfigs()
{
    const QVariantMap weightExtras = QVariantMap{
        {QStringLiteral("linkedWeightGroup"), QStringLiteral("growth")},
        {QStringLiteral("linkedWeightTotal"), 100},
        {QStringLiteral("linkedWeightDecimals"), 0}
    };

    return QList<ParamConfigSpec>{
        buildMultiSelectConfig(QStringLiteral("growthMetrics"),
                               QStringLiteral("成长指标"),
                               QStringLiteral("使用的成长指标"),
                               buildEnumList({
                                   factor::GrowthMetric::REVENUE_GROWTH,
                                   factor::GrowthMetric::NET_PROFIT_GROWTH,
                                   factor::GrowthMetric::DELTA_ROE,
                                   factor::GrowthMetric::SUE
                               }),
                               buildOptionList({
                                   buildOption(enumValue(factor::GrowthMetric::REVENUE_GROWTH), QStringLiteral("营收增速")),
                                   buildOption(enumValue(factor::GrowthMetric::NET_PROFIT_GROWTH), QStringLiteral("单季净利同比增速")),
                                   buildOption(enumValue(factor::GrowthMetric::DELTA_ROE), QStringLiteral("ROE同比变化（DELTAROE）")),
                                   buildOption(enumValue(factor::GrowthMetric::SUE), QStringLiteral("标准化预期外盈利（SUE）"))
                               })),
        buildSliderConfig(QStringLiteral("revenueGrowthWeight"),
                          QStringLiteral("营收增速权重"),
                          QStringLiteral("营收增速在成长组合中的权重，四项合计为 100"),
                          25,
                          0,
                          100,
                          1,
                          QStringLiteral("%"),
                          0,
                          QVariantList(),
                          false,
                          weightExtras),
        buildSliderConfig(QStringLiteral("netProfitGrowthWeight"),
                          QStringLiteral("单季净利同比增速权重"),
                          QStringLiteral("单季净利同比增速在成长组合中的权重，四项合计为 100"),
                          25,
                          0,
                          100,
                          1,
                          QStringLiteral("%"),
                          0,
                          QVariantList(),
                          false,
                          weightExtras),
        buildSliderConfig(QStringLiteral("deltaRoeWeight"),
                          QStringLiteral("ROE同比变化权重（DELTAROE）"),
                          QStringLiteral("ROE同比变化（DELTAROE）在成长组合中的权重，四项合计为 100"),
                          25,
                          0,
                          100,
                          1,
                          QStringLiteral("%"),
                          0,
                          QVariantList(),
                          false,
                          weightExtras),
        buildSliderConfig(QStringLiteral("sueWeight"),
                          QStringLiteral("标准化预期外盈利权重（SUE）"),
                          QStringLiteral("标准化预期外盈利（SUE）在成长组合中的权重，四项合计为 100"),
                          25,
                          0,
                          100,
                          1,
                          QStringLiteral("%"),
                          0,
                          QVariantList(),
                          false,
                          weightExtras)
    };
}

QList<ParamConfigSpec> sizeConfigs()
{
    return QList<ParamConfigSpec>{
        buildSelectConfig(QStringLiteral("sizeMetric"),
                          QStringLiteral("规模指标"),
                          QStringLiteral("使用的规模指标"),
                          enumValue(factor::SizeMetric::CIRCULATING_MARKET_CAP),
                          buildOptionList({
                              buildOption(enumValue(factor::SizeMetric::MARKET_CAP), QStringLiteral("总市值")),
                              buildOption(enumValue(factor::SizeMetric::CIRCULATING_MARKET_CAP), QStringLiteral("流通市值")),
                              buildOption(enumValue(factor::SizeMetric::TOTAL_ASSETS), QStringLiteral("总资产"))
                          }))
    };
}

QList<ParamConfigSpec> lowVolatilityConfigs()
{
    const QVariantMap weightExtras = QVariantMap{
        {QStringLiteral("linkedWeightGroup"), QStringLiteral("low_volatility")},
        {QStringLiteral("linkedWeightTotal"), 100},
        {QStringLiteral("linkedWeightDecimals"), 1}
    };

    return QList<ParamConfigSpec>{
        buildSliderConfig(QStringLiteral("window"),
                          QStringLiteral("波动率窗口"),
                          QStringLiteral("计算波动率的时间窗口（天数）"),
                          60,
                          5,
                          250,
                          1,
                          QStringLiteral("天"),
                          0,
                          buildIntList({20, 60, 120, 250})),
        buildMultiSelectConfig(QStringLiteral("components"),
                               QStringLiteral("低波构成"),
                               QStringLiteral("选择参与排序的低波信号，可多选"),
                               buildEnumList({factor::LowVolComponent::VOLATILITY, factor::LowVolComponent::DRAWDOWN, factor::LowVolComponent::BETA}),
                               buildOptionList({
                                   buildOption(enumValue(factor::LowVolComponent::VOLATILITY), QStringLiteral("波动率倒数")),
                                   buildOption(enumValue(factor::LowVolComponent::DRAWDOWN), QStringLiteral("最大回撤倒数")),
                                   buildOption(enumValue(factor::LowVolComponent::BETA), QStringLiteral("贝塔倒数（Beta）"))
                               }),
                               true),
        buildSliderConfig(QStringLiteral("volatilityWeight"),
                          QStringLiteral("波动率权重"),
                          QStringLiteral("波动率倒数在低波组合中的权重，三项合计为 100"),
                          33.4,
                          0.0,
                          100.0,
                          0.1,
                          QStringLiteral("%"),
                          1,
                          QVariantList(),
                          false,
                          weightExtras),
        buildSliderConfig(QStringLiteral("drawdownWeight"),
                          QStringLiteral("最大回撤权重"),
                          QStringLiteral("最大回撤倒数在低波组合中的权重，三项合计为 100"),
                          33.3,
                          0.0,
                          100.0,
                          0.1,
                          QStringLiteral("%"),
                          1,
                          QVariantList(),
                          false,
                          weightExtras),
        buildSliderConfig(QStringLiteral("betaWeight"),
                          QStringLiteral("贝塔权重（Beta）"),
                          QStringLiteral("贝塔倒数（Beta）在低波组合中的权重，三项合计为 100"),
                          33.3,
                          0.0,
                          100.0,
                          0.1,
                          QStringLiteral("%"),
                          1,
                          QVariantList(),
                          false,
                          weightExtras)
    };
}

QList<ParamConfigSpec> dividendConfigs()
{
    return QList<ParamConfigSpec>{
        buildMultiSelectConfig(QStringLiteral("dividendMetrics"),
                               QStringLiteral("红利核心指标"),
                               QStringLiteral("红利策略核心指标，支持多选"),
                               buildEnumList({factor::DividendMetric::DIVIDEND_YIELD}),
                               buildOptionList({
                                   buildOption(enumValue(factor::DividendMetric::DIVIDEND_YIELD), QStringLiteral("股息率")),
                                   buildOption(enumValue(factor::DividendMetric::DIVIDEND_STABILITY), QStringLiteral("分红稳定性")),
                                   buildOption(enumValue(factor::DividendMetric::PAYOUT_RATIO), QStringLiteral("股利支付率"))
                               }),
                               true),
        buildSliderConfig(QStringLiteral("minDividendYield"),
                          QStringLiteral("最低股息率"),
                          QStringLiteral("最低股息率要求（%）"),
                          2.0,
                          0.0,
                          20.0,
                          0.1,
                          QStringLiteral("%"),
                          2)
    };
}

QList<ParamConfigSpec> sentimentConfigs()
{
    return QList<ParamConfigSpec>{
        buildSelectConfig(QStringLiteral("sentimentSource"),
                          QStringLiteral("情绪数据源"),
                          QStringLiteral("情绪数据来源"),
                          enumValue(factor::SentimentSource::NEWS),
                          buildOptionList({
                              buildOption(enumValue(factor::SentimentSource::NEWS), QStringLiteral("新闻情绪")),
                              buildOption(enumValue(factor::SentimentSource::SOCIAL_MEDIA), QStringLiteral("社交媒体")),
                              buildOption(enumValue(factor::SentimentSource::ANALYST_RATING), QStringLiteral("分析师评级")),
                              buildOption(enumValue(factor::SentimentSource::MARKET), QStringLiteral("市场情绪"))
                          })),
        buildSliderConfig(QStringLiteral("window"),
                          QStringLiteral("情绪窗口"),
                          QStringLiteral("情绪数据计算窗口（天数）"),
                          20,
                          1,
                          60,
                          1,
                          QStringLiteral("天"),
                          0,
                          buildIntList({5, 10, 20, 30}))
    };
}

QList<ParamConfigSpec> technicalConfigs()
{
    const QVariantList indicatorOptions = buildOptionList({
        buildOption(enumValue(factor::TechnicalIndicator::RSI), QStringLiteral("相对强弱指数（RSI）")),
        buildOption(enumValue(factor::TechnicalIndicator::MACD), QStringLiteral("指数平滑异同平均线（MACD）")),
        buildOption(enumValue(factor::TechnicalIndicator::MA), QStringLiteral("移动平均线（MA）")),
        buildOption(enumValue(factor::TechnicalIndicator::EMA), QStringLiteral("指数移动平均（EMA）")),
        buildOption(enumValue(factor::TechnicalIndicator::BOLL), QStringLiteral("布林带（BOLL）")),
        buildOption(enumValue(factor::TechnicalIndicator::KDJ), QStringLiteral("随机指标（KDJ）")),
        buildOption(enumValue(factor::TechnicalIndicator::ATR), QStringLiteral("真实波幅（ATR）")),
        buildOption(enumValue(factor::TechnicalIndicator::OBV), QStringLiteral("能量潮（OBV）")),
        buildOption(enumValue(factor::TechnicalIndicator::VWAP), QStringLiteral("成交量加权平均价（VWAP）")),
        buildOption(enumValue(factor::TechnicalIndicator::VOLUME_RATIO), QStringLiteral("量比")),
        buildOption(enumValue(factor::TechnicalIndicator::TURNOVER_STABILITY), QStringLiteral("换手率稳定性"))
    });

    const QVariantMap priceVisibility = QVariantMap{
        {QStringLiteral("visibleWhenField"), QStringLiteral("technicalIndicators")},
        {QStringLiteral("visibleWhenAnyOf"), buildIntList({0, 1, 2, 3, 4, 5, 6, 7, 8})}
    };

    return QList<ParamConfigSpec>{
        buildMultiSelectConfig(QStringLiteral("technicalIndicators"),
                               QStringLiteral("技术指标组合"),
                               QStringLiteral("选择一个或多个技术指标进行组合计算"),
                               buildEnumList({factor::TechnicalIndicator::RSI}),
                               indicatorOptions,
                               true),
        buildSelectConfig(QStringLiteral("technicalPriceType"),
                          QStringLiteral("价格字段"),
                          QStringLiteral("RSI、MACD、OBV 参考的价格字段"),
                          enumValue(factor::TechnicalPriceType::CLOSE),
                          buildOptionList({
                              buildOption(enumValue(factor::TechnicalPriceType::CLOSE), QStringLiteral("收盘价")),
                              buildOption(enumValue(factor::TechnicalPriceType::OPEN), QStringLiteral("开盘价")),
                              buildOption(enumValue(factor::TechnicalPriceType::HIGH), QStringLiteral("最高价")),
                              buildOption(enumValue(factor::TechnicalPriceType::LOW), QStringLiteral("最低价"))
                          }),
                          false,
                          priceVisibility),
        buildSliderConfig(QStringLiteral("rsiWindow"), QStringLiteral("RSI窗口"), QStringLiteral("RSI 计算窗口（天数）"), 14, 5, 250, 1, QStringLiteral("天"), 0, buildIntList({6, 9, 14, 21}), false, QVariantMap{{QStringLiteral("visibleWhenField"), QStringLiteral("technicalIndicators")}, {QStringLiteral("visibleWhenAnyOf"), buildIntList({0})}}),
        buildSliderConfig(QStringLiteral("maWindow"), QStringLiteral("MA窗口"), QStringLiteral("MA 计算窗口（天数）"), 20, 5, 250, 1, QStringLiteral("天"), 0, buildIntList({5, 10, 20, 60, 120}), false, QVariantMap{{QStringLiteral("visibleWhenField"), QStringLiteral("technicalIndicators")}, {QStringLiteral("visibleWhenAnyOf"), buildIntList({2})}}),
        buildSliderConfig(QStringLiteral("emaWindow"), QStringLiteral("EMA窗口"), QStringLiteral("EMA 计算窗口（天数）"), 20, 5, 250, 1, QStringLiteral("天"), 0, buildIntList({5, 10, 20, 60, 120}), false, QVariantMap{{QStringLiteral("visibleWhenField"), QStringLiteral("technicalIndicators")}, {QStringLiteral("visibleWhenAnyOf"), buildIntList({3})}}),
        buildSliderConfig(QStringLiteral("bollWindow"), QStringLiteral("BOLL窗口"), QStringLiteral("布林带计算窗口"), 20, 5, 250, 1, QStringLiteral("天"), 0, buildIntList({10, 20, 26, 60}), false, QVariantMap{{QStringLiteral("visibleWhenField"), QStringLiteral("technicalIndicators")}, {QStringLiteral("visibleWhenAnyOf"), buildIntList({4})}}),
        buildSliderConfig(QStringLiteral("bollStdDev"), QStringLiteral("BOLL标准差倍数"), QStringLiteral("布林带上下轨标准差倍数"), 2.0, 1.0, 4.0, 0.1, QStringLiteral("倍"), 1, QVariantList(), false, QVariantMap{{QStringLiteral("visibleWhenField"), QStringLiteral("technicalIndicators")}, {QStringLiteral("visibleWhenAnyOf"), buildIntList({4})}}),
        buildSliderConfig(QStringLiteral("kdjWindow"), QStringLiteral("KDJ窗口"), QStringLiteral("KDJ 计算窗口"), 9, 5, 120, 1, QStringLiteral("天"), 0, buildIntList({5, 9, 14, 21}), false, QVariantMap{{QStringLiteral("visibleWhenField"), QStringLiteral("technicalIndicators")}, {QStringLiteral("visibleWhenAnyOf"), buildIntList({5})}}),
        buildSliderConfig(QStringLiteral("kdjKPeriod"), QStringLiteral("K值平滑周期"), QStringLiteral("KDJ 中 K 值平滑周期"), 3, 2, 10, 1, QStringLiteral("天"), 0, buildIntList({2, 3, 5}), false, QVariantMap{{QStringLiteral("visibleWhenField"), QStringLiteral("technicalIndicators")}, {QStringLiteral("visibleWhenAnyOf"), buildIntList({5})}}),
        buildSliderConfig(QStringLiteral("kdjDPeriod"), QStringLiteral("D值平滑周期"), QStringLiteral("KDJ 中 D 值平滑周期"), 3, 2, 10, 1, QStringLiteral("天"), 0, buildIntList({2, 3, 5}), false, QVariantMap{{QStringLiteral("visibleWhenField"), QStringLiteral("technicalIndicators")}, {QStringLiteral("visibleWhenAnyOf"), buildIntList({5})}}),
        buildSliderConfig(QStringLiteral("atrWindow"), QStringLiteral("ATR窗口"), QStringLiteral("ATR 计算窗口"), 14, 5, 250, 1, QStringLiteral("天"), 0, buildIntList({7, 14, 20, 26}), false, QVariantMap{{QStringLiteral("visibleWhenField"), QStringLiteral("technicalIndicators")}, {QStringLiteral("visibleWhenAnyOf"), buildIntList({6})}}),
        buildSliderConfig(QStringLiteral("macdFastPeriod"), QStringLiteral("MACD快线周期"), QStringLiteral("MACD 快线 EMA 周期"), 12, 2, 120, 1, QStringLiteral("天"), 0, buildIntList({8, 12, 13}), false, QVariantMap{{QStringLiteral("visibleWhenField"), QStringLiteral("technicalIndicators")}, {QStringLiteral("visibleWhenAnyOf"), buildIntList({1})}}),
        buildSliderConfig(QStringLiteral("macdSlowPeriod"), QStringLiteral("MACD慢线周期"), QStringLiteral("MACD 慢线 EMA 周期"), 26, 3, 250, 1, QStringLiteral("天"), 0, buildIntList({20, 26, 30}), false, QVariantMap{{QStringLiteral("visibleWhenField"), QStringLiteral("technicalIndicators")}, {QStringLiteral("visibleWhenAnyOf"), buildIntList({1})}}),
        buildSliderConfig(QStringLiteral("macdSignalPeriod"), QStringLiteral("MACD信号线周期"), QStringLiteral("MACD 信号线 EMA 周期"), 9, 2, 120, 1, QStringLiteral("天"), 0, buildIntList({5, 9}), false, QVariantMap{{QStringLiteral("visibleWhenField"), QStringLiteral("technicalIndicators")}, {QStringLiteral("visibleWhenAnyOf"), buildIntList({1})}}),
        buildSliderConfig(QStringLiteral("obvWindow"), QStringLiteral("OBV窗口"), QStringLiteral("OBV 斜率/变化率计算窗口"), 20, 5, 250, 1, QStringLiteral("天"), 0, buildIntList({10, 20, 60}), false, QVariantMap{{QStringLiteral("visibleWhenField"), QStringLiteral("technicalIndicators")}, {QStringLiteral("visibleWhenAnyOf"), buildIntList({7})}}),
        buildSliderConfig(QStringLiteral("vwapWindow"), QStringLiteral("VWAP窗口"), QStringLiteral("VWAP 计算窗口"), 20, 5, 250, 1, QStringLiteral("天"), 0, buildIntList({5, 10, 20, 60}), false, QVariantMap{{QStringLiteral("visibleWhenField"), QStringLiteral("technicalIndicators")}, {QStringLiteral("visibleWhenAnyOf"), buildIntList({8})}}),
        buildSliderConfig(QStringLiteral("volumeRatioWindow"), QStringLiteral("量比窗口"), QStringLiteral("量比计算窗口"), 20, 5, 250, 1, QStringLiteral("天"), 0, buildIntList({5, 10, 20, 60}), false, QVariantMap{{QStringLiteral("visibleWhenField"), QStringLiteral("technicalIndicators")}, {QStringLiteral("visibleWhenAnyOf"), buildIntList({9})}}),
        buildSliderConfig(QStringLiteral("turnoverStabilityWindow"), QStringLiteral("换手率稳定性窗口"), QStringLiteral("换手率稳定性计算窗口"), 60, 20, 250, 1, QStringLiteral("天"), 0, buildIntList({20, 60, 120, 250}), false, QVariantMap{{QStringLiteral("visibleWhenField"), QStringLiteral("technicalIndicators")}, {QStringLiteral("visibleWhenAnyOf"), buildIntList({10})}}),
        buildSelectConfig(QStringLiteral("turnoverStabilityMetric"),
                          QStringLiteral("稳定性参考值"),
                          QStringLiteral("换手率稳定性参考字段"),
                          enumValue(factor::LiquidityMetric::TURNOVER_RATE),
                          buildOptionList({
                              buildOption(enumValue(factor::LiquidityMetric::TURNOVER_RATE), QStringLiteral("换手率")),
                              buildOption(enumValue(factor::LiquidityMetric::VOLUME), QStringLiteral("成交量")),
                              buildOption(enumValue(factor::LiquidityMetric::AMPLITUDE), QStringLiteral("振幅"))
                          }),
                          false,
                          QVariantMap{{QStringLiteral("visibleWhenField"), QStringLiteral("technicalIndicators")}, {QStringLiteral("visibleWhenAnyOf"), buildIntList({10})}}),
        buildSelectConfig(QStringLiteral("technicalCombinationMode"),
                          QStringLiteral("组合方式"),
                          QStringLiteral("多个技术指标的组合方式"),
                          enumValue(factor::TechnicalCombinationMode::EqualWeight),
                          buildOptionList({
                              buildOption(enumValue(factor::TechnicalCombinationMode::EqualWeight), QStringLiteral("等权平均")),
                              buildOption(enumValue(factor::TechnicalCombinationMode::NormalizedAverage), QStringLiteral("标准化平均"))
                          }),
                          false,
                          QVariantMap{{QStringLiteral("visibleWhenField"), QStringLiteral("technicalIndicators")}, {QStringLiteral("visibleWhenMinCount"), 2}})
    };
}

QList<ParamConfigSpec> macroConfigs()
{
    return QList<ParamConfigSpec>{
        buildMultiSelectConfig(QStringLiteral("macroDimensions"),
                               QStringLiteral("因子维度"),
                               QStringLiteral("选择宏观维度"),
                               buildEnumList({
                                   factor::MacroDimension::GROWTH,
                                   factor::MacroDimension::INFLATION,
                                   factor::MacroDimension::CREDIT,
                                   factor::MacroDimension::RATES,
                                   factor::MacroDimension::POLICY,
                                   factor::MacroDimension::RISK_APPETITE
                               }),
                               buildOptionList({
                                   buildOption(enumValue(factor::MacroDimension::GROWTH), QStringLiteral("经济增长")),
                                   buildOption(enumValue(factor::MacroDimension::INFLATION), QStringLiteral("通货膨胀")),
                                   buildOption(enumValue(factor::MacroDimension::CREDIT), QStringLiteral("货币信用")),
                                   buildOption(enumValue(factor::MacroDimension::RATES), QStringLiteral("利率水平")),
                                   buildOption(enumValue(factor::MacroDimension::POLICY), QStringLiteral("政策环境")),
                                   buildOption(enumValue(factor::MacroDimension::RISK_APPETITE), QStringLiteral("风险偏好"))
                               }),
                               true),
        buildMultiSelectConfig(QStringLiteral("macroIndicators"),
                               QStringLiteral("核心指标（推荐）"),
                               QStringLiteral("选择核心宏观指标"),
                               buildEnumList({
                                   factor::MacroIndicator::INDUSTRIAL_ADDED_VALUE_YOY,
                                   factor::MacroIndicator::CPI_YOY,
                                   factor::MacroIndicator::M2_YOY,
                                   factor::MacroIndicator::TEN_YEAR_BOND_YIELD,
                                   factor::MacroIndicator::LPR_1Y,
                                   factor::MacroIndicator::AA_CREDIT_SPREAD
                               }),
                               buildOptionList({
                                   buildOption(enumValue(factor::MacroIndicator::INDUSTRIAL_ADDED_VALUE_YOY), QStringLiteral("工业增加值同比")),
                                   buildOption(enumValue(factor::MacroIndicator::MANUFACTURING_PMI), QStringLiteral("制造业采购经理指数（PMI）")),
                                   buildOption(enumValue(factor::MacroIndicator::GDP_YOY), QStringLiteral("国内生产总值同比（GDP）")),
                                   buildOption(enumValue(factor::MacroIndicator::CPI_YOY), QStringLiteral("居民消费价格指数同比（CPI）")),
                                   buildOption(enumValue(factor::MacroIndicator::PPI_YOY), QStringLiteral("工业生产者出厂价格指数同比（PPI）")),
                                   buildOption(enumValue(factor::MacroIndicator::M2_YOY), QStringLiteral("广义货币同比（M2）")),
                                   buildOption(enumValue(factor::MacroIndicator::SOCIAL_FINANCING_STOCK_YOY), QStringLiteral("社融存量同比")),
                                   buildOption(enumValue(factor::MacroIndicator::M1_M2_SPREAD), QStringLiteral("M1-M2剪刀差")),
                                   buildOption(enumValue(factor::MacroIndicator::TEN_YEAR_BOND_YIELD), QStringLiteral("10年国债收益率")),
                                   buildOption(enumValue(factor::MacroIndicator::SHIBOR_3M), QStringLiteral("3个月上海银行间同业拆放利率（SHIBOR）")),
                                   buildOption(enumValue(factor::MacroIndicator::LPR_1Y), QStringLiteral("1年期贷款市场报价利率（LPR）")),
                                   buildOption(enumValue(factor::MacroIndicator::RESERVE_REQUIREMENT_RATIO), QStringLiteral("存款准备金率")),
                                   buildOption(enumValue(factor::MacroIndicator::AA_CREDIT_SPREAD), QStringLiteral("AA信用利差")),
                                   buildOption(enumValue(factor::MacroIndicator::VIX_PROXY), QStringLiteral("波动率指数代理（VIX）"))
                               }),
                               true),
        buildSelectConfig(QStringLiteral("macroFrequency"),
                          QStringLiteral("宏观对齐频率"),
                          QStringLiteral("宏观指标对齐频率"),
                          enumValue(factor::DataFrequency::Monthly),
                          buildOptionList({
                              buildOption(enumValue(factor::DataFrequency::Daily), QStringLiteral("日频")),
                              buildOption(enumValue(factor::DataFrequency::Weekly), QStringLiteral("周频")),
                              buildOption(enumValue(factor::DataFrequency::Monthly), QStringLiteral("月频")),
                              buildOption(enumValue(factor::DataFrequency::Quarterly), QStringLiteral("季频"))
                          })),
        buildSliderConfig(QStringLiteral("macroWindow"),
                          QStringLiteral("观察周期"),
                          QStringLiteral("宏观观察周期"),
                          12,
                          3,
                          60,
                          1,
                          QStringLiteral("期"),
                          0,
                          buildIntList({3, 6, 12, 24, 36}))
    };
}

QList<ParamConfigSpec> industryConfigs()
{
    return QList<ParamConfigSpec>{
        buildSelectConfig(QStringLiteral("sectorType"),
                          QStringLiteral("行业分类标准"),
                          QStringLiteral("行业分类标准"),
                          enumValue(factor::ConfigurableSectorType::SW_L1),
                          buildOptionList({
                              buildOption(enumValue(factor::ConfigurableSectorType::SW_L1), QStringLiteral("申万一级")),
                              buildOption(enumValue(factor::ConfigurableSectorType::SW_L2), QStringLiteral("申万二级")),
                              buildOption(enumValue(factor::ConfigurableSectorType::CITIC_L1), QStringLiteral("中信一级")),
                              buildOption(enumValue(factor::ConfigurableSectorType::CITIC_L2), QStringLiteral("中信二级"))
                          })),
        buildSelectConfig(QStringLiteral("industryMetric"),
                          QStringLiteral("行业指标"),
                          QStringLiteral("行业因子类型"),
                          enumValue(factor::IndustryMetric::INDUSTRY_MOMENTUM),
                          buildOptionList({
                              buildOption(enumValue(factor::IndustryMetric::INDUSTRY_PROSPERITY), QStringLiteral("行业景气度")),
                              buildOption(enumValue(factor::IndustryMetric::INDUSTRY_MOMENTUM), QStringLiteral("行业动量")),
                              buildOption(enumValue(factor::IndustryMetric::INDUSTRY_CONCENTRATION), QStringLiteral("行业集中度"))
                          })),
        buildSliderConfig(QStringLiteral("window"),
                          QStringLiteral("观察窗口"),
                          QStringLiteral("行业因子回看窗口（天数）"),
                          60,
                          20,
                          750,
                          1,
                          QStringLiteral("天"),
                          0,
                          buildIntList({20, 60, 120, 250, 750}))
    };
}

QList<ParamConfigSpec> liquidityConfigs()
{
    return QList<ParamConfigSpec>{
        buildSelectConfig(QStringLiteral("metric"),
                          QStringLiteral("流动性指标"),
                          QStringLiteral("使用的流动性指标"),
                          enumValue(factor::LiquidityMetric::TURNOVER_RATE),
                          buildOptionList({
                              buildOption(enumValue(factor::LiquidityMetric::TURNOVER_RATE), QStringLiteral("换手率")),
                              buildOption(enumValue(factor::LiquidityMetric::AMIHUD_ILLIQUIDITY), QStringLiteral("Amihud 非流动性指标")),
                              buildOption(enumValue(factor::LiquidityMetric::AMPLITUDE), QStringLiteral("振幅")),
                              buildOption(enumValue(factor::LiquidityMetric::VOLUME), QStringLiteral("成交量"))
                          })),
        buildSliderConfig(QStringLiteral("window"),
                          QStringLiteral("流动性窗口"),
                          QStringLiteral("计算流动性的时间窗口（天数）"),
                          20,
                          5,
                          120,
                          1,
                          QStringLiteral("天"),
                          0,
                          buildIntList({5, 10, 20, 60}))
    };
}

QList<ParamConfigSpec> customConfigs()
{
    return QList<ParamConfigSpec>{
        buildInputConfig(QStringLiteral("expression"),
                         QStringLiteral("表达式"),
                         QStringLiteral("因子计算表达式"),
                         QStringLiteral(""),
                         QStringLiteral("例如: close / open - 1"),
                         false,
                         2000),
        buildInputConfig(QStringLiteral("variables"),
                         QStringLiteral("变量定义"),
                         QStringLiteral("表达式变量绑定。可指定 field 映射真实数据字段，或仅指定 defaultValue 作为常量/缺失回退值"),
                         QStringLiteral("[]"),
                         QStringLiteral("[\n  {\n    \"name\": \"p1\",\n    \"field\": \"close\"\n  },\n  {\n    \"name\": \"p0\",\n    \"field\": \"open\"\n  }\n]"),
                         true,
                         4000,
                         false,
                         QVariantMap{
                             {QStringLiteral("validator"), QStringLiteral("json")},
                             {QStringLiteral("serializeAsJson"), true}
                         })
    };
}

QList<ParamConfigSpec> reversalConfigs()
{
    return QList<ParamConfigSpec>{
        buildSelectConfig(QStringLiteral("splitMethod"),
                          QStringLiteral("反转方式"),
                          QStringLiteral("传统反转或W式切割理想反转"),
                          enumValue(factor::ReversalSplitMethod::NONE),
                          buildOptionList({
                              buildOption(enumValue(factor::ReversalSplitMethod::NONE), QStringLiteral("传统反转")),
                              buildOption(enumValue(factor::ReversalSplitMethod::W_CUT), QStringLiteral("W式切割"))
                          })),
        buildSliderConfig(QStringLiteral("window"),
                          QStringLiteral("回顾窗口"),
                          QStringLiteral("回溯交易日数（W式切割需偶数）"),
                          20, 5, 60, 1, QStringLiteral("天"), 0,
                          buildIntList({5, 10, 20, 40, 60})),
        buildInputConfig(QStringLiteral("splitMetric"),
                         QStringLiteral("切割指标"),
                         QStringLiteral("W式切割使用的字段名"),
                         QStringLiteral("avg_trade_amount"),
                         QStringLiteral("avg_trade_amount")),
        buildToggleConfig(QStringLiteral("useHighOnly"),
                          QStringLiteral("仅用高D组"),
                          QStringLiteral("仅使用高D组收益率作为因子值"),
                          false, QStringLiteral("是"), QStringLiteral("否"))
    };
}

QList<ParamConfigSpec> highFreqConfigs()
{
    return QList<ParamConfigSpec>{
        buildSliderConfig(QStringLiteral("frequency"),
                          QStringLiteral("分钟频率"),
                          QStringLiteral("数据频率（分钟）"),
                          5, 1, 60, 1, QStringLiteral("分钟"), 0,
                          buildIntList({1, 5, 10, 30})),
        buildSliderConfig(QStringLiteral("lookbackDays"),
                          QStringLiteral("回顾天数"),
                          QStringLiteral("回顾天数"),
                          10, 1, 60, 1, QStringLiteral("天"), 0),
        buildSliderConfig(QStringLiteral("window"),
                          QStringLiteral("聚合天数"),
                          QStringLiteral("月度因子聚合天数"),
                          20, 5, 60, 1, QStringLiteral("天"), 0,
                          buildIntList({5, 10, 20})),
        buildSelectConfig(QStringLiteral("aggregation"),
                          QStringLiteral("聚合方式"),
                          QStringLiteral("聚合方式"),
                          enumValue(factor::HFAggregation::MEAN),
                          buildOptionList({
                              buildOption(enumValue(factor::HFAggregation::MEAN), QStringLiteral("均值")),
                              buildOption(enumValue(factor::HFAggregation::CUMULATIVE), QStringLiteral("累计")),
                              buildOption(enumValue(factor::HFAggregation::MAX), QStringLiteral("最大值"))
                          })),
        buildSliderConfig(QStringLiteral("percentile"),
                          QStringLiteral("百分位阈值"),
                          QStringLiteral("聪明钱识别阈值"),
                          0.2, 0.01, 0.5, 0.01, QStringLiteral(""), 2),
        buildSelectConfig(QStringLiteral("momentType"),
                          QStringLiteral("矩类型"),
                          QStringLiteral("已实现高阶矩类型"),
                          enumValue(factor::HFMomentType::VARIANCE),
                          buildOptionList({
                              buildOption(enumValue(factor::HFMomentType::VARIANCE), QStringLiteral("已实现方差")),
                              buildOption(enumValue(factor::HFMomentType::SKEWNESS), QStringLiteral("已实现偏度")),
                              buildOption(enumValue(factor::HFMomentType::KURTOSIS), QStringLiteral("已实现峰度"))
                          }))
    };
}

QList<ParamConfigSpec> dlConfigs()
{
    return QList<ParamConfigSpec>{
        buildSelectConfig(QStringLiteral("modelType"),
                          QStringLiteral("模型类型"),
                          QStringLiteral("深度学习模型架构"),
                          enumValue(factor::DLModelType::LSTM),
                          buildOptionList({
                              buildOption(enumValue(factor::DLModelType::RNN), QStringLiteral("RNN")),
                              buildOption(enumValue(factor::DLModelType::LSTM), QStringLiteral("LSTM")),
                              buildOption(enumValue(factor::DLModelType::GRU), QStringLiteral("GRU")),
                              buildOption(enumValue(factor::DLModelType::CNN), QStringLiteral("CNN")),
                              buildOption(enumValue(factor::DLModelType::TRANSFORMER), QStringLiteral("Transformer"))
                          })),
        buildSliderConfig(QStringLiteral("hiddenLayers"),
                          QStringLiteral("隐藏层数"),
                          QStringLiteral("隐藏层数量"),
                          3, 1, 6, 1, QStringLiteral("层"), 0),
        buildSliderConfig(QStringLiteral("hiddenUnits"),
                          QStringLiteral("神经元数"),
                          QStringLiteral("每层神经元数量"),
                          128, 16, 672, 16, QStringLiteral("个"), 0),
        buildSliderConfig(QStringLiteral("featureCount"),
                          QStringLiteral("输入特征数"),
                          QStringLiteral("输入特征数量"),
                          64, 16, 176, 8, QStringLiteral("个"), 0),
        buildSliderConfig(QStringLiteral("predictionHorizon"),
                          QStringLiteral("预测周期"),
                          QStringLiteral("预测未来N日收益"),
                          5, 1, 20, 1, QStringLiteral("天"), 0,
                          buildIntList({1, 5, 10, 20})),
        buildSliderConfig(QStringLiteral("learningRate"),
                          QStringLiteral("学习率"),
                          QStringLiteral("学习率"),
                          0.001, 0.0001, 0.01, 0.0001, QStringLiteral(""), 4),
        buildSliderConfig(QStringLiteral("batchSize"),
                          QStringLiteral("批量大小"),
                          QStringLiteral("批量大小"),
                          512, 128, 1024, 128, QStringLiteral(""), 0),
        buildSliderConfig(QStringLiteral("epochs"),
                          QStringLiteral("训练轮数"),
                          QStringLiteral("训练轮数"),
                          100, 50, 200, 10, QStringLiteral("轮"), 0),
        buildSelectConfig(QStringLiteral("optimizer"),
                          QStringLiteral("优化器"),
                          QStringLiteral("优化器"),
                          enumValue(factor::DLOptimizer::ADAM),
                          buildOptionList({
                              buildOption(enumValue(factor::DLOptimizer::ADAM), QStringLiteral("Adam")),
                              buildOption(enumValue(factor::DLOptimizer::SGD), QStringLiteral("SGD"))
                          })),
        buildSliderConfig(QStringLiteral("dropoutRate"),
                          QStringLiteral("Dropout比率"),
                          QStringLiteral("Dropout比率"),
                          0.2, 0.1, 0.5, 0.05, QStringLiteral(""), 2),
        buildToggleConfig(QStringLiteral("orthogonalConstraint"),
                          QStringLiteral("正交化"),
                          QStringLiteral("是否剥离风格/行业暴露"),
                          false, QStringLiteral("是"), QStringLiteral("否"))
    };
}

const QMap<factor::FactorType, QList<ParamConfigSpec>>& factorParameterConfigCatalog()
{
    static const QMap<factor::FactorType, QList<ParamConfigSpec>> kCatalog = {
        {factor::FactorType::VALUE, appendConfigs(buildCommonConfigs(), valueConfigs())},
        {factor::FactorType::MOMENTUM, appendConfigs(buildCommonConfigs(), momentumConfigs())},
        {factor::FactorType::SIZE, appendConfigs(buildCommonConfigs(), sizeConfigs())},
        {factor::FactorType::QUALITY, appendConfigs(buildCommonConfigs(), qualityConfigs())},
        {factor::FactorType::GROWTH, appendConfigs(buildCommonConfigs(), growthConfigs())},
        {factor::FactorType::DIVIDEND, appendConfigs(buildCommonConfigs(), dividendConfigs())},
        {factor::FactorType::TECHNICAL, appendConfigs(buildCommonConfigs(), technicalConfigs())},
        {factor::FactorType::LIQUIDITY, appendConfigs(buildCommonConfigs(), liquidityConfigs())},
        {factor::FactorType::MACRO, appendConfigs(buildCommonConfigs(), macroConfigs())},
        {factor::FactorType::INDUSTRY, appendConfigs(buildCommonConfigs(), industryConfigs())},
        {factor::FactorType::REVERSAL, appendConfigs(buildCommonConfigs(), reversalConfigs())},
        {factor::FactorType::HIGH_FREQ, appendConfigs(buildCommonConfigs(), highFreqConfigs())},
        {factor::FactorType::DL, appendConfigs(buildCommonConfigs(), dlConfigs())},
        {factor::FactorType::SENTIMENT, appendConfigs(buildCommonConfigs(), sentimentConfigs())},
        {factor::FactorType::CUSTOM, appendConfigs(buildCommonConfigs(), customConfigs())},
        {factor::FactorType::LOW_VOLATILITY, appendConfigs(buildCommonConfigs(), lowVolatilityConfigs())},
        {factor::FactorType::COMPOSITE, buildCommonConfigs()}
    };
    return kCatalog;
}

const QMap<factor::FactorType, QVariantMap>& factorUiMetaCatalog()
{
    static const QMap<factor::FactorType, QVariantMap> kCatalog = {
        {factor::FactorType::VALUE, buildFactorUiMeta(QStringLiteral("value"), 0,
            QStringLiteral("价值因子"),
            QStringLiteral("BP、EP、股息率、CF/P"),
            QStringLiteral("基于BP、EP、股息率和CF/P构建的价值因子"),
            QStringLiteral("例如：低估值组合因子"),
            QStringLiteral("描述价值因子的计算方法、应用场景等..."),
            QStringLiteral("#F59E0B"),
            QStringLiteral("💰"),
            QStringLiteral("估值"))},
        {factor::FactorType::MOMENTUM, buildFactorUiMeta(QStringLiteral("momentum"), 1,
            QStringLiteral("动量因子"),
            QStringLiteral("价格动量、收益率趋势"),
            QStringLiteral("基于价格动量、收益率趋势构建的动量因子"),
            QStringLiteral("例如：60日动量因子"),
            QStringLiteral("描述动量因子的计算方法、应用场景等..."),
            QStringLiteral("#3B82F6"),
            QStringLiteral("📈"),
            QStringLiteral("趋势动量"))},
        {factor::FactorType::SIZE, buildFactorUiMeta(QStringLiteral("size"), 2,
            QStringLiteral("规模因子"),
            QStringLiteral("市值规模、流通市值"),
            QStringLiteral("基于市值规模、流通市值构建的规模因子"),
            QStringLiteral("例如：小市值因子"),
            QStringLiteral("描述规模因子的计算方法、应用场景等..."),
            QStringLiteral("#8B5CF6"),
            QStringLiteral("📏"),
            QStringLiteral("市值规模"))},
        {factor::FactorType::QUALITY, buildFactorUiMeta(QStringLiteral("quality"), 3,
            QStringLiteral("质量因子"),
            QStringLiteral("财务健康、盈利能力"),
            QStringLiteral("基于财务健康、盈利能力构建的质量因子"),
            QStringLiteral("例如：高ROE质量因子"),
            QStringLiteral("描述质量因子的计算方法、应用场景等..."),
            QStringLiteral("#10B981"),
            QStringLiteral("🏆"),
            QStringLiteral("盈利能力"))},
        {factor::FactorType::GROWTH, buildFactorUiMeta(QStringLiteral("growth"), 4,
            QStringLiteral("成长因子"),
            QStringLiteral("营收、利润增长率"),
            QStringLiteral("基于营收、利润增长率构建的成长因子"),
            QStringLiteral("例如：高增长潜力因子"),
            QStringLiteral("描述成长因子的计算方法、应用场景等..."),
            QStringLiteral("#8B5CF6"),
            QStringLiteral("🚀"),
            QStringLiteral("营收增长"))},
        {factor::FactorType::DIVIDEND, buildFactorUiMeta(QStringLiteral("dividend"), 5,
            QStringLiteral("红利因子"),
            QStringLiteral("股息率、股息支付率"),
            QStringLiteral("基于股息率、股息支付率构建的红利因子"),
            QStringLiteral("例如：高股息率组合"),
            QStringLiteral("描述红利因子的计算方法、应用场景等..."),
            QStringLiteral("#EC4899"),
            QStringLiteral("💵"),
            QStringLiteral("股息"))},
        {factor::FactorType::TECHNICAL, buildFactorUiMeta(QStringLiteral("technical"), 6,
            QStringLiteral("技术因子"),
            QStringLiteral("RSI、MACD等技术指标"),
            QStringLiteral("基于RSI、MACD等技术指标构建的技术因子"),
            QStringLiteral("例如：RSI超卖信号"),
            QStringLiteral("描述技术因子的计算方法、应用场景等..."),
            QStringLiteral("#EF4444"),
            QStringLiteral("📊"),
            QStringLiteral("技术指标"))},
        {factor::FactorType::LIQUIDITY, buildFactorUiMeta(QStringLiteral("liquidity"), 7,
            QStringLiteral("流动性因子"),
            QStringLiteral("换手率、买卖价差"),
            QStringLiteral("基于换手率、买卖价差构建的流动性因子"),
            QStringLiteral("例如：高流动性组合"),
            QStringLiteral("描述流动性因子的计算方法、应用场景等..."),
            QStringLiteral("#8B5CF6"),
            QStringLiteral("💧"),
            QStringLiteral("市场微观结构"))},
        {factor::FactorType::MACRO, buildFactorUiMeta(QStringLiteral("macro"), 8,
            QStringLiteral("宏观因子"),
            QStringLiteral("利率、通胀、经济周期"),
            QStringLiteral("基于利率、通胀、经济周期构建的宏观因子"),
            QStringLiteral("例如：利率敏感度因子"),
            QStringLiteral("描述宏观因子的计算方法、应用场景等..."),
            QStringLiteral("#F97316"),
            QStringLiteral("🌐"),
            QStringLiteral("宏观"))},
        {factor::FactorType::INDUSTRY, buildFactorUiMeta(QStringLiteral("industry"), 9,
            QStringLiteral("行业因子"),
            QStringLiteral("行业景气度、行业动量"),
            QStringLiteral("基于行业景气度、行业动量构建的行业因子"),
            QStringLiteral("例如：行业动量因子"),
            QStringLiteral("描述行业因子的计算方法、应用场景等..."),
            QStringLiteral("#EA580C"),
            QStringLiteral("🏭"),
            QStringLiteral("行业"))},
        {factor::FactorType::SENTIMENT, buildFactorUiMeta(QStringLiteral("sentiment"), 10,
            QStringLiteral("情绪因子"),
            QStringLiteral("新闻情感、社交媒体"),
            QStringLiteral("基于新闻情感、社交媒体构建的情绪因子"),
            QStringLiteral("例如：市场情绪指标"),
            QStringLiteral("描述情绪因子的计算方法、应用场景等..."),
            QStringLiteral("#EC4899"),
            QStringLiteral("😊"),
            QStringLiteral("行为金融"))},
        {factor::FactorType::CUSTOM, buildFactorUiMeta(QStringLiteral("custom"), 11,
            QStringLiteral("自定义因子"),
            QStringLiteral("用户自定义表达式"),
            QStringLiteral("用户自定义表达式构建的因子"),
            QStringLiteral("例如：自定义组合因子"),
            QStringLiteral("描述自定义因子的计算方法、应用场景等..."),
            QStringLiteral("#94A3B8"),
            QStringLiteral("🛠"),
            QStringLiteral("自定义"))},
        {factor::FactorType::LOW_VOLATILITY, buildFactorUiMeta(QStringLiteral("low_volatility"), 12,
            QStringLiteral("低波因子"),
            QStringLiteral("波动率、贝塔值"),
            QStringLiteral("基于波动率、贝塔值构建的低波因子"),
            QStringLiteral("例如：低波动率组合"),
            QStringLiteral("描述低波因子的计算方法、应用场景等..."),
            QStringLiteral("#06B6D4"),
            QStringLiteral("📉"),
            QStringLiteral("波动率"))},
        {factor::FactorType::COMPOSITE, buildFactorUiMeta(QStringLiteral("composite"), 13,
            QStringLiteral("组合因子"),
            QStringLiteral("多因子加权组合"),
            QStringLiteral("多个子因子按权重组合而成的复合因子"),
            QStringLiteral("例如：价值+动量双因子组合"),
            QStringLiteral("描述组合因子的计算方法、权重配置等..."),
            QStringLiteral("#A78BFA"),
            QStringLiteral("🧩"),
            QStringLiteral("多因子组合"))},
        {factor::FactorType::REVERSAL, buildFactorUiMeta(QStringLiteral("reversal"), 15,
            QStringLiteral("反转因子"),
            QStringLiteral("短期反转与理想反转"),
            QStringLiteral("基于均值回归逻辑，捕捉短期过度涨跌后的反转效应"),
            QStringLiteral("传统反转：-Return(20) ｜ 理想反转：W式切割大单成交"),
            QStringLiteral("传统反转取过去N日收益率负值；理想反转按日均单笔成交额切割高低组"),
            QStringLiteral("#EF4444"),
            QStringLiteral("🔄"),
            QStringLiteral("反转,均值回归"))},
        {factor::FactorType::HIGH_FREQ, buildFactorUiMeta(QStringLiteral("high_freq"), 16,
            QStringLiteral("高频因子"),
            QStringLiteral("分钟级微观结构"),
            QStringLiteral("基于分钟级量价数据捕捉日内交易微观结构信号"),
            QStringLiteral("聪明钱因子、已实现方差/偏度/峰度、量价相关性"),
            QStringLiteral("需分钟/逐笔数据管线支持，当前为骨架实现"),
            QStringLiteral("#F59E0B"),
            QStringLiteral("⚡"),
            QStringLiteral("高频,微观结构,聪明钱"))},
        {factor::FactorType::DL, buildFactorUiMeta(QStringLiteral("dl"), 17,
            QStringLiteral("AI因子"),
            QStringLiteral("深度学习自动特征"),
            QStringLiteral("利用神经网络从海量量价数据中自动提取非线性特征"),
            QStringLiteral("LSTM/GRU/CNN/Transformer 架构，离线训练+在线推理"),
            QStringLiteral("需预训练模型权重文件，推理引擎建议 libtorch / ONNX Runtime"),
            QStringLiteral("#8B5CF6"),
            QStringLiteral("🧠"),
            QStringLiteral("AI,深度学习,神经网络"))}
    };
    return kCatalog;
}

}  // namespace

// 构造函数
FactorMetaService::FactorMetaService(QObject* parent) : QObject(parent)
{
    INTERNAL_DEBUG_STREAM << "FactorMetaService created";
}

// 析构函数
FactorMetaService::~FactorMetaService()
{
    INTERNAL_DEBUG_STREAM << "FactorMetaService destroyed";
}

// 初始化服务
void FactorMetaService::initialize()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_initialized) {
        INTERNAL_DEBUG_STREAM << "FactorMetaService already initialized";
        return;
    }
    
    loadMetaData(); // JSON 文件缺失不阻塞初始化，因子数据在数据库，静态目录提供 UI 兜底
    
    m_initialized = true;
    emit initializedChanged();
    
    INTERNAL_DEBUG_STREAM << "FactorMetaService initialized successfully";
    emit metaDataLoaded(true, "因子元数据加载成功");
}

// 重新加载元数据
void FactorMetaService::reloadMetaData()
{
    QMutexLocker locker(&m_mutex);
    
    m_commonMetaData.clear();
    m_parameterMetaData.clear();
    m_factorCategories.clear();
    m_mergedParameters.clear();
    m_initialized = false;

    loadMetaData(); // JSON 缺失不阻塞，静态目录兜底

    m_initialized = true;
    emit initializedChanged();
    emit factorCategoriesChanged();
    emit parameterTypesChanged();

    INTERNAL_DEBUG_STREAM << "FactorMetaService metadata reloaded";
    emit metaDataLoaded(true, "因子元数据重新加载成功");
}

// 获取因子分类信息
QVariantMap FactorMetaService::getFactorCategory(factor::FactorType factorType)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_initialized) {
        INTERNAL_WARN_STREAM << "FactorMetaService not initialized";
        return QVariantMap();
    }
    
    if (m_factorCategories.contains(factorType)) {
        return m_factorCategories[factorType];
    }
    
    // 如果缓存中没有，尝试从commonMetaData中查找
    QString factorTypeId = factorTypeToString(factorType);
    if (m_commonMetaData.contains("categories")) {
        QVariantList categories = m_commonMetaData["categories"].toList();
        for (const QVariant& category : categories) {
            QVariantMap categoryMap = category.toMap();
            if (categoryMap["id"].toString() == factorTypeId) {
                m_factorCategories[factorType] = categoryMap;
                return categoryMap;
            }
        }
    }
    
    return QVariantMap();
}

// 通过ID获取因子分类信息
QVariantMap FactorMetaService::getFactorCategoryById(const QString& factorTypeId)
{
    factor::FactorType factorType = stringToFactorType(factorTypeId);
    return getFactorCategory(factorType);
}

// 获取可用因子类型列表
QStringList FactorMetaService::getAvailableFactorTypes()
{
    QStringList types;
    for (auto it = FACTOR_TYPE_TO_ID.constBegin(); it != FACTOR_TYPE_TO_ID.constEnd(); ++it) {
        types.append(it.value());
    }
    return types;
}

QVariantList FactorMetaService::getAllFactorUiMeta() const
{
    QVariantList items;
    const auto& catalog = factorUiMetaCatalog();
    for (auto it = catalog.constBegin(); it != catalog.constEnd(); ++it) {
        items.append(it.value());
    }
    return items;
}

QVariantMap FactorMetaService::getFactorUiMeta(const QVariant& factorType) const
{
    const factor::FactorType resolvedType = variantToFactorType(factorType);
    return factorUiMetaCatalog().value(resolvedType);
}

QVariantList FactorMetaService::getFactorParameterConfigs(const QVariant& factorType) const
{
    const factor::FactorType resolvedType = variantToFactorType(factorType);
    return toVariantList(factorParameterConfigCatalog().value(resolvedType));
}

// 获取参数定义
QVariantMap FactorMetaService::getParameterDefinition(const QString& paramName, factor::FactorType factorType)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_initialized) {
        INTERNAL_WARN_STREAM << "FactorMetaService not initialized";
        return QVariantMap();
    }
    
    // 确保合并参数已加载
    if (!m_mergedParameters.contains(factorType)) {
        m_mergedParameters[factorType] = mergeCommonAndSpecificParams(factorType);
    }
    
    QVariantMap mergedParams = m_mergedParameters[factorType];
    if (mergedParams.contains(paramName)) {
        return mergedParams[paramName].toMap();
    }
    
    return QVariantMap();
}

// 获取通用参数
QVariantList FactorMetaService::getCommonParameters(factor::FactorType factorType)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_initialized) {
        INTERNAL_WARN_STREAM << "FactorMetaService not initialized";
        return QVariantList();
    }
    
    QVariantList commonParams;
    
    if (m_parameterMetaData.contains("commonParams")) {
        QVariantMap commonParamsMap = m_parameterMetaData["commonParams"].toMap();
        for (auto it = commonParamsMap.constBegin(); it != commonParamsMap.constEnd(); ++it) {
            QVariantMap paramDef = parseParameterDefinition(it.value().toMap());
            paramDef["name"] = it.key();
            commonParams.append(paramDef);
        }
    }
    
    return commonParams;
}

// 获取特定类型参数
QVariantList FactorMetaService::getSpecificParameters(factor::FactorType factorType)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_initialized) {
        INTERNAL_WARN_STREAM << "FactorMetaService not initialized";
        return QVariantList();
    }
    
    QVariantList specificParams;
    QString factorTypeId = factorTypeToString(factorType);
    
    if (m_parameterMetaData.contains("factorTypeSpecificParams")) {
        QVariantMap specificParamsMap = m_parameterMetaData["factorTypeSpecificParams"].toMap();
        if (specificParamsMap.contains(factorTypeId)) {
            QVariantMap typeParams = specificParamsMap[factorTypeId].toMap();
            if (typeParams.contains("params")) {
                QVariantMap paramsMap = typeParams["params"].toMap();
                for (auto it = paramsMap.constBegin(); it != paramsMap.constEnd(); ++it) {
                    QVariantMap paramDef = parseParameterDefinition(it.value().toMap());
                    paramDef["name"] = it.key();
                    specificParams.append(paramDef);
                }
            }
        }
    }
    
    return specificParams;
}

// 获取所有参数（通用+特定）
QVariantList FactorMetaService::getAllParameters(factor::FactorType factorType)
{
    QVariantList allParams;
    
    // 添加通用参数
    QVariantList commonParams = getCommonParameters(factorType);
    for (const QVariant& param : commonParams) {
        allParams.append(param);
    }
    
    // 添加特定参数
    QVariantList specificParams = getSpecificParameters(factorType);
    for (const QVariant& param : specificParams) {
        allParams.append(param);
    }
    
    return allParams;
}

// 获取默认参数值
QVariantMap FactorMetaService::getDefaultParameterValues(factor::FactorType factorType)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_initialized) {
        INTERNAL_WARN_STREAM << "FactorMetaService not initialized";
        return QVariantMap();
    }
    
    QVariantMap defaultValues;
    
    // 从UI配置中获取默认值
    if (m_parameterMetaData.contains("uiConfig") && 
        m_parameterMetaData["uiConfig"].toMap().contains("defaultValues")) {
        QVariantMap uiDefaults = m_parameterMetaData["uiConfig"].toMap()["defaultValues"].toMap();
        for (auto it = uiDefaults.constBegin(); it != uiDefaults.constEnd(); ++it) {
            defaultValues[it.key()] = it.value();
        }
    }
    
    return defaultValues;
}

// 获取单个参数的默认值
QVariant FactorMetaService::getDefaultParameterValue(const QString& paramName, factor::FactorType factorType)
{
    QVariantMap defaultValues = getDefaultParameterValues(factorType);
    if (defaultValues.contains(paramName)) {
        return defaultValues[paramName];
    }
    
    // 尝试从参数定义中获取默认值
    QVariantMap paramDef = getParameterDefinition(paramName, factorType);
    if (paramDef.contains("default")) {
        return paramDef["default"];
    }
    
    return QVariant();
}

// 验证单个参数
bool FactorMetaService::validateParameter(const QString& paramName, const QVariant& value, factor::FactorType factorType)
{
    QVariantMap paramDef = getParameterDefinition(paramName, factorType);
    if (paramDef.isEmpty()) {
        return false;
    }
    
    QString type = paramDef["type"].toString();
    
    if (type == "integer") {
        return validateIntegerParameter(value, paramDef);
    } else if (type == "float") {
        return validateFloatParameter(value, paramDef);
    } else if (type == "boolean") {
        return validateBooleanParameter(value, paramDef);
    } else if (type == "enum") {
        return validateEnumParameter(value, paramDef);
    } else if (type == "string") {
        return validateStringParameter(value, paramDef);
    }
    
    return false;
}

// 验证所有参数
QString FactorMetaService::validateAllParameters(const QVariantMap& parameters, factor::FactorType factorType)
{
    QStringList validationErrors;
    
    for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
        if (!validateParameter(it.key(), it.value(), factorType)) {
            QVariantMap paramDef = getParameterDefinition(it.key(), factorType);
            QString displayName = paramDef["displayName"].toString();
            validationErrors.append(QString("%1 参数值无效").arg(displayName));
        }
    }
    
    if (validationErrors.isEmpty()) {
        return "参数验证通过";
    } else {
        return validationErrors.join("; ");
    }
}

// 静态工具方法：因子类型转字符串
QString FactorMetaService::factorTypeToString(factor::FactorType type)
{
    return FACTOR_TYPE_TO_ID.value(type, "custom");
}

// 静态工具方法：字符串转因子类型
factor::FactorType FactorMetaService::stringToFactorType(const QString& typeStr)
{
    return ID_TO_FACTOR_TYPE.value(typeStr.trimmed().toLower(), factor::FactorType::UNKNOWN);
}

// 静态工具方法：因子类型转显示名称
QString FactorMetaService::factorTypeToDisplayName(factor::FactorType type)
{
    return FACTOR_TYPE_TO_DISPLAY_NAME.value(type, "自定义因子");
}

factor::FactorType FactorMetaService::variantToFactorType(const QVariant& factorType)
{
    bool ok = false;
    const int typeIndex = factorType.toInt(&ok);
    if (ok) {
        return factor::factorTypeFromIndex(typeIndex);
    }

    if (factorType.metaType().id() == qMetaTypeId<factor::FactorType>()) {
        return factorType.value<factor::FactorType>();
    }

    return factor::FactorType::UNKNOWN;
}

// 属性访问器
bool FactorMetaService::isInitialized() const
{
    QMutexLocker locker(const_cast<QMutex*>(&m_mutex));
    return m_initialized;
}

QVariantList FactorMetaService::factorCategories() const
{
    QMutexLocker locker(const_cast<QMutex*>(&m_mutex));
    
    QVariantList categories;
    for (auto it = m_factorCategories.constBegin(); it != m_factorCategories.constEnd(); ++it) {
        categories.append(it.value());
    }
    
    return categories;
}

QVariantMap FactorMetaService::parameterTypes() const
{
    QMutexLocker locker(const_cast<QMutex*>(&m_mutex));
    
    if (m_parameterMetaData.contains("parameterTypes")) {
        return m_parameterMetaData["parameterTypes"].toMap();
    }
    
    return QVariantMap();
}

// 私有方法实现
bool FactorMetaService::loadMetaData()
{
    const bool commonOk = loadCommonMetaData();
    if (!commonOk) {
        INTERNAL_WARN_STREAM << "Failed to load common metadata JSON, using static catalog fallback";
    }

    const bool paramsOk = loadParameterMetaData();
    if (!paramsOk) {
        INTERNAL_WARN_STREAM << "Failed to load parameter metadata JSON, using static catalog fallback";
    }

    // 初始化因子分类缓存 — JSON 优先
    if (m_commonMetaData.contains("categories")) {
        QVariantList categories = m_commonMetaData["categories"].toList();
        for (const QVariant& category : categories) {
            QVariantMap categoryMap = category.toMap();
            QString categoryId = categoryMap["id"].toString();
            factor::FactorType factorType = stringToFactorType(categoryId);
            m_factorCategories[factorType] = categoryMap;
        }
    }

    // 静态目录兜底：JSON 缺失时从 C++ 静态数据填充分类信息
    if (m_factorCategories.isEmpty()) {
        const auto& uiCatalog = factorUiMetaCatalog();
        for (auto it = uiCatalog.constBegin(); it != uiCatalog.constEnd(); ++it) {
            m_factorCategories.insert(it.key(), it.value());
        }
    }

    return true; // 因子数据在数据库，JSON 仅用于 UI 装饰，缺失不阻塞初始化
}

bool FactorMetaService::loadCommonMetaData()
{
    QString filePath = getConfigFilePath("config/views/factor_common.json");
    QFile file(filePath);
    
    if (!file.exists()) {
        INTERNAL_WARN_STREAM << "Common metadata file not found: " << toStdString(filePath);
        return false;
    }
    
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        INTERNAL_WARN_STREAM << "Failed to open common metadata file: " << toStdString(filePath);
        return false;
    }
    
    QByteArray jsonData = file.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (doc.isNull()) {
        INTERNAL_WARN_STREAM << "Failed to parse common metadata JSON";
        return false;
    }
    
    m_commonMetaData = doc.object().toVariantMap();
    INTERNAL_DEBUG_STREAM << "Loaded common metadata from: " << toStdString(filePath);
    return true;
}

bool FactorMetaService::loadParameterMetaData()
{
    QString filePath = getConfigFilePath("config/views/factor_common_params.json");
    QFile file(filePath);
    
    if (!file.exists()) {
        INTERNAL_WARN_STREAM << "Parameter metadata file not found: " << toStdString(filePath);
        return false;
    }
    
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        INTERNAL_WARN_STREAM << "Failed to open parameter metadata file: " << toStdString(filePath);
        return false;
    }
    
    QByteArray jsonData = file.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (doc.isNull()) {
        INTERNAL_WARN_STREAM << "Failed to parse parameter metadata JSON";
        return false;
    }
    
    m_parameterMetaData = doc.object().toVariantMap();
    INTERNAL_DEBUG_STREAM << "Loaded parameter metadata from: " << toStdString(filePath);
    return true;
}

QVariantMap FactorMetaService::mergeCommonAndSpecificParams(factor::FactorType factorType)
{
    QVariantMap mergedParams;
    
    // 添加通用参数
    QVariantList commonParams = getCommonParameters(factorType);
    for (const QVariant& param : commonParams) {
        QVariantMap paramMap = param.toMap();
        QString paramName = paramMap["name"].toString();
        mergedParams[paramName] = paramMap;
    }
    
    // 添加特定参数
    QVariantList specificParams = getSpecificParameters(factorType);
    for (const QVariant& param : specificParams) {
        QVariantMap paramMap = param.toMap();
        QString paramName = paramMap["name"].toString();
        mergedParams[paramName] = paramMap;
    }
    
    return mergedParams;
}

QVariantMap FactorMetaService::parseParameterDefinition(const QVariantMap& paramDef)
{
    QVariantMap parsedDef = paramDef;
    
    // 确保有必要的字段
    if (!parsedDef.contains("displayName")) {
        parsedDef["displayName"] = parsedDef["name"];
    }
    
    if (!parsedDef.contains("description")) {
        parsedDef["description"] = "";
    }
    
    if (!parsedDef.contains("type")) {
        parsedDef["type"] = "string";
    }
    
    return parsedDef;
}

bool FactorMetaService::validateIntegerParameter(const QVariant& value, const QVariantMap& paramDef)
{
    bool ok;
    int intValue = value.toInt(&ok);
    if (!ok) return false;
    
    if (paramDef.contains("minValue") && intValue < paramDef["minValue"].toInt()) return false;
    if (paramDef.contains("maxValue") && intValue > paramDef["maxValue"].toInt()) return false;
    
    return true;
}

bool FactorMetaService::validateFloatParameter(const QVariant& value, const QVariantMap& paramDef)
{
    bool ok;
    double doubleValue = value.toDouble(&ok);
    if (!ok) return false;
    
    if (paramDef.contains("minValue") && doubleValue < paramDef["minValue"].toDouble()) return false;
    if (paramDef.contains("maxValue") && doubleValue > paramDef["maxValue"].toDouble()) return false;
    
    return true;
}

bool FactorMetaService::validateBooleanParameter(const QVariant& value, const QVariantMap& paramDef)
{
    return value.canConvert<bool>();
}

bool FactorMetaService::validateEnumParameter(const QVariant& value, const QVariantMap& paramDef)
{
    if (!value.canConvert<QString>()) return false;
    
    if (paramDef.contains("options")) {
        QVariantList options = paramDef["options"].toList();
        QString strValue = value.toString();
        for (const QVariant& option : options) {
            QVariantMap optionMap = option.toMap();
            if (optionMap["value"].toString() == strValue) {
                return true;
            }
        }
        return false;
    }
    
    return true;
}

bool FactorMetaService::validateStringParameter(const QVariant& value, const QVariantMap& paramDef)
{
    if (!value.canConvert<QString>()) return false;
    
    QString strValue = value.toString();
    if (paramDef.contains("maxLength") && strValue.length() > paramDef["maxLength"].toInt()) {
        return false;
    }
    
    return true;
}

QString FactorMetaService::getConfigFilePath(const QString& relativePath)
{
    // 1) 应用程序目录优先（bin/Release/ 或 bin/Debug/）
    const QString appDir = QCoreApplication::applicationDirPath();
    QString filePath = QDir::cleanPath(appDir + "/" + relativePath);
    if (QFile::exists(filePath)) {
        return filePath;
    }

    // 2) 当前工作目录
    const QString currentDir = QDir::currentPath();
    filePath = QDir::cleanPath(currentDir + "/" + relativePath);
    if (QFile::exists(filePath)) {
        return filePath;
    }

    // 3) 项目根目录回退：bin/{Release,Debug} → 往上 2 层
    const QString projectRoot = QDir::cleanPath(appDir + "/../..");
    filePath = QDir::cleanPath(projectRoot + "/" + relativePath);
    if (QFile::exists(filePath)) {
        return filePath;
    }

    // 返回原始路径（Qt 资源系统兜底）
    return relativePath;
}

void FactorMetaService::updateError(const QString& error)
{
    m_lastError = error;
    INTERNAL_WARN_STREAM << "FactorMetaService error: " << toStdString(error);
    emit errorOccurred(error);
}
