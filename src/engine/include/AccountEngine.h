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

    // 数据变更通知
    using DataFn = std::function<void()>;
    void setOnDataChanged(DataFn cb);

    // gmsdk 回调入口（GmSessionEngine 调用, 线程安全）
    void onCash(const AccountInfo& a);
    void onPositionUpdate(const std::vector<Position>& positions);

    // 对接 domain 层 PositionAccountEngine 的接口（线程安全）
    void applyAccountEvent(const AccountInfo& a);
    void applyPositionEvent(const std::string& symbol, const Position& p);

private:
    AccountEngine();
    ~AccountEngine() = default;

    ::Strategy* m_strategy = nullptr;
    AccountInfo m_cachedAccount;
    std::unordered_map<std::string, Position> m_cachedPositions;
    DataFn m_onDataChanged;
    bool m_cacheValid = false;
    foundation::utils::Uuid m_accountSub;
    foundation::utils::Uuid m_positionSub;
    foundation::utils::Uuid m_tickSub;
    mutable std::shared_mutex m_mutex;  // 保护 m_cachedAccount + m_cachedPositions
};

} // namespace engine
