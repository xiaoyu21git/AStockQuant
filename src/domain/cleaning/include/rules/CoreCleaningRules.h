// CoreCleaningRules.h — 纯 C++ 清洗规则集（零 Qt 依赖）
// 使用 foundation::json::JsonFacade，字段名使用 DataFieldKeys 常量
#pragma once
#include "ICleaningRule.h"
#include "DataFieldKeys.h"
#include "LightRow.h"
#include "foundation/json/json_facade.h"  // JCfg::parse 用
#include <algorithm>
#include <cmath>
#include <functional>
#include <ankerl/unordered_dense.h>

namespace cleaning {

using J = LightRow;
using JCfg = foundation::json::JsonFacade;  // 仅构造用

namespace detail {
// 辅助函数
inline bool isMissing(J& row, const FieldKey& field) {
    if (!row.has(field.c_str())) return true;
    auto v = row.get(field.c_str());
    if (v.isNumber()) return false;
    if (v.isString()) return v.asString().empty();
    return true;
}

inline double safeDouble(J& row, const FieldKey& field, double def = 0.0) {
    if (!row.has(field.c_str())) return def;
    auto v = row.get(field.c_str());
    if (v.isNumber()) return v.asDouble();
    try { return std::stod(v.asString()); } catch(...) { return def; }
}

inline int safeInt(J& row, const FieldKey& field, int def = 0) {
    if (!row.has(field.c_str())) return def;
    auto v = row.get(field.c_str());
    if (v.isNumber()) return v.asInt();
    try { return std::stoi(v.asString()); } catch(...) { return def; }
}
} // namespace detail

// ════════════════════════════════════════════════════════════════════
// CompletenessRule — 确保 symbol 和 trade_date 非空
// ════════════════════════════════════════════════════════════════════
class CompletenessRule final : public ICleaningRule {
public:
    const char* ruleName() const override { return "completeness"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::Completeness); }

    bool clean(J& row) override {
        if (!row.has(CF::SYMBOL.c_str()) || !row.has(CF::TRADE_DATE.c_str())) return false;
        auto sym = row.get(CF::SYMBOL.c_str());
        auto dt = row.get(CF::TRADE_DATE.c_str());
        if (!sym.isString() || sym.asString().empty()) return false;
        if (!dt.isString() || dt.asString().empty()) return false;
        return true;
    }
};

// ════════════════════════════════════════════════════════════════════
// DuplicateRemovalRule — 按 keyFields 去重（保留首次出现）
// ════════════════════════════════════════════════════════════════════
class DuplicateRemovalRule final : public ICleaningRule {
public:
    explicit DuplicateRemovalRule(const std::string& configJson = {}) {
        m_keyFields = {CF::SYMBOL.c_str(), CF::TRADE_DATE.c_str()}; // 默认
        if (!configJson.empty()) {
            auto cfg = JCfg::parse(configJson);
            if (cfg.has("keyFields") && cfg.get("keyFields").isArray()) {
                m_keyFields.clear();
                auto arr = cfg.get("keyFields");
                for (size_t i = 0; i < arr.size(); ++i)
                    m_keyFields.push_back(arr.at(i).asString());
            }
        }
    }

    const char* ruleName() const override { return "duplicateRemoval"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::DuplicateRemoval); }

    void cleanCrossSectional(std::vector<J>& rows) override { m_seen.clear(); }

    std::string validateConfig() const override {
        if (m_keyFields.empty()) return "keyFields must not be empty";
        return "";
    }

    bool clean(J& row) override {
        if (m_keyFields.empty()) return true;
        std::string key;
        for (const auto& f : m_keyFields) {
            if (row.has(f.c_str())) {
                key += f + "=" + row.get(f.c_str()).asString() + ";";
            } else {
                key += f + "=\x01;";  // 缺失键用 SOH 标记，与空值区分
            }
        }
        if (m_seen.count(key)) return false;
        m_seen.insert(key);
        return true;
    }
private:
    std::vector<std::string> m_keyFields;
    ankerl::unordered_dense::set<std::string> m_seen;
};

