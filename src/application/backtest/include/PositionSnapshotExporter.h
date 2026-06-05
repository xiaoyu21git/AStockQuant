#pragma once

#include "../../../domain/factor/include/factor_compute/GroupedBacktestTypes.h"
#include "../../../domain/trading/include/TradingTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace application::backtest {

struct TradeExportRow {
    std::string date;
    std::string symbol;
    std::string side;
    double quantity{0.0};
    double price{0.0};
    double commission{0.0};
    double slippage{0.0};
    double tax{0.0};
};

struct PositionSnapshotRow {
    std::string date;
    std::string symbol;
    double weight{0.0};
    double price{0.0};
};

class PositionSnapshotExporter final {
public:
    PositionSnapshotExporter() = default;

    void setOutputDir(const std::string& dir) { outputDir_ = dir; }

    void addTrade(const TradeExportRow& row) { trades_.push_back(row); }
    void addPosition(const PositionSnapshotRow& row) { positions_.push_back(row); }

    void addTradesFromLedger(
        const std::vector<domain::trading::FillEvent>& fills,
        const std::string& date,
        double commissionRate, double slippageRate, double taxRate);

    void addPositionsFromGroups(
        const factor::compute::SimulatedTradingResult& result,
        const std::string& date,
        const std::unordered_map<uint32_t, std::string>& idToSymbol);

    bool exportToFiles() const;

private:
    static std::string escapeCsv(const std::string& field);

    std::vector<TradeExportRow> trades_;
    std::vector<PositionSnapshotRow> positions_;
    std::string outputDir_{"."};
};

} // namespace application::backtest