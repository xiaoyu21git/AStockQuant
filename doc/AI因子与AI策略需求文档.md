# AI 因子与 AI 策略 需求文档 v1.3

> 基于项目已有代码架构分析 | 三轮评审通过 | P0 推理引擎已实现 (2026-07-28)

---

## 一、现有架构基础

### 1.1 数据管线

PostgreSQL → Parquet → Arrow → HistoricalView

**可用字段**：OHLCV、market_cap、pe_ratio/pb_ratio、eps/roe/roa 等

**缺失字段**：`vwap` → FeatureTensorBuilder 自动计算 `(high+low+close)/3`；高频/宏观字段待后期

### 1.2 因子系统

`DLFactor` = `FactorType::DL = 17`，工厂已注册，框架层完整

### 1.3 策略系统

`StrategyEngine::step()` → `FactorSignalProcessor` → `ICandidatePoolSelector` → 执行

### 1.4 Python 子系统

pybind11 绑定 + EventBus + 策略插件系统

### 1.5 依赖矩阵

| 已有 | 用途 | 缺失 | 用途 |
|------|------|------|------|
| Apache Arrow 15.0.0 | 数据 I/O | ONNX Runtime 1.12+ | 模型推理 |
| Eigen 3.4.0 | 矩阵运算 | PyTorch | 神经网络训练 |
| pybind11 2.11.1 | Python 绑定 | gymnasium | RL 环境 |

---

## 二、AI 因子 (DLFactor) 需求

### 2.1 架构

```
CalculationContext → FeatureTensorBuilder → [N,F,W] tensor
  → IModelInference::infer → [N] scores → result.values
```

### 2.2 特征配置 (feature_config.json)

```json
{
  "fields": [
    {"name":"close","normalize":"zscore","fillna":"ffill"},
    {"name":"volume","normalize":"log","fillna":0},
    {"name":"turnover_rate","normalize":"minmax","fillna":0},
    {"name":"market_cap","normalize":"log","fillna":"ffill"}
  ],
  "lookbackWindow":20, "predictionHorizon":5, "batchSize":512
}
```

- `fields` 顺序 = 张量维度顺序
- `fillna` 支持 ffill/bfill/常量
- **优先级**：`feature_config.json` > `DLFactor::Params`（运行时配置文件为准，Params 为默认值和 UI 占位）
- 加载时若与 Params 不一致 → 告警日志 + 采用配置文件值

### 2.3 vwap 字段处理

```
若 HistoricalView 有 vwap → 直接用真实值
若 HistoricalView 无 vwap → FeatureTensorBuilder 自动计算 (high+low+close)/3.0
管线升级后自动切换，无需修改配置
```

### 2.4 批量推理

`[5000,F,W]` 张量 → 单次推理 → 50-100ms (CPU, ONNX Runtime)
分批策略：`batchSize` 默认 512

### 2.5 模型推理接口

```cpp
class IModelInference {
public:
    virtual bool load(const std::string& modelPath) = 0;
    virtual bool isLoaded() const = 0;
    // timeSteps = lookbackWindow (回溯窗口长度)
    virtual std::vector<float> infer(const float* input,
        int batchSize, int featureCount, int timeSteps) = 0;
};
// 推荐实现: ONNX Runtime 1.12+ CPU 版 (~5MB)
```

### 2.6 CMake 集成

```cmake
option(USE_ONNX_RUNTIME "Enable ONNX Runtime inference" ON)
if(USE_ONNX_RUNTIME)
    find_package(onnxruntime QUIET)
    if(onnxruntime_FOUND)
        target_link_libraries(factor PRIVATE onnxruntime::onnxruntime)
        target_compile_definitions(factor PRIVATE HAS_ONNX_RUNTIME)
    else()
        message(WARNING "ONNX Runtime not found. DLFactor will return all-zero values.")
    endif()
endif()
```

默认 ON，检测不到则编译告警（而非静默失败）。运行时 `metadata["inferenceAvailable"] = false`。

### 2.7 模型热更新

首期检测 `modelPath` 文件 mtime，目录模式留待后期

### 2.8 特征维度对齐契约 (5 条硬约束)

1. 字段列表一致 2. 窗口长度一致 3. 特征数一致
4. 预处理一致 5. 缺失值填充一致
违反 → `metadata["featureMismatch"]=true` + 返回全零

### 2.9 新增文件

`IModelInference.h`, `OnnxInference.cpp`, `FeatureTensorBuilder.h/.cpp`
`config/ai/feature_config_template.json`, `astock_engine/ai/train.py`, `astock_engine/ai/export_onnx.py`

---

## 三、AI 策略需求

### 3.1 定位