// ════════════════════════════════════════════════════════════════════
// FinancialDateValidityRule — 清理财务日期（trim, 格式检查）
// ════════════════════════════════════════════════════════════════════
class FinancialDateValidityRule final : public ICleaningRule {
public:
    const char* ruleName() const override { return "financialDateValidity"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::FinancialDateValidity); }

    bool appliesTo(const J& row) const override {
        return row.has(FF::REPORT_DATE.c_str()) || row.has(FF::DISCLOSURE_DATE.c_str());
    }

    bool clean(J& row) override {
        // 清理 report_date
        if (row.has(FF::REPORT_DATE.c_str())) {
            auto v = row.get(FF::REPORT_DATE.c_str());
            if (v.isString()) {
                std::string d = v.asString();
                // trim
                d.erase(0, d.find_first_not_of(" \t\r\n"));
                d.erase(d.find_last_not_of(" \t\r\n") + 1);
                if (d.empty() || d == "null" || d == "NULL") {
                    row.set(FF::REPORT_DATE.c_str(), LightRow::createNull());
                } else if (d.size() >= 10) {
                    row.set(FF::REPORT_DATE.c_str(), LightRow::createString(d.substr(0, 10)));
                } else {
                    row.set(FF::REPORT_DATE.c_str(), LightRow::createNull());  // 短日期无效
                }
            }
        }
        // 清理 disclosure_date
        if (row.has(FF::DISCLOSURE_DATE.c_str())) {
            auto v = row.get(FF::DISCLOSURE_DATE.c_str());
            if (v.isString()) {
                std::string d = v.asString();
                d.erase(0, d.find_first_not_of(" \t\r\n"));
                d.erase(d.find_last_not_of(" \t\r\n") + 1);
                if (d.empty() || d == "null" || d == "NULL") {
                    row.set(FF::DISCLOSURE_DATE.c_str(), LightRow::createNull());
                } else if (d.size() >= 10) {
                    row.set(FF::DISCLOSURE_DATE.c_str(), LightRow::createString(d.substr(0, 10)));
                } else {
                    row.set(FF::DISCLOSURE_DATE.c_str(), LightRow::createNull());  // 短日期无效
                }
            }
        }
        return true;
    }
};

// ════════════════════════════════════════════════════════════════════
// FinancialMetricSanitizeRule — 清除无效财务值（区分三种策略）
//   sanitizeFinite:    仅清 NaN/Inf，保留负值（净利润、营收、现金流等可为负）
//   sanitizePositive:  清 NaN/Inf 及 ≤0（总资产、流动/速动比率必须 > 0）
//   sanitizeNonNegative: 清 NaN/Inf 及 <0（总负债可以为 0 但不能为负）
//   同时校验 quick_ratio <= current_ratio（流动性约束）
// ════════════════════════════════════════════════════════════════════
class FinancialMetricSanitizeRule final : public ICleaningRule {
public:
    const char* ruleName() const override { return "financialMetricSanitize"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::FinancialMetricSanitize); }

    bool appliesTo(const J& row) const override {
        return row.has(FF::REPORT_DATE.c_str()) || row.has(FF::EPS.c_str());
    }

    bool clean(J& row) override {
        // sanitizeFinite: 仅清 NaN/Inf，保留负值 — 适用于可正可负的财务指标
        static const FieldKey* finiteFields[] = {
            &FF::EPS, &FF::BPS, &FF::ROE, &FF::ROA, &FF::NET_PROFIT,
            &FF::TOTAL_REVENUE, &FF::EQUITY, &FF::PROFIT_MARGIN,
            &FF::GROSS_MARGIN, &FF::OPERATING_MARGIN,
            &FF::DEBT_TO_EQUITY, &FF::DIVIDEND_YIELD, &FF::PAYOUT_RATIO,
            &FF::OPERATING_CASH_FLOW, &FF::INVESTING_CASH_FLOW, &FF::FINANCING_CASH_FLOW
        };
        for (auto* f : finiteFields) {
            sanitizeFinite(row, *f);
        }

        // sanitizePositive: 清 NaN/Inf 或 ≤0 — 这些字段必须严格为正
        static const FieldKey* positiveFields[] = {
            &FF::TOTAL_ASSETS, &FF::CURRENT_RATIO, &FF::QUICK_RATIO
        };
        for (auto* f : positiveFields) {
            sanitizePositive(row, *f);
        }

        // sanitizeNonNegative: 清 NaN/Inf 或 <0 — 可以为 0 但不能为负
        static const FieldKey* nonNegFields[] = {
            &FF::TOTAL_LIABILITIES
        };
        for (auto* f : nonNegFields) {
            sanitizeNonNegative(row, *f);
        }

        // quick_ratio > current_ratio 一致性校验：速动比率不应超过流动比率
        auto cr = parseFinite(row, FF::CURRENT_RATIO);
        auto qr = parseFinite(row, FF::QUICK_RATIO);
        if (cr && qr && *qr > *cr) {
            row.set(FF::QUICK_RATIO.c_str(), LightRow::createNull());
        }

        return true;
    }

