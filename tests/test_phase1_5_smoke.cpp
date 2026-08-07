// test_phase1_5_smoke.cpp
// Phase 1-5 重构回归测试 — 纯函数，零外部依赖
// 用法: 直接运行，assert 失败会 abort

#include "domain/trading/include/PositionUtils.h"
#include "foundation/market/AStockSymbol.h"
#include "domain/trading/TradeExecutionEngine.h"

#include <cassert>
#include <cstdio>

using namespace domain::trading;
using namespace foundation::market;

// ═══════════════════════════════════════════════════════════════
// Phase 3.4: boardLimitRatio() — A股涨跌停比例
// ═══════════════════════════════════════════════════════════════
static void test_boardLimitRatio() {
    assert(boardLimitRatio("000001") == 0.10);       // 深圳主板
    assert(boardLimitRatio("600000") == 0.10);       // 上海主板
    assert(boardLimitRatio("000001.SZ") == 0.10);     // 带后缀
    assert(boardLimitRatio("300001") == 0.20);       // 创业板
    assert(boardLimitRatio("301000") == 0.20);       // 创业板
    assert(boardLimitRatio("688001") == 0.20);       // 科创板
    assert(boardLimitRatio("830000") == 0.30);       // 北交所 8开头
    assert(boardLimitRatio("400001") == 0.30);       // 新三板 4开头
    std::printf("  PASS: boardLimitRatio\n");
}

// ═══════════════════════════════════════════════════════════════
// Phase 4.2: normalizeToFullSymbol() — 符号标准化
// ═══════════════════════════════════════════════════════════════
static void test_normalizeToFullSymbol() {
    using foundation::market::AStockSymbol;
    // 纯6位代码 → 完整symbol
    assert(AStockSymbol::normalizeToFullSymbol("000001") == "000001.SZ");
    assert(AStockSymbol::normalizeToFullSymbol("600000") == "600000.SH");
    assert(AStockSymbol::normalizeToFullSymbol("300001") == "300001.SZ");
    assert(AStockSymbol::normalizeToFullSymbol("688001") == "688001.SH");
    assert(AStockSymbol::normalizeToFullSymbol("830000") == "830000.BJ");
    // 已有后缀 → 原样返回
    assert(AStockSymbol::normalizeToFullSymbol("000001.SZ") == "000001.SZ");
    // 非6位 → 原样返回
    assert(AStockSymbol::normalizeToFullSymbol("abc") == "abc");
    assert(AStockSymbol::normalizeToFullSymbol("00001") == "00001");
    std::printf("  PASS: normalizeToFullSymbol\n");
}

// ═══════════════════════════════════════════════════════════════
// Phase 3.5: isAbnormalTerminal() — Filled 不触发暂停
// ═══════════════════════════════════════════════════════════════
static void test_isAbnormalTerminal() {
    TradeOrder order;

    order.setStatus(OrderStatusValue::Filled);
    assert(!order.isAbnormalTerminal());  // 修复点: Filled 不是异常终态
    assert(order.isClosed());

    order.setStatus(OrderStatusValue::Cancelled);
    assert(order.isAbnormalTerminal());
    order.setStatus(OrderStatusValue::Rejected);
    assert(order.isAbnormalTerminal());
    order.setStatus(OrderStatusValue::Expired);
    assert(order.isAbnormalTerminal());

    order.setStatus(OrderStatusValue::Pending);
    assert(!order.isAbnormalTerminal());
    order.setStatus(OrderStatusValue::New);
    assert(!order.isAbnormalTerminal());
    order.setStatus(OrderStatusValue::PartiallyFilled);
    assert(!order.isAbnormalTerminal());

    std::printf("  PASS: isAbnormalTerminal\n");
}

// ═══════════════════════════════════════════════════════════════
// Phase 4.5: OrderStatusValue 枚举完整性 — 确保 PendingCancel=4
// 位于 Filled=3 和 Cancelled=5 之间，toOrderStatusValue() 的
// switch 映射跳过此 gap，纠正了 +1 映射的 bug
// ═══════════════════════════════════════════════════════════════
static void test_enumIntegrity() {
    assert(static_cast<int>(OrderStatusValue::Pending) == 0);
    assert(static_cast<int>(OrderStatusValue::New) == 1);
    assert(static_cast<int>(OrderStatusValue::PartiallyFilled) == 2);
    assert(static_cast<int>(OrderStatusValue::Filled) == 3);
    assert(static_cast<int>(OrderStatusValue::PendingCancel) == 4);
    assert(static_cast<int>(OrderStatusValue::Cancelled) == 5);
    assert(static_cast<int>(OrderStatusValue::Rejected) == 6);
    assert(static_cast<int>(OrderStatusValue::Expired) == 7);
    std::printf("  PASS: OrderStatusValue enum integrity\n");
}

// ═══════════════════════════════════════════════════════════════

int main() {
    std::printf("=== Phase 1-5 Smoke Tests ===\n");
    test_boardLimitRatio();
    test_normalizeToFullSymbol();
    test_isAbnormalTerminal();
    test_enumIntegrity();
    std::printf("=== All tests passed ===\n");
    return 0;
}
