// test_eod_order_flow.cpp
// 日终 EOD 下单流程测试 — 使用今天行情触发 evaluateEndOfDay，验证订单生成
//
// 用法:
//   test_eod_order_flow <strategyId> [tradingDay] [pgConnString]
//
//   pgConnString 默认: "host=localhost port=5432 dbname=astock user=postgres password="
//
// 环境要求:
//   - strategy 表中存在对应 strategyId 的策略记录
//   - mkt.daily_bar 有目标交易日的行情数据

#include "domain/strategy/include/IStrategyService.h"
#include "domain/strategy/include/StrategyServiceTypes.h"
#include "domain/strategy/include/IOrderListener.h"
#include "domain/strategy/include/DailyEodScheduler.h"
#include "engine/include/AccountEngine.h"
#include "engine/include/GmSessionEngine.h"
#include "database/NativePgConnectionPool.h"
#include "database/DatabaseConfig.h"
#include "foundation/log/logging.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>

using namespace domain::strategy;
using namespace domain::trading;

// ═══════════════════════════════════════════════════════════════════
// 订单监听器 — 打印订单到 stdout
// ═══════════════════════════════════════════════════════════════════

class TestOrderListener final : public IOrderListener {
public:
    int m_submittedCount = 0;
    double m_totalQuantity = 0;

    void onOrders(const std::vector<OrderRequest>& orders) override {
        m_submittedCount += static_cast<int>(orders.size());
        std::cout << "\n═══════════════════════════════════════════\n";
        std::cout << "EOD 篮子提交: " << orders.size() << " 笔订单\n";
        std::cout << "═══════════════════════════════════════════\n";
        for (const auto& o : orders) {
            const char* side = o.side() == OrderSide::Buy ? "Buy " : "Sell";
            double score = o.extensionAs<double>(ExtKey::kSignalScore, 0.0);
            double weight = o.extensionAs<double>(ExtKey::kTargetWeight, 0.0);
            m_totalQuantity += o.quantity();
            std::printf("  %s %s qty=%.0f price=%.2f score=%.2f weight=%.1f%%\n",
                        side, o.symbol().c_str(),
                        o.quantity(), o.price(), score, weight * 100.0);
        }
        std::cout << "═══════════════════════════════════════════\n\n";
    }
};

// ═══════════════════════════════════════════════════════════════════

static std::string getTodayStr() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    struct tm local;
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&local, &tt);
#else
    localtime_r(&tt, &local);
#endif
    char buf[16];
    snprintf(buf, sizeof(buf), "%04d%02d%02d",
             local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    return std::string(buf);
}

static void injectTestAccount() {
    engine::AccountInfo acc;
    acc.accountId = "test_account";
    acc.totalAsset = 1'000'000.0;
    acc.availableCash = 1'000'000.0;
    acc.marketValue = 0.0;
    acc.frozenCash = 0.0;
    engine::AccountEngine::instance().applyAccountEvent(acc);

    engine::Position emptyPos;
    emptyPos.symbol = "";
    emptyPos.quantity = 0;
    engine::AccountEngine::instance().applyPositionEvent("", emptyPos);
}