private:
    /// @brief 提取有限数值，非数字或非有限返回 nullopt
    static std::optional<double> parseFinite(J& row, const FieldKey& f) {
        if (!row.has(f.c_str())) return std::nullopt;
        auto v = row.get(f.c_str());
        if (v.isNull()) return std::nullopt;
        if (!v.isNumber()) return std::nullopt;
        double d = v.asDouble();
        if (!std::isfinite(d)) return std::nullopt;
        return d;
    }

    /// @brief 仅清除非有限值（NaN / Inf / -Inf），保留负数
    static void sanitizeFinite(J& row, const FieldKey& f) {
        if (!row.has(f.c_str())) return;
        auto v = row.get(f.c_str());
        if (v.isNull()) return;
        if (v.isNumber()) {
            double d = v.asDouble();
            if (!std::isfinite(d)) {
                row.set(f.c_str(), LightRow::createNull());
            }
        }
    }

    /// @brief 清除非有限值及 ≤0 的值
    static void sanitizePositive(J& row, const FieldKey& f) {
        if (!row.has(f.c_str())) return;
        auto v = row.get(f.c_str());
        if (v.isNull()) return;
        if (v.isNumber()) {
            double d = v.asDouble();
            if (!std::isfinite(d) || d <= 0.0) {
                row.set(f.c_str(), LightRow::createNull());
            }
        }
    }

    /// @brief 清除非有限值及 <0 的值（0 保留）
    static void sanitizeNonNegative(J& row, const FieldKey& f) {
        if (!row.has(f.c_str())) return;
        auto v = row.get(f.c_str());
        if (v.isNull()) return;
        if (v.isNumber()) {
            double d = v.asDouble();
            if (!std::isfinite(d) || d < 0.0) {
                row.set(f.c_str(), LightRow::createNull());
            }
        }
    }
};

// ════════════════════════════════════════════════════════════════════
// SuspensionFillRule — 停牌期间向前复权填充价格
// ════════════════════════════════════════════════════════════════════
class SuspensionFillRule final : public ICleaningRule {
public:
    explicit SuspensionFillRule(const std::string& configJson = {}) {
        m_maxForwardDays = 10;
        m_dropAfterMax = true;
        m_fillFields = {MF::OPEN.c_str(), MF::HIGH.c_str(), MF::LOW.c_str(), MF::CLOSE.c_str()};
        if (!configJson.empty()) {
            auto cfg = JCfg::parse(configJson);
            if (cfg.has("maxForwardFillDays")) m_maxForwardDays = cfg.get("maxForwardFillDays").asInt();
            if (cfg.has("dropAfterMaxDays")) m_dropAfterMax = cfg.get("dropAfterMaxDays").asBool();
            if (cfg.has("fillFields") && cfg.get("fillFields").isArray()) {
                m_fillFields.clear();
                auto arr = cfg.get("fillFields");
                for (size_t i = 0; i < arr.size(); ++i)
                    m_fillFields.push_back(arr.at(i).asString());
            }
        }
    }

    const char* ruleName() const override { return "suspensionFill"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::SuspensionFill); }

    bool appliesTo(const J& row) const override {
        return row.has(MF::VOLUME.c_str()) && row.has(CF::SYMBOL.c_str());
    }

    std::string validateConfig() const override {
        if (m_maxForwardDays <= 0) return "maxForwardFillDays must be > 0";
        if (m_fillFields.empty()) return "fillFields must not be empty";
        return "";
    }

    void cleanCrossSectional(std::vector<J>& rows) override {
        // 按 symbol 分组，记录最后有效值
        m_lastValid.clear();
        m_suspensionCount.clear();
    }

    bool clean(J& row) override {
        if (!row.has(CF::SYMBOL.c_str())) return true;
        std::string sym = row.get(CF::SYMBOL.c_str()).asString();
        double vol = detail::safeDouble(row, MF::VOLUME, -1.0);

        if (vol <= 0.0) {
            // 停牌或零成交量
            auto it = m_lastValid.find(sym);
            if (it != m_lastValid.end()) {
                int& count = m_suspensionCount[sym];
                ++count;
                if (m_dropAfterMax && count > m_maxForwardDays) return false;
                // 用上次有效值填充
                const auto& lastVals = it->second;
                row.set(TF::IS_SUSPENDED.c_str(), LightRow::createBool(true));
                row.set(TF::FORWARD_FILLED.c_str(), LightRow::createBool(true));
                for (const auto& f : m_fillFields) {
                    auto fi = lastVals.find(f);
                    if (fi != lastVals.end()) {
                        row.set(f.c_str(), LightRow::createDouble(fi->second));
                    }
                }
            }
        } else {
            m_suspensionCount[sym] = 0;
            // 更新最后有效值
            auto& vals = m_lastValid[sym];
            for (const auto& f : m_fillFields) {
                if (row.has(f.c_str())) {
                    auto v = row.get(f.c_str());
                    if (v.isNumber()) vals[f] = v.asDouble();
                }
            }
        }
        return true;
    }

private:
    int m_maxForwardDays;
    bool m_dropAfterMax;
    std::vector<std::string> m_fillFields;
    ankerl::unordered_dense::map<std::string, ankerl::unordered_dense::map<std::string, double>> m_lastValid;
    ankerl::unordered_dense::map<std::string, int> m_suspensionCount;
};

