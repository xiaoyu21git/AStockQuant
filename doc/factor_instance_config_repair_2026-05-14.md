# factor_instance 配置修复工具

> **2026-08-16 更新**：本页描述的 `tools/repair_factor_instance_configs.py` 已不在仓库中（其历史修复内容已被后续脚本吸收）。
> 当前参数键统一为 `parameters`（读取侧 FactorConfigAccess、写入侧 FactorService、数据库 42 行全部一致），
> 键名迁移与内容修补统一由 `tools/restore_parameters_key.py` 完成（支持 `--dry-run`，幂等）。
> 该脚本同时纠正了 fix_factors.py/fix_factors2.py 留下的错误枚举映射（两脚本注释 "低频=2, Z-Score=0, Rank=1" 与 C++ 枚举
> `DataFrequency{Minute=0,Daily=1,Weekly=2...}` / `StandardizationMethod{None=0,ZScore=1,Rank=3...}` 任何历史版本都不符）。

用途：把 `factor_instance.full_config` 的历史字符串枚举和别名键收口到当前 numeric-only 配置口径，不在运行时继续保留字符串兼容。

当前脚本：`tools/restore_parameters_key.py`

推荐先做干跑：

```powershell
python .\tools\restore_parameters_key.py --dry-run
```

确认输出后再执行写入：

```powershell
python .\tools\restore_parameters_key.py
```

当前修复范围：

- 顶层 `calculation` 键平移为 `parameters`（calculation 是 2026-05-15~07-28 窗口期计算侧实际读取的键，平移保持行为不变）。
- 嵌套 `config.calculation` 平移为 `config.parameters`。
- `parameters` 下已确认出现过的字符串枚举字段改写为数字枚举（按因子名意图翻译，如 "低频"→1(Daily)、"Z-Score"→1、"Rank"→3、"None"→0）。
- 非 configurable 因子的 `lagEnabled -> laggedEnabled` 补齐。
- 46079 低波因子按用户确认修补 components 与三项权重。

脚本不会在发现未知字符串枚举时静默吞掉；变更行与字段级修补说明全部打印，可据此复核。