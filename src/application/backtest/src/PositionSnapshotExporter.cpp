#include "../include/PositionSnapshotExporter.h"

#include <fstream>
#include <iostream>

namespace application::backtest {

std::string PositionSnapshotExporter::escapeCsv(const std::string& field)
{
    if (field.find(',') == std::string::npos
        && field.find('"') == std::string::npos
        && field.find('\n') == std::string::npos) {
        return field;
    }
    std::string escaped = "\"";
    for (char c : field) {
        if (c == '"') escaped += "\"\"";
        else escaped += c;
    }
    escaped += "\"";
    return escaped;
}

void PositionSnapshotExporter::addTradesFromLedger(
    const std::vector<domain::trading::FillEvent>& fills,
    const std::string& date,
    double commissionRate, double slippageRate, double taxRate)
{
    for (const auto& fill : fills) {
        TradeExportRow row;
        row.date = date;
        row.symbol = fill.symbol.value;
        row.side = (fill.side == domain::trading::OrderSide::Buy) ? "BUY" : "SELL";
        row.quantity = static_cast<double>(fill.quantity.value);
        row.price = static_cast<double>(fill.priceTicks.value) * 0.01;
        double notional = row.quantity * row.price;
        row.commission = notional * commissionRate;
        row.slippage = notional * slippageRate;
        row.tax = notional * taxRate;
        trades_.push_back(row);
    }
}

void PositionSnapshotExporter::addPositionsFromGroups(
    const factor::compute::SimulatedTradingResult& result,
    const std::string& date,
    const std::unordered_map<uint32_t, std::string>& idToSymbol)
{
    for (const auto& group : result.groups) {
        const uint32_t id = static_cast<uint32_t>(group.groupIndex);
        auto symIt = idToSymbol.find(id);
        if (symIt == idToSymbol.end()) continue;

        PositionSnapshotRow row;
        row.date = date;
        row.symbol = symIt->second;
        row.weight = static_cast<double>(group.stockCount);
        row.price = 0.0;
        positions_.push_back(row);
    }
}

bool PositionSnapshotExporter::exportToFiles() const
{
    {
        std::string path = outputDir_ + "/trades.csv";
        std::ofstream ofs(path, std::ios::out | std::ios::trunc);
        if (!ofs.is_open()) return false;
        ofs << "Date,Symbol,Side,Quantity,Price,Commission,Slippage,Tax\n";
        for (const auto& row : trades_) {
            ofs << escapeCsv(row.date) << ","
                << escapeCsv(row.symbol) << ","
                << escapeCsv(row.side) << ","
                << row.quantity << ","
                << row.price << ","
                << row.commission << ","
                << row.slippage << ","
                << row.tax << "\n";
        }
    }
    {
        std::string path = outputDir_ + "/positions_snapshot.csv";
        std::ofstream ofs(path, std::ios::out | std::ios::trunc);
        if (!ofs.is_open()) return false;
        ofs << "Date,Symbol,Weight,Price\n";
        for (const auto& row : positions_) {
            ofs << escapeCsv(row.date) << ","
                << escapeCsv(row.symbol) << ","
                << row.weight << ","
                << row.price << "\n";
        }
    }
    return true;
}

} // namespace application::backtest