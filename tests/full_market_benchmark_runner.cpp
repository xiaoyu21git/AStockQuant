// ══════════════════════════════════════════════════════════════════════════════
// 因子回测管线基准程序 (Phase 0: 基线台账 + 基线 JSON + 小块对拍门禁)
//
// 用法:
//   --list                                   列出 DB 中全部因子实例 (id/名称/类型)
//   --arrow <path>                           数据集 Arrow 文件路径 (必填, 除 --list)
//   --factor <id>                            因子实例 id (可重复, 多因子取均值)
//   --start <YYYY-MM-DD> / --end <YYYY-MM-DD> 日期过滤 (小块对拍: 只留 ~120 交易日)
//   --out <json>                             回测结果 JSON 输出路径 (基线)
//   --forward <d> / --rebalance <d>          前向/调仓周期 (默认 30/15)
//   --groups <n> / --ascending <0|1>         分组数/因子方向 (默认 5/1)
//   --winsorize <q>                          缩尾分位数 (默认 0.005, 0=不缩尾)
//   --threads <n>                            块级并行 worker 数 (默认 1; <2 走串行路径)
//   --cancel-after-ms <t>                    软取消单测: t 毫秒后置取消标志; 断言结果不发布
//
// 与 UI 完全同接线 (FactorBacktestBridge::initialize + startRun):
//   BacktestScheduler + BacktestDataService + FactorEngine + BacktestReporter
//   + FactorInstanceManager(NativePg 连接) + ArrowMarketDataView → Orchestrator
// 结果 JSON = Orchestrator::onComplete 原文 = UI 展示的同一格式 → 基线可位级对拍。
// 耗时台账: [PERF] 阶段级汇总 (视图构建/编排回测/JSON 落盘)。
// ══════════════════════════════════════════════════════════════════════════════

#include "FactorBacktestOrchestrator.h"
#include "BacktestRunConfig.h"
#include "FactorInstanceManager.h"
#include "DataAvailabilityChecker.h"
#include "factor_compute/FactorEngine.h"
#include "factor_compute/ArrowMarketDataView.h"
#include "BacktestScheduler.h"
#include "database/NativePgConnectionPool.h"
#include "database/DatabaseConfig.h"
#include "foundation/perf/Stopwatch.h"
#include "foundation/log/logging.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

/// @brief 基准运行参数 (argv 解析结果)
struct BenchArgs {
    bool listOnly = false;
    std::string arrowPath;
    std::vector<std::string> factorIds;   // 直接 id (纯 ASCII 时可用; 中文 id 受 argv 编码影响不可靠)
    std::vector<int> factorIdxs;          // 因子索引 (--list 输出的序号, 0 起; 编码无关, 推荐)
    std::string startDate;      // "YYYY-MM-DD" 或空
    std::string endDate;        // "YYYY-MM-DD" 或空
    std::string outPath;        // 结果 JSON 输出路径
    int forwardDays = 30;
    int rebalanceDays = 15;
    int numGroups = 5;
    bool ascending = true;
    double winsorize = 0.005;
    int workerThreads = 1;      // 块级并行 worker 数
    int cancelAfterMs = 0;      // 软取消单测: >0 时启用
};

/// @brief 解析 "YYYY-MM-DD" → YYYYMMDD 整数 (非法返回 0)
std::int32_t parseIsoDateToInt(const std::string& text)
{
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') return 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == 4 || i == 7) continue;
        if (text[i] < '0' || text[i] > '9') return 0;
    }
    return static_cast<std::int32_t>(
        (text[0] - '0') * 10000000 + (text[1] - '0') * 1000000
        + (text[2] - '0') * 100000 + (text[3] - '0') * 10000
        + (text[5] - '0') * 1000 + (text[6] - '0') * 100
        + (text[8] - '0') * 10 + (text[9] - '0'));
}

/// @brief 构建 PG 连接配置 (与应用 config.yaml 同源: astock/astock123@localhost:5432/astock_quant)
astock::database::DatabaseConfig makeDatabaseConfig()
{
    astock::database::DatabaseConfig cfg;
    cfg.username = "astock";
    cfg.password = "astock123";
    return cfg;
}

