# Phase 1 板块共振 — 全链路多阶段验证与对齐

## 链路总览

```
PG 数据源                       Arrow 缓存                   训练产物                C++ 推理              决策层
┌──────────────────┐    ┌──────────────────┐    ┌──────────────────┐    ┌──────────────────┐    ┌──────────┐
│ mkt.minute_bar   │    │ data.arrow       │    │ feature_config   │    │ DLFactor         │    │ composite│
│ fund.money_flow  │───▶│  +9 sector 列    │───▶│ scaler.json      │───▶│ FeatureTensor     │───▶│ Score ×  │
│ ref.symbol_info  │    │  (21 raw cols)   │    │ model.onnx       │    │ Builder          │    │ envCoeff │
└──────────────────┘    └──────────────────┘    └──────────────────┘    └──────────────────┘    └──────────┘
      Phase 0               Phase 1-2               Phase 3-4              Phase 5                Phase 6
```

每个 Phase 验证三件事：**输入正确 → 产出正确 → 与下一阶段对齐**。

---

## Phase 0: PG 数据源验证

**目标**：确认 `si.industry_code` 可用、三张源表数据覆盖正常。

### V0-1: industry_code 覆盖率

```sql
-- 检查 symbol_info 中 industry_code 的填充率和样本值
SELECT
    COUNT(*)                                                    AS total_symbols,
    COUNT(*) FILTER (WHERE industry_code IS NOT NULL)           AS with_code,
    ROUND(COUNT(*) FILTER (WHERE industry_code IS NOT NULL)
          * 100.0 / COUNT(*), 1)                               AS pct,
    COUNT(DISTINCT industry_code)                              AS unique_codes
FROM ref.symbol_info
WHERE status = 'ACTIVE';
```

**通过标准**：`with_code` > 3000, `pct` > 80%, `unique_codes` 在 20~500 之间。

### V0-2: minute_bar 覆盖检查

```sql
-- 检查 minute_bar 按日期的覆盖量和 industry_code 可 JOIN 率
SELECT
    mb.trade_ts::date                                          AS trade_date,
    COUNT(DISTINCT mb.symbol_id)                               AS symbols_with_data,
    COUNT(DISTINCT si.industry_code) FILTER (
        WHERE si.industry_code IS NOT NULL)                    AS industries_with_code
FROM mkt.minute_bar mb
JOIN ref.symbol_info si ON mb.symbol_id = si.id
WHERE mb.trade_ts::date BETWEEN '2024-01-01' AND '2026-07-31'
GROUP BY mb.trade_ts::date
ORDER BY trade_date
LIMIT 5;
```

**通过标准**：每天 `industries_with_code` > 20。

### V0-3: money_flow_daily 覆盖检查

```sql
SELECT
    mf.trade_date,
    COUNT(DISTINCT mf.symbol_id)                               AS symbols,
    COUNT(DISTINCT si.industry_code) FILTER (
        WHERE si.industry_code IS NOT NULL)                    AS industries
FROM fund.money_flow_daily mf
JOIN ref.symbol_info si ON mf.symbol_id = si.id
WHERE mf.trade_date BETWEEN '2024-01-01' AND '2026-07-31'
GROUP BY mf.trade_date
ORDER BY trade_date
LIMIT 5;
```

**通过标准**：每天 `symbols` > 2000, `industries` > 20。

### V0-4: sector SQL 试跑

```sql
-- 取一个月数据试跑 sqlSectorDailyAgg（替换日期后执行）
SELECT industry_code, trade_date, stock_count, sector_is_reliable,
       ROUND(sector_vwap_change::numeric, 6) AS vwap_change,
       ROUND(sector_breadth::numeric, 4)     AS breadth,
       ROUND(sector_turnover_ratio::numeric, 4) AS turnover_ratio
FROM (
    -- 此处粘贴 DataSourceRegistry::sqlSectorDailyAgg() 替换 {start_date}/{end_date} 后的完整 SQL
    SELECT ... 
) sub
ORDER BY industry_code, trade_date
LIMIT 20;
```

**通过标准**：返回非空结果，`industry_code` 值格式与 `symbol_info.industry_code` 一致（关键对齐点！）。

### V0-5: concentration SQL 试跑

