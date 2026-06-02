#!/bin/bash
# C-T1: CI Qt 下沉扫描 (实施任务清单跨阶段公共工单)
#
# 验收标准：扫描结果必须为空（核心模块零 Qt 依赖）
# 阻断条件: 任何匹配的行出现
#
# 扫描范围：
# - src/domain/factor/include/factor_compute/   (因子服务核心)
# - src/domain/trading/                          (交易执行层)
# - src/domain/backtest/                         (回测域 - 仅核心部分)
#
# 排除范围 (Qt 允许层):
# - src/ui/bridge/     (桥接层，允许 Qt 类型)
# - src/app/           (应用层，QML 入口)

set -euo pipefail

echo "=== C-T1: CI Qt 下沉扫描 ==="
echo "扫描核心模块中的 Qt 类型引用..."
echo ""

VIOLATIONS=0

# 扫描核心目录中的 Qt 类型
scan_dir() {
    local dir="$1"
    local label="$2"
    echo "扫描: $label ($dir)"
    
    # 匹配: #include <Q...>, QString, QDate, QVariant, QObject, 信号槽
    local matches=$(grep -rn \
        -e '#include\s*<Qt' \
        -e '#include\s*<Q[A-Z]' \
        -e '\bQString\b' \
        -e '\bQDate\b' \
        -e '\bQVariant\b' \
        -e '\bQObject\b' \
        -e '\bQByteArray\b' \
        -e '\bQHash\b' \
        -e '\bQMap\b' \
        -e '\bQVector\b' \
        -e '\bQList\b' \
        -e '\bQDateTime\b' \
        -e '\bqHash\b' \
        -e '\bemit\b' \
        -e '\bSIGNAL\b' \
        -e '\bSLOT\b' \
        -e '\bQ_OBJECT\b' \
        -- "$dir" 2>/dev/null || true)
    
    if [ -n "$matches" ]; then
        echo "  ❌ 发现 Qt 类型引用:"
        echo "$matches" | head -20
        VIOLATIONS=$((VIOLATIONS + 1))
    else
        echo "  ✅ 通过 (0 Qt引用)"
    fi
    echo ""
}

# 核心域: factor_compute (因子服务核心)
scan_dir "src/domain/factor/include/factor_compute" "factor_compute 头文件"
scan_dir "src/domain/factor/src/factor_compute" "factor_compute 源文件"

# 交易执行层核心
scan_dir "src/domain/trading/include/execution" "trading/execution 头文件"
scan_dir "src/domain/trading/src/execution" "trading/execution 源文件"

# 回测域核心
scan_dir "src/domain/backtest/include" "backtest/include"
scan_dir "src/domain/backtest/src" "backtest/src"

# 策略域核心接口
scan_dir "src/domain/strategy/include" "strategy/include"
scan_dir "src/domain/strategy/src" "strategy/src"

# 基础库核心
scan_dir "src/infrastructure/include" "infrastructure/include"
scan_dir "src/infrastructure/src" "infrastructure/src"

echo "=== 扫描完成 ==="
if [ "$VIOLATIONS" -gt 0 ]; then
    echo "❌ 发现 $VIOLATIONS 个目录包含 Qt 类型引用"
    echo "CI 阻断: 核心模块不得依赖 Qt"
    exit 1
else
    echo "✅ 所有核心模块通过 Qt 下沉扫描"
    exit 0
fi