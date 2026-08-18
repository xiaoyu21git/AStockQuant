#include "domain/factor/include/ReversalFactor.h"
#include "domain/factor/include/BaseFactor.h"
#include "domain/factor/include/FactorConfigAccess.h"
#include "domain/factor/include/FactorInstanceManager.h"
#include "infrastructure/include/database/NativePgConnectionPool.h"
#include "infrastructure/include/database/ISqlDatabase.h"
#include "foundation/log/logging.hpp"
#include <cmath>
#include <algorithm>
#include <vector>

namespace factor {

namespace {
    // ── 质量过滤常量 (反转效应语义: 质量高的超跌) ──
    constexpr double kEpsPositiveGate = 0.0;         // eps>0 盈利门槛
    constexpr int64_t kMinListedDays = 365;          // 非次新: 上市 ≥365 自然日
    constexpr double kExRightDeviation = 0.03;       // 除权日: raw 涨跌与 change_pct 背离阈值
    constexpr int kQualityMetaLookupDays = 2;        // 除权日判断需前一日 close (2 个点)

    /// @brief 格里高利历 → 连续天数 (Howard Hinnant days_from_civil), 用于上市时长计算
    inline int64_t ymdToDays(int32_t y, int32_t m, int32_t d) {
        y -= m <= 2;
        const int64_t era = (y >= 0 ? y : y - 399) / 400;
        const int64_t yoe = static_cast<int64_t>(y) - era * 400;
        const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + doe - 719468;
    }
} // namespace

ReversalFactor::ReversalFactor()
{
    factorType_ = FactorType::REVERSAL;
}

CalculationResult ReversalFactor::calculate(const CalculationContext& context)
{
    if (!context.historicalView) {
        return createHistoricalViewRuntimeError(context, "反转因子需要 HistoricalView");
    }

    const CommonParams& common = params_;
    const auto symbols = effectiveSymbols(context);
    const int window = (std::max)(2, static_cast<int>(params_.window));
    const ReversalSplitMethod method = params_.splitMethod;

    return executeWithCommonParams(
        context,
        common,
        [&]() { return context.date; },
        [&](const CommonRuntimeState& runtime, CalculationResult& result) {
            if (method == ReversalSplitMethod::W_CUT) {
                calculateWCut(context, runtime.effectiveDate, symbols, result.values);
            } else {
                calculateTraditionalReversal(context, runtime.effectiveDate, symbols, result.values);
            }
            if (result.values.empty()) {
                result.metadata.set("emptyReason",
                    json_helper::toJsonValue("反转因子没有可用数据"));
            }
        },
        [](const CommonRuntimeState&, CalculationResult&) {},
        [&](const CommonRuntimeState&, CalculationResult& result) {
            result.metadata.set("splitMethod",
                json_helper::toJsonValue(static_cast<int>(method)));
            result.metadata.set("window",
                json_helper::toJsonValue(window));
        });
}

void ReversalFactor::calculateWCut(
    const CalculationContext& context,
    const std::string& effectiveDate,
    const std::vector<std::string>& symbols,
    std::unordered_map<std::string, double>& outValues) const
{
    const int window = params_.window;
    const std::string metricField = params_.splitMetric;
    // 回溯足够日历天覆盖 window 个交易日
    const std::string startDate = BaseFactor::subtractCalendarDays(
        effectiveDate, window * 2 + 5);

    // 检查 splitMetric 字段是否存在
    const bool hasMetric = context.historicalView->hasField(metricField);
    if (!hasMetric) {
        return; // 数据未就绪，返回空
    }

    for (const auto& symbol : symbols) {
        auto priceSeries = context.historicalView->getSeries(
            symbol, startDate, effectiveDate, "close");
        auto amountSeries = context.historicalView->getSeries(
            symbol, startDate, effectiveDate, metricField);

        // 序列对齐校验：长度不足则跳过
        if (priceSeries.size() < static_cast<size_t>(window)
            || amountSeries.size() < static_cast<size_t>(window)) continue;
        if (priceSeries.size() != amountSeries.size()) continue;

        // 逐日计算收益率与成交额排序
        struct DayInfo {
            double ret;
            double amount;
        };
        std::vector<DayInfo> days;
        days.reserve(priceSeries.size() - 1);
        for (size_t i = 0; i < priceSeries.size() - 1; ++i) {
            const double prevClose = priceSeries[i].value;
            const double currClose = priceSeries[i + 1].value;
            const double amt = amountSeries[i].value;
            if (!std::isfinite(prevClose) || !std::isfinite(currClose)
                || prevClose <= 0.0 || !std::isfinite(amt)) continue;
            days.push_back({(currClose - prevClose) / prevClose, amt});
        }
        if (days.size() < static_cast<size_t>(window)) continue;

        // 取最近 window 个有效交易日
        if (days.size() > static_cast<size_t>(window)) {
            days.erase(days.begin(), days.end() - window);
        }

        // 按成交额排序，前 N/2 为高D组
        std::sort(days.begin(), days.end(),
            [](const DayInfo& a, const DayInfo& b) { return a.amount > b.amount; });

        const size_t half = window / 2;
        double mHigh = 0.0, mLow = 0.0;
        for (size_t i = 0; i < half; ++i) mHigh += days[i].ret;
        for (size_t i = half; i < days.size(); ++i) mLow += days[i].ret;

        // M = M_high - M_low，取负使因子正向（值越大越好，ascending=true）
        const double m = params_.useHighOnly ? -mHigh : -(mHigh - mLow);
        if (std::isfinite(m)) outValues[symbol] = m;
    }
}

void ReversalFactor::calculateTraditionalReversal(
    const CalculationContext& context,
    const std::string& effectiveDate,
    const std::vector<std::string>& symbols,
    std::unordered_map<std::string, double>& outValues) const
{
    // 传统反转：M = -Return(window)
    // adjustedClose=true → close × pre_adjust_factor 前复权价口径, 消除除权假跌
    // qualityFilter=true → 先过质量门 (盈利+非ST+非次新+非已退市+非除权日)
    const int window = params_.window;
    for (const auto& symbol : symbols) {
        if (params_.qualityFilter && !passQualityGate(context, effectiveDate, symbol)) {
            continue;
        }
        auto series = context.historicalView->getSeries(
            symbol, effectiveDate, window + 1, "close");
        if (params_.adjustedClose) {
            auto adjSeries = context.historicalView->getSeries(
                symbol, effectiveDate, window + 1, "pre_adjust_factor");
            // 调整因子序列不完整或与收盘价序列日期不对齐 → 跳过该标的, 不用未复权口径顶替
            if (adjSeries.size() < series.size()) continue;
            bool fullyAligned = true;
            for (size_t i = 0; i < series.size(); ++i) {
                if (series[i].date != adjSeries[i].date) { fullyAligned = false; break; }
                // 除权因子缺失/非正 → 视为当日无除权调整
                if (std::isfinite(adjSeries[i].value) && adjSeries[i].value > 0.0)
                    series[i].value *= adjSeries[i].value;
            }
            if (!fullyAligned) continue;
        }
        if (series.size() < 2) continue;
        const double first = series.front().value;
        const double last  = series.back().value;
        if (std::isfinite(first) && std::isfinite(last) && first > 0.0 && last > 0.0) {
            const double ret = -(last / first - 1.0);
            if (std::isfinite(ret)) outValues[symbol] = ret;
        }
    }
}

std::shared_ptr<ReversalFactor> ReversalFactor::create(
    const FactorInstanceInfo& info,
    std::shared_ptr<DataAvailabilityChecker> dataChecker)
{
    auto factor = std::make_shared<ReversalFactor>();
    factor->dataChecker_ = std::move(dataChecker);
    factor->instanceId_ = info.instanceId;
    factor->name_ = info.instanceName;
    factor->description_ = info.description;
    factor->loadConfig(info.config);
    if (factor->params_.qualityFilter) {
        factor->loadQualityMeta(); // 质量名单懒加载 (create 单线程时机, 一次查库)
    }
    return factor;
}

void ReversalFactor::loadQualityMeta()
{
    // 加载 ref.symbol_info 静态名单: name(ST判定) / list_date(次新) / delist_date(已退市)
    // 与清洗链路 STFilterRule 的 ST 判定口径一致: name 前缀 "ST" / "*ST"
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) {
        INTERNAL_WARN_STREAM << "[ReversalFactor] 质量名单加载失败: DB 不可用"
                             << " — qualityFilter 将剔除全部标的 (保守)";
        return;
    }
    auto rows = db->executeQuery(
        "SELECT symbol, name, list_date::text, delist_date::text FROM ref.symbol_info");
    for (std::size_t i = 0; i < rows.rowCount(); ++i) {
        auto& row = rows.getRow(i);
        SymbolQualityMeta meta;
        meta.name         = row.getString("name");
        meta.listDateVal  = dateToInt(row.getString("list_date"));
        meta.delistDateVal = dateToInt(row.getString("delist_date"));
        const std::string sym = row.getString("symbol");
        if (!sym.empty()) m_qualityMeta.emplace(sym, std::move(meta));
    }
    INTERNAL_INFO_STREAM << "[ReversalFactor] 质量名单加载完成: " << m_qualityMeta.size()
                         << " 标的";
}

