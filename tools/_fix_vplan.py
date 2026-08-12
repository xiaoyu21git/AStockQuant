"""Clean fix: replace the v0.16.0 section in version plan"""
path = r'C:\Users\wang\.claude\projects\g--C---AStockQuantEngine\memory\version-management-plan.md'
with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

old_block = '''\t当前: v0.16.0-dev → 策略效果优化 + 合规信号模式:
\t    ├─ ✅ 参数自动调优 (网格搜索/贝叶斯优化) — Phase 1-4 已完成
\t    ├─ ✅ 信号模式系统 + IBasketInterceptor 拦截器 + BasketConfirmDialog 确认窗口
\t    │   ├─ ✅ 半自动篮子确认: traceId 匹配 + 价格编辑(市⇄限互转) + 删除 + signalScore排序
\t    │   ├─ ✅ 资金占用统计: latestPrice 估算 + 买入预估金额/占比/净额
\t    │   ├─ ✅ testEmitBasket traceId 修复 (空串覆盖→唯一ID)
\t    │   └─ ✅ 版本决策: 保持全 SemiAuto, 不拆信号版/自用版'''

# Find the actual text in the file (which may have been mangled)
# Just look for the anchor lines and rewrite everything from "当前: v0.16.0" to "v0.17.0"

lines = content.split('\n')
start_idx = None
end_idx = None
for i, line in enumerate(lines):
    if '当前: v0.16.0-dev' in line:
        start_idx = i
    if start_idx is not None and 'v0.17.0 → 产品化与扩展' in line:
        end_idx = i
        break

if start_idx is None or end_idx is None:
    print('ERROR: could not find section boundaries')
    exit(1)

# Rebuild the section
new_section = [
    '\t当前: v0.16.0-dev → 策略效果优化 + 合规信号模式:',
    '\t    ├─ ✅ 参数自动调优 (网格搜索/贝叶斯优化) — Phase 1-4 已完成',
    '\t    ├─ ✅ 信号模式系统 + IBasketInterceptor 拦截器 + BasketConfirmDialog 确认窗口',
    '\t    │   ├─ ✅ 半自动篮子确认: traceId 匹配 + 价格编辑(市⇄限互转) + 删除 + signalScore排序',
    '\t    │   ├─ ✅ 资金占用统计: latestPrice 估算 + 买入预估金额/占比/净额',
    '\t    │   ├─ ✅ testEmitBasket traceId 修复 (空串覆盖→唯一ID)',
    '\t    │   └─ ✅ 版本决策: 保持全 SemiAuto, 不拆信号版/自用版',
    '\t    ├─ 🔴 资金流接入增量更新管线 (v0.16.0 必做，不进下一版本)',
    '\t    │   ├─ P0-1: MarketDataRepository::queryMoneyFlow() 新增',
    '\t    │   ├─ P0-2: RawMarketDataAssembler 资金流注入块 + 大小写/日期归一化 + 异常降级',
    '\t    │   ├─ P0-3: DataCleaningServiceRefactored 增量更新检测 money_flow 列',
    '\t    │   ├─ P0-4: DataFetchController 全量构建从已有数据集继承 money_flow 类型',
    '\t    │   ├─ P0-5/6: 日期格式归一化(substr(0,10)) + symbol 大小写归一化(toupper)',
    '\t    │   ├─ P1-7: DataCacheAdapter 内嵌SQL替换为 MarketDataRepository::queryMoneyFlow()',
    '\t    │   ├─ P1-8: DataCacheParquet Windows rename 文件锁修复',
    '\t    │   ├─ P1-9: SQL ANY(ARRAY) + 单引号转义',
    '\t    │   └─ 计划: C:\\Users\\wang\\.claude\\plans\\elegant-dazzling-deer.md',
    '\t    └─ 绩效归因 (因子/板块/择时贡献拆解)',
]

result = lines[:start_idx] + new_section + lines[end_idx:]

with open(path, 'w', encoding='utf-8') as f:
    f.write('\n'.join(result))

# Verify
with open(path, 'r', encoding='utf-8') as f:
    for i, line in enumerate(f, 1):
        if 91 <= i <= 110:
            print(f'{i}: {line}', end='')
print('OK')
