// TradingSessionConstants.h — A股交易时段与市场规则常量 (engine 层, 零 Qt)
#pragma once

namespace engine::session {

// ── 交易时段 (HHMM 编码, minutes from midnight) ──

/// 15:00 收盘 → 900 min
constexpr int kCloseMinutes = 900;
/// 15:05 封锁结束 → 905 min
constexpr int kLockEndMinutes = 905;
/// 15:30 盘后时段结束 → 930 min
constexpr int kAfterHoursEndMinutes = 930;

// ── A股涨跌停板比例 ──

constexpr double kMainBoardLimitRatio    = 10.0;
constexpr double kGemStarBoardLimitRatio = 20.0;
constexpr double kNewThirdBoardLimitRatio = 30.0;

// ── gmsdk 订单状态码 → OrderUpdate 映射 ──

enum class GmOrderStatus : int {
    kPartialFilled  = 2,
    kFilled         = 3,
    kCancelled      = 5,
    kWastedRejected = 6,  // 废单/拒绝
    kExpired        = 7,
    kOtherRejected  = 8,
};

} // namespace engine::session
