// CoreCleaningRules.h — 纯 C++ 清洗规则集（零 Qt 依赖）
// 使用 foundation::json::JsonFacade，字段名使用 DataFieldKeys 常量
#pragma once
#include "ICleaningRule.h"
#include "DataFieldKeys.h"
#include "foundation/json/json_facade.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace cleaning {

using J = foundation::json::JsonFacade;

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
            auto cfg = J::parse(configJson);
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

    bool clean(J& row) override {
        if (m_keyFields.empty()) return true;
        std::string key;
        for (const auto& f : m_keyFields) {
            if (row.has(f)) key += f + "=" + row.get(f).asString() + ";";
        }
        if (m_seen.count(key)) return false;
        m_seen.insert(key);
        return true;
    }
private:
    std::vector<std::string> m_keyFields;
    std::unordered_set<std::string> m_seen;
};

// ════════════════════════════════════════════════════════════════════
// FinancialDateValidityRule — 清理财务日期（trim, 格式检查）
// ════════════════════════════════════════════════════════════════════
class FinancialDateValidityRule final : public ICleaningRule {
public:
    const char* ruleName() const override { return "financialDateValidity"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::FinancialDateValidity); }

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
                    row.set(FF::REPORT_DATE.c_str(), J::createNull());
                } else if (d.size() >= 10) {
                    row.set(FF::REPORT_DATE.c_str(), J::createString(d.substr(0, 10)));
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
                    row.set(FF::DISCLOSURE_DATE.c_str(), J::createNull());
                } else if (d.size() >= 10) {
                    row.set(FF::DISCLOSURE_DATE.c_str(), J::createString(d.substr(0, 10)));
                }
            }
        }
        return true;
    }
};

// ════════════════════════════════════════════════════════════════════
// FinancialMetricSanitizeRule — 清除无效财务值（NaN/Inf/负值）
// ════════════════════════════════════════════════════════════════════
class FinancialMetricSanitizeRule final : public ICleaningRule {
public:
    const char* ruleName() const override { return "financialMetricSanitize"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::FinancialMetricSanitize); }

    bool clean(J& row) override {
        static const FieldKey* finFields[] = {
            &FF::EPS, &FF::BPS, &FF::ROE, &FF::ROA, &FF::NET_PROFIT,
            &FF::TOTAL_REVENUE, &FF::TOTAL_ASSETS, &FF::TOTAL_LIABILITIES,
            &FF::EQUITY, &FF::PROFIT_MARGIN, &FF::GROSS_MARGIN,
            &FF::OPERATING_MARGIN, &FF::DEBT_TO_EQUITY, &FF::CURRENT_RATIO,
            &FF::QUICK_RATIO, &FF::DIVIDEND_YIELD, &FF::PAYOUT_RATIO
        };
        for (auto* f : finFields) {
            if (!row.has(f->c_str())) continue;
            auto v = row.get(f->c_str());
            if (v.isNumber()) {
                double d = v.asDouble();
                if (!std::isfinite(d) || std::isnan(d) || d < 0.0) {
                    row.set(f->c_str(), J::createNull());
                }
            }
        }
        return true;
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
            auto cfg = J::parse(configJson);
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
                row.set(TF::IS_SUSPENDED.c_str(), J::createBool(true));
                row.set(TF::FORWARD_FILLED.c_str(), J::createBool(true));
                for (const auto& f : m_fillFields) {
                    auto fi = lastVals.find(f);
                    if (fi != lastVals.end()) {
                        row.set(f, J::createDouble(fi->second));
                    }
                }
            }
        } else {
            m_suspensionCount[sym] = 0;
            // 更新最后有效值
            auto& vals = m_lastValid[sym];
            for (const auto& f : m_fillFields) {
                if (row.has(f)) {
                    auto v = row.get(f);
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
    std::unordered_map<std::string, std::unordered_map<std::string, double>> m_lastValid;
    std::unordered_map<std::string, int> m_suspensionCount;
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
            auto cfg = J::parse(configJson);
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
                    row.set(f, J::createDouble(it->second));
                    anyFilled = true;
                }
            } else {
                counter = 0;
                double v = detail::safeDouble(row, FieldKey(f.c_str()), 0.0);
                lastMap[f] = v;
            }
        }
        if (anyFilled) {
            row.set(TF::MISSING_VALUE_FILLED.c_str(), J::createBool(true));
        }
        return true;
    }

private:
    int m_maxLookback;
    std::vector<std::string> m_fields;
    std::unordered_map<std::string, std::unordered_map<std::string, double>> m_lastKnown;
    std::unordered_map<std::string, std::unordered_map<std::string, int>> m_missCount;
};

