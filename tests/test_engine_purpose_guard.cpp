// test_engine_purpose_guard.cpp
// 引擎用途守卫回归测试 — 回测/实盘实例分离契约: 用途构造时定死, 用途不可混用
// 零 DB 依赖: fromParams 走 Builder 路径构建 Live 引擎; fromDbForBacktest 用不存在 id 验证失败语义
// 用法: 直接运行, 失败计数非零时返回非零退出码

#include "domain/strategy/include/IStrategyService.h"
#include "domain/backtest/include/BacktestRequest.h"

#include <cstdio>

using domain::strategy::StrategyEngine;
using domain::strategy::EnginePurpose;
using domain::strategy::StrategyCreationParams;

int main() {
    int failures = 0;
    const auto expect = [&failures](bool cond, const char* msg) {
        std::printf("%s: %s\n", cond ? "  PASS" : "  FAIL", msg);
        if (!cond) ++failures;
    };

    // ── 1. fromParams 构建 Live 引擎 (零 DB 依赖), purpose() 与默认用途匹配 ──
    StrategyCreationParams params;
    params.strategyId = "guard-test-live";
    params.strategyName = "guard_test";
    auto liveEngine = StrategyEngine::fromParams(params);
    expect(liveEngine != nullptr, "fromParams 构建引擎成功");
    if (liveEngine) {
        expect(liveEngine->purpose() == EnginePurpose::Live, "fromParams 引擎用途 = Live");

        // ── 2. 用途守卫: 实盘用途引擎禁止执行回测 (dataSvc 传 nullptr, 守卫先行拒绝) ──
        domain::backtest::BacktestRequest req;
        auto result = liveEngine->backtest(req, nullptr);
        expect(!result.success, "Live 引擎 backtest() 被用途守卫拒绝");
        expect(!result.errorMessage.empty(), "守卫拒绝时携带错误信息");

        // ── 3. 未回测引擎快照为默认零值 (值类型, 调用安全) ──
        auto snap = liveEngine->snapshotStats();
        expect(snap.backtestDateRange.empty(), "未回测引擎快照日期区间为空");
    }

    // ── 4. fromDbForBacktest 失败语义: 不存在的策略 id → nullptr, 不产生注册副作用 ──
    auto btEngine = StrategyEngine::fromDbForBacktest("__guard_test_nonexistent_uuid__", nullptr);
    expect(btEngine == nullptr, "fromDbForBacktest(不存在id) 返回 nullptr");

    std::printf(failures == 0 ? "ALL PASSED\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