// ════════════════════════════════════════════════════════════════════
// MissingValueFillRule — 前向填充缺失字段
// ════════════════════════════════════════════════════════════════════
class MissingValueFillRule final : public ICleaningRule {
public:
    explicit MissingValueFillRule(const std::string& configJson = {}) {
        m_maxLookback = 5;
        m_fields = {MF::OPEN.c_str(), MF::HIGH.c_str(), MF::LOW.c_str(), MF::CLOSE.c_str(),
                    MF::TURNOVER_RATE.c_str(), MF::MARKET_CAP.c_str(), MF::CIRCULATING_MARKET_CAP.c_str()};
        if (!configJson.empty()) {
            auto cfg = JCfg::parse(configJson);
            if (cfg.has("maxLookbackDays")) m_maxLookback = cfg.get("maxLookbackDays").asInt();
            if (cfg.has("fields") && cfg.get("fields").isArray()) {
                m_fields.clear();
                auto arr = cfg.get("fields");
                for (size_t i = 0; i < arr.size(); ++i)
                    m_fields.push_back(arr.at(i).asString());
            }
        }
    }

    const char* ruleName() const override { return "missingValueFill"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::MissingValueFill); }

    void cleanCrossSectional(std::vector<J>& rows) override {
        m_lastKnown.clear();
    }

    bool clean(J& row) override {
        if (!row.has(CF::SYMBOL.c_str())) return true;
        std::string sym = row.get(CF::SYMBOL.c_str()).asString();
        auto& lastMap = m_lastKnown[sym];

        bool anyFilled = false;
        for (const auto& f : m_fields) {
            int& counter = m_missCount[sym][f];
            if (detail::isMissing(row, FieldKey(f.c_str()))) {
                ++counter;
                auto it = lastMap.find(f);
                if (it != lastMap.end() && counter <= m_maxLookback) {
                    row.set(f.c_str(), LightRow::createDouble(it->second));
                    anyFilled = true;
                }
            } else {
                counter = 0;
                double v = detail::safeDouble(row, FieldKey(f.c_str()), 0.0);
                lastMap[f] = v;
            }
        }
        if (anyFilled) {
            row.set(TF::MISSING_VALUE_FILLED.c_str(), LightRow::createBool(true));
        }
        return true;
    }

private:
    int m_maxLookback;
    std::vector<std::string> m_fields;
    ankerl::unordered_dense::map<std::string, ankerl::unordered_dense::map<std::string, double>> m_lastKnown;
    ankerl::unordered_dense::map<std::string, ankerl::unordered_dense::map<std::string, int>> m_missCount;
};

// ════════════════════════════════════════════════════════════════════
// AdjustedPriceRule — 价格复权
// ════════════════════════════════════════════════════════════════════
class AdjustedPriceRule final : public ICleaningRule {
public:
    const char* ruleName() const override { return "adjustedPrice"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::AdjustedPrice); }

    bool appliesTo(const J& row) const override {
        return row.has(MF::CLOSE.c_str());
    }

    bool clean(J& row) override {
        bool hasPref = row.has(MF::PRE_ADJ_FACTOR.c_str());
        bool hasPostf = row.has(MF::POST_ADJ_FACTOR.c_str());

        if (!hasPref && !hasPostf) {
            row.set(IF::ADJUSTED_PRICE_APPLIED.c_str(), LightRow::createBool(false));
            return true;
        }

        double pref = detail::safeDouble(row, MF::PRE_ADJ_FACTOR, 1.0);
        double postf = detail::safeDouble(row, MF::POST_ADJ_FACTOR, 1.0);

        if (pref > 0.0 && pref != 1.0) {
            for (const auto* f : {&MF::OPEN, &MF::HIGH, &MF::LOW, &MF::CLOSE}) {
                if (row.has(f->c_str())) {
                    double v = detail::safeDouble(row, *f, 0.0);
                    if (v > 0.0) row.set(f->c_str(), LightRow::createDouble(v * pref));
                }
            }
            row.set(IF::ADJUSTED_PRICE_APPLIED.c_str(), LightRow::createBool(true));
        } else if (postf > 0.0 && postf != 1.0) {
            for (const auto* f : {&MF::OPEN, &MF::HIGH, &MF::LOW, &MF::CLOSE}) {
                if (row.has(f->c_str())) {
                    double v = detail::safeDouble(row, *f, 0.0);
                    if (v > 0.0) row.set(f->c_str(), LightRow::createDouble(v * postf));
                }
            }
            row.set(IF::ADJUSTED_PRICE_APPLIED.c_str(), LightRow::createBool(true));
        }
        return true;
    }
};

// ════════════════════════════════════════════════════════════════════
// PriceValidityRule — 价格合理性校验
// ════════════════════════════════════════════════════════════════════
class PriceValidityRule final : public ICleaningRule {
public:
    explicit PriceValidityRule(const std::string& configJson = {}) {
        m_minPrice = 0.01; m_maxPrice = 10000.0; m_enforceChain = true;
        if (!configJson.empty()) {
            auto cfg = JCfg::parse(configJson);
            if (cfg.has("minPrice")) m_minPrice = cfg.get("minPrice").asDouble();
            if (cfg.has("maxPrice")) m_maxPrice = cfg.get("maxPrice").asDouble();
            if (cfg.has("enforceChain")) m_enforceChain = cfg.get("enforceChain").asBool();
        }
    }