```sql
-- 同上，试跑 sqlSectorConcentration
SELECT industry_code, trade_date,
       ROUND(sector_concentration::numeric, 4) AS concentration
FROM (
    -- 粘贴完整 SQL
    SELECT ...
) sub
ORDER BY industry_code, trade_date
LIMIT 20;
```

**通过标准**：`sector_concentration` 在 0.0~1.0 范围内。

---

## Phase 1: 缓存重建后 Arrow 列验证

**前置条件**：应用内触发全量数据拉取（含 day_data），重建完成。

**目标**：确认 9 列 sector 数据正确写入 Arrow，无 NaN 异常。

### V1-1: 列存在性 + 基本统计

```bash
python astock_engine/validate_sector_data.py --data <新arrow文件> --sample 5
```

**通过标准**：
- 全部 9 列 ✅
- `sector_is_reliable` 有 0/1 两个值
- `sector_breadth` 在 [0, 1]
- `sector_concentration` 在 [0, 1]，非全 NaN
- `sector_relative_strength` 非全 0（否则 C++ 计算异常）

### V1-2: industry_code 对齐检查

```python
# 追加到 validate_sector_data.py 或单独执行
import pyarrow.ipc as ipc
f = pa.memory_map("data.arrow", "rb")
reader = ipc.open_file(f)
t = pa.Table.from_batches([reader.get_batch(0)])
# 取前 100 行的 industry_code 样本
ic_samples = t.column("industry_code").to_pylist()[:100]
print("industry_code 样本:", sorted(set(str(v) for v in ic_samples if v)))
```

**通过标准**：industry_code 值为数字（如 480000、640000），不含 "SW"/"BK" 前缀。若含前缀则说明 `symbol_info.industry_code` 不是纯数字 → 需要进一步检查 SQL 是否正确匹配。

### V1-3: sector 列 NaN 率

```python
# 全量扫描 sector 列 NaN 率
import numpy as np
for sf in SECTOR_FIELDS:
    vals = []
    for bi in range(reader.num_record_batches):
        t = pa.Table.from_batches([reader.get_batch(bi)])
        if sf in t.column_names:
            col = t.column(sf).to_pylist()
            vals.extend([v for v in col if v is not None])
    arr = np.array(vals, dtype=np.float64)
    valid = np.sum(np.isfinite(arr))
    total = len(vals)
    print(f"  {sf:<28} NaN率={(total-valid)/max(total,1)*100:.1f}%")
```

**通过标准**：每列 NaN 率 < 30%（新股/停牌/无行业分类的标的正常会有 NaN）。

---

## Phase 2: 训练数据验证

**前置条件**：Phase 1 通过。

**目标**：确认 train.py 正确加载 sector 列、标签为 sector-relative alpha。

### V2-1: 字段探测

```bash
python astock_engine/ai/train.py --data <arrow文件> --output /tmp/dl_test --epochs 1 --symbols all 2>&1 | grep -E "板块字段|原始字段|市场字段"
```

**通过标准**：
```
[train] 板块字段已检测: ['sector_vwap_change', 'sector_breadth', ...] (9 个)
[train] 原始字段: [...] (21 个)
[train] 市场字段: [...] (4 个)
```

### V2-2: 标签类型确认

在 train.py 的 Step 4 之后临时插入：
```python
print(f"[验证] y_train 均值={y_train.mean():.6f} 标准差={y_train.std():.6f}")
print(f"[验证] y_train 样本: {y_train[:5]}")
```

**通过标准**：均值接近 0（sector-relative alpha 的截面均值为 0），标准差 < 0.15。

### V2-3: 特征维度对齐

训练输出中确认：
```
特征数=31 (raw+market)
```
即 12 FEATURE_FIELDS + 9 SECTOR_FIELDS + 6 派生 + 4 市场 = 31。

---

## Phase 3: 模型训练验证

**前置条件**：Phase 2 通过。

**目标**：训练产出 feature_config.json / scaler.json / model.onnx 且维度一致。

### V3-1: 训练完成

```bash
python astock_engine/ai/train.py \
    --data <arrow文件> \
    --output models/dl_v4 \
    --epochs 100 \
    --symbols all
```

