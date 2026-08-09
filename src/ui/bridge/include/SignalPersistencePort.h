// SignalPersistencePort.h — 信号持久化端口 PG 实现
// 桥接层: 依赖 NativePgConnectionPool, 异步写入 live.signal_history
#pragma once

#include "../../../domain/signal/include/ISignalPersistencePort.h"

#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace astock::database {
class ISqlDatabase;
}

/// @brief 信号持久化端口 PG 实现
/// 使用 NativePgConnectionPool 异步批量写入 live.signal_history 表
class SignalPersistencePort final : public domain::sigout::ISignalPersistencePort {
public:
    SignalPersistencePort();
    ~SignalPersistencePort() override;

    void saveAsync(const std::vector<domain::sigout::SignalOutput>& signalList,
                   const std::string& pushError) override;
    void flush() override;

private:
    /// @brief 同步批量 INSERT (在异步任务中执行)
    void saveBatchSync(const std::vector<domain::sigout::SignalOutput>& signalList,
                       const std::string& pushError);

    /// @brief 生成今日 trading_day (北京时间)
    static std::string todayBeijingDate();

    /// @brief 枚举 → DB 字符串
    static const char* signalFormatToDbString(int formatIndex);
    static const char* pushTargetToDbString(int targetIndex);

    std::mutex m_futuresMutex;
    std::vector<std::future<void>> m_pendingFutures;
};
