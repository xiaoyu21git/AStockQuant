// SignalFormatter.cpp — THS/TDX 格式化器实现
#include "ISignalFormatter.h"
#include "../../strategy/include/StrategyServiceTypes.h"  // SignalIntent 枚举

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace domain::sigout {
namespace {

using domain::strategy::SignalIntent;

// ── 辅助: SignalIntent → 中文方向 ──
const char* intentToDirection(int intent) noexcept {
    switch (static_cast<SignalIntent>(intent)) {
        case SignalIntent::OPEN:   return "买入";
        case SignalIntent::ADD:    return "加仓";
        case SignalIntent::REDUCE: return "减仓";
        case SignalIntent::CLOSE:  return "卖出";
        default: return "--";
    }
}

// ── 辅助: SignalIntent → 英文信号类型 (TDX) ──
const char* intentToTdxType(int intent) noexcept {
    switch (static_cast<SignalIntent>(intent)) {
        case SignalIntent::OPEN:   return "BUY";
        case SignalIntent::ADD:    return "ADD";
        case SignalIntent::REDUCE: return "REDUCE";
        case SignalIntent::CLOSE:  return "SELL";
        default: return "HOLD";
    }
}

// ── 辅助: "600001.SH" → ("SH", "600001") ──
void splitSymbol(const std::string& full, std::string& market, std::string& code) {
    auto dotPos = full.rfind('.');
    if (dotPos != std::string::npos && dotPos + 1 < full.size()) {
        market = full.substr(dotPos + 1);
        code = full.substr(0, dotPos);
    } else {
        market = "SH";
        code = full;
    }
}

// ── 辅助: 得分 → 信号强度 (0-100) ──
int scoreToStrength(double score) noexcept {
    // 简单映射: |score| → [0,100], 截断
    double absScore = std::abs(score);
    if (absScore >= 1.0) return 100;
    return static_cast<int>(absScore * 100.0);
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// ThsSignalFormatter: 同花顺制表符分隔格式
// ═══════════════════════════════════════════════════════════════════════════

class ThsSignalFormatter final : public ISignalFormatter {
public:
    [[nodiscard]] std::string format(const SignalOutput& signal) const override {
        // 格式: 时间\t代码\t名称\t买卖方向\t价格\t成交量\t信号强度
        std::ostringstream oss;
        oss << signal.timestamp << '\t'
            << signal.symbol << '\t'
            << (signal.stockName.empty() ? signal.symbol : signal.stockName) << '\t'
            << intentToDirection(signal.signalIntent) << '\t'
            << "--" << '\t'      // 价格 (V2: 可填入当前价)
            << "--" << '\t'      // 成交量 (V2: 可填入建议量)
            << scoreToStrength(signal.score);
        return oss.str();
    }

    [[nodiscard]] std::string formatName() const override {
        return "同花顺(THS)";
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// TdxSignalFormatter: 通达信 CSV 格式
// ═══════════════════════════════════════════════════════════════════════════

class TdxSignalFormatter final : public ISignalFormatter {
public:
    [[nodiscard]] std::string format(const SignalOutput& signal) const override {
        // 格式: 市场,代码,名称,日期,时间,信号类型,信号值
        std::string market, code;
        splitSymbol(signal.symbol, market, code);

        // 解析 timestamp (ISO8601: "2026-08-09T09:35:00") → 日期 + 时间
        std::string datePart = signal.timestamp.substr(0, 10);
        std::string timePart = signal.timestamp.size() >= 16
            ? signal.timestamp.substr(11, 5) : "--:--";
        // 日期转为 "2026/08/09" 格式
        std::replace(datePart.begin(), datePart.end(), '-', '/');

        std::ostringstream oss;
        oss << market << ','
            << code << ','
            << (signal.stockName.empty() ? signal.symbol : signal.stockName) << ','
            << datePart << ','
            << timePart << ','
            << intentToTdxType(signal.signalIntent) << ','
            << scoreToStrength(signal.score);
        return oss.str();
    }

    [[nodiscard]] std::string formatName() const override {
        return "通达信(TDX)";
    }
};

} // anonymous namespace

// ── 工厂函数 ──

std::unique_ptr<domain::sigout::ISignalFormatter>
domain::sigout::createThsFormatter() {
    return std::make_unique<domain::sigout::ThsSignalFormatter>();
}

std::unique_ptr<domain::sigout::ISignalFormatter>
domain::sigout::createTdxFormatter() {
    return std::make_unique<domain::sigout::TdxSignalFormatter>();
}