### V3-2: 产出文件校验

```bash
python -c "
import json
with open('models/dl_v4/feature_config.json') as f:
    cfg = json.load(f)
fields = cfg['fields']
n_features = cfg['n_features']
print(f'字段数: {len(fields)}')
print(f'特征维度: {n_features}')
print(f'字段: {fields}')
# 确认包含 sector 列
sector_cols = [f for f in fields if f.startswith('sector_')]
print(f'板块列: {sector_cols} ({len(sector_cols)} 个)')

with open('models/dl_v4/scaler.json') as f:
    s = json.load(f)
print(f'scaler 维度: {len(s[\"mean\"])}')
assert len(s['mean']) == n_features, 'scaler 维度与特征数不匹配!'
print('✅ scaler 维度与特征数一致')
"
```

**通过标准**：
- `fields` 包含全部 9 个 sector 列
- `n_features == 31`
- `scaler['mean']` 长度 == 31
- `model.onnx` 文件 > 100KB

---

## Phase 4: C++ 推理对齐

**前置条件**：Phase 3 通过，应用已部署 `models/dl_v4/`。

**目标**：FeatureTensorBuilder 正确加载 sector 字段、scaler 维度匹配、ONNX 推理正常。

### V4-1: 启动日志检查

启动应用后搜索日志：
```
[FeatureTensorBuilder] 特征配置: 21 raw + 6 衍生 + 4 市场 = 31 总计
[DLFactor] 模型加载成功: .../models/dl_v4/model.onnx
```

**通过标准**：无 scaler 拒绝日志（`scaler维度(XX) 无法匹配已知特征组合`），无字段缺失日志。

### V4-2: 首次推理诊断

启动后首次触发 DLFactor 计算，检查日志：
```
[DLFactor] TENSOR[...] min=... max=... mean=...
[FeatureTensorBuilder] 首次构建完成: validSymbols=... W=20 F=31
```

**通过标准**：
- `validSymbols` > 100（大量标的有有效特征）
- `F=31`（特征维度正确）
- `hasScaler=true`

### V4-3: 字段缺失降级测试

若 Arrow 缓存不含 sector 列（旧缓存），确认 FeatureTensorBuilder 诊断输出清晰：
```
[FeatureTensorBuilder] scaler维度(31) 无法匹配已知特征组合, 拒绝使用
```
→ 因子返回全 0 → 策略自动降级到非 AI 因子。

---

## Phase 5: 决策层验证

**前置条件**：Phase 4 通过。

**目标**：FactorSignalProcessor 在板块环境系数注入后行为正常。

### V5-1: 默认行为（无系数注入）

不调用 `setSectorEnvCoeff()` → `sectorEnvCoeff(symbol)` 返回 1.0 → `compositeScore` 行为不变。

### V5-2: 系数注入行为

模拟注入系数后验证：
```cpp
processor.setSectorEnvCoeff("000001.SZ", 1.3);  // 强共振
processor.setSectorEnvCoeff("000002.SZ", 0.5);  // 弱共振
// 000001.SZ 的 compositeScore 应放大 1.3 倍
// 000002.SZ 的 compositeScore 应缩小 0.5 倍
// 000003.SZ (未设置) 保持原值
```

---

## 对齐矩阵（✅ = 已通过代码审查确认）

