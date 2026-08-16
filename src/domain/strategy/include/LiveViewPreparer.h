#pragma once
// LiveViewPreparer — 实盘视图当日合成行准备 (P0 基础件)
// 职责: 为构造实盘视图前的原始行集合注入"当日合成行", 保证因子锚点存在
// 纯函数: 不修改输入, 返回新行集合; 仅日频链调用 (澄清 C4: 非交易日 no-op)

#include "EvalTypes.h"

#include <string>
#include <vector>

// tag 必须与 ISqlDatabase.h 权威定义一致 (class): MSVC 将 struct/class tag 编入符号名,
// 不一致会导致跨 TU 调用 LNK2001 (定义侧 U vs 调用侧 V)
namespace astock::database { class SqlQueryResultRow; }
namespace astock::infrastructure::database { class MarketDataRepository; }

namespace domain::strategy {

/// @brief 合成行准备策略
struct LiveViewPreparePolicy {
    BarPeriod period{BarPeriod::Daily};  // 仅 Daily 注入合成行; 其他周期原样返回
};

class LiveViewPreparer {
public:
    /// @brief 若评估日缺当日行且为交易日, 为每只标的追加当日合成行
    /// 合成行沿用各标的最后一行数据, 仅改写 trade_date (锚点值不参与推理)
    /// @return 新行集合 (历史行 + 合成行); 缺行已满足/非交易日/非日频 → 与输入等价
    std::vector<astock::database::SqlQueryResultRow> prepareRows(
        astock::infrastructure::database::MarketDataRepository& repo,
        const std::vector<astock::database::SqlQueryResultRow>& rawRows,
        const std::string& endDate,
        const LiveViewPreparePolicy& policy) const;
};

} // namespace domain::strategy