/// @brief 打印用法
void printUsage(const char* prog)
{
    std::printf(
        "用法: %s [--list] | [--arrow <path> --factor <id>...] [选项]\n"
        "  --list                     列出 DB 全部因子实例\n"
        "  --arrow <path>             Arrow 数据集路径\n"
        "  --factor <id>              因子实例 id (可重复, 纯 ASCII; 中文 id 请用 --factor-idx)\n"
        "  --factor-idx <n>            因子索引 (--list 输出的序号 0 起, 可重复, 编码无关)\n"
        "  --start <YYYY-MM-DD>       日期过滤起点 (小块对拍用)\n"
        "  --end <YYYY-MM-DD>         日期过滤终点\n"
        "  --out <json>               结果 JSON 输出路径\n"
        "  --forward <d>              前向窗口 (默认 30)\n"
        "  --rebalance <d>            调仓周期 (默认 15)\n"
        "  --groups <n>               分组数 (默认 5)\n"
        "  --ascending <0|1>          因子方向 (默认 1)\n"
        "  --winsorize <q>            缩尾分位数 (默认 0.005)\n"
        "  --threads <n>              块级并行 worker 数 (默认 1)\n"
        "  --cancel-after-ms <t>      软取消单测: t 毫秒后置取消标志, 断言不发布结果\n",
        prog);
}

/// @brief argv 解析 (返回 false = 参数错误)
bool parseArgs(int argc, char** argv, BenchArgs& out)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* optName) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "缺少参数值: %s\n", optName);
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--list") {
            out.listOnly = true;
        } else if (arg == "--arrow") {
            const char* v = next("--arrow"); if (!v) return false;
            out.arrowPath = v;
        } else if (arg == "--factor") {
            const char* v = next("--factor"); if (!v) return false;
            out.factorIds.push_back(v);
        } else if (arg == "--factor-idx") {
            const char* v = next("--factor-idx"); if (!v) return false;
            out.factorIdxs.push_back(std::atoi(v));
        } else if (arg == "--start") {
            const char* v = next("--start"); if (!v) return false;
            out.startDate = v;
        } else if (arg == "--end") {
            const char* v = next("--end"); if (!v) return false;
            out.endDate = v;
        } else if (arg == "--out") {
            const char* v = next("--out"); if (!v) return false;
            out.outPath = v;
        } else if (arg == "--forward") {
            const char* v = next("--forward"); if (!v) return false;
            out.forwardDays = std::atoi(v);
        } else if (arg == "--rebalance") {
            const char* v = next("--rebalance"); if (!v) return false;
            out.rebalanceDays = std::atoi(v);
        } else if (arg == "--groups") {
            const char* v = next("--groups"); if (!v) return false;
            out.numGroups = std::atoi(v);
        } else if (arg == "--ascending") {
            const char* v = next("--ascending"); if (!v) return false;
            out.ascending = (std::atoi(v) != 0);
        } else if (arg == "--winsorize") {
            const char* v = next("--winsorize"); if (!v) return false;
            out.winsorize = std::atof(v);
        } else if (arg == "--threads") {
            const char* v = next("--threads"); if (!v) return false;
            out.workerThreads = std::atoi(v);
        } else if (arg == "--cancel-after-ms") {
            const char* v = next("--cancel-after-ms"); if (!v) return false;
            out.cancelAfterMs = std::atoi(v);
        } else {
            std::fprintf(stderr, "未知参数: %s\n", arg.c_str());
            return false;
        }
    }
    if (!out.listOnly && out.arrowPath.empty()) {
        std::fprintf(stderr, "必须提供 --arrow\n");
        return false;
    }
    if (!out.listOnly && out.factorIds.empty() && out.factorIdxs.empty()) {
        std::fprintf(stderr, "必须提供至少一个 --factor <id> 或 --factor-idx <n> (或 --list)\n");
        return false;
    }
    return true;
}