int32_t ReversalFactor::dateToInt(const std::string& date)
{
    // "YYYY-MM-DD" 或 "YYYYMMDD" → YYYYMMDD int; 非法 → 0
    if (date.size() < 8) return 0;
    std::string digits;
    digits.reserve(8);
    for (char c : date) {
        if (c >= '0' && c <= '9') digits.push_back(c);
    }
    if (digits.size() < 8) return 0;
    int32_t result = 0;
    for (std::size_t i = 0; i < 8; ++i)
        result = result * 10 + (digits[i] - '0');
    return result;
}

bool ReversalFactor::passQualityGate(const CalculationContext& context,
                                     const std::string& effectiveDate,
                                     const std::string& symbol) const
{
    // ── 静态名单: ST / 次新 / 已退市 (名单缺失 → 保守剔除, 宁缺毋滥) ──
    const auto metaIt = m_qualityMeta.find(symbol);
    if (metaIt == m_qualityMeta.end()) return false;

    const SymbolQualityMeta& meta = metaIt->second;
    if (meta.name.size() >= 2 && meta.name.substr(0, 2) == "ST") return false;   // ST
    if (meta.name.size() >= 3 && meta.name.substr(0, 3) == "*ST") return false;  // *ST

    const int32_t effVal = dateToInt(effectiveDate);
    if (effVal <= 0) return false;
    if (meta.delistDateVal > 0 && meta.delistDateVal <= effVal) return false;    // 已退市

    if (meta.listDateVal <= 0) return false;                                     // 上市日未知 → 保守剔除
    const int64_t listedDays = ymdToDays(effVal / 10000, (effVal / 100) % 100, effVal % 100)
                             - ymdToDays(meta.listDateVal / 10000,
                                         (meta.listDateVal / 100) % 100,
                                         meta.listDateVal % 100);
    if (listedDays < kMinListedDays) return false;                               // 次新

    // ── 盈利门槛: 当日 eps>0 (缺失/非有限 → 剔除) ──
    // 日期比较统一走 dateToInt: adapter 返回紧凑 "YYYYMMDD", 而 effectiveDate 是 ISO "YYYY-MM-DD"
    {
        auto epsSeries = context.historicalView->getSeries(
            symbol, effectiveDate, 1, "eps");
        if (epsSeries.size() != 1 || dateToInt(epsSeries.back().date) != effVal) return false;
        const double epsVal = epsSeries.back().value;
        if (!std::isfinite(epsVal) || epsVal <= kEpsPositiveGate) return false;
    }

    // ── 除权日: raw close 单日变动与 change_pct 背离 (>3%) → 剔除 ──
    {
        auto close2 = context.historicalView->getSeries(
            symbol, effectiveDate, kQualityMetaLookupDays, "close");
        auto chgSeries = context.historicalView->getSeries(
            symbol, effectiveDate, 1, "change_pct");
        if (close2.size() == kQualityMetaLookupDays
            && dateToInt(close2.back().date) == effVal
            && chgSeries.size() == 1 && dateToInt(chgSeries.back().date) == effVal) {
            const double prev = close2[close2.size() - 2].value;
            const double curr = close2.back().value;
            const double chgPct = chgSeries.back().value;   // 数据集口径: 百分数 (3.5=3.5%)
            if (std::isfinite(prev) && std::isfinite(curr) && prev > 0.0
                && std::isfinite(chgPct)) {
                const double rawRet = curr / prev - 1.0;
                if (std::abs(rawRet - chgPct / 100.0) > kExRightDeviation) return false;
            }
        }
    }

    return true;
}

DataRequirements ReversalFactor::getDataRequirements() const
{
    DataRequirements req;
    appendRequiredField(req, "close");
    if (params_.adjustedClose) {
        appendRequiredField(req, "pre_adjust_factor");
    }
    if (params_.splitMethod == ReversalSplitMethod::W_CUT) {
        appendRequiredField(req, params_.splitMetric);
    }
    if (params_.qualityFilter) {
        appendRequiredField(req, "eps");         // 盈利门槛
        appendRequiredField(req, "change_pct");  // 除权日检测
    }
    appendHistoricalNeutralizationRequirements(req, params_.neutralizationEnabled);
    return req;
}

BoundaryRules ReversalFactor::getBoundaryRules() const
{
    BoundaryRules rules = boundaryRules_;
    rules.minDataPoints = (std::max)(rules.minDataPoints, params_.window);
    return rules;
}

void ReversalFactor::loadConfig(const foundation::json::JsonFacade& config)
{
    BaseFactor::loadConfig(config);
    if (config::hasParametersConfig(config))
        params_.fromJson(config::parametersConfig(config));
    dataRequirements_ = getDataRequirements();
}

} // namespace factor