// ════════════════════════════════════════════════════════════════════
// AdjustedPriceRule — 价格复权
// ════════════════════════════════════════════════════════════════════
class AdjustedPriceRule final : public ICleaningRule {
public:
    const char* ruleName() const override { return "adjustedPrice"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::AdjustedPrice); }

    bool clean(J& row) override {
        double pref = detail::safeDouble(row, MF::PRE_ADJ_FACTOR, 1.0);
        double postf = detail::safeDouble(row, MF::POST_ADJ_FACTOR, 1.0);

        if (pref > 0.0 && pref != 1.0) {
            for (const auto* f : {&MF::OPEN, &MF::HIGH, &MF::LOW, &MF::CLOSE}) {
                if (row.has(f->c_str())) {
                    double v = detail::safeDouble(row, *f, 0.0);
                    if (v > 0.0) row.set(f->c_str(), J::createDouble(v * pref));
                }
            }
            row.set(IF::ADJUSTED_PRICE_APPLIED.c_str(), J::createBool(true));
        } else if (postf > 0.0 && postf != 1.0) {
            for (const auto* f : {&MF::OPEN, &MF::HIGH, &MF::LOW, &MF::CLOSE}) {
                if (row.has(f->c_str())) {
                    double v = detail::safeDouble(row, *f, 0.0);
                    if (v > 0.0) row.set(f->c_str(), J::createDouble(v * postf));
                }
            }
            row.set(IF::ADJUSTED_PRICE_APPLIED.c_str(), J::createBool(true));
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
            auto cfg = J::parse(configJson);
            if (cfg.has("minPrice")) m_minPrice = cfg.get("minPrice").asDouble();
            if (cfg.has("maxPrice")) m_maxPrice = cfg.get("maxPrice").asDouble();
            if (cfg.has("enforceChain")) m_enforceChain = cfg.get("enforceChain").asBool();
        }
    }

    const char* ruleName() const override { return "priceValidity"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::PriceValidity); }

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
            auto cfg = J::parse(configJson);
            if (cfg.has("minVolume")) m_minVolume = cfg.get("minVolume").asDouble();
            if (cfg.has("maxVolume")) m_maxVolume = cfg.get("maxVolume").asDouble();
            if (cfg.has("allowZeroWhenSuspended")) m_allowZeroWhenSuspended = cfg.get("allowZeroWhenSuspended").asBool();
        }
    }

    const char* ruleName() const override { return "volumeFilter"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::VolumeFilter); }

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
            auto cfg = J::parse(configJson);
            if (cfg.has("upThreshold")) m_upThreshold = cfg.get("upThreshold").asDouble();
            if (cfg.has("downThreshold")) m_downThreshold = cfg.get("downThreshold").asDouble();
        }
    }

    const char* ruleName() const override { return "limitMoveTag"; }
    uint8_t executionOrder() const override { return static_cast<uint8_t>(RuleExecutionOrder::LimitMoveTag); }

    bool clean(J& row) override {
        double chg = 0.0;
        if (row.has(MF::CHANGE_PCT.c_str())) {
            auto v = row.get(MF::CHANGE_PCT.c_str());
            if (v.isNumber()) chg = v.asDouble();
            else if (v.isString()) try { chg = std::stod(v.asString()); } catch(...) {}
        }
        bool limitUp = chg >= m_upThreshold;
        bool limitDown = chg <= m_downThreshold;
        row.set(TF::LIMIT_UP.c_str(), J::createBool(limitUp));
        row.set(TF::LIMIT_DOWN.c_str(), J::createBool(limitDown));
        row.set(TF::CAN_BUY.c_str(), J::createBool(!limitUp));
        row.set(TF::CAN_SELL.c_str(), J::createBool(!limitDown));
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

    bool clean(J& row) override {
        std::string invalidFields;
        auto check = [&](const FieldKey& f, bool cond) {
            if (row.has(f.c_str()) && cond) {
                row.set(f.c_str(), J::createNull());
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
            row.set(MF::CIRCULATING_MARKET_CAP.c_str(), J::createNull());
            if (!invalidFields.empty()) invalidFields += ",";
            invalidFields += MF::CIRCULATING_MARKET_CAP.c_str();
        }

        row.set(TF::VALUATION_SANITIZED.c_str(), J::createBool(true));
        if (!invalidFields.empty()) {
            row.set(IF::VALUATION_INVALID_FIELDS.c_str(), J::createString(invalidFields));
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
                        row.set(f->c_str(), J::createDouble(d));
                    } else {
                        row.set(f->c_str(), J::createNull());
                    }
                }
            }
        }

        // 2. 重命名 effective_disclosure_date → disclosure_date
        if (row.has("effective_disclosure_date") && !row.has(FF::DISCLOSURE_DATE.c_str())) {
            auto v = row.get("effective_disclosure_date");
            if (v.isString() && !v.asString().empty()) {
                row.set(FF::DISCLOSURE_DATE.c_str(), J::createString(v.asString().substr(0, 10)));
            }
            row.set("effective_disclosure_date", J::createNull());
        }

        // 3. 格式化日期字段（去掉时间后缀）
        auto fixDate = [&](const FieldKey& f) {
            if (row.has(f.c_str())) {
                auto v = row.get(f.c_str());
                if (v.isString()) {
                    std::string s = v.asString();
                    auto sp = s.find(' ');
                    if (sp != std::string::npos) {
                        row.set(f.c_str(), J::createString(s.substr(0, sp)));
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
                    row.set(CF::DATA_SOURCE.c_str(), J::createString(v.asString()));
            }
            row.set("source", J::createNull());
        }
        if (row.has("dataType")) {
            auto v = row.get("dataType");
            if (v.isString() && !v.asString().empty()) {
                if (!row.has(IF::DATA_TYPE.c_str()))
                    row.set(IF::DATA_TYPE.c_str(), J::createString(v.asString()));
            }
            row.set("dataType", J::createNull());
        }

        // 5. 删除内部字段
        for (const char* f : {"id","created_at","updated_at","symbol_id","indicator_id"}) {
            row.set(f, J::createNull());
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

} // namespace cleaning