    const char* ruleName() const override { return "priceValidity"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::PriceValidity); }

    bool appliesTo(const J& row) const override {
        return row.has(MF::OPEN.c_str()) || row.has(MF::CLOSE.c_str());
    }

    std::string validateConfig() const override {
        if (m_minPrice < 0) return "minPrice must be >= 0";
        if (m_maxPrice <= m_minPrice) return "maxPrice must be > minPrice";
        return "";
    }

    bool clean(J& row) override {
        double o = detail::safeDouble(row, MF::OPEN, -1.0);
        double h = detail::safeDouble(row, MF::HIGH, -1.0);
        double l = detail::safeDouble(row, MF::LOW, -1.0);
        double c = detail::safeDouble(row, MF::CLOSE, -1.0);

        if (o <= 0.0 || h <= 0.0 || l <= 0.0 || c <= 0.0) return false;
        if (o < m_minPrice || o > m_maxPrice) return false;
        if (h < m_minPrice || h > m_maxPrice) return false;
        if (l < m_minPrice || l > m_maxPrice) return false;
        if (c < m_minPrice || c > m_maxPrice) return false;

        if (m_enforceChain) {
            if (h < l) return false;
            if (c < l || c > h) return false;
            if (o < l || o > h) return false;
        }
        return true;
    }
private:
    double m_minPrice, m_maxPrice;
    bool m_enforceChain;
};

// ════════════════════════════════════════════════════════════════════
// VolumeFilterRule — 成交量范围过滤
// ════════════════════════════════════════════════════════════════════
class VolumeFilterRule final : public ICleaningRule {
public:
    explicit VolumeFilterRule(const std::string& configJson = {}) {
        m_minVolume = 0.0; m_maxVolume = 1e12; m_allowZeroWhenSuspended = true;
        if (!configJson.empty()) {
            auto cfg = JCfg::parse(configJson);
            if (cfg.has("minVolume")) m_minVolume = cfg.get("minVolume").asDouble();
            if (cfg.has("maxVolume")) m_maxVolume = cfg.get("maxVolume").asDouble();
            if (cfg.has("allowZeroWhenSuspended")) m_allowZeroWhenSuspended = cfg.get("allowZeroWhenSuspended").asBool();
        }
    }

    const char* ruleName() const override { return "volumeFilter"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::VolumeFilter); }

    bool appliesTo(const J& row) const override {
        return row.has(MF::VOLUME.c_str());
    }

    std::string validateConfig() const override {
        if (m_minVolume < 0) return "minVolume must be >= 0";
        if (m_maxVolume <= m_minVolume) return "maxVolume must be > minVolume";
        return "";
    }

    bool clean(J& row) override {
        double vol = detail::safeDouble(row, MF::VOLUME, -1.0);
        if (vol < 0.0) return false;
        if (vol == 0.0 && m_allowZeroWhenSuspended && row.has(TF::IS_SUSPENDED.c_str())) return true;
        if (vol < m_minVolume || vol > m_maxVolume) return false;
        return true;
    }
private:
    double m_minVolume, m_maxVolume;
    bool m_allowZeroWhenSuspended;
};

// ════════════════════════════════════════════════════════════════════
// LimitMoveTagRule — 涨跌停标记
// ════════════════════════════════════════════════════════════════════
class LimitMoveTagRule final : public ICleaningRule {
public:
    explicit LimitMoveTagRule(const std::string& configJson = {}) {
        m_upThreshold = 9.5; m_downThreshold = -9.5;
        if (!configJson.empty()) {
            auto cfg = JCfg::parse(configJson);
            if (cfg.has("upThreshold")) m_upThreshold = cfg.get("upThreshold").asDouble();
            if (cfg.has("downThreshold")) m_downThreshold = cfg.get("downThreshold").asDouble();
        }
    }

    const char* ruleName() const override { return "limitMoveTag"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::LimitMoveTag); }

    std::string validateConfig() const override {
        if (m_downThreshold > m_upThreshold)
            return "downThreshold must be <= upThreshold";
        return "";
    }

    bool appliesTo(const J& row) const override {
        return row.has(MF::CHANGE_PCT.c_str());
    }

    bool clean(J& row) override {
        double chg = 0.0;
        if (row.has(MF::CHANGE_PCT.c_str())) {
            auto v = row.get(MF::CHANGE_PCT.c_str());
            if (v.isNumber()) chg = v.asDouble();
            else if (v.isString()) try { chg = std::stod(v.asString()); } catch(...) {}
        }
        bool limitUp = chg >= m_upThreshold;
        bool limitDown = chg <= m_downThreshold;
        row.set(TF::LIMIT_UP.c_str(), LightRow::createBool(limitUp));
        row.set(TF::LIMIT_DOWN.c_str(), LightRow::createBool(limitDown));
        row.set(TF::CAN_BUY.c_str(), LightRow::createBool(!limitUp));
        row.set(TF::CAN_SELL.c_str(), LightRow::createBool(!limitDown));
        return true;
    }
