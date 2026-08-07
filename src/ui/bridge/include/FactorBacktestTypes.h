#pragma once
// FactorBacktestTypes.h — 因子回测结果 typed DTO (Phase 29)
//
// 将 processRunResult / buildCoreMetrics / buildRatingChecks 中散落的
// QVariantMap 裸字典组装替换为有类型约束的 struct, 仅在边界处调用 toMap()
// 转换为 QML 所需的 QVariantMap。
//
// 设计原则:
//   1. 所有字段与现有 QML 契约精确一致 — toMap() 的输出必须与旧内联代码逐字段相同
//   2. bridge 层可使用 Qt 类型 (QString/QVariantMap 等)
//   3. struct 仅作数据容器, 不含业务逻辑

#include <QString>
#include <QVariantMap>
#include <QVariantList>
#include <vector>
#include <cmath>

namespace factor::bridge {

// ── 单个指标项 ──
// 对应 QML 端 FactorMetricCard / MetricTile 的字段契约
struct MetricItem {
    QString key;
    QString title;
    QString subtitle;
    QString label;
    double  value        = 0.0;
    QString format;
    bool    emphasize    = false;
    QString tier;
    int     units        = 1;
    bool    available    = true;
    double  goodThreshold = 0.0;
    QString direction{QStringLiteral("high")};

    [[nodiscard]] QVariantMap toMap() const {
        QVariantMap m;
        m[QStringLiteral("key")]           = key;
        m[QStringLiteral("title")]         = title;
        m[QStringLiteral("subtitle")]      = available ? subtitle : QStringLiteral("不可用");
        m[QStringLiteral("label")]         = label.isEmpty() ? title : label;
        m[QStringLiteral("value")]         = available ? value : 0.0;
        m[QStringLiteral("format")]        = format;
        m[QStringLiteral("emphasize")]     = emphasize;
        m[QStringLiteral("tier")]          = tier;
        m[QStringLiteral("units")]         = units;
        m[QStringLiteral("available")]     = available;
        m[QStringLiteral("goodThreshold")] = goodThreshold;
        m[QStringLiteral("direction")]     = direction;
        return m;
    }
};

// ── 评级检查项 ──
struct RatingCheckItem {
    QString label;
    bool    passed = false;
    QString actualText;
    QString thresholdText;

    [[nodiscard]] QVariantMap toMap() const {
        QVariantMap c;
        c[QStringLiteral("label")]         = label;
        c[QStringLiteral("passed")]        = passed;
        c[QStringLiteral("actualText")]    = actualText;
        c[QStringLiteral("thresholdText")] = thresholdText;
        return c;
    }
};

// ── 分组收益柱状图 series 元素 ──
struct GroupBarItem {
    QString label;
    double  value = 0.0;

    [[nodiscard]] QVariantMap toMap() const {
        QVariantMap bar;
        bar[QStringLiteral("label")] = label;
        bar[QStringLiteral("value")] = value;
        return bar;
    }
};

// ── 因子质量报告 ──
// 对应 QML 端 AnalysisPage 的 factorQuality 字段
struct FactorQualityReport {
    // 指标分组
    std::vector<MetricItem> coreMetrics;
    std::vector<MetricItem> optionalMetrics;
    std::vector<MetricItem> auxiliaryMetrics;

    // 评级
    int     coreRating       = 1;
    QString coreRatingLabel;
    QString coreRatingTitle;
    QString coreRatingSummary;
    std::vector<RatingCheckItem> coreRatingChecks;

    // 图表
    struct GroupChart {
        QString title;
        QString subtitle;
        bool    isPercent = true;
        std::vector<GroupBarItem> bars;

        [[nodiscard]] QVariantMap toMap() const {
            QVariantMap chart;
            chart[QStringLiteral("title")]     = title;
            chart[QStringLiteral("subtitle")]  = subtitle;
            chart[QStringLiteral("isPercent")] = isPercent;
            QVariantList series;
            for (const auto& b : bars)
                series.append(b.toMap());
            chart[QStringLiteral("series")] = series;
            return chart;
        }
    };
    std::vector<GroupChart> groupCharts;

