# Copilot Instructions for AStockQuantEngine

## 项目架构与核心组件
- **AStockQuantEngine** 是一个多语言（C++/Python）量化交易引擎，核心在 `astock_engine/` 目录。
- 主要模块：
  - `market.py`, `market_myquant.py`：行情数据接入与仿真。
  - `quant_core_native.py`：核心量化逻辑。
  - `quant_data_manager.py`：数据管理与持久化。
  - `pybindings_*.cpp`：C++/Python 互操作桥接。
  - `backtest/`, `strategies/`, `optimization/`：回测、策略、优化相关。
- **EventBus** 事件驱动架构，支持高性能异步通信，详见 `astock_engine/pybindings_eventbus.cpp`。
- 测试体系详见 `astock_engine/tests/README.md`，包含单元、集成、互操作、烟雾测试。

## 关键开发流程
- **构建 C++ 组件**：
  - 推荐用 CMake，主入口 `CMakeLists.txt`。
  - 构建 demo 示例：
    ```sh
    cmake --build . --target demo_gm_strategy --config Release
    ```
- **运行测试**：
  - Python 测试统一入口：
    ```sh
    python run_tests.py
    ```
  - 详细测试覆盖与统计见 `astock_engine/tests/README.md`。
- **调试/集成**：
  - C++/Python 互操作需关注 pybind11 相关桥接文件。
  - 事件流、数据流调试建议从 `EventBus` 相关测试入手。

## 项目约定与风格
- Python 代码风格以 PEP8 为主，C++ 遵循 Google C++ Style。
- 事件、数据流均采用显式注册与解耦，避免隐式依赖。
- 测试用例命名规范：`test_<模块>_<功能>.py`。
- 策略算法实现禁止直接使用数字字面量；所有阈值、窗口、容量、步长、默认值、上下界、数量单位都必须先定义为具名常量或宏，再在算法中引用。
- 策略算法默认按大批量大数据场景设计；实现时必须优先考虑时间复杂度、空间占用、批量吞吐、容量预留、避免重复分配、避免无界缓存和不必要的数据复制。
- 策略算法若存在复杂向量、矩阵、协方差、回归、优化等数值计算，可以直接使用 Eigen；但仍需保持 typed-only 边界清晰，并以批量吞吐、内存效率和可验证的性能收益为前提。
- 重要文档：
  - `README.md`（主项目说明）
  - `astock_engine/tests/README.md`（测试体系与覆盖）
  - `QUICKSTART.md`、`OPTIMIZATION_GUIDE.md`（快速上手与优化指南）

## 外部依赖与集成
- Python 依赖见 `astock_engine/requirements.txt`。
- C++ 依赖通过 CMake 管理，部分三方库需手动配置（如 pybind11、gmsdk）。
- 数据与配置文件位于 `data/`、`config/` 目录。

## 典型模式与示例
- 事件驱动：所有跨模块通信建议通过 EventBus。
- C++/Python 互操作：参考 `pybindings_eventbus_simple.cpp`、`test_cpp_python_eventbus.py`。
- 策略开发：在 `strategies/` 下新建策略，继承核心接口。

---

如需了解详细测试覆盖、业务组件与代码质量指标，请查阅 `astock_engine/tests/README.md`。
如遇特殊构建或集成问题，优先查阅主目录下各类 `GUIDE.md` 文档。
