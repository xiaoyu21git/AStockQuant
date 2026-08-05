#pragma once

#include "BaseFactor.h"
#include "FactorMetricConfig.h"
#include "factor_enums.h"

#include "../../../engine/include/Event/EventFormat.hpp"

#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace factor {

/// @brief 事件缓存记录 (由 EventBus 回调写入)
struct EventRecord {
    std::string eventType;                             // "news.earnings" 等
    double sentimentScore = 0.0;                       // -1.0 ~ 1.0
    double confidence = 0.0;                           // 0.0 ~ 1.0
    std::unordered_map<std::string, std::string> tags;  // {"超预期":"true",...}
    int64_t timestampMs = 0;                           // Unix 毫秒
};

/// @brief 事件驱动因子
///
/// 数据来源: EventBus 上的 "news.*" 事件 (非传统行情/财务数据库)
/// 接入方式: 继承 BaseFactor, 通过 FactorInstanceManager 注册为 FactorType::EVENT_DRIVEN
///
/// 核心逻辑:
///   1. onEvent() 从 EventBus 接收金融事件, 写入 m_eventCache
///   2. calculate() 从事件缓存中计算每个标的的因子值:
///      score = avg(sentiment × weight + 标签调整) × confidence
///   3. applyCommonStandardization() 标准化输出
///
/// 线程安全: shared_mutex (onEvent 写, calculate 读)
class EventDrivenFactor final : public BaseFactor {
public:
    struct Params : CommonParams {
        double sentimentWeight = 0.2;       // 情感分贡献权重
        double superExpectedBonus = 0.1;    // 超预期标签额外加分
        double investigationPenalty = 0.3;  // 立案调查标签减分
        int maxEventAgeHours = 24;          // 事件有效期 (小时)
        int maxRecordsPerSymbol = 100;      // 每个标的最多保留事件数

        void fromJson(const foundation::json::JsonFacade& json);
    };

    EventDrivenFactor() = default;
    ~EventDrivenFactor() override;

    // ── BaseFactor 接口 ──
    CalculationResult calculate(const CalculationContext& ctx) override;
    DataRequirements getDataRequirements() const override { return {}; }
    BoundaryRules getBoundaryRules() const override;
    int getLookbackDays() const override { return 1; }

    // ── EventBus 事件接收 ──
    /// @brief 接收来自 EventBus 的金融事件, 写入内存缓存
    void onEvent(const engine::EventFormat& event);

    /// @brief 订阅 C++ EventBus (AppBootstrap 中调用一次)
    static void subscribeToEventBus();

    /// @brief 注册活跃实例 (构造时调用)
    static void registerInstance(EventDrivenFactor* instance);
    /// @brief 注销实例 (析构时调用)
    static void unregisterInstance(EventDrivenFactor* instance);

    // ── 回测兼容 ──
    /// @brief 回测专用: 批量加载历史事件
    void loadHistoricalEvents(
        const std::vector<engine::EventFormat>& events);
    /// @brief 回测专用: 从PG加载商品事件到缓存
    void loadEventsFromDb(const std::string& startDate, const std::string& endDate);

    /// @brief 清空事件缓存 (回测日期切换时调用)
    void clearCache();

    // ── 工厂 ──
    static std::unique_ptr<BaseFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> checker);

private:
    Params m_params;
    mutable std::shared_mutex m_cacheMutex;

    /// symbol → 该标的所有事件记录
    std::unordered_map<std::string, std::vector<EventRecord>> m_eventCache;

    void loadConfig(const foundation::json::JsonFacade& config) override;

    /// @brief 计算单个标的的信号分 (调用者持有锁)
    double computeSignalScore(
        const std::string& symbol,
        const std::vector<EventRecord>& records) const;
};

} // namespace factor