端到端 AI 决策，直接输出交易信号

### 3.2 与 AI 因子的边界

| | AI 因子 | AI 策略 |
|---|---|---|
| 层级 | 因子层 (打分) | 策略层 (决策) |
| 输入 | 单股量价序列 | 全市场截面+持仓+因子值 |
| 输出 | `float` score | `List[OrderRequest]` |
| 训练 | 监督学习 | 强化学习 (PPO) |

### 3.3 交互接口 (首期: 离线推理)

```
训练 (Python, 离线): Parquet → RL环境 → PPO训练 → 保存模型
推理 (C++): 预生成信号文件 → C++ 直接读取 (零 Python 依赖, 无性能开销)
           或: C++ get_market_snapshot(date) → Python model.predict → action
```

**get_market_snapshot 返回格式**:
`py::dict {"symbols":[...], "close":[...], "factor_values":{"facA":[...]}}`

### 3.4 RL 环境

**状态**: 因子值矩阵 + 价格 + 持仓 + 市场环境
**动作**: Top-K 选股，K=50 (默认，可通过策略配置参数调整)，每维 ∈ [-1,1]
**奖励**: `(Vt-Vt₋1)/Vt₋1 - cost*turnover - λ*max(0,-drawdown)`
**算法**: PPO, lr=3e-4, gamma=0.99

### 3.5 动作映射链

`action[k]∈[-1,1] → Top-K stocks → target_pos → delta → OrderRequest → SimulatedTradingExecutor`

### 3.6 训练数据

首期: Python 直接读 Parquet，滚动窗口 5+1+1 划分

### 3.7 模型版本管理

`models/ppo_v1/{actor.pt, metadata.json, feature_config.json}`

### 3.8 新增文件

`ai/env.py`, `ai/models/ppo.py`, `ai/train.py`, `ai/inference.py`, `ai/state_encoder.py`
`strategies/ai_strategy.py`, `pybindings_strategy.cpp`

---

## 四、实施路线图 (后续版本)

| 阶段 | 产出 |
|------|------|
| **P0 1周** | ONNX Runtime 集成 + FeatureTensorBuilder + DLFactor 推理可用 |
| **P0 2周** | Python 训练脚本 + 导出 ONNX + 端到端验证 |
| **P1** | 热更新 + 批量推理优化 |
| **P2** | AI 策略 RL 环境 + 离线信号回测 |

---

## 五、风险与缓解

| 风险 | 缓解 |
|------|------|
| ONNX Runtime × MSVC 2019 兼容 | 预编译 Windows x64 包 (1.12+) |
| vwap 字段缺失 | FeatureTensorBuilder 自动计算 typical price 替代 |
| PPO 高维动作空间 | Top-K=50 选股，大幅降维 |
| 特征维度不对齐 | feature_config.json 强制 5 条校验 |
| 批量推理内存溢出 | batchSize=512 分批 + 内存上限保护 |
| Python ↔ C++ 实时调用性能 | 首期离线信号文件，后续逐步优化 |

---

## 六、编码前确认清单

- [x] vwap fallback: FeatureTensorBuilder 优先真实值，否则 typical price
- [x] feature_config.json 优先级 > Params，加载时校验并告警
- [x] IModelInference::infer timeSteps = lookbackWindow
- [x] Top-K 默认 50，策略配置参数可调
- [x] fillna 支持 ffill / bfill / 常量
- [x] 热更新首期检测文件 mtime
- [x] ONNX Runtime 默认 ON，检测不到编译告警
- [x] 训练数据首期直接读 Parquet

> 三轮评审通过，编码前确认全部闭环。P0 推理引擎已于 2026-07-28 实现。

---

## 七、P0 实施状态 (2026-07-28)

| 模块 | 文件 | 状态 |
|------|------|------|
| IModelInference | `include/factor_compute/IModelInference.h` | ✅ 已实现 |
| OnnxInference | `src/factor_compute/OnnxInference.cpp` | ✅ 已实现 |
| FeatureTensorBuilder | `src/factor_compute/FeatureTensorBuilder.cpp` | ✅ 已实现 |
| DLFactor::calculate() | `src/DLFactor.cpp` | ✅ 推理通路完成 |
| train.py | `astock_engine/ai/train.py` | ✅ 可用 (CPU) |
| 测试 ONNX 模型 | `models/dl_test.onnx` | ✅ C++ 推理验证通过 |
| 80只训练模型 | `models/dl_v1/model.onnx` | ✅ IC 0.083 |
| 500只训练模型 | `models/dl_v2/` | 🔄 训练中 (IC 0.053+) |
| 特征配置对齐 | `feature_config.json` | 🔄 training/inference 特征数未对齐 |

