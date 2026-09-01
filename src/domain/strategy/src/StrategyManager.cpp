#include "../include/StrategyManager.h"
#include "../include/RuntimeFactorSvc.h"
#include "../../factor/include/FactorInstanceManager.h"
#include "foundation/config/ConfigManager.hpp"
#include "foundation/log/logging.hpp"
#include "foundation/market/AStockSymbol.h"

#include <cstdio>
#include <sstream>
#include <stdexcept>

namespace domain::strategy {

StrategyManager& StrategyManager::instance() {
    static StrategyManager s_instance;
    return s_instance;
}

std::shared_ptr<StrategyEngine> StrategyManager::extractEngineLocked(const std::string& id)
{
    // 仅锁内调用: 取出并移除引擎, 停止动作由调用方在锁外执行 (stopEngineOutsideLock)
    auto it = m_engines.find(id);
    if (it == m_engines.end()) return nullptr;
    auto engine = std::move(it->second);
    m_engines.erase(it);
    return engine;
}

void StrategyManager::stopEngineOutsideLock(std::shared_ptr<StrategyEngine>& engine)
{
    // 统一停止序列: 停实盘循环 (阻塞 join 专用线程) → 停策略服务
    // 必须在锁外 — stopLiveLoop 的 worker 回调可能再取 m_mutex, 持锁 join 有死锁风险
    if (!engine) return;
    engine->stopLiveLoop();
    engine->stop();
}

StrategyEngine* StrategyManager::createEngine(const std::string& strategyId,
                                               std::unique_ptr<IRuntimeFactorService> factorSvc) {
    INTERNAL_INFO_STREAM << "[SM] 创建引擎: id=" << strategyId << " factorSvc=" << static_cast<void*>(factorSvc.get());
    auto engine = StrategyEngine::fromDb(strategyId, std::move(factorSvc));
    INTERNAL_INFO_STREAM << "[SM] 创建引擎: fromDb 返回 engine=" << static_cast<void*>(engine.get());
    if (!engine) return nullptr;

    // 旧引擎 (同 id 替换, 参数可能已变更): 锁内取出、锁外停止 (阻塞 join 不持锁)
    std::shared_ptr<StrategyEngine> old;
    StrategyEngine* ptr = nullptr;
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        ptr = engine.get();
        old = extractEngineLocked(strategyId);
        // 注册表以 shared_ptr 持有 (券商回调 weak_ptr 依赖), unique_ptr 移交所有权
        m_engines[strategyId] = std::shared_ptr<StrategyEngine>(std::move(engine));
        INTERNAL_INFO_STREAM << "[SM] 创建引擎: 已存储, 数量=" << m_engines.size();
    }
    stopEngineOutsideLock(old);
    return ptr;
}

StrategyEngine* StrategyManager::get(const std::string& id) const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_engines.find(id);
    return it != m_engines.end() ? it->second.get() : nullptr;
}

void StrategyManager::remove(const std::string& id) {
    if (id.empty()) return;
    std::shared_ptr<StrategyEngine> engine;
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        engine = extractEngineLocked(id);
    }
    stopEngineOutsideLock(engine);
}

void StrategyManager::startAll() {
    const std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, engine] : m_engines) {
        if (engine) engine->start();
    }
}

void StrategyManager::pauseAll() {
    const std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, engine] : m_engines) {
        if (engine) engine->pause();
    }
}

void StrategyManager::resumeAll() {
    const std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, engine] : m_engines) {
        if (engine) engine->resume();
    }
}

void StrategyManager::stopAll() {
    // 锁内取出全部引擎并清空注册表, 锁外逐个停止 — 阻塞 join 不持锁
    std::vector<std::shared_ptr<StrategyEngine>> engines;
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        engines.reserve(m_engines.size());
        for (auto& [id, engine] : m_engines) {
            if (engine) engines.push_back(std::move(engine));
        }
        m_engines.clear();
    }
    for (auto& engine : engines) {
        stopEngineOutsideLock(engine);
    }
}

std::vector<OrderRequest> StrategyManager::stepAll(const MarketDataPoint& mdp) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<OrderRequest> orders;
    for (auto& [id, engine] : m_engines) {
        if (!engine) continue;
        auto result = engine->step(mdp);
        if (result.has_value()) {
            orders.insert(orders.end(),
                          std::make_move_iterator(result->begin()),
                          std::make_move_iterator(result->end()));
        }
    }
    return orders;
}

void StrategyManager::setOrderListener(IOrderListener* listener)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, engine] : m_engines) {
        if (engine) {
            engine->setOrderListener(listener);
        }
    }
}

void StrategyManager::setBasketInterceptor(IBasketInterceptor* interceptor)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, engine] : m_engines) {
        if (engine) {
            engine->setBasketInterceptor(interceptor);
        }
    }
}

void StrategyManager::setExecutionMode(EngineExecutionMode mode)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, engine] : m_engines) {
        if (engine) {
            engine->setExecutionMode(mode);
        }
    }
}

std::size_t StrategyManager::count() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_engines.size();
}

bool StrategyManager::empty() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_engines.empty();
}

// ── 因子服务工厂 ──

std::unique_ptr<IRuntimeFactorService> StrategyManager::createFactorService()
{
    if (!m_factorInstanceManager) return nullptr;

    auto symbolResolver = [](std::uint32_t id) -> std::string {
        return foundation::market::AStockSymbol::fromInstrumentId(id);
    };
    auto factorNameResolver = [](std::uint64_t fid) -> std::string {
        return std::to_string(fid);
    };
    return std::make_unique<RuntimeFactorSvc>(
        *m_factorInstanceManager,
        std::move(symbolResolver),
        std::move(factorNameResolver));
}

