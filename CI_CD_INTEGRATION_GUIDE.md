# CI/CD 集成指南

## 概述

本文档说明如何将C++ Event系统测试集成到CI/CD流程中。

---

## 1️⃣ 本地运行（快速开发）

### 快速运行所有测试

```bash
# 使用Python脚本（推荐）
python run_tests.py

# 或指定构建类型
python run_tests.py --build-type Release
```

### 仅运行特定测试

```bash
# 运行Event单元测试
python run_tests.py --tests EventTest*

# 运行压力测试
python run_tests.py --tests EventStressTest*

# 运行Domain集成测试
python run_tests.py --tests EventDomainIntegrationTest*
```

### 分步运行

```bash
# 仅配置
python run_tests.py --configure

# 仅编译
python run_tests.py --build

# 仅运行测试
python run_tests.py --run

# 仅验证性能
python run_tests.py --performance
```

---

## 2️⃣ GitHub Actions CI/CD

### 配置文件位置

```
.github/workflows/cpp-tests.yml
```

### 工作流特性

✅ **自动触发条件**:
- main 分支 push
- develop 分支 push
- PR (拉取请求)
- 涉及以下路径的更改:
  - `src/engine/**`
  - `tests/**`
  - `CMakeLists.txt`

✅ **矩阵测试**:
- Debug 和 Release 两种构建类型
- 自动并行执行

✅ **性能检查**:
- Release版本自动验证性能基准
- 性能下降超过10%时失败构建

✅ **测试报告**:
- 生成JUnit XML格式报告
- 自动发布到GitHub Actions UI

### 查看构建结果

```
https://github.com/[your-repo]/actions/workflows/cpp-tests.yml
```

---

## 3️⃣ Azure Pipelines CI/CD

### 配置文件位置

```
azure-pipelines.yml
```

### 工作流特性

✅ **自动触发条件**:
- main/develop 分支 push
- PR (拉取请求)
- 日程触发 (每日UTC 2点)

✅ **矩阵测试**:
- Debug 和 Release 构建
- 自动并行执行

✅ **性能监控**:
- Release版本性能基准检查
- 超过10%下降时标记警告
- 超过20%下降时构建失败

✅ **输出管理**:
- 自动发布测试报告
- 上传测试可执行文件
- 代码覆盖率集成

### 配置Azure Pipeline

1. 在Azure DevOps中创建新Pipeline
2. 连接到Git仓库
3. 选择 `azure-pipelines.yml` 文件
4. 配置构建代理（Windows）
5. 运行管道

### 查看构建结果

```
https://dev.azure.com/[your-org]/[your-project]/_build
```

---

## 4️⃣ 性能监控

### 运行性能监控

```bash
# 运行完整监控流程
python performance_monitor.py --monitor

# 指定构建类型
python performance_monitor.py --monitor --build-type Release

# 查看性能历史
python performance_monitor.py --history

# 查看特定测试历史
python performance_monitor.py --history --test PythonParity
```

### 性能告警阈值

| 下降程度 | 阈值 | 影响 |
|---------|------|------|
| ✅ 无下降 | 0% | 正常 |
| ⚠️ 轻度下降 | 1-10% | 警告 |
| 🚨 中度下降 | 10-20% | CI/CD警告 |
| 💥 严重下降 | 20%+ | CI/CD失败 |

### 性能指标基准

| 测试 | 基准 | 单位 |
|------|------|------|
| HighVolumeEventPublishing | 769,231 | events/sec |
| ConcurrentPublishers (8线程) | 258,065 | events/sec |
| PythonParity (8线程×4订阅) | 1,495,327 | events/sec |

---

## 5️⃣ 手动编译和测试

### 完整流程

```bash
# 进入工程目录
cd G:\C++\AStockQuantEngine

# 清除旧的构建
Remove-Item build -Recurse -ErrorAction SilentlyContinue

# 创建构建目录
mkdir build

# 配置CMake (Debug)
cd build
cmake .. -G "Visual Studio 16 2019" -DENABLE_TESTING=ON

# 编译
cmake --build . --target test_eventsystem --config Debug -j4

# 运行测试
cd tests
.\test_eventsystem.exe
```