---

## 八、AI 因子能力升级路线图

> 目标维度: 准确性 / 稳定性 / 收益能力 / 抗风险 / 易用性

### 第一阶段: 数据基础 (准确性和稳定性基础)

**目标**: IC > 0.08, 样本外 IC 衰减 < 20%

| 改进项 | 内容 | 预期收益 |
|--------|------|---------|
| 特征扩维 | 4 → 12+：加 pe_ratio, pb_ratio, roe, amplitude, volume_ratio, index_return, industry_code | IC +0.02~0.04 |
| 标签优化 | fwd_return → fwd_rank (截面排序) | 信号稳定性 +30% |
| 样本外验证 | 训练 2020-2023 / 验证 2024-2026 | 真实评估标准 |
| 缺失值处理 | ffill → 自适应 (ffill + 大盘均值兜底) | 减少无效样本 |
| 极端值过滤 | 双侧 1% winsorize | IC 方差收窄 |

### 第二阶段: 模型升级 (准确性提升)

**目标**: IC > 0.10

| 改进项 | 内容 | 预期收益 |
|--------|------|---------|
| 全市场训练 | 500 → 5741 只股票，GPU 训练 | 泛化能力 |
| 市场环境特征 | 大盘波动率、涨跌比、行业相对强弱 | 风格感知 |
| 多尺度输入 | 5日 + 20日 + 60日三个时间尺度 | 短中长期兼顾 |
| 多任务学习 | 同时预测 1/5/20 日收益 | 多周期信号融合 |
| 注意力机制 | LSTM → Transformer (仅长序列 60+) | IC +0.01~0.02 |

### 第三阶段: 自我纠错与风控 (抗风险)

**目标**: 信号衰减自动恢复, 最大回撤 < 25%

| 改进项 | 内容 | 触发条件 |
|--------|------|---------|
| 滚动 IC 监控 | 每周计算最近 60 天 IC | IC < 0.03 → 告警 |
| 自动再训练 | 检测衰减 → 触发增量训练 → 覆盖 ONNX | IC 跌破阈值 |
| 模型回滚 | 保留最近 3 个模型版本 | 新模型 IC 不如旧 |
| 特征漂移检测 | KL 散度监控输入分布 | 漂移 > 阈值 → 告警 |
| 极端行情熔断 | 大盘单日涨跌 > 5% → 暂停 AI 信号 | 波动率暴增 |

### 第四阶段: 策略融合 (收益能力)

**目标**: 多因子组合 IC > 0.12，年化超额 > 15%

| 改进项 | 内容 |
|--------|------|
| AI 因子 + 价值因子 | AI 动量 + 估值锚定，互补 |
| AI 因子 + 质量因子 | AI 趋势 + 基本面过滤 |
| 动态权重 | 牛市 AI 权重↑、熊市价值权重↑ |
| 行业中性 | AI 信号按行业去均值，消除行业偏差 |
| 容量控制 | AI 选股池限制 Top 30%，避免过度拥挤 |

### 第五阶段: 自动化运维 (易用性)

**目标**: 零人工干预闭环

| 改进项 | 内容 |
|--------|------|
| 一站式训练 | `python train.py --auto`，自动选特征+调参 |
| 定时调度 | Windows 任务计划 + 项目内置定时器 |
| 模型 A/B 测试 | 新模型在模拟盘跑 30 天后再切实盘 |
| 监控仪表盘 | UI 上实时展示 IC 曲线、特征漂移、模型版本 |
| 一键回滚 | UI 按钮退回上一模型版本 |

---

## 九、特征设计完整清单

### 价格类
`close, open, high, low, pre_close, amplitude, change_pct`

### 量价类
`volume, amount, turnover_rate, volume_ratio`

### 估值类
`pe_ratio, pb_ratio, market_cap, circulating_market_cap, dividend_yield`

### 质量/财务类
`roe, roa, profit_margin, gross_margin, debt_to_equity, current_ratio, quick_ratio, revenue_yoy`

### 市场环境类 (缓存已有/可派生)
`index_return_5d, index_volatility, market_breadth, industry_code`

### 标签设计
| 标签 | 说明 | 适用场景 |
|------|------|---------|
| `fwd_return_5d` | 原始收益率 | 短期择时 |
| `fwd_return_20d` | 月频收益率 | 组合调仓 |
| `fwd_rank` | 截面排序 | 横截面选股 |
| `fwd_excess_vs_industry` | 行业超额 | 行业中性策略 |

---

> 当前 P0 状态: 推理引擎就绪，训练管线可用，500 只训练中。
> 下一优先: 第一阶段（扩特征 + 改标签 + 样本外验证）→ GPU 全市场训练。
