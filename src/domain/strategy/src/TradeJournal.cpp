// TradeJournal — 交易日志实现
#include "../include/TradeJournal.h"

#include "foundation/log/logging.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace domain::strategy {

std::string TradeJournal::todayStr() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::setfill('0')
        << (tm.tm_year + 1900) << "-"
        << std::setw(2) << (tm.tm_mon + 1) << "-"
        << std::setw(2) << tm.tm_mday;
    return oss.str();
}

void TradeJournal::ensureFileForToday() {
    std::string today = todayStr();
    if (today == m_currentDate && m_file.is_open()) return;

    // 日期变了 → 关旧文件, 开新文件
    if (m_file.is_open()) m_file.close();

    m_currentDate = today;

    // 创建目录: logs/策略名/
    std::filesystem::path dir = std::filesystem::path(m_logDir) / m_strategyName;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    // 文件名: trade_2026-08-06.log
    std::string filename = "trade_" + today + ".log";
    m_currentFilePath = (dir / filename).string();

    m_file.open(m_currentFilePath, std::ios::app | std::ios::out);
    if (!m_file) {
        INTERNAL_WARN_STREAM << "[TradeJournal] 无法创建日志文件: " << m_currentFilePath;
    } else {
        INTERNAL_INFO_STREAM << "[TradeJournal] 日志文件: " << m_currentFilePath;
    }
}

void TradeJournal::log(const std::string& jsonLine) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ensureFileForToday();
    if (m_file.is_open()) {
        m_file << jsonLine << "\n";
        m_file.flush();  // 每条立即落盘, 防崩溃丢数据
    }
}

void TradeJournal::close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file.is_open()) {
        m_file.close();
        INTERNAL_INFO_STREAM << "[TradeJournal] 日志已关闭: " << m_currentFilePath;
    }
}

} // namespace domain::strategy
