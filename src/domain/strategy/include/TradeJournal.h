#pragma once
// TradeJournal — 策略交易日志, 独立于系统日志
// 按 策略名/日期 分文件, JSON Lines 格式, 方便查询和分析
// 纯 C++17, 零 Qt 依赖

#include <cstdint>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>

namespace domain::strategy {

/// @brief 交易日志记录器 — 线程安全
/// 用法:
///   TradeJournal journal("logs", "策略名");
///   journal.log(R"({{"type":"signal","symbol":"300767.SZ"}})");
class TradeJournal {
public:
    TradeJournal() = default;

    /// @param logDir  日志根目录 (如 "logs")
    /// @param strategyName  策略名称 (如 "AI因子策略v2"), 作为子目录名
    TradeJournal(const std::string& logDir, const std::string& strategyName)
        : m_logDir(logDir), m_strategyName(strategyName) {}

    ~TradeJournal() { close(); }

    TradeJournal(const TradeJournal&) = delete;
    TradeJournal& operator=(const TradeJournal&) = delete;
    TradeJournal(TradeJournal&& other) noexcept
        : m_logDir(std::move(other.m_logDir))
        , m_strategyName(std::move(other.m_strategyName))
        , m_currentDate(std::move(other.m_currentDate))
        , m_file(std::move(other.m_file)) {}

    /// @brief 写入一条 JSON 记录 (自动追加换行, 自动按日期切文件)
    void log(const std::string& jsonLine);

    /// @brief 获取当前日志文件路径
    [[nodiscard]] const std::string& currentFilePath() const noexcept {
        return m_currentFilePath;
    }

    /// @brief 关闭当前文件 (程序退出时调用)
    void close();

private:
    /// @brief 检查是否需要切文件 (日期变更)
    void ensureFileForToday();

    static std::string todayStr();

    std::string m_logDir;
    std::string m_strategyName;
    std::string m_currentDate;
    std::string m_currentFilePath;
    std::ofstream m_file;
    std::mutex m_mutex;
};

} // namespace domain::strategy