    // 收益序列
    QVariantList rawReturns;
    QVariantList costAdjustedReturns;
    QVariantList riskAdjustedReturns;

    // 分组收益序列
    struct GroupReturnSeries {
        int         groupIndex = 0;
        QString     groupName;
        QVariantList data;

        [[nodiscard]] QVariantMap toMap() const {
            QVariantMap m;
            m[QStringLiteral("groupIndex")] = groupIndex;
            m[QStringLiteral("groupName")]  = groupName;
            m[QStringLiteral("data")]       = data;
            return m;
        }
    };
    std::vector<GroupReturnSeries> groupReturnSeries;

    // Section 元信息
    QVariantMap coreSection;
    QVariantMap optionalSection;
    QVariantMap auxiliarySection;

    double numGroups = 5.0;

    /// @brief 转换为 QML 端期望的 factorQuality QVariantMap
    [[nodiscard]] QVariantMap toMap() const {
        QVariantMap fq;

        // 指标列表
        {
            QVariantList cm;
            for (const auto& m : coreMetrics) cm.append(m.toMap());
            fq[QStringLiteral("coreMetrics")] = cm;
        }
        {
            QVariantList om;
            for (const auto& m : optionalMetrics) om.append(m.toMap());
            fq[QStringLiteral("optionalMetrics")] = om;
        }
        {
            QVariantList am;
            for (const auto& m : auxiliaryMetrics) am.append(m.toMap());
            fq[QStringLiteral("auxiliaryMetrics")] = am;
        }

        // 评级
        fq[QStringLiteral("coreRating")]        = coreRating;
        fq[QStringLiteral("coreRatingLabel")]   = coreRatingLabel;
        fq[QStringLiteral("coreRatingTitle")]   = coreRatingTitle;
        fq[QStringLiteral("coreRatingSummary")] = coreRatingSummary;
        {
            QVariantList checks;
            for (const auto& c : coreRatingChecks) checks.append(c.toMap());
            fq[QStringLiteral("coreRatingChecks")] = checks;
        }

        // 图表
        {
            QVariantList charts;
            for (const auto& gc : groupCharts) charts.append(gc.toMap());
            fq[QStringLiteral("groupCharts")] = charts;
        }

        // 收益序列
        {
            QVariantMap rs;
            rs[QStringLiteral("rawReturns")]          = rawReturns;
            rs[QStringLiteral("costAdjustedReturns")] = costAdjustedReturns;
            rs[QStringLiteral("riskAdjustedReturns")] = riskAdjustedReturns;
            fq[QStringLiteral("returnSeries")] = rs;
        }

        // 分组收益序列
        {
            QVariantList grs;
            for (const auto& g : groupReturnSeries) grs.append(g.toMap());
            fq[QStringLiteral("groupReturnSeries")] = grs;
        }

        fq[QStringLiteral("numGroups")]         = numGroups;
        fq[QStringLiteral("coreSection")]       = coreSection;
        fq[QStringLiteral("optionalSection")]   = optionalSection;
        fq[QStringLiteral("auxiliarySection")]  = auxiliarySection;

        return fq;
    }
};

// ── 工厂函数: 创建可用指标 ──
inline MetricItem makeMetric(const QString& key, const QString& title,
                              const QString& subtitle, double val,
                              const QString& format, bool emphasize,
                              const QString& tier, int units = 1) {
    MetricItem m;
    m.key       = key;
    m.title     = title;
    m.subtitle  = subtitle;
    m.label     = title;
    m.value     = std::isfinite(val) ? val : 0.0;
    m.format    = format;
    m.emphasize = emphasize;
    m.tier      = tier;
    m.units     = units;
    m.available = std::isfinite(val);
    return m;
}

} // namespace factor::bridge
