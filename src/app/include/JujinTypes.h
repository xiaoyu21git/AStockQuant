#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace app::jujin {

struct GmToken { std::string value; };
struct GmAccountId { std::string value; };
struct GmRuntimeStrategyId { std::string value; };
struct GmBoundStrategyId { std::string value; };
struct GmBoundStrategyEntry { GmBoundStrategyId strategyId; std::string strategyName; };
struct GmSymbol { std::string value; };
struct GmServerUrl { std::string value; };

enum class GmSessionPhase : std::uint8_t { Unknown = 0, PreOpen = 1, Trading = 2,
    LunchBreak = 3, AfterHours = 4, Closed = 5 };
const char* toString(GmSessionPhase phase) noexcept;

enum class GmOrderSide : std::uint8_t { Unknown = 0, Buy = 1, Sell = 2 };
GmOrderSide gmOrderSideFromInt(int value) noexcept;

enum class GmOrderStatus : std::uint8_t { Pending = 0, Submitted = 1,
    PartialFilled = 2, Filled = 3, Cancelled = 4, Rejected = 5 };
GmOrderStatus gmOrderStatusFromInt(int value) noexcept;

struct GmConnectorConfig {
    GmToken token;
    GmAccountId accountId;
    GmRuntimeStrategyId runtimeStrategyId;
    GmServerUrl serverUrl;
    std::vector<GmBoundStrategyEntry> boundStrategies;
    std::vector<GmSymbol> symbols;
    std::string mode{"1"};
    bool simtradeOnly{false};
    bool readOnly{false};
    bool enabled{false};
    std::uint32_t maxMarketSubscriptions{32};
    std::uint32_t marketSubscriptionBatchSize{4};
    std::vector<std::string> clientProcessNames;
};

struct GmUnfinishedOrderItem {
    std::string orderId;
    std::string businessStrategyId;
    std::string runtimeStrategyId;
    std::string symbol;
    int side{0};
    double price{0.0};
    std::int64_t quantity{0};
    std::int64_t filledQuantity{0};
    double filledNotional{0.0};
    int status{0};
    std::string message;
    std::string createdAt;
    std::string updatedAt;
};

} // namespace app::jujin