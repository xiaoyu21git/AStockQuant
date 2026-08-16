#pragma once
// LocalClock — 本地时间工具类（零 Qt 依赖, header-only）
// 收敛散落各处的 localtime_s/localtime_r + 分钟换算副本 (P6: 减少造轮子)
// 使用方: EvalScheduleConfig(HH:MM 解析) / CronEvaluationScheduler / IntervalEvaluationScheduler
//        / PostMarketSyncService / GmSessionEngine / JujinMarketConnector / MarketDataService

#include <ctime>
#include <string>

namespace foundation::time {

class LocalClock {
public:
    /// @brief 解析 "HH:MM" 字符串为自零点分钟数
    /// 严格校验: 长度==5、第3字符为':'、数字、hour<=23、minute<=59; 非法返回 -1 (调用方决定兜底策略)
    static int parseHhMm(const std::string& time) noexcept {
        if (time.size() != 5 || time[2] != ':') return -1;
        const auto isDigit = [](char c) noexcept { return c >= '0' && c <= '9'; };
        if (!isDigit(time[0]) || !isDigit(time[1])
            || !isDigit(time[3]) || !isDigit(time[4])) return -1;
        const int hour = (time[0] - '0') * 10 + (time[1] - '0');
        const int minute = (time[3] - '0') * 10 + (time[4] - '0');
        if (hour > 23 || minute > 59) return -1;
        return hour * 60 + minute;
    }

    /// @brief 当前本地时间自零点分钟数 (0-1439)
    static int minutesOfDay() noexcept {
        std::tm local;
        if (!localTime(local)) return 0;
        return local.tm_hour * 60 + local.tm_min;
    }

    /// @brief 指定时间戳 (秒, epoch) 对应本地时间自零点分钟数 (tick 等历史时刻语义, 非"当前")
    static int minutesOfDay(std::time_t tt) noexcept {
        std::tm local;
        if (!localTime(tt, local)) return 0;
        return local.tm_hour * 60 + local.tm_min;
    }

    /// @brief 当前本地时间自零点秒数 (0-86399)
    static int secondsOfDay() noexcept {
        std::tm local;
        if (!localTime(local)) return 0;
        return local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
    }

private:
    /// @brief 取当前本地时间分量; 失败返回 false (分量未定义, 调用方按 0 处理)
    static bool localTime(std::tm& out) noexcept { return localTime(std::time(nullptr), out); }

    /// @brief 取指定时间戳的本地时间分量; 失败返回 false
    static bool localTime(std::time_t tt, std::tm& out) noexcept {
#if defined(_WIN32) || defined(_WIN64)
        return localtime_s(&out, &tt) == 0;
#else
        return localtime_r(&tt, &out) != nullptr;
#endif
    }
};

} // namespace foundation::time
