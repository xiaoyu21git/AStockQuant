# AI 因子与 AI 策略 实施任务计划

> 基于 AI因子与AI策略需求文档 v1.2 | 2026-07-27

---

## P0 — AI 因子推理引擎 (预估 5 工作日)

### 任务 1: IModelInference 接口 + ONNX 实现

| 项 | 内容 |
|---|---|
| 目标 | 创建推理引擎抽象层，实现 ONNX Runtime 后端 |
| 新增文件 | `src/domain/factor/include/factor_compute/IModelInference.h` |
| | `src/domain/factor/src/factor_compute/OnnxInference.cpp` |
| 依赖 | ONNX Runtime 已集成 |
| 验收 | 可以加载 .onnx 文件，执行一次推理 |

### 任务 2: FeatureTensorBuilder

| 项 | 内容 |
|---|---|
| 目标 | 从 HistoricalView 构建模型输入张量 |
| 新增文件 | `src/domain/factor/include/factor_compute/FeatureTensorBuilder.h` |
| | `src/domain/factor/src/factor_compute/FeatureTensorBuilder.cpp` |
| | `config/ai/feature_config_template.json` |
| 核心功能 | 批量读取多字段序列 → ZScore/MinMax/Log 归一化 → 拼接 [N,F,W] 张量 |
| | vwap fallback: 无字段时自动计算 (high+low+close)/3 |
| | 分批策略: batchSize=512 |
| 验收 | 输入配置文件和日期，输出正确维度的 float 数组 |

### 任务 3: feature_config.json + Params 对齐

| 项 | 内容 |
|---|---|
| 目标 | 训练和推理共用特征配置，加载时校验 |
| 核心功能 | DLFactor::loadConfig() 从 modelPath 目录读取 feature_config.json |
| | 校验 5 条对齐契约 (字段列表、窗口、特征数、预处理、fillna) |
| | 与 Params 冲突时以配置文件为准，告警 |
| 验收 | 配置文件与 Params 不一致时输出告警并采用配置值 |

### 任务 4: DLFactor::calculate() 推理实现

| 项 | 内容 |
|---|---|
| 目标 | 替换全零骨架为真实推理 |
| 修改文件 | `src/domain/factor/src/DLFactor.cpp` |
| 核心逻辑 | load ONNX 模型 (懒加载) → FeatureTensorBuilder 构建张量 → 批量推理 → result.values |
| | `#ifdef HAS_ONNX_RUNTIME` 条件编译 |
| | unfound 时 metadata["inferenceAvailable"]=false |
| 验收 | 端到端: 配置 modelPath → 回测 → 输出非零因子值 |

### 任务 5: 模型热更新

| 项 | 内容 |
|---|---|
| 目标 | 检测模型文件变化，自动重新加载 |
| 核心逻辑 | 每个回测日 stat(modelPath) 检查 mtime |
| | 变化 → 重新加载 → 校验特征维度 → 更新缓存 |
| | 失败 → 保持旧模型 + 告警 |
| 验收 | 替换模型文件后下一交易日自动切换 |

---

## P1 — 训练管线 (预估 3 工作日)

### 任务 6: Python 训练脚本

| 项 | 内容 |
|---|---|
| 目标 | 从 Parquet 读取历史数据，训练监督学习模型 |
| 新增文件 | `astock_engine/ai/train.py` |
| | `astock_engine/ai/export_onnx.py` |
| 核心逻辑 | 读取 Parquet → 构造特征/标签 (预测 predHorizon 日后收益) → 训练 LSTM → 导出 ONNX |
| 输出 | `models/dl_v1/feature_config.json` + `model.onnx` + `metadata.json` |
| 验收 | 产出第一个可用模型，DLFactor 加载后可推理 |

---

## P2 — AI 策略 (预估 5 工作日)

### 任务 7: RL 环境 + PPO

| 项 | 内容 |
|---|---|
| 目标 | 构建强化学习交易环境 |
| 新增文件 | `astock_engine/ai/env.py`, `ai/models/ppo.py` |
| 核心设计 | Top-K=50 选股，动作空间 [-1,1]^K |
| | reward = 日收益 - 成本 - 回撤惩罚 |

### 任务 8: 策略插件 + 离线信号回测

| 项 | 内容 |
|---|---|
| 目标 | 将 AI 策略接入回测 |
| 新增文件 | `astock_engine/strategies/ai_strategy.py` |
| | `astock_engine/pybindings_strategy.cpp` (get_market_snapshot) |
| 集成方式 | 离线: Python 预生成信号 CSV → C++ 回测读取 |
| 验收 | AI 策略跑通完整回测 |

---

## 依赖关系

```
任务1 (接口) ──→ 任务2 (特征构建) ──→ 任务4 (DLFactor推理)
                    │                      │
                    └── 任务3 (配置对齐) ──┘
                                          │
                    任务5 (热更新) ──────┘

任务6 (训练) ──→ 任务4 可用

任务7 (RL环境) ──→ 任务8 (策略插件)
```

P0 可并行: 任务1+2+3 → 联调 任务4 → 收尾 任务5

---

## 当前完成状态

| 阶段 | 进度 |
|------|------|
| 需求文档 v1.2 | ✅ 三轮评审通过 |
| ONNX Runtime 集成 | ✅ CMake + IMPORTED 目标 |
| DLFactor 骨架 | ✅ Params + 注册 + UI |
| P0 编码 | ⬜ 待启动 |
| P1 训练管线 | ⬜ 待 P0 完成 |
| P2 AI 策略 | ⬜ 待 P1 完成 |
