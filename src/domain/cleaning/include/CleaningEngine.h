// CleaningEngine.h — 纯 C++ 数据清洗引擎（零 Qt 依赖）
// 使用 foundation::json::JsonFacade，与 bin 缓存一致
// 支持取消操作、进度回调、规则统计
#pragma once
#include "ICleaningRule.h"
#include "DataFieldKeys.h"
#include "foundation/json/json_facade.h"
#include <algorithm>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstdio>

namespace cleaning {

using J = foundation::json::JsonFacade;

class CleaningEngine {
public:
    using ProgressCallback = std::function<void(int current, int total, const std::string& stage)>;

    CleaningEngine() = default;
    ~CleaningEngine() = default;

    /// @brief 注册规则（按 executionOrder 自动排序）
    void addRule(std::unique_ptr<ICleaningRule> rule) {
        if (!rule) return;
        m_rules.push_back(std::move(rule));
        std::sort(m_rules.begin(), m_rules.end(),
            [](const auto& a, const auto& b) { return a->executionOrder() < b->executionOrder(); });
    }

    void setOnProgress(ProgressCallback cb) { m_onProgress = std::move(cb); }

    /// @brief 请求取消（异步安全，下次检查点生效）
    void requestCancel() { m_cancelled.store(true, std::memory_order_relaxed); }

    /// @brief 是否已取消
    bool isCancelled() const { return m_cancelled.load(std::memory_order_relaxed); }

    /// @brief 执行清洗
    std::vector<foundation::json::JsonFacade> clean(std::vector<foundation::json::JsonFacade> data) {
        // 重置取消标志
        m_cancelled.store(false, std::memory_order_relaxed);

        CleaningStats& s = m_lastStats;
        s = CleaningStats{};
        s.totalRecords = static_cast<int>(data.size());

        // 初始化规则统计
        s.ruleStats.clear();
        for (const auto& rule : m_rules)
            s.ruleStats.push_back({rule->ruleName(), 0, 0});

        if (m_onProgress) m_onProgress(0, s.totalRecords, "start");

        if (m_onProgress) m_onProgress(0, s.totalRecords, "sorting");

        // 0. 按 symbol 分组、组内按 trade_date 升序排序（预提取键，避免 O(NlogN) 次 JSON 访问）
        std::vector<std::pair<std::string, std::string>> keys(data.size());
        for (size_t i = 0; i < data.size(); ++i) {
            keys[i] = {
                data[i].has(CF::SYMBOL.c_str()) ? data[i].get(CF::SYMBOL.c_str()).asString() : "",
                data[i].has(CF::TRADE_DATE.c_str()) ? data[i].get(CF::TRADE_DATE.c_str()).asString() : ""
            };
            if (m_onProgress && i % 200000 == 0)
                m_onProgress(static_cast<int>(i), s.totalRecords, "sorting");
        }
        std::vector<size_t> idx(data.size());
        for (size_t i = 0; i < data.size(); ++i) idx[i] = i;
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
            if (keys[a].first != keys[b].first) return keys[a].first < keys[b].first;
            return keys[a].second < keys[b].second;
        });
        std::vector<J> sorted; sorted.reserve(data.size());
        for (size_t i : idx) sorted.push_back(std::move(data[i]));
        data = std::move(sorted);
        keys.clear(); idx.clear();

        // 1. 预处理
        for (auto& rule : m_rules) {
            rule->cleanCrossSectional(data);
        }
        if (m_cancelled.load(std::memory_order_relaxed)) { return {}; }
        if (m_onProgress) m_onProgress(s.totalRecords / 10, s.totalRecords, "pre_clean");

        // 2. 逐行过滤
        std::vector<foundation::json::JsonFacade> kept;
        kept.reserve(data.size());
        int processed = 0;
        for (auto& row : data) {
            // 每 1000 行检查取消标志
            if (processed > 0 && processed % 1000 == 0) {
                if (m_cancelled.load(std::memory_order_relaxed)) {
                    fprintf(stderr, "[CleaningEngine] cancelled after %d rows\n", processed);
                    fflush(stderr);
                    return {};
                }
            }

            bool keep = true;
            int ruleIdx = 0;
            for (auto& rule : m_rules) {
                if (!rule->appliesTo(row)) { ++ruleIdx; continue; }
                if (!rule->clean(row)) {
                    keep = false;
                    ++s.removedRecords;
                    if (ruleIdx < static_cast<int>(s.ruleStats.size())) ++s.ruleStats[ruleIdx].removed;
                    break;
                } else {
                    if (ruleIdx < static_cast<int>(s.ruleStats.size())) ++s.ruleStats[ruleIdx].passed;
                }
                ++ruleIdx;
            }
            if (keep) {
                kept.push_back(std::move(row));
                ++s.keptRecords;
            }
            ++processed;
            if (m_onProgress && processed % 1000 == 0) {
                m_onProgress(processed, s.totalRecords, "filter");
            }
        }
        if (m_onProgress) m_onProgress(s.totalRecords, s.totalRecords, "post_clean");

        // 3. 后处理
        for (auto& rule : m_rules) {
            rule->postCrossSectional(kept);
        }

        // 4. 清理残留字段
        stripInternalFields(kept);

        if (m_onProgress) m_onProgress(s.totalRecords, s.totalRecords, "done");

        fprintf(stderr, "[CleaningEngine] %d -> %d rows (%d removed)\n",
                s.totalRecords, s.keptRecords, s.removedRecords);
        fflush(stderr);
        return kept;
    }

    const CleaningStats& lastStats() const { return m_lastStats; }

private:
    void stripInternalFields(std::vector<foundation::json::JsonFacade>& rows) {
        for (auto& row : rows) {
            if (!row.isObject()) continue;
            // 使用 remove 彻底删除内部字段（而非 set null 保留占位）
            static const char* internalKeys[] = {
                IF::ADJUSTED_PRICE_APPLIED.c_str(),
                IF::DATA_TYPE.c_str(),
                IF::VALUATION_INVALID_FIELDS.c_str(),
                IF::CLEANING_TAGS.c_str(),
                "adj_factor", "amount", "date", "tradeDate", "industry", "turnover_amount"
            };
            for (auto* key : internalKeys) {
                if (row.has(key)) row.remove(key);
            }
        }
    }

    std::vector<std::unique_ptr<ICleaningRule>> m_rules;
    CleaningStats m_lastStats;
    ProgressCallback m_onProgress;
    std::atomic<bool> m_cancelled{false};
};

} // namespace cleaning
