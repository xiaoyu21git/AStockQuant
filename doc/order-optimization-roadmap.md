# OrderRequest / Order 性能优化路线图

> 2026-06-29 创建。记录当前实现状态与未来极致优化的差距。

---

## 已实现（C++17 标准，零外部依赖）

| 优化项 | 实现方式 |
|--------|---------|
| 枚举 `uint8_t` 底层 | `enum class Xxx : uint8_t` — 6 个枚举各占 1 字节 |
| 扩展槽零堆分配 | `std::array<ExtensionSlot, 4>` + `std::variant<int64_t, double, uint64_t>` — O(4) 线性扫描 |
| `clone()` trivial copy | `return *this` — 编译器生成 memcpy |
| 无锁状态机 | `std::atomic<uint8_t>` + CAS (`compare_exchange_strong`) |
| 位图转换表 | `constexpr uint64_t TRANS` — 8×8 状态矩阵, O(1) 位运算查表 |
| applyFill 纯算术 | `noexcept`，加权均价，零锁 |
| 缓存行对齐 | `alignas(64)` — 只读字段与高频写入字段分属不同缓存行 |
| batch validate | `validateBatch(reqs, n, errors)` — 连续内存, SIMD 友好 |
| 零虚函数 | Order / OrderRequest 均无 vtable |
| 时间戳 | 复用 `foundation::utils::Timestamp`（微秒精度） |

## 待实现（需基础设施变更）

| 优化项 | 阻塞原因 | 建议触发条件 |
|--------|---------|-------------|
| **Symbol → uint64_t 句柄** | 需全局符号注册表（`SymbolRegistry`），gmsdk API 全用 `const char*`，需双向映射 | Tick 延迟 > 50μs 时启动 |
| **cl_ord_id → uint64_t** | 同上，UUID 字符串 → 时间戳+序号合成 | 同上 |
| **位域打包枚举** (`side:2, order_type:3, tif:3, pos_effect:3`) | C++ 位域不可靠取地址，getter/setter 需适配 | 配合 POD 化一起做 |
| **POD struct 公开字段** | 违反 CLAUDE.md §1.1.3（数据封装必须 private），需评审是否对 OrderRequest 豁免 | 架构评审通过后 |
| **`boost::object_pool`** | 项目无 Boost 依赖，引入需评估编译时间/平台兼容性 | 日下单量 > 10万笔时启动 |
| **侵入式链表 (`boost::intrusive`)** | 同上 + `TradeExecutionEngine` 当前用 `std::vector<TradeOrder>` | 同上 |
| **`expire_timestamp` → `uint64_t` 纳秒** | `OrderRequest::m_expireTime` 当前为 `std::string` (ISO8601)，需迁移 | GTD 订单接入时 |

## 实施铁律

1. **消灭堆分配**：扩展槽已做，Symbol/cl_ord_id 句柄化待做
2. **消灭锁**：Order 状态机已做（无锁 CAS）
3. **消灭虚函数**：已做（零 vtable）
4. **缓存行对齐**：已做（`alignas(64)`）
5. **禁止 `std::shared_ptr`** — 引用计数的原子加减在高频路径不可接受
6. **禁止 `std::function` 回调** — 类型擦除触发堆分配
7. **禁止 validate 中加锁** — 纯函数，无状态
8. **禁止虚函数** — 编译期多态（variant + visit 或 CRTP）

## 当前类位置

- `src/domain/trading/TradingTypes.h` — OrderRequest, Order, 枚举, ExtensionSlot
- `src/domain/trading/TradingTypes.cpp` — 全部实现
