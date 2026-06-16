// CleaningEngine.h — 纯 C++ 数据清洗引擎（零 Qt 依赖）
// 使用 foundation::json::JsonFacade，与 bin 缓存一致
#pragma once
#include "ICleaningRule.h"
#include "DataFieldKeys.h"
#include "foundation/json/json_facade.h"
#include <algorithm>
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

    /// @brief 执行清洗
    std::vector<foundation::json::JsonFacade> clean(std::vector<foundation::json::JsonFacade> data) {
        CleaningStats& s = m_lastStats;
        s = CleaningStats{};
        s.totalRecords = static_cast<int>(data.size());

        if (m_onProgress) m_onProgress(0, s.totalRecords, "start");

        // 1. 预处理
        for (auto& rule : m_rules) {
            rule->cleanCrossSectional(data);
        }
        if (m_onProgress) m_onProgress(s.totalRecords / 10, s.totalRecords, "pre_clean");

        // 2. 逐行过滤
        std::vector<foundation::json::JsonFacade> kept;
        kept.reserve(data.size());
        int processed = 0;
        for (auto& row : data) {
            bool keep = true;
            for (auto& rule : m_rules) {
                if (!rule->clean(row)) {
                    keep = false;
                    ++s.removedRecords;
                    break;
                }
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
            row.set(IF::ADJUSTED_PRICE_APPLIED.c_str(), J::createNull());
            row.set(IF::DATA_TYPE.c_str(), J::createNull());
            row.set(IF::VALUATION_INVALID_FIELDS.c_str(), J::createNull());
            row.set(IF::CLEANING_TAGS.c_str(), J::createNull());

            // 旧版字段
            row.set("adj_factor", J::createNull());
            row.set("amount", J::createNull());
            row.set("date", J::createNull());
            row.set("tradeDate", J::createNull());
            row.set("industry", J::createNull());
            row.set("turnover_amount", J::createNull());
        }
    }

    std::vector<std::unique_ptr<ICleaningRule>> m_rules;
    CleaningStats m_lastStats;
    ProgressCallback m_onProgress;
};

} // namespace cleaning
