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
    static AccountEngine& instance();

    bool initialize(void* strategy);
    void shutdown();
    bool initialized() const;

    // 同步查询（线程安全: 返回快照副本）
    AccountInfo           account();
    std::vector<Position> positions();

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

    void* m_strategy = nullptr;
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