/// @brief 列出 DB 全部因子实例 (id/类型/名称) — 供挑选场景因子
int runList(factor::FactorInstanceManager& instanceMgr)
{
    const auto all = instanceMgr.listAllInstances();
    std::printf("共 %zu 个因子实例:\n", all.size());
    for (const auto& info : all) {
        std::printf("  %-50s type=%d  %s\n",
                    info.instanceId.c_str(),
                    static_cast<int>(info.factorType),
                    info.instanceName.c_str());
    }
    return 0;
}

/// @brief 运行回测并落盘基线 JSON (与 UI 同一接线)
int runBacktest(const BenchArgs& args)
{
    // ── PG 连接池 (与应用同源: astock/astock123@localhost:5432/astock_quant) ──
    astock::database::DatabaseConfig dbConfig = makeDatabaseConfig();
    auto& pool = astock::database::NativePgConnectionPool::instance();
    if (!pool.initialize(dbConfig)) {
        std::fprintf(stderr, "FATAL: PG 连接池初始化失败\n");
        return 2;
    }
    auto db = pool.getConnection();
    if (!db || !db->isOpen()) {
        std::fprintf(stderr, "FATAL: PG 连接不可用\n");
        return 2;
    }

    foundation::perf::Stopwatch swTotal;
    swTotal.start();

    // ── 域组件 (与 FactorBacktestBridge::initialize 完全同构) ──
    auto scheduler = std::make_unique<domain::scheduler::BacktestScheduler>(0ULL);
    auto dataSvc = std::make_unique<factor::compute::BacktestDataService>();
    auto engine = std::make_unique<factor::compute::FactorEngine>(512ULL * 1024ULL * 1024ULL);
    auto reporter = std::make_unique<factor::compute::BacktestReporter>();

    auto dataChecker = std::make_shared<factor::DataAvailabilityChecker>(db);
    factor::FactorInstanceManager instanceMgr(db, dataChecker);
    engine->setInstanceManager(&instanceMgr);
    engine->setDataService(dataSvc.get());

    // ── --factor-idx → id 解析 (从 DB 直接取 id, 绕开中文 argv 编码问题) ──
    std::vector<std::string> resolvedFactorIds = args.factorIds;
    if (!args.factorIdxs.empty()) {
        const auto all = instanceMgr.listAllInstances();
        for (const int idx : args.factorIdxs) {
            if (idx < 0 || static_cast<size_t>(idx) >= all.size()) {
                std::fprintf(stderr, "FATAL: 因子索引越界: %d (共 %zu 个)\n", idx, all.size());
                return 3;
            }
            resolvedFactorIds.push_back(all[static_cast<size_t>(idx)].instanceId);
            std::printf("[因子] idx=%d -> %s\n", idx, all[static_cast<size_t>(idx)].instanceId.c_str());
        }
    }

    Factor::backtest::FactorBacktestOrchestrator orchestrator;
    orchestrator.setScheduler(scheduler.get());
    orchestrator.setDataService(dataSvc.get());
    orchestrator.setFactorEngine(engine.get());
    orchestrator.setReporter(reporter.get());

    // ── Arrow 视图 ──
    foundation::perf::Stopwatch swView;
    swView.start();
    factor::compute::ArrowMarketDataView arrowView(args.arrowPath);
    swView.stop();
    if (arrowView.dates().empty() || arrowView.instruments().empty()) {
        std::fprintf(stderr, "FATAL: Arrow 数据集为空或索引失败: %s\n", args.arrowPath.c_str());
        return 3;
    }
    std::printf("[视图] dates=%zu symbols=%zu\n",
                arrowView.dates().size(), arrowView.instruments().size());
    swView.report("ArrowView 构造+索引");
    dataSvc->setMarketView(&arrowView);
    dataSvc->buildViewForFields({});

    // ── 回测配置 ──
    Factor::backtest::BacktestRunConfig cfg;
    cfg.factorMode = Factor::backtest::FactorMode::Single;   // 多因子走均值路径
    cfg.factorIds = resolvedFactorIds;
    if (!args.startDate.empty()) {
        cfg.cacheStartDate.value = parseIsoDateToInt(args.startDate);
    }
    if (!args.endDate.empty()) {
        cfg.cacheEndDate.value = parseIsoDateToInt(args.endDate);
    }
    cfg.forwardDays = args.forwardDays;
    cfg.rebalanceDays = args.rebalanceDays;
    cfg.numGroups = args.numGroups;
    cfg.ascending = args.ascending;
    cfg.winsorizeQuantile = args.winsorize;
    cfg.workerThreads = std::max(1, args.workerThreads);

    std::printf("[回测] factors=%zu forward=%dd rebalance=%dd groups=%d "
                "threads=%d start=%s end=%s\n",
                resolvedFactorIds.size(), cfg.forwardDays, cfg.rebalanceDays,
                cfg.numGroups, cfg.workerThreads,
                args.startDate.c_str(), args.endDate.c_str());

    // ── 编排回测 (同步执行) ──
    foundation::perf::Stopwatch swRun;
    std::string serializedResult;
    swRun.start();

    // 软取消单测: 独立线程定时置取消标志 (模拟 UI cancelBacktest), 断言不发布结果
    std::atomic<bool> cancelFlag{false};
    std::thread cancelThread;
    if (args.cancelAfterMs > 0) {
        cancelThread = std::thread([&cancelFlag, ms = args.cancelAfterMs]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            cancelFlag.store(true, std::memory_order_release);
        });
    }

    orchestrator.run(
        cfg,
        [](double progress, const std::string& status) {
            std::printf("[进度] %.1f%% %s\n", progress, status.c_str());
        },
        [&serializedResult](const std::string& serialized) {
            serializedResult = serialized;
        },
        args.cancelAfterMs > 0 ? &cancelFlag : nullptr);
    swRun.stop();
    swRun.report("Orchestrator 全链回测");
    if (cancelThread.joinable()) cancelThread.join();

    // ── 结果落盘 ──
    if (args.cancelAfterMs > 0) {
        // 取消路径: 结果必须不发布 (serializedResult 为空), 且远早于完整回测返回
        const double elapsedMs = swRun.elapsedMilliseconds();
        if (serializedResult.empty()) {
            std::printf("[取消单测] PASS: %.0f ms 返回, 结果未发布 (取消延迟=单日检查点+排水)\n",
                        elapsedMs);
        } else {
            std::fprintf(stderr, "[取消单测] FAIL: 取消后仍发布了结果 (%zu 字节)\n",
                         serializedResult.size());
            return 6;
        }
        if (cancelFlag.load(std::memory_order_acquire)) {
            return 0;
        }
        std::fprintf(stderr, "[取消单测] WARN: 结果为空但取消标志未置位 (回测早于取消定时自然结束)\n");
        return 0;
    }
    if (serializedResult.empty()) {
        std::fprintf(stderr, "FATAL: 回测未产出结果 JSON\n");
        return 4;
    }
    if (!args.outPath.empty()) {
        std::ofstream ofs(args.outPath, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
            std::fprintf(stderr, "FATAL: 无法写入输出文件: %s\n", args.outPath.c_str());
            return 5;
        }
        ofs << serializedResult;
        ofs.close();
        std::printf("[输出] 基线 JSON 已写入: %s (%zu 字节)\n",
                    args.outPath.c_str(), serializedResult.size());
    } else {
        std::printf("[结果] %s\n", serializedResult.c_str());
    }

    swTotal.stop();
    swTotal.report("基准程序总耗时");
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    BenchArgs args;
    if (!parseArgs(argc, argv, args)) {
        printUsage(argv[0]);
        return 1;
    }

    // --list 只需 DB, 不需要 Arrow 数据集
    if (args.listOnly) {
        astock::database::DatabaseConfig dbConfig = makeDatabaseConfig();
        auto& pool = astock::database::NativePgConnectionPool::instance();
        if (!pool.initialize(dbConfig)) {
            std::fprintf(stderr, "FATAL: PG 连接池初始化失败\n");
            return 2;
        }
        auto db = pool.getConnection();
        if (!db || !db->isOpen()) {
            std::fprintf(stderr, "FATAL: PG 连接不可用\n");
            return 2;
        }
        auto dataChecker = std::make_shared<factor::DataAvailabilityChecker>(db);
        factor::FactorInstanceManager instanceMgr(db, dataChecker);
        return runList(instanceMgr);
    }

    return runBacktest(args);
}