### 快速命令

```powershell
# 编译 + 运行 (快捷方式)
cd build
cmake --build . --target test_eventsystem --config Debug && `
cd tests && `
.\test_eventsystem.exe
```

---

## 6️⃣ 测试报告格式

### XML 报告 (JUnit)

```bash
.\test_eventsystem.exe --gtest_output=xml:test_results.xml
```

生成的文件可用于:
- Azure Pipelines 结果发布
- GitHub Actions 报告
- Jenkins 集成
- 其他CI/CD工具

### JSON 报告 (本地)

```bash
python run_tests.py  # 自动生成 test_report.json
```

包含内容:
- 时间戳
- 编译配置
- 测试结果
- 性能指标

---

## 7️⃣ 常见问题

### Q: 如何只在Release版本运行性能测试？

**A:** 修改工作流文件的性能检查步骤:

```yaml
- name: Performance Check
  if: matrix.build_type == 'Release'  # 只在Release运行
```

### Q: 如何自定义性能基准？

**A:** 编辑 `performance_monitor.py`:

```python
self.baseline = {
    "HighVolumeEventPublishing": 700000,  # 修改此值
    "ConcurrentPublishers": 250000,
    "PythonParity": 1400000,
}
```

### Q: 如何禁用某个测试？

**A:** 在工作流中使用过滤:

```yaml
--gtest_filter=*-DisabledTest*
```

或在C++代码中:

```cpp
TEST_F(TestSuite, DISABLED_TestName) {
    // 此测试被禁用
}
```

### Q: 如何增加超时时间？

**A:** 修改工作流的 `timeout`:

```yaml
- name: Run tests
  timeout-minutes: 10  # 修改此值
```

---

## 8️⃣ 最佳实践

### 1. 定期运行测试

- ✅ 每次提交都运行
- ✅ 每日定时运行 (夜间)
- ✅ 发布前完整测试

### 2. 监控性能趋势

- 🔍 定期查看性能历史
- 📊 在性能报告中追踪趋势
- 🚨 对异常下降立即调查

### 3. 维护测试代码

- 🔄 定期审查测试用例
- 🎯 添加新的测试场景
- 🧹 删除过时或不再适用的测试

### 4. 文档管理

- 📝 记录性能基准变更原因
- 📋 维护已知限制列表
- 🔗 链接到相关的issue和PR

---

## 9️⃣ 集成检查清单

- [ ] 本地 Python 脚本运行成功
- [ ] GitHub Actions 工作流配置完整
- [ ] Azure Pipelines 配置完整
- [ ] 性能监控脚本运行成功
- [ ] 性能基准文档更新
- [ ] 团队成员培训完成
- [ ] 告警通知配置 (可选)

---

## 🔟 故障排查

### 编译失败

```bash
# 检查CMake版本
cmake --version

# 检查MSVC版本
cl.exe /?  # 显示编译器信息

# 清除缓存并重新配置
rm -r build
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019" -DENABLE_TESTING=ON
```

### 测试超时

```bash
# 在Debug模式下运行（可能更慢）
cmake --build . --config Debug

# 或增加超时时间
.\test_eventsystem.exe --gtest_break_on_failure
```

### 性能下降

```bash
# 1. 使用Release版本重新测试
cmake --build . --config Release

# 2. 检查系统资源
Get-Process | Sort-Object CPU -Desc | Select-Object -First 5

# 3. 查看性能历史
python performance_monitor.py --history
```

---

## 需要帮助？

- 📧 Email: [project-team@company.com]
- 🐛 Issues: [GitHub Issues链接]
- 💬 Discussions: [GitHub Discussions链接]
- 📚 Wiki: [项目Wiki链接]

---

**最后更新**: 2026年1月30日
