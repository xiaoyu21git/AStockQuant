// AccountEngine.h — 账户引擎（engine 层，零 Qt，不依赖其他引擎）
// 职责：账户/持仓缓存 + 查询，接收 gmsdk 回调。共享 GmSessionEngine 的 Strategy。
#pragma once

#include "GmSessionEngine.h"
#include "foundation/Utils/Uuid.h"
#include <shared_mutex>
#include <unordered_map>

namespace engine {

class AccountEngine {
public:
    /// @brief 账户+持仓原子快照 — 单次加锁同时返回两者，保证数据一致性
    struct Snapshot {
        AccountInfo account;
        std::vector<Position> positions;

        /// @brief 按纯代码（无交易所后缀）查持仓量
        /// 内部调用 AStockSymbol::codeOnly() 做 key 标准化
        [[nodiscard]] std::unordered_map<std::string, int64_t> posQtyByCode() const;
    };

    static AccountEngine& instance();

    bool initialize(::Strategy* strategy);
    void shutdown();
    bool initialized() const;

    // 同步查询（线程安全: 返回快照副本）
    AccountInfo           account();
    std::vector<Position> positions();

    /// @brief 原子快照 — 单次加锁同时返回 account + positions
    /// 替换 account()+positions() 两次独立调用，消除时间窗口不一致
    [[nodiscard]] Snapshot snapshot();

    // 数据变更通知 (多订阅者: UI 刷新 / 账本实时对账; 回调在锁外执行)
    using DataFn = std::function<void()>;
    using CallbackToken = std::uint64_t;

    /// @brief 注册数据变更回调, 返回注销 token (供 removeOnDataChanged 真注销)
    CallbackToken addOnDataChanged(DataFn cb);

    /// @brief 注销回调 (token 无效/已注销 → 无操作)
    /// 注意: notifyDataChanged 先拷贝后执行 — 注销前已拷贝的在途回调可能仍执行一次,
    /// 订阅方须自保 (如 weak_ptr/运行标志), 本接口保证的是不再产生新的执行
    void removeOnDataChanged(CallbackToken token);

    // gmsdk 回调入口（GmSessionEngine 调用, 线程安全）
    void onCash(const AccountInfo& a);
    void onPositionUpdate(const std::vector<Position>& positions);

    // 对接 domain 层 PositionAccountEngine 的接口（线程安全）
    void applyAccountEvent(const AccountInfo& a);
    void applyPositionEvent(const std::string& symbol, const Position& p);

private:
    AccountEngine();
    ~AccountEngine() = default;

    /// @brief 通知全部订阅者 (拷贝列表后在锁外执行, 回调可能再次进出本引擎)
    void notifyDataChanged();

    /// @brief 单条持仓写入缓存 + 首次持仓时间追踪 (调用方持锁外的统一入口)
    void updatePosition(const Position& p);

    ::Strategy* m_strategy = nullptr;
    AccountInfo m_cachedAccount;
    std::unordered_map<std::string, Position> m_cachedPositions;
    std::unordered_map<std::string, std::int64_t> m_firstSeenSec;  // sym → 首次出现于券商快照的 epoch 秒 (清零即抹除)
    std::vector<std::pair<CallbackToken, DataFn>> m_onDataChanged;
    CallbackToken m_nextCbToken{1};
    bool m_cacheValid = false;
    foundation::utils::Uuid m_accountSub;
    foundation::utils::Uuid m_positionSub;
    foundation::utils::Uuid m_tickSub;
    mutable std::shared_mutex m_mutex;  // 保护 m_cachedAccount + m_cachedPositions + m_firstSeenSec
    int m_positionLogThrottle = 0;
};

} // namespace engine