private:
    double m_upThreshold, m_downThreshold;
};

// ════════════════════════════════════════════════════════════════════
// ValuationSanitizeRule — 估值指标清洗
// ════════════════════════════════════════════════════════════════════
class ValuationSanitizeRule final : public ICleaningRule {
public:
    const char* ruleName() const override { return "valuationSanitize"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::ValuationSanitize); }

    bool appliesTo(const J& row) const override {
        return row.has(MF::PE_RATIO.c_str()) || row.has(MF::PB_RATIO.c_str())
            || row.has(MF::MARKET_CAP.c_str());
    }

    bool clean(J& row) override {
        std::string invalidFields;
        auto check = [&](const FieldKey& f, bool cond) {
            if (row.has(f.c_str()) && cond) {
                row.set(f.c_str(), LightRow::createNull());
                if (!invalidFields.empty()) invalidFields += ",";
                invalidFields += f.c_str();
            }
        };

        // PE <= 0 无效
        double pe = detail::safeDouble(row, MF::PE_RATIO, -1.0);
        check(MF::PE_RATIO, pe <= 0.0 || !std::isfinite(pe));
        // PB <= 0 无效
        double pb = detail::safeDouble(row, MF::PB_RATIO, -1.0);
        check(MF::PB_RATIO, pb <= 0.0 || !std::isfinite(pb));
        // market_cap <= 0 无效
        double mc = detail::safeDouble(row, MF::MARKET_CAP, -1.0);
        check(MF::MARKET_CAP, mc <= 0.0 || !std::isfinite(mc));
        // circulating < total 异常
        double cmc = detail::safeDouble(row, MF::CIRCULATING_MARKET_CAP, -1.0);
        if (mc > 0 && cmc > mc) {
            row.set(MF::CIRCULATING_MARKET_CAP.c_str(), LightRow::createNull());
            if (!invalidFields.empty()) invalidFields += ",";
            invalidFields += MF::CIRCULATING_MARKET_CAP.c_str();
        }

        row.set(TF::VALUATION_SANITIZED.c_str(), LightRow::createBool(true));
        if (!invalidFields.empty()) {
            row.set(IF::VALUATION_INVALID_FIELDS.c_str(), LightRow::createString(invalidFields));
        }
        return true;
    }
};

// ════════════════════════════════════════════════════════════════════
// FieldStandardizationRule — 字段标准化（零 Qt，DB 部分可注入回调）
// ════════════════════════════════════════════════════════════════════
class FieldStandardizationRule final : public ICleaningRule {
public:
    /// @brief 可选回调：根据 symbol 补充元数据（name, exchange, asset_class, status, list_date, delist_date, industry_code）
    using SymbolInfoProvider = std::function<void(const std::string& symbol, J& outputRow)>;

    explicit FieldStandardizationRule(SymbolInfoProvider provider = nullptr)
        : m_provider(std::move(provider)) {}

    const char* ruleName() const override { return "fieldStandardization"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::FieldStandardization); }

    bool clean(J& row) override {
        // 1. 保留复权因子（值存入标准字段）
        for (const auto* f : {&MF::PRE_ADJ_FACTOR, &MF::POST_ADJ_FACTOR}) {
            if (row.has(f->c_str())) {
                auto v = row.get(f->c_str());
                if (v.isNumber()) {
                    double d = v.asDouble();
                    if (std::isfinite(d) && d > 0.0) {
                        row.set(f->c_str(), LightRow::createDouble(d));
                    } else {
                        row.set(f->c_str(), LightRow::createNull());
                    }
                }
            }
        }

        // 2. 重命名 effective_disclosure_date → disclosure_date
        if (row.has(F_F::EFFECTIVE_DISCLOSURE_DATE.c_str()) && !row.has(FF::DISCLOSURE_DATE.c_str())) {
            auto v = row.get(F_F::EFFECTIVE_DISCLOSURE_DATE.c_str());
            if (v.isString() && !v.asString().empty()) {
                row.set(FF::DISCLOSURE_DATE.c_str(), LightRow::createString(v.asString().substr(0, 10)));
            }
            row.set(F_F::EFFECTIVE_DISCLOSURE_DATE.c_str(), LightRow::createNull());
        }

        // 3. 格式化日期字段（去掉时间后缀）
        auto fixDate = [&](const FieldKey& f) {
            if (row.has(f.c_str())) {
                auto v = row.get(f.c_str());
                if (v.isString()) {
                    std::string s = v.asString();
                    auto sp = s.find(' ');
                    if (sp != std::string::npos) {
                        row.set(f.c_str(), LightRow::createString(s.substr(0, sp)));
                    }
                }
            }
        };
        fixDate(CF::TRADE_DATE);
        fixDate(FF::REPORT_DATE);
        fixDate(FF::DISCLOSURE_DATE);

        // 4. 标准化 data_source / data_type
        if (row.has("source")) {
            auto v = row.get("source");
            if (v.isString() && !v.asString().empty()) {
                if (!row.has(CF::DATA_SOURCE.c_str()))
                    row.set(CF::DATA_SOURCE.c_str(), LightRow::createString(v.asString()));
            }
            row.set("source", LightRow::createNull());
        }
        if (row.has("dataType")) {
            auto v = row.get("dataType");
            if (v.isString() && !v.asString().empty()) {
                if (!row.has(IF::DATA_TYPE.c_str()))
                    row.set(IF::DATA_TYPE.c_str(), LightRow::createString(v.asString()));
            }
            row.set("dataType", LightRow::createNull());
        }

        // 5. 删除内部字段
        for (const char* f : {"id","created_at","updated_at","symbol_id","indicator_id"}) {
            row.setNull(f);
        }

        // 6. 补充元数据（可选回调，桥接层注入 DB 查询）
        if (m_provider && row.has(CF::SYMBOL.c_str())) {
            auto sym = row.get(CF::SYMBOL.c_str());
            if (sym.isString()) m_provider(sym.asString(), row);
        }

        return true;
    }

