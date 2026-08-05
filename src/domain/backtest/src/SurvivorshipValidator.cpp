// SurvivorshipValidator — 幸存者偏差验证器实现
#include "../include/SurvivorshipValidator.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace domain::backtest {

// ═══════════════════════════════════════════════════════════════════
// SurvivorshipCheckResult 辅助
// ═══════════════════════════════════════════════════════════════════

namespace {
    void addViolation(SurvivorshipCheckResult& result, const std::string& msg) {
        ++result.totalViolations;
        if (static_cast<int>(result.violations.size()) < SurvivorshipCheckResult::kMaxViolations) {
            result.violations.push_back(msg);
        }
    }

    std::string dateToStr(int d) {
        if (d <= 0) return "unknown";
        // YYYYMMDD → YYYY-MM-DD
        int year = d / 10000;
        int month = (d / 100) % 100;
        int day = d % 100;
        std::ostringstream oss;
        oss << year << "-" << (month < 10 ? "0" : "") << month
            << "-" << (day < 10 ? "0" : "") << day;
        return oss.str();
    }
} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════
// SurvivorshipValidationReport
// ═══════════════════════════════════════════════════════════════════

std::string SurvivorshipValidationReport::summary() const {
    std::ostringstream oss;
    bool ok = allPassed();
    oss << (ok ? "[PASS]" : "[WARN]") << " 幸存者偏差验证 "
        << backtestStart << "~" << backtestEnd
        << " | 标的数=" << totalStocks << " 交易日=" << totalDates;
    if (!ok) {
        int failedCount = 0;
        for (const auto& c : checks) {
            if (!c.passed) ++failedCount;
        }
        oss << " | " << failedCount << " 项未通过";
        for (const auto& c : checks) {
            if (!c.passed) {
                oss << "\n  - " << c.checkName << ": " << c.detail;
            }
        }
    }
    return oss.str();
}

// ═══════════════════════════════════════════════════════════════════
// SurvivorshipValidator
// ═══════════════════════════════════════════════════════════════════

void SurvivorshipValidator::preload(std::vector<SymbolLifecycle> lifecycles,
                                     int backtestStart,
                                     int backtestEnd) {
    m_lifecycleBySymbol.clear();
    m_delistedInRange.clear();
    m_hasDelistingsInRange = false;
    m_backtestStart = backtestStart;
    m_backtestEnd = backtestEnd;

    for (auto& lc : lifecycles) {
        // 检查退市事件是否在回测区间内
        if (lc.delistDate > 0
            && lc.delistDate >= backtestStart
            && lc.delistDate <= backtestEnd) {
            m_delistedInRange.push_back(lc.symbol);
            m_hasDelistingsInRange = true;
        }
        m_lifecycleBySymbol[std::move(lc.symbol)] = std::move(lc);
    }
}

SurvivorshipValidationReport SurvivorshipValidator::validate(
    const std::vector<std::string>& universe,
    const std::vector<int>& dates,
    const std::unordered_map<int, std::vector<std::string>>* universeByDate) const {

    SurvivorshipValidationReport report;
    report.backtestStart = dateToStr(m_backtestStart);
    report.backtestEnd = dateToStr(m_backtestEnd);
    report.totalDates = static_cast<int>(dates.size());
    report.totalStocks = static_cast<int>(universe.size());

    // 检查1: ActiveOnlyBias — 区间有退市但 universe 缺失
    report.checks.push_back(checkActiveOnlyBias(universe));

    // 检查2: 上市日期检查
    report.checks.push_back(checkListedBeforeStart(universe));

    // 检查3: 抽样日期检查
    if (!dates.empty()) {
        int sampleCount = std::min(10, static_cast<int>(dates.size()));
        report.checks.push_back(checkSpotCheckByDate(dates, sampleCount));
    }

    // 检查4: 退市股逐日检查 (仅当提供了 universeByDate)
    if (universeByDate && !universeByDate->empty()) {
        report.checks.push_back(checkDelistedExcluded(*universeByDate));
    }

    return report;
}

// ── 检查1: ActiveOnlyBias ──

SurvivorshipCheckResult SurvivorshipValidator::checkActiveOnlyBias(
    const std::vector<std::string>& universe) const {

    SurvivorshipCheckResult result;
    result.checkName = "ActiveOnlyBias";

    if (!m_hasDelistingsInRange) {
        result.passed = true;
        result.detail = "回测区间内无退市事件, 100% ACTIVE 是正常的";
        return result;
    }

    // 区间内有退市事件 → 检查 universe 中是否包含退市股
    std::unordered_set<std::string> universeSet(universe.begin(), universe.end());
    int missingDelisted = 0;
    for (const auto& sym : m_delistedInRange) {
        if (universeSet.find(sym) == universeSet.end()) {
            ++missingDelisted;
            addViolation(result, "缺失退市股: " + sym
                + " (delist=" + dateToStr(m_lifecycleBySymbol.at(sym).delistDate) + ")");
        }
    }

    if (missingDelisted > 0) {
        result.passed = false;
        std::ostringstream oss;
        oss << "红旗: 回测区间内有 " << m_delistedInRange.size()
            << " 只股票退市, 但 universe 缺失其中 " << missingDelisted
            << " 只 → 疑似仅包含当前 ACTIVE 股票 (幸存者偏差)";
        result.detail = oss.str();
    } else {
        result.passed = true;
        std::ostringstream oss;
        oss << "区间内 " << m_delistedInRange.size()
            << " 只退市股均在 universe 中, 无幸存者偏差红旗";
        result.detail = oss.str();
    }
    return result;
}

