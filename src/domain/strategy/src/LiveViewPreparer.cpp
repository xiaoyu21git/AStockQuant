#include "../include/LiveViewPreparer.h"
#include "../../../infrastructure/include/database/ISqlDatabase.h"
#include "../../../infrastructure/include/database/MarketDataRepository.h"
#include "foundation/log/logging.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace domain::strategy {

// ── 当日实时合成行 ──
// 实盘启动时当日日K通常尚未入库(日终同步), 视图最后日期=昨日 → 因子锚点(今日)不在视图,
// HistoricalAdapter::getSeries 找不到锚点直接返回空 → 因子全空(14:50 EOD 评估无信号)。
// 修复: 若今日是交易日且视图缺今日行, 为每只标的复制其最后一行生成今日合成行。
// 锚点行值不进入模型输入(窗口=[anchor-W, anchor-1], 锚点仅作边界), 故沿用昨日值
// 与回测语义完全一致; 次日启动时 DB 已有昨日真实日K, 合成行自然被真实数据替换。
std::vector<astock::database::SqlQueryResultRow> LiveViewPreparer::prepareRows(
    astock::infrastructure::database::MarketDataRepository& repo,
    const std::vector<astock::database::SqlQueryResultRow>& rawRows,
    const std::string& endDate,
    const LiveViewPreparePolicy& policy) const
{
    // 纯函数: 复制输入, 绝不修改原始查询结果
    std::vector<astock::database::SqlQueryResultRow> out = rawRows;

    // 仅日频链注入合成行 (澄清 C4: 非日频/非交易日 → no-op 原样返回, 不报错)
    if (policy.period != BarPeriod::Daily) return out;
    if (out.empty() || endDate.empty()) return out;

    // 行已按 symbol, trade_date ASC 排序 → 末行即视图最大日期
    const std::string lastDate = out.back().getString("trade_date");
    if (lastDate >= endDate) return out;  // 当日日K已入库, 无需合成

    // 交易日校验: 非交易日(周末/节假日)不合成, 避免视图出现无效日期锚点
    std::string compactDate = endDate;
    compactDate.erase(std::remove(compactDate.begin(), compactDate.end(), '-'),
                      compactDate.end());
    if (!repo.isTradingDay(compactDate)) return out;

    // 每只标的取其最后一行 → 复制改写 trade_date (其余字段沿用昨日, 锚点值不参与推理)
    std::unordered_map<std::string, astock::database::SqlQueryResultRow> lastRowBySym;
    for (const auto& row : out)
        lastRowBySym[row.getString("symbol")] = row;

    out.reserve(out.size() + lastRowBySym.size());
    for (const auto& [sym, row] : lastRowBySym) {
        out.push_back(row);
        out.back().setValue("trade_date", endDate);
    }
    INTERNAL_INFO_STREAM << "[Engine] 当日实时合成行: " << lastRowBySym.size()
                         << " 标的 (交易日=" << endDate
                         << ", 视图最后日=" << lastDate << ")";
    return out;
}

} // namespace domain::strategy
