#pragma once
// RawMarketDataAssembler — 原始行情数据组装器（纯 C++，零 Qt）
//
// 给定 (dataTypes, symbols, 日期范围)，复用统一 schema + 财务最近对齐(report_date ≤ trade_date)
// + 指数成分映射，按月分片流式产出与 cleaning::fullSchemaForTypes(dataTypes) 完全一致的 Arrow Table。
//
// 全量拉取(DataFetchController)与增量更新(DataCleaningServiceRefactored)共用此组装逻辑，
// 从根本上保证两条链路产出的列集(schema)逐列一致——这是增量 append 能对齐旧文件的前提。
#include <arrow/api.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "DataSourceRegistry.h"  // cleaning::FieldSchema / fullSchemaForTypes

namespace bridge {

class RawMarketDataAssembler {
public:
    struct Result {
        int totalRows = 0;   // 已装配的原始行数
        bool ok = false;     // true=完整装配；false=中途失败(error 说明)，onTable 可能已产出部分块
        std::string error;
    };

    /// 每装配好一块 Arrow Table 时回调（由调用方决定写文件/转行清洗）
    using TableSink = std::function<void(const std::shared_ptr<arrow::Table>&)>;
    /// 进度回调：已完成月数 / 总月数 / 累计行数（可选）
    using ProgressFn = std::function<void(int doneMonths, int totalMonths, int totalRows)>;

    /// @brief 按月分片装配 [startDate, endDate] 的原始行情
    /// @param dataTypes 数据类型（如 {"kline_daily","financial"}）
    /// @param symbols   标的列表（调用方提供，本类不做覆盖发现）
    /// @param startDate/endDate  "YYYY-MM-DD"（含）
    /// @param onTable   每块 Table 的接收器
    /// @param onProgress 进度回调（可选）
    Result assemble(const std::vector<std::string>& dataTypes,
                    const std::vector<std::string>& symbols,
                    const std::string& startDate,
                    const std::string& endDate,
                    const TableSink& onTable,
                    const ProgressFn& onProgress = {});

    /// @brief 该 dataTypes 对应的统一 schema（列名有序 + 数值字段集），用于 schema 对齐检查
    static cleaning::FieldSchema schemaFor(const std::vector<std::string>& dataTypes) {
        return cleaning::fullSchemaForTypes(dataTypes);
    }
};

} // namespace bridge