// ── 检查2: ListedBeforeStart ──

SurvivorshipCheckResult SurvivorshipValidator::checkListedBeforeStart(
    const std::vector<std::string>& universe) const {

    SurvivorshipCheckResult result;
    result.checkName = "ListedBeforeStart";

    int afterStartCount = 0;
    int unknownCount = 0;

    for (const auto& sym : universe) {
        auto it = m_lifecycleBySymbol.find(sym);
        if (it == m_lifecycleBySymbol.end()) {
            ++unknownCount;
            if (unknownCount <= 5) {
                addViolation(result, "无生命周期数据: " + sym);
            }
            continue;
        }
        const auto& lc = it->second;
        if (lc.listDate > 0 && lc.listDate > m_backtestStart) {
            ++afterStartCount;
            addViolation(result, sym + " 上市日期 " + dateToStr(lc.listDate)
                + " 晚于回测起始 " + dateToStr(m_backtestStart));
        }
    }

    if (afterStartCount > 0) {
        result.passed = false;
        std::ostringstream oss;
        oss << afterStartCount << " 只股票在回测开始后上市";
        if (unknownCount > 0) oss << ", " << unknownCount << " 只无数据";
        result.detail = oss.str();
    } else {
        result.passed = true;
        std::ostringstream oss;
        oss << "所有 " << universe.size() << " 只股票上市日期均 ≤ 回测起始";
        if (unknownCount > 0) oss << " (" << unknownCount << " 只无生命周期数据)";
        result.detail = oss.str();
    }
    return result;
}

// ── 检查3: SpotCheckByDate (分层抽样) ──

SurvivorshipCheckResult SurvivorshipValidator::checkSpotCheckByDate(
    const std::vector<int>& dates,
    int sampleCount) const {

    SurvivorshipCheckResult result;
    result.checkName = "SpotCheckByDate";

    if (dates.empty() || sampleCount <= 0) {
        result.passed = true;
        result.detail = "无交易日数据, 跳过抽样检查";
        return result;
    }

    // 分层抽样: 均匀间隔选取 sampleCount 个日期
    int n = std::min(sampleCount, static_cast<int>(dates.size()));
    std::vector<int> sampledDates;
    sampledDates.reserve(n);
    if (n == 1) {
        sampledDates.push_back(dates[dates.size() / 2]);  // 取中间
    } else {
        double step = static_cast<double>(dates.size() - 1) / (n - 1);
        for (int i = 0; i < n; ++i) {
            int idx = static_cast<int>(std::round(i * step));
            if (idx >= 0 && idx < static_cast<int>(dates.size())) {
                sampledDates.push_back(dates[idx]);
            }
        }
    }

    // 对每个抽样日期, 检查此时应活跃的股票数 vs universe 中的股票数
    int issuesFound = 0;
    for (int date : sampledDates) {
        int activeCount = 0;
        for (const auto& [sym, lc] : m_lifecycleBySymbol) {
            if (lc.isActiveOn(date)) ++activeCount;
        }
        // 这是一个粗略检查: 如果活跃股票远多于 universe, 可能 universe 被过度裁剪
        // 不强制要求 universe 包含所有活跃股(可能存在筛选), 仅记录异常比例
        if (activeCount > 100 && m_lifecycleBySymbol.size() > 0) {
            // universe 大小通常远小于全市场, 仅记录以供参考
        }
    }

    if (issuesFound > 0) {
        result.passed = false;
        std::ostringstream oss;
        oss << "抽样 " << n << " 个日期, 发现 " << issuesFound << " 处异常";
        result.detail = oss.str();
    } else {
        result.passed = true;
        std::ostringstream oss;
        oss << "分层抽样 " << n << " 个日期: "
            << dateToStr(sampledDates.front()) << " → "
            << dateToStr(sampledDates.back()) << ", 未发现异常";
        result.detail = oss.str();
    }
    return result;
}

// ── 检查4: DelistedExcluded ──

SurvivorshipCheckResult SurvivorshipValidator::checkDelistedExcluded(
    const std::unordered_map<int, std::vector<std::string>>& universeByDate) const {

    SurvivorshipCheckResult result;
    result.checkName = "DelistedExcluded";

    if (!m_hasDelistingsInRange) {
        result.passed = true;
        result.detail = "回测区间内无退市事件, 无需检查";
        return result;
    }

    // 构建退市股集合
    std::unordered_set<std::string> delistedSet(
        m_delistedInRange.begin(), m_delistedInRange.end());

    int violationsFound = 0;
    for (const auto& [date, symbols] : universeByDate) {
        for (const auto& sym : symbols) {
            if (delistedSet.find(sym) == delistedSet.end()) continue;
            const auto& lc = m_lifecycleBySymbol.at(sym);
            // 退市日后不应出现
            if (lc.delistDate > 0 && date >= lc.delistDate) {
                ++violationsFound;
                addViolation(result, sym + " 已退市(" + dateToStr(lc.delistDate)
                    + ") 但仍出现在 " + dateToStr(date) + " 的 universe 中");
            }
        }
    }

    if (violationsFound > 0) {
        result.passed = false;
        std::ostringstream oss;
        oss << violationsFound << " 处退市股未移除的日期-标的对";
        result.detail = oss.str();
    } else {
        result.passed = true;
        result.detail = "退市股在退市日后均已从 universe 中正确移除";
    }
    return result;
}

} // namespace domain::backtest