| # | 对齐点 | 上游产出 | 下游消费 | 验证结果 |
|:---|:---|:---|:---|:---|
| A1 | industry_code 源 | PG `si.industry_code` | sectorIdx key + daily bar row key | ✅ SQL 均用 `si.industry_code`，同源 |
| A2 | sector 列顺序 | `sector_daily_columns::names()` (C++) | `train.py SECTOR_FIELDS` (Python) | ✅ 9 列完全一致 |
| A3 | sector 列顺序 | `train.py SECTOR_FIELDS` (Python) | `DLFactor::getDataRequirements()` (C++) | ✅ 9 列完全一致 |
| A4 | sector 列顺序 | `DLFactor::getDataRequirements()` (C++) | `validate_sector_data.py` (Python) | ✅ 9 列完全一致 |
| A5 | 原始字段数 | Arrow schema → 21 raw | train.py load_data → 21 raw | ✅ 12 FEATURE + 9 SECTOR = 21 |
| A6 | 派生特征 | train.py (隐含) | `FeatureTensorBuilder::kDerivedFields` | ✅ 6 字段顺序一致 |
| A7 | 市场特征 | `train.py MARKET_FEATURES` | `FeatureTensorBuilder::kMarketFields` | ✅ 4 字段顺序一致 |
| A8 | 总特征维度 | train.py export → 31 | FeatureTensorBuilder 检测 → 31 | ✅ 21+6+4=31，第4分支命中 |
| A9 | Scaler 维度 | `scaler.json` → 31 | FeatureTensorBuilder 加载 → 31 | ✅ 自动检测路径可匹配 |
| A10 | 标签语义 | train.py sector-relative alpha | ONNX 模型预测目标 | ✅ y=stock_ret-sector_avg_fwd_ret |
| A11 | envCoeff 默认值 | 1.0（未注入） | compositeScore 不变 | ✅ `!=1.0` 条件跳过乘法 |
| A12 | NaN 防护 | train.py Step3.5/Step4 | `int(NaN)` → ValueError | ✅ `isfinite` 检查先于 `int()` |
| A13 | sectorIdx key 格式 | C++ `industry_code\|YYYYMMDD` | daily bar row key | ✅ 两端均去横杠、大写 |

---

## 维度链详细推导

```
┌──────────────────────────────────────────────────────────────┐
│ 12 FEATURE_FIELDS + 9 SECTOR_FIELDS = 21 raw                │
│ FeatureTensorBuilder: m_fields = 21 → m_rawFieldCount = 21  │
│                                                              │
│ nScaler(31) == 21 + 6 + 4 → 第4分支命中 ✅                   │
│   → m_rawFieldCount = 21                                     │
│   → m_marketFeatureCount = 4                                 │
│   → 追加 kDerivedFields (6) + kMarketFields (4)             │
│   → F = 31                                                   │
│                                                              │
│ hasDerived = true, hasMarket = true → 两轮扫描路径            │
│ Pass1: 构建 21 raw + 保留 prevClose/industry                 │
│ Cross-sectional: 按窗口日计算 4 市场统计量                     │
│ Pass2: 拼接 + scaler 归一化 → [N, 20, 31] 张量               │
└──────────────────────────────────────────────────────────────┘
```

## FeatureTensorBuilder 数据请求链路

```
DLFactor::getDataRequirements()
  → 12 FEATURE_FIELDS (close..industry_code)
  → 9 SECTOR_FIELDS (sector_vwap_change..sector_turnover_ratio)
  → 21 个 requiredFields
  → FeatureTensorBuilder::build()
    → view.getSeries(symbol, anchorDate, W*5+1, field)
    → 每个标的请求 21 列的时序数据
    → HistoricalView 从 Arrow 缓存读取
    → 若 Arrow 不含 sector 列 → 全 NaN → ffill 无法修复 → 标的被跳过
    → validSymbols 减少，但不会崩溃 ✅
```

## 命令速查

```bash
# 静态验证（即刻执行，无需数据）
PYTHONIOENCODING=utf-8 python astock_engine/verify_sector_pipeline.py --mode static

# Arrow 缓存验证（重建后）
PYTHONIOENCODING=utf-8 python astock_engine/verify_sector_pipeline.py --mode arrow --data <arrow文件>

# 训练产出验证
PYTHONIOENCODING=utf-8 python astock_engine/verify_sector_pipeline.py --mode training --model-dir models/dl_v4

# 全链路
PYTHONIOENCODING=utf-8 python astock_engine/verify_sector_pipeline.py --mode full --data <arrow> --model-dir <dir>
```

---

## 执行顺序

```
Phase 0 (静态验证 54项 ✅) ──▶ Phase 1 (缓存重建+Arrow验证) ──▶ Phase 2+3 (训练验证)
                                      │                                    │
                                      │ 待执行                              │
                                      ▼                                    ▼
                              Phase 4 (C++推理对齐) ──▶ Phase 5 (决策层验证)
```

**当前状态**：
- ✅ **Phase 0 (静态代码对齐)**：已完成，54/54 项通过
- ⏳ **Phase 1-5**：需重建 Arrow 缓存后执行
