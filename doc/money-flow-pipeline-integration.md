# 资金流接入增量更新管线 v3（终版）

## Context

资金流两数据集扩列完成。但增量更新/全量重建后 money_* 列为 NaN。根因：`RawMarketDataAssembler` 未注入资金流数据，且 dataTypes 传递链中断。

## 改动清单（9 项：6 项 P0 阻塞 + 2 项 P1 优化 + 1 项 P1 修复）

### P0-1: MarketDataRepository::queryMoneyFlow()
**文件：** `MarketDataRepository.h/.cpp`  
**内容：** 新增方法，带内部分批查询（每批 500 symbols），日期格式输出 `YYYY-MM-DD`：
```sql
SELECT si.symbol, mf.trade_date::text AS trade_date, money_flow_columns::sqlSelect()
FROM fund.money_flow_daily mf
JOIN ref.symbol_info si ON mf.symbol_id = si.id
WHERE si.symbol = ANY(ARRAY[...]) AND mf.trade_date BETWEEN '...' AND '...'
```
**防御：** 使用 `ANY(ARRAY[...])` 避免 IN 子句过长 + 单引号转义 `replace("'", "''")`。

### P0-2: RawMarketDataAssembler — 资金流注入块
**文件：** `RawMarketDataAssembler.cpp`（minute_data 注入块之后，约第 146 行后）  
**内容：**
```cpp
bool hasMoneyFlow = std::find(dataTypes.begin(), dataTypes.end(), "money_flow") != dataTypes.end();
if (hasMoneyFlow) {
    try {
        auto mfRows = repo.queryMoneyFlow(chunk, ms, me);
        // 构建索引：key = "SYMBOL|YYYYMMDD"（大写 + 去横杠）
        std::map<std::string, std::unordered_map<std::string, std::string>> mfIdx;
        for (const auto& mr : mfRows) {
            const auto& mv = mr.getValues();
            auto si = mv.find("symbol");
            auto ti = mv.find("trade_date");
            if (si == mv.end() || ti == mv.end()) continue;
            std::string key;
            for (char c : si->second) key += static_cast<char>(toupper(c));
            key += '|';
            std::string rawDate = ti->second;
            if (rawDate.size() >= 10) rawDate = rawDate.substr(0, 10); // 截断时间戳 "YYYY-MM-DD HH:MM:SS" → "YYYY-MM-DD"
            for (char c : rawDate) if (c != '-') key += c;
            std::unordered_map<std::string, std::string> colVals;
            for (const auto& cn : cleaning::money_flow_columns::names()) {
                auto it = mv.find(cn);
                if (it != mv.end() && !it->second.empty())
                    colVals[cn] = it->second;
            }
            mfIdx[std::move(key)] = std::move(colVals);
        }
        // 注入日线 rows
        for (auto& row : rows) {
            const auto& rv = row.getValues();
            auto si = rv.find("symbol");
            auto ti = rv.find("trade_date");
            if (si == rv.end() || ti == rv.end()) continue;
            std::string key;
            for (char c : si->second) key += static_cast<char>(toupper(c));
            key += '|';
            std::string rawDate = ti->second;
            if (rawDate.size() >= 10) rawDate = rawDate.substr(0, 10); // 截断时间戳 "YYYY-MM-DD HH:MM:SS" → "YYYY-MM-DD"
            for (char c : rawDate) if (c != '-') key += c;
            auto it = mfIdx.find(key);
            if (it == mfIdx.end()) continue;
            for (const auto& [cn, cv] : it->second)
                row.setValue(cn, cv);
        }
    } catch (const std::exception& e) {
        INTERNAL_WARN_STREAM << "[RawMktAsm] 资金流注入失败，降级跳过: " << e.what();
        // 继续执行，基础数据不受影响
    }
}
```
**回归修复：** minute_data 注入块同样归一化 symbol 大小写 + 日期格式。

### P0-3: DataCleaningServiceRefactored 增量更新 — 检测 money_flow
**文件：** `DataCleaningServiceRefactored.cpp` 第 476 行后  
**内容：** 在 `minute_data` 检测之后加入：
```cpp
for (const auto& mc : cleaning::money_flow_columns::names()) {
    if (fileFields.count(mc)) { dataTypes.push_back("money_flow"); break; }
}
```
现有文件含 money_* 列 → 增量更新自动注入资金流数据。

### P0-4: DataFetchController 全量构建 — 检测 money_flow
**文件：** `DataFetchController.cpp` 第 186-188 行附近  
**内容：** 在 `typeNames` 构建完后、schema/assemble 前，检查是否已有同名数据集（sourceDataSetId > 0）且其字段含 money_* 列：
```cpp
// 从已有数据集继承 money_flow 类型（全量重建时保持字段一致性）
if (sourceDataSetId > 0) {
    auto existingInfo = DataCacheAdapter::instance().getDataSetInfo(sourceDataSetId);
    auto existingFields = DataCacheAdapter::instance().getDataSetSchemaFields(sourceDataSetId);
    std::unordered_set<std::string> fileFields;
    for (const auto& f : existingFields) fileFields.insert(f.toStdString());
    for (const auto& mc : cleaning::money_flow_columns::names()) {
        if (fileFields.count(mc)) {
            typeNames.push_back("money_flow");
            break;
        }
    }
}
```
**逻辑：** 首次全量构建 → 文件无 money_* 列 → typeNames 不含 money_flow → 用 augmentMoneyFlow 补列。之后任何重建（全量或增量）→ 自动检测到 money_* 列 → 自动注入。

### P0-5: 日期格式归一化
**位置：** `RawMarketDataAssembler.cpp` 资金流注入块（见 P0-2）  
**做法：** 索引 key 构建时 `for (char c : ti->second) if (c != '-') key += c` → 统一成 `YYYYMMDD`。

### P0-6: Symbol 大小写归一化
**位置：** `RawMarketDataAssembler.cpp` 资金流注入块和 minute_data 注入块  
**做法：** `for (char c : si->second) key += static_cast<char>(toupper(c))`。

### P1-7: DataCacheAdapter SQL 去重
**文件：** `DataCacheAdapter.cpp` 第 304-316 行  
**内容：** 删除内嵌 SQL，改为调用 `MarketDataRepository::queryMoneyFlow()`。

### P1-8: Windows 文件锁修复
**文件：** `DataCacheParquet.cpp`（已编码，待编译生效）

### P1-9: SQL 转义安全
**文件：** `MarketDataRepository::queryMoneyFlow()`  
**做法：** 使用 `ANY(ARRAY[...])` 参数化形式，并对 symbol 做单引号转义。

## 不改的文件
- `DataSourceRegistry.h` — schema 已就绪
- `DataFieldKeys.h` — 类型已定义
- `DataTableAssembler.cpp` — 泛型装配器
- `DataSelectionPanel.qml` — 不在 QML 加 money_flow 选项（资金流是衍生列，不应作为独立源）

## 验证
1. 编译全量 → app 启动
2. 现有数据集（已扩列）增量更新 → 新行 money_* 非 NaN
3. 现有数据集全量重建 → schema 含 money_* 列
4. 异常降级：断 PG 查 money_flow 表 → 日线仍正常产出
5. 幂等：augmentMoneyFlow(14) → 返回 0（已有列跳过）
