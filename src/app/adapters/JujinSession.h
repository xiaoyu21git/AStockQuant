#pragma once
// JujinSession — 掘金 SDK 会话实现 (零 Qt, 纯 C++17)
// 实现 IBrokerSession 接口，直接封装 gmsdk::Strategy

#include "../../domain/trading/include/IBrokerSession.h"
#include "../../../thirdparty/gmsdk/strategy.h"
#include "../../../thirdparty/gmsdk/gmdef.h"

#include <mutex>
#include <string>
#include <vector>
#include <memory>

namespace app::adapters {

// ── SDK 枚举常量（纯整数，零字符串）───
namespace Sdk {
    constexpr int OrderSideBuy   = 1;
    constexpr int OrderSideSell  = 2;
    constexpr int OrderTypeMarket = 1;
    constexpr int OrderTypeLimit  = 2;
    constexpr int PositionEffectOpen  = 1;
    constexpr int PositionEffectClose = 2;
    constexpr int ModeLive = 1;
}

class JujinSession final : public domain::trading::IBrokerSession {
public:
    JujinSession() = default;
    ~JujinSession() override;

    // 禁止拷贝
    JujinSession(const JujinSession&) = delete;
    JujinSession& operator=(const JujinSession&) = delete;

    // ── IBrokerSession 实现 ──
    bool initialize(const char* token, const char* account_id) override;
    bool connect() override;
    void disconnect() override;
    [[nodiscard]] bool is_connected() const override;

    [[nodiscard]] std::string place_order(
        const char* symbol, int side, int order_type,
        int64_t volume, double price) override;
    bool cancel_order(const char* order_id) override;
    bool cancel_all_orders() override;

    [[nodiscard]] std::vector<std::unique_ptr<domain::trading::OrderRecord>>
    query_orders() override;

    [[nodiscard]] std::vector<std::unique_ptr<domain::trading::ExecutionReport>>
    query_execution_reports() override;

    [[nodiscard]] std::vector<std::unique_ptr<domain::trading::PositionInfo>>
    query_positions() override;

    [[nodiscard]] std::vector<std::unique_ptr<domain::trading::CashRecord>>
    query_cash() override;

    [[nodiscard]] std::unique_ptr<domain::trading::BrokerAccountInfo>
    query_account() override;

    [[nodiscard]] const char* last_error() const override;

private:
    std::unique_ptr<Strategy> m_strategy;
    mutable std::mutex m_mutex;
    std::string m_token;
    std::string m_account_id;
    std::string m_last_error;
    int m_connected{0};
};

} // namespace app::adapters