private:
    SymbolInfoProvider m_provider;
};

// ════════════════════════════════════════════════════════════════════
// ReportDateAlignmentRule — 财报日期对齐到披露后首个交易日
// 纯 C++ 层不依赖 SQL，通过回调注入下一交易日查询逻辑
// ════════════════════════════════════════════════════════════════════
class ReportDateAlignmentRule final : public ICleaningRule {
public:
    /// @brief 回调：给定日期，返回下一个交易日（YYYY-MM-DD）
    /// 未注入时，fallback 使用 disclosure_date 或 report_date 自身
    using NextTradingDayFn = std::function<std::string(const std::string& date)>;

    explicit ReportDateAlignmentRule(NextTradingDayFn fn = nullptr)
        : m_nextTradingDay(std::move(fn)) {}

    const char* ruleName() const override { return "reportDateAlignment"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::ReportDateAlignment); }

    bool appliesTo(const J& row) const override {
        return row.has(FF::REPORT_DATE.c_str());
    }

    bool clean(J& row) override {
        if (!row.has(FF::REPORT_DATE.c_str())) return true;

        std::string rd = row.get(FF::REPORT_DATE.c_str()).asString();
        if (rd.size() < 10) return true;

        std::string effectiveDate;
        if (row.has(FF::DISCLOSURE_DATE.c_str())) {
            std::string dd = row.get(FF::DISCLOSURE_DATE.c_str()).asString();
            if (dd.size() >= 10) {
                if (m_nextTradingDay) {
                    effectiveDate = m_nextTradingDay(dd.substr(0, 10));
                } else {
                    effectiveDate = dd.substr(0, 10); // 无回调时直接使用披露日
                }
                row.set(CF::TRADE_DATE.c_str(), LightRow::createString(effectiveDate));
                row.set(TF::REPORT_DATE_ALIGNED.c_str(), LightRow::createBool(true));
                return true;
            }
        }

        // 无披露日时使用报告日本身作为交易日
        row.set(CF::TRADE_DATE.c_str(), LightRow::createString(rd.substr(0, 10)));
        row.set(TF::REPORT_DATE_ALIGNED.c_str(), LightRow::createBool(true));
        return true;
    }

private:
    NextTradingDayFn m_nextTradingDay;
};

// ════════════════════════════════════════════════════════════════════
// SurvivorBiasRule — 生存者偏差处理：剔除退市日之后的交易数据
// 使用 XF::DELIST_DATE 字段（"YYYY-MM-DD" 格式可直接字典序比较）
// ════════════════════════════════════════════════════════════════════
class SurvivorBiasRule final : public ICleaningRule {
public:
    const char* ruleName() const override { return "survivorBias"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::SurvivorBias); }

    bool appliesTo(const J& row) const override {
        return row.has(XF::DELIST_DATE.c_str());
    }

    bool clean(J& row) override {
        // 无退市日期字段则保留
        if (!row.has(XF::DELIST_DATE.c_str())) return true;
        std::string delist = row.get(XF::DELIST_DATE.c_str()).asString();
        // 退市日期为空则保留
        if (delist.empty()) return true;

        // 获取交易日期
        std::string tradeDate;
        if (row.has(CF::TRADE_DATE.c_str()))
            tradeDate = row.get(CF::TRADE_DATE.c_str()).asString();
        if (tradeDate.empty()) return true;

        // "YYYY-MM-DD" 格式可直接字典序比较
        // tradeDate > delistDate → 退市后数据，剔除
        if (tradeDate.size() >= 10 && delist.size() >= 10) {
            std::string td = tradeDate.substr(0, 10);
            std::string dd = delist.substr(0, 10);
            if (td > dd) return false;
        }

        row.set(TF::SURVIVOR_BIAS_CHECKED.c_str(), LightRow::createBool(true));
        return true;
    }
};

