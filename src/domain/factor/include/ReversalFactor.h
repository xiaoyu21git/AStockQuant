#pragma once

#include "BaseFactor.h"
#include "FactorMetricConfig.h"
#include "factor_enums.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace factor {

class ReversalFactor final : public BaseFactor {
public:
    struct Params : CommonParams {
        ReversalSplitMethod splitMethod{ReversalSplitMethod::NONE};
        int window = 20;
        std::string splitMetric = "avg_trade_amount";
        bool useHighOnly = false;
        bool adjustedClose = false; // 前复权价口径 (close×pre_adjust_factor), 消除除权假跌; false=原始收盘价(现有行为)
        bool qualityFilter = false; // 质量过滤(反转效应语义): 盈利(eps>0)+非ST+非次新(上市≥365天)+非已退市+非除权日; false=现有行为

        void fromJson(const foundation::json::JsonFacade& json);
    };

    ReversalFactor();

    CalculationResult calculate(const CalculationContext& context) override;
    DataRequirements getDataRequirements() const override;
    BoundaryRules getBoundaryRules() const override;
    int getLookbackDays() const override { return params_.window; }

    static std::shared_ptr<ReversalFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);

private:
    Params params_;

    void loadConfig(const foundation::json::JsonFacade& config) override;

    // W式切割实现
    void calculateWCut(const CalculationContext& context,
                       const std::string& effectiveDate,
                       const std::vector<std::string>& symbols,
                       std::unordered_map<std::string, double>& outValues) const;

    // 传统反转实现: M = -Return(window); adjustedClose=true 时用前复权价序列
    void calculateTraditionalReversal(const CalculationContext& context,
                                      const std::string& effectiveDate,
                                      const std::vector<std::string>& symbols,
                                      std::unordered_map<std::string, double>& outValues) const;

    // ── 质量过滤 (qualityFilter=true 时启用, 反转效应语义) ──

    /// @brief 质量名单条目: 来自 ref.symbol_info 的静态元数据
    struct SymbolQualityMeta {
        std::string name;        // 名称 (ST 前缀判定)
        int32_t listDateVal = 0; // 上市日期 YYYYMMDD (0=未知 → 按次新剔除, 保守)
        int32_t delistDateVal = 0; // 退市日期 YYYYMMDD (0=未退市)
    };

    /// @brief 加载 ref.symbol_info 静态名单 (qualityFilter 开启时由 create 调用一次)
    void loadQualityMeta();

    /// @brief 质量门: 盈利(eps>0)+非ST+非次新(≥365天)+非已退市+非除权日
    /// 不通过返回 false (该标的不进反转候选池)
    bool passQualityGate(const CalculationContext& context,
                         const std::string& effectiveDate,
                         const std::string& symbol) const;

    /// @brief "YYYY-MM-DD" → YYYYMMDD int (空/非法 → 0)
    static int32_t dateToInt(const std::string& date);

    // qualityFilter 开启时加载的质量名单 (symbol → 元数据)
    std::unordered_map<std::string, SymbolQualityMeta> m_qualityMeta;
};

} // namespace factor
