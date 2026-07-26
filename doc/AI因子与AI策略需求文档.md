# AI 因子与 AI 策略 需求文档 v1.2

> 基于项目已有代码架构分析 | 三轮评审通过 | 后续版本规划，当前不实施

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

> 三轮评审通过，编码前确认全部闭环。后续可随时启动 P0 实施。