// ════════════════════════════════════════════════════════════════════
// STFilterRule — ST 股票剔除
// 检查 XF::STATUS 字段（"ST"/"*ST"）、XF::NAME 前缀、is_st 标记
// ════════════════════════════════════════════════════════════════════
class STFilterRule final : public ICleaningRule {
public:
    const char* ruleName() const override { return "stFilter"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::STFilter); }

    bool clean(J& row) override {
        // 检查 is_st 标记（外部导入数据可能有此字段；PG 标准查询无此列，此分支为兼容预留）
        if (row.has("is_st")) {
            auto v = row.get("is_st");
            if (v.isBool() && v.asBool()) return false;
            if (v.isNumber() && v.asInt() != 0) return false;
            if (v.isString()) {
                std::string s = v.asString();
                // trim
                s.erase(0, s.find_first_not_of(" \t\r\n"));
                s.erase(s.find_last_not_of(" \t\r\n") + 1);
                if (s == "1" || s == "true" || s == "True" || s == "TRUE" || s == "y" || s == "Y")
                    return false;
            }
        }

        // 检查 status 字段
        if (row.has(XF::STATUS.c_str())) {
            std::string s = row.get(XF::STATUS.c_str()).asString();
            if (s == "ST" || s == "*ST") return false;
        }

        // 检查 name 字段（名称以 ST 或 *ST 开头）
        if (row.has(XF::NAME.c_str())) {
            std::string n = row.get(XF::NAME.c_str()).asString();
            if (n.size() >= 2 && n.substr(0, 2) == "ST") return false;
            if (n.size() >= 3 && n.substr(0, 3) == "*ST") return false;
        }

        return true;
    }
};

// ════════════════════════════════════════════════════════════════════
// NewStockFilterRule — 新股过滤（上市不足 N 个交易日剔除）
// 纯 C++ 层不依赖 SQL，通过回调注入交易日计数逻辑
// ════════════════════════════════════════════════════════════════════
class NewStockFilterRule final : public ICleaningRule {
public:
    /// @brief 回调：计算 symbol 从上市日到指定交易日之间的交易天数
    /// 返回 < 0 表示查询失败，应保留（安全策略：不确定时不剔除）
    using TradeDayCountFn = std::function<int(const std::string& symbol,
                                              const std::string& listDate,
                                              const std::string& tradeDate)>;

    explicit NewStockFilterRule(int minTradeDays = 60, TradeDayCountFn fn = nullptr)
        : m_minTradeDays(minTradeDays)
        , m_countTradingDays(std::move(fn)) {}

    explicit NewStockFilterRule(const std::string& configJson, TradeDayCountFn fn = nullptr)
        : m_minTradeDays(60), m_countTradingDays(std::move(fn)) {
        if (!configJson.empty()) {
            auto cfg = JCfg::parse(configJson);
            if (cfg.has("minTradeDays")) m_minTradeDays = cfg.get("minTradeDays").asInt();
        }
    }

    const char* ruleName() const override { return "newStockFilter"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::NewStockFilter); }

    std::string validateConfig() const override {
        if (m_minTradeDays < 0) return "minTradeDays must be >= 0";
        return "";
    }

    bool appliesTo(const J& row) const override {
        return m_minTradeDays > 0 && row.has(XF::LIST_DATE.c_str());
    }

    bool clean(J& row) override {
        // minTradeDays=0 时不过滤
        if (m_minTradeDays <= 0) return true;

        // 获取上市日期
        if (!row.has(XF::LIST_DATE.c_str())) return true;
        std::string listDate = row.get(XF::LIST_DATE.c_str()).asString();
        if (listDate.empty()) return true;

        // 获取交易日期
        std::string tradeDate;
        if (row.has(CF::TRADE_DATE.c_str()))
            tradeDate = row.get(CF::TRADE_DATE.c_str()).asString();
        if (tradeDate.empty()) return true;

        // 截取前 10 字符用于比较
        if (listDate.size() >= 10) listDate = listDate.substr(0, 10);
        if (tradeDate.size() >= 10) tradeDate = tradeDate.substr(0, 10);

        // 交易日早于上市日，剔除
        if (tradeDate < listDate) return false;

        // 有回调时精确计算交易日数
        if (m_countTradingDays) {
            std::string sym;
            if (row.has(CF::SYMBOL.c_str()))
                sym = row.get(CF::SYMBOL.c_str()).asString();
            int days = m_countTradingDays(sym, listDate, tradeDate);
            if (days < 0) return true;   // 查询失败 → 保留（安全策略）
            if (days < m_minTradeDays) return false;
        }

        return true;
    }

private:
    int m_minTradeDays;
    TradeDayCountFn m_countTradingDays;
};

} // namespace cleaning
