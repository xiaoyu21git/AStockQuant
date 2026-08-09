// SignalTypes.h — 信号输出数据类型
// 纯 C++, 零 Qt 依赖, 领域层独立模块
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace domain::sigout {

// ═══════════════════════════════════════════════════════════════════════════
// 信号推送目标
// ═══════════════════════════════════════════════════════════════════════════

/// @brief 推送目标枚举
enum class PushTarget : std::uint8_t {
    File   = 0,   // 本地文件
    Socket = 1,   // Socket 推送
    WeChat = 2,   // 微信 (v0.17.0)
};

// ═══════════════════════════════════════════════════════════════════════════
// 信号输出记录
// ═══════════════════════════════════════════════════════════════════════════

/// @brief 单条信号输出 (领域层最小数据结构)
/// stockName 字段由 Bridge 层缓存填充, 引擎层产出时留空
struct SignalOutput {
    std::string symbol;          // 完整代码 (如 "600001.SH")
    std::string stockName;       // 标的名称 (Bridge 层缓存查询)
    int signalIntent{0};         // SignalIntent 枚举值 (OPEN=1, ADD=2, REDUCE=3, CLOSE=4)
    double targetWeight{0.0};    // 目标权重
    double score{0.0};           // 因子得分
    std::string strategyName;    // 策略名称
    std::string timestamp;       // 信号时间 ISO8601
    std::string traceId;         // TraceID 跨日志关联 (P0-3: UUID)
};

/// @brief 持久化的信号记录 (Phase 1: 内存缓存, Phase 2: 写入 DB)
struct StoredSignalRecord {
    std::string signalId;        // UUID
    std::string strategyId;
    std::string symbol;
    std::string fullSymbol;
    int signalIntent{0};         // SignalIntent 枚举值
    double targetWeight{0.0};
    double score{0.0};
    std::string formatterName;   // "同花顺(THS)" / "通达信(TDX)" / "内部JSON"
    std::string pusherName;      // "本地文件" / "Socket"
    std::string signalTime;      // ISO8601
    std::string traceId;
    bool pushed{false};
    std::string pushError;       // 失败原因 (空=成功)
};

} // namespace domain::sigout
