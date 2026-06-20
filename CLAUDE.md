# AStockQuantEngine 项目准则

## 代码修改强制流程

**动手前必须用中文写出方案，用户确认后才能写代码。** 不允许直接切到 Edit/Write。

方案要包含：
1. 改哪个文件、哪个函数、哪个位置
2. 旧代码中类似实现作为模板
3. 最简方案是什么（代码量最少、不改架构）
4. 改动如何服务于原始目标

用户回复"可以"或"继续"后才允许调用 Edit/Write。

## 架构约束

- Qt 只允许在 `src/ui/bridge/` 及上层出现
- 底层模块（`domain/`, `infrastructure/`）不允许混用 Qt 类型
- 数据流向：QML → bridge → service → repository → database（只上到下，禁止反向依赖）