// ═══════════════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "用法: test_eod_order_flow <strategyId> [tradingDay] [pgConnString]\n\n";
        std::cerr << "示例:\n";
        std::cerr << "  test_eod_order_flow strat_001\n";
        std::cerr << "  test_eod_order_flow strat_001 20260806\n";
        std::cerr << "  test_eod_order_flow strat_001 20260806 \"host=localhost port=5432 dbname=astock user=postgres\"\n";
        return 1;
    }

    std::string strategyId = argv[1];
    std::string tradingDay = argc >= 3 ? argv[2] : getTodayStr();
    std::string connStr = argc >= 4 ? argv[3]
        : "host=localhost port=5432 dbname=astock user=postgres";

    std::cout << "╔══════════════════════════════════════════╗\n";
    std::cout << "║  EOD 下单流程测试                         ║\n";
    std::cout << "╠══════════════════════════════════════════╣\n";
    std::printf("║  策略ID: %-30s ║\n", strategyId.c_str());
    std::printf("║  交易日: %-30s ║\n", tradingDay.c_str());
    std::printf("║  PG连接: %-30s ║\n", connStr.c_str());
    std::cout << "╚══════════════════════════════════════════╝\n\n";

    // ── 1. 初始化 PG 连接池 ──
    std::cout << "[1/6] 初始化 PG 连接池...\n";
    astock::database::DatabaseConfig dbConfig;
    // 手动解析简化的连接字符串
    {
        std::string s = connStr;
        auto findVal = [&s](const std::string& key) -> std::string {
            auto pos = s.find(key + "=");
            if (pos == std::string::npos) return "";
            pos += key.size() + 1;
            auto end = s.find(' ', pos);
            return s.substr(pos, end == std::string::npos ? end : end - pos);
        };
        std::string host = findVal("host");
        if (!host.empty()) dbConfig.host = host;
        std::string port = findVal("port");
        if (!port.empty()) dbConfig.port = std::stoi(port);
        std::string dbname = findVal("dbname");
        if (!dbname.empty()) dbConfig.database = dbname;
        std::string user = findVal("user");
        if (!user.empty()) dbConfig.username = user;
        std::string password = findVal("password");
        if (!password.empty()) dbConfig.password = password;
    }

    auto& pool = astock::database::NativePgConnectionPool::instance();
    if (!pool.initialize(dbConfig)) {
        std::cerr << "FATAL: PG 连接池初始化失败 — " << dbConfig.getConnectionUrl() << "\n";
        return 2;
    }
    std::cout << "  PG 连接成功: " << dbConfig.host << ":" << dbConfig.port
              << "/" << dbConfig.database << "\n\n";

    // ── 2. 从 DB 创建策略引擎 ──
    std::cout << "[2/6] 从数据库加载策略...\n";
    auto engine = StrategyEngine::fromDb(strategyId, nullptr);
    if (!engine) {
        std::cerr << "FATAL: 策略加载失败 — strategyId=" << strategyId << "\n";
        return 3;
    }
    std::cout << "  策略加载成功: " << strategyId << "\n";

    auto startResult = engine->start();
    if (!startResult.isOk()) {
        std::cerr << "FATAL: 引擎启动失败\n";
        return 3;
    }
    std::cout << "  引擎启动成功\n\n";

    // ── 3. 加载历史行情数据 ──
    std::cout << "[3/6] 加载历史行情 (prepareMarketData)...\n";
    if (!engine->prepareMarketData()) {
        std::cerr << "FATAL: 行情加载失败\n";
        return 4;
    }

    const auto* view = engine->liveMarketView();
    if (!view) {
        std::cerr << "FATAL: liveMarketView 为空\n";
        return 4;
    }
    std::cout << "  行情就绪: " << view->dates().size() << " 天 "
              << view->symbolStrings().size() << " 标的\n";
    std::cout << "  日期范围: " << view->dates().front().value
              << " ~ " << view->dates().back().value << "\n\n";

    // ── 4. 设置订单监听器 ──
    std::cout << "[4/6] 设置订单监听器...\n";
    auto listener = std::make_shared<TestOrderListener>();
    engine->setOrderListener(listener.get());
    std::cout << "  监听器就绪\n\n";

    // ── 5. 注入模拟账户 ──
    std::cout << "[5/6] 注入模拟账户 (100万)...\n";
    injectTestAccount();
    auto acc = engine::AccountEngine::instance().account();
    std::cout << "  总资产: " << acc.totalAsset
              << "  可用: " << acc.availableCash << "\n\n";

    // ── 6. 执行日终评估 ──
    std::cout << "[6/6] evaluateEndOfDay(" << tradingDay << ", false)...\n";
    std::cout << "───────────────────────────────────────────\n";

    EodEvaluationStatus status = EodEvaluationStatus::Error;
    try {
        status = engine->evaluateEndOfDay(tradingDay, false);
    } catch (const std::exception& e) {
        std::cerr << "FATAL: evaluateEndOfDay 异常: " << e.what() << "\n";
        return 5;
    }

    // ── 结果 ──
    std::cout << "\n╔══════════════════════════════════════════╗\n";
    std::cout << "║  测试结果                                ║\n";
    std::cout << "╠══════════════════════════════════════════╣\n";
    const char* statusStr = "Unknown";
    switch (status) {
    case EodEvaluationStatus::Submitted:   statusStr = "Submitted (已提交)"; break;
    case EodEvaluationStatus::NoSignal:    statusStr = "NoSignal (无信号)"; break;
    case EodEvaluationStatus::Skipped:     statusStr = "Skipped (跳过)"; break;
    case EodEvaluationStatus::AllRejected: statusStr = "AllRejected"; break;
    case EodEvaluationStatus::Error:       statusStr = "Error"; break;
    }
    std::cout << "║  状态: " << statusStr << "\n";
    std::cout << "║  订单数: " << listener->m_submittedCount << "\n";
    std::cout << "║  总股数: " << listener->m_totalQuantity << "\n";
    std::cout << "╚══════════════════════════════════════════╝\n";

    return (status == EodEvaluationStatus::Error) ? 6 : 0;
}
