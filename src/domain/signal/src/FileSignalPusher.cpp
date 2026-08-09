// FileSignalPusher.cpp — 信号文件推送实现
#include "ISignalPusher.h"

#include <foundation/log/logging.hpp>

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace domain::sigout {
namespace {

/// @brief 生成文件名: signal_{format}_{YYYYMMDD}.txt
std::string makeFilename(const std::string& outputDir,
                         const std::string& formatTag,
                         const std::string& dateStr) {
    std::ostringstream oss;
    oss << outputDir;
    if (!outputDir.empty() && outputDir.back() != '/' && outputDir.back() != '\\') {
        oss << '/';
    }
    // dateStr 来自 ISO8601: "2026-08-09T09:35:00" → "20260809"
    std::string compactDate = dateStr.substr(0, 4) + dateStr.substr(5, 2) + dateStr.substr(8, 2);
    oss << "signal_" << formatTag << "_" << compactDate << ".txt";
    return oss.str();
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// FileSignalPusher
// ═══════════════════════════════════════════════════════════════════════════

class FileSignalPusher final : public ISignalPusher {
public:
    /// @param outputDir 信号文件输出目录 (如 "./signals/")
    /// @param formatTag 格式标签 (如 "ths", "tdx")
    explicit FileSignalPusher(std::string outputDir, std::string formatTag)
        : m_outputDir(std::move(outputDir))
        , m_formatTag(std::move(formatTag))
    {
    }

    bool push(const std::string& formattedBatch) override {
        if (formattedBatch.empty()) return true;

        // 生成当日文件名
        auto now = std::chrono::system_clock::now();
        auto nowTime = std::chrono::system_clock::to_time_t(now);
        std::tm tmBuf{};
#ifdef _WIN32
        localtime_s(&tmBuf, &nowTime);
#else
        localtime_r(&nowTime, &tmBuf);
#endif
        std::ostringstream dateOss;
        dateOss << std::setfill('0')
                << std::setw(4) << (tmBuf.tm_year + 1900) << "-"
                << std::setw(2) << (tmBuf.tm_mon + 1) << "-"
                << std::setw(2) << tmBuf.tm_mday
                << "T"
                << std::setw(2) << tmBuf.tm_hour << ":"
                << std::setw(2) << tmBuf.tm_min << ":"
                << std::setw(2) << tmBuf.tm_sec;

        std::string filename = makeFilename(m_outputDir, m_formatTag, dateOss.str());
        std::string traceId = extractTraceId(formattedBatch);

        // 追加写入 (append mode: 同一天多次 step() 合并到同一文件)
        std::ofstream file(filename, std::ios::app);
        if (!file.is_open()) {
            INTERNAL_ERROR_STREAM << "[FileSignalPusher] Cannot open file: " << filename
                                  << " trace:" << traceId;
            return false;
        }

        file << formattedBatch;
        if (!file.good()) {
            INTERNAL_ERROR_STREAM << "[FileSignalPusher] Write failed: " << filename
                                  << " bytes=" << formattedBatch.size()
                                  << " trace:" << traceId;
            return false;
        }

        file.close();
        INTERNAL_INFO_STREAM << "[FileSignalPusher] Written to " << filename
                             << " bytes=" << formattedBatch.size()
                             << " trace:" << traceId;
        return true;
    }

    [[nodiscard]] std::string targetName() const override {
        return "本地文件";
    }

private:
    /// @brief 从 batch 中提取第一条信号的 traceId (用于日志关联)
    static std::string extractTraceId(const std::string& batch) {
        // 简单启发式: 查找 "trace:" 前缀, 取下一个字串
        auto pos = batch.find("trace:");
        if (pos == std::string::npos) return "unknown";
        pos += 6; // skip "trace:"
        auto end = batch.find_first_of(" \t\n\r", pos);
        if (end == std::string::npos) end = batch.size();
        return batch.substr(pos, end - pos);
    }

    std::string m_outputDir;
    std::string m_formatTag;
};

} // namespace domain::sigout

// ── 工厂函数 ──

std::unique_ptr<domain::sigout::ISignalPusher>
domain::sigout::createFilePusher(const std::string& outputDir,
                                 const std::string& formatTag) {
    return std::make_unique<domain::sigout::FileSignalPusher>(outputDir, formatTag);
}