// ── 统一生命周期 ──

StrategyEngine* StrategyManager::getOrCreateEngine(const std::string& strategyId)
{
    // 先查缓存
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_engines.find(strategyId);
        if (it != m_engines.end()) return it->second.get();
    }

    // 创建因子服务（如果 FactorInstanceManager 已注入）
    std::unique_ptr<IRuntimeFactorService> factorSvc = createFactorService();

    return createEngine(strategyId, std::move(factorSvc));
}

void StrategyManager::startStrategy(const std::string& strategyId)
{
    auto* engine = getOrCreateEngine(strategyId);
    if (!engine) {
        throw std::runtime_error("引擎创建失败: " + strategyId);
    }

    // ── 注入交易账户 ID (从 TradingConnection 配置读取, Phase 1.4) ──
    {
        auto cfg = foundation::config::ConfigManager::instance()
            .loadConfigFile(foundation::config::ConfigFile::TradingConnection);
        if (cfg && !cfg->isNull() && cfg->has("accountId")) {
            std::string accountId = cfg->get("accountId").asString();
            if (!accountId.empty()) {
                engine->setAccountId(accountId);
                INTERNAL_INFO_STREAM << "[SM] 已注入 accountId: " << strategyId
                                     << " -> " << accountId;
            } else {
                INTERNAL_ERROR_STREAM << "[SM] accountId 为空, 策略将无法下单: " << strategyId;
            }
        } else {
            INTERNAL_ERROR_STREAM << "[SM] 配置中未找到 accountId, 策略将无法下单: " << strategyId;
        }

        // ST禁新买开关 (可选键, 缺省关闭; 缺键不影响现有行为)
        if (cfg && !cfg->isNull() && cfg->has("stBuyFilterEnabled")) {
            const bool stFilter = cfg->get("stBuyFilterEnabled").asBool();
            engine->setStBuyFilterEnabled(stFilter);
            INTERNAL_INFO_STREAM << "[SM] ST禁新买过滤: " << strategyId
                                 << " -> " << (stFilter ? "启用" : "禁用");
        }
    }

    auto result = engine->start();
    if (!result.isOk()) {
        throw std::runtime_error("引擎启动失败: " + strategyId);
    }

    // 加载历史行情数据并注入引擎 — 失败则拒绝启动
    if (!engine->prepareMarketData()) {
        INTERNAL_ERROR_STREAM << "[SM] 历史数据加载失败或为空, 策略启动被拒绝: " << strategyId;
        engine->stop();
        throw std::runtime_error("历史数据不可用, 策略启动失败: " + strategyId);
    }

    // 注入订单监听器
    if (m_defaultOrderListener) {
        engine->setOrderListener(m_defaultOrderListener);
    }

    // 注入篮子拦截器 + 切换到半自动模式 (v0.16.0)
    if (m_defaultBasketInterceptor) {
        engine->setBasketInterceptor(m_defaultBasketInterceptor);
        engine->setExecutionMode(EngineExecutionMode::SemiAuto);
    }

    // 注入实盘数据持久化路径（lastEvalDay JSON 等）
    if (!m_liveDataPath.empty()) {
        engine->setLiveDataPath(m_liveDataPath);
    }

    // 启动实盘循环
    engine->startLiveLoop();

    INTERNAL_INFO_STREAM << "[SM] 启动策略成功: " << strategyId;
}

void StrategyManager::stopStrategy(const std::string& strategyId)
{
    // 锁内取出、锁外停止 (stopLiveLoop 阻塞 join, 持锁有死锁风险)
    std::shared_ptr<StrategyEngine> engine;
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        engine = extractEngineLocked(strategyId);
    }
    if (!engine) return;

    stopEngineOutsideLock(engine);
    INTERNAL_INFO_STREAM << "[SM] 停止策略成功: " << strategyId << " count=" << count();
}

// ── 回测产物快照表 (回测/实盘实例分离: 引擎不进注册表, 只发布不可变结果) ──

void StrategyManager::publishBacktestSnapshot(const std::string& strategyId,
                                              BacktestStatsSnapshot snapshot)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    // 关闭清理后丢弃发布 (防御调用顺序被改动; 正常流程 QML 销毁 → worker join 之后才 clear)
    if (m_snapshotsCleared) {
        INTERNAL_WARN_STREAM << "[SM] 快照表已清理, 丢弃回测快照发布: " << strategyId;
        return;
    }
    m_backtestSnapshots[strategyId] =
        std::make_shared<const BacktestStatsSnapshot>(std::move(snapshot));
    INTERNAL_INFO_STREAM << "[SM] 回测快照已发布: " << strategyId
                         << " 快照数=" << m_backtestSnapshots.size();
}

std::shared_ptr<const BacktestStatsSnapshot> StrategyManager::getBacktestSnapshot(const std::string& id) const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_backtestSnapshots.find(id);
    return it != m_backtestSnapshots.end() ? it->second : nullptr;
}

void StrategyManager::removeBacktestSnapshot(const std::string& id)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_backtestSnapshots.erase(id);
}

void StrategyManager::clearBacktestSnapshots()
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    // 先置标志再清空: 清空过程中任何新发布都被标志拦截 (publish 同锁, 无竞态)
    // map 只清登记, 不干预已取出的 shared_ptr 对象 (引用计数自然管理其生命周期)
    m_snapshotsCleared = true;
    m_backtestSnapshots.clear();
    INTERNAL_INFO_STREAM << "[SM] 回测快照表已清理";
}

std::size_t StrategyManager::backtestSnapshotCount() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_backtestSnapshots.size();
}

} // namespace domain::strategy