# factor_instance 配置修复工具

用途：把 `factor_instance.full_config` 的历史字符串枚举和别名键收口到当前 numeric-only 配置口径，不在运行时继续保留字符串兼容。

当前脚本：`tools/repair_factor_instance_configs.py`

推荐先做干跑：

```powershell
python .\tools\repair_factor_instance_configs.py --dry-run --fail-on-unsupported
```

确认输出后再执行写入：

```powershell
python .\tools\repair_factor_instance_configs.py --fail-on-unsupported
```

只看 ACTIVE 实例：

```powershell
python .\tools\repair_factor_instance_configs.py --dry-run --only-active
```

当前修复范围：

- 顶层 `factorType` 的历史字符串值改写为数字枚举。
- `calculation` 下已确认出现过的字符串枚举字段改写为数字枚举。
- `boundaryRules` 下历史字符串枚举字段改写为数字枚举。
- 非 configurable 因子的 `lagEnabled -> laggedEnabled` 补齐。
- configurable 因子的 `laggedEnabled -> lagEnabled` 补齐。
- 已确认出现过的别名键补齐，例如 `lookback_window -> window`、`quality_threshold -> qualityThreshold`。

脚本不会在发现未知字符串枚举时静默吞掉；配合 `--fail-on-unsupported` 可以把未覆盖的历史脏数据直接暴露出来。