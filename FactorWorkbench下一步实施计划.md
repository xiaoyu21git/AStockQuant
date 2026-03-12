# FactorWorkbench下一步实施计划

## 当前完成状态

✅ **已完成的任务**：
1. **C++数据服务架构**：GlobalDataService、FactorDataModel、FactorParamController
2. **QML类型注册**：registerQmlTypes.cpp已更新，支持新类型
3. **CMake配置**：bridge库配置已更新，包含新源文件
4. **组件目录结构**：完整的模块化目录结构已创建
5. **核心组件开发**：
   - Navigation/ModeTitleBar.qml - 顶部导航栏
   - Home/HomePage.qml - 首页模块
   - Library/FactorLibraryPage.qml - 因子库模块
6. **重构主文件**：FactorWorkbench_Refactored.qml（300行，模块化架构）
7. **验证计划**：完整的验证和测试计划已制定

## 下一步行动计划

### 阶段一：编译验证和基础测试（1天）

#### 1.1 编译验证
```bash
# 进入构建目录
cd g:\C++\AStockQuantEngine\build

# 清理旧构建（可选）
cmake --build . --target clean --config Debug

# 重新生成项目
cmake ..

# 构建bridge库
cmake --build . --target bridge --config Debug

# 构建整个应用
cmake --build . --config Debug
```

#### 1.2 创建测试页面
在`src/app/Qml/test/`目录下创建`TestRefactoredComponents.qml`：

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import "../components/FactorWorkbench/Navigation" as Navigation
import "../components/FactorWorkbench/Home" as Home
import "../components/FactorWorkbench/Library" as Library

Window {
    width: 1200
    height: 800
    visible: true
    title: "重构组件测试"
    
    Column {
        anchors.fill: parent
        spacing: 20
        
        // 测试导航组件
        Navigation.ModeTitleBar {
            width: parent.width
            currentMode: "home"
            onModeSelected: console.log("导航切换:", mode)
            onBackClicked: console.log("返回")
        }
        
        // 测试切换
        TabBar {
            width: parent.width
            TabButton { text: "首页组件" }
            TabButton { text: "因子库组件" }
            TabButton { text: "完整重构" }
        }
        
        // 页面切换
        StackLayout {
            width: parent.width
            height: parent.height - 200
            
            // 首页组件测试
            Home.HomePage {
                width: parent.width
                height: parent.height
                onStartCreation: console.log("开始创建")
                onOpenLibrary: console.log("打开因子库")
            }
            
            // 因子库组件测试
            Library.FactorLibraryPage {
                width: parent.width
                height: parent.height
                onFactorSelected: console.log("选择因子:", factorId)
            }
            
            // 完整重构页面测试
            Loader {
                width: parent.width
                height: parent.height
                source: "../page/FactorWorkbench_Refactored.qml"
            }
        }
    }
}
```

#### 1.3 运行基础测试
1. 启动应用程序
2. 导航到测试页面
3. 验证：
   - 所有组件都能正确加载
   - 没有QML警告或错误
   - 点击事件能正常触发
   - C++绑定工作正常

### 阶段二：数据迁移（2-3天）

#### 2.1 分析需要迁移的数据
从原始`FactorWorkbench.qml`中识别需要迁移到C++的数据：

| 数据类型 | 原始位置 | 迁移目标 | 优先级 |
|---------|---------|---------|---------|
| **因子数据** | factorData数组（硬编码） | FactorDataModel | 高 |
| **系统状态** | systemStatus对象 | GlobalDataService | 高 |
| **通知消息** | notifications数组 | GlobalDataService | 中 |
| **创建模板** | templateModel | GlobalDataService | 中 |
| **历史记录** | historyModel | GlobalDataService | 中 |
| **最近分析** | recentAnalysisModel | GlobalDataService | 中 |
| **参数配置** | 各种inline*属性 | FactorParamController | 高 |

#### 2.2 扩展C++数据服务

**2.2.1 更新GlobalDataService.h/cpp**：
```cpp
// 添加系统状态API
Q_PROPERTY(QVariantMap systemStatus READ systemStatus NOTIFY systemStatusChanged)
Q_INVOKABLE void updateSystemStatus(const QString& key, const QVariant& value);

// 添加通知API
Q_PROPERTY(QVariantList notifications READ notifications NOTIFY notificationsChanged)
Q_INVOKABLE void addNotification(const QString& type, const QString& text, 
                                  const QString& time, const QString& action);
Q_INVOKABLE void clearNotifications();

// 添加模板API
Q_PROPERTY(QVariantList templates READ templates NOTIFY templatesChanged)
Q_INVOKABLE void loadTemplates();
Q_INVOKABLE QVariant getTemplate(const QString& templateId);

// 添加历史记录API
Q_PROPERTY(QVariantList historyRecords READ historyRecords NOTIFY historyRecordsChanged)
Q_INVOKABLE void addHistoryRecord(const QString& factorId, const QString& factorName,
                                   const QString& time, const QString& operation);

// 添加最近分析API
Q_PROPERTY(QVariantList recentAnalysis READ recentAnalysis NOTIFY recentAnalysisChanged)
Q_INVOKABLE void addAnalysisRecord(const QString& factorId, const QString& factorName,
                                    const QString& analysisType, const QString& result);
```

**2.2.2 更新FactorDataModel.h/cpp**：
```cpp
// 添加数据初始化方法
Q_INVOKABLE void initializeWithSampleData();
Q_INVOKABLE void loadFromDatabase();
Q_INVOKABLE void saveToDatabase();

// 添加CRUD操作
Q_INVOKABLE bool addFactor(const QVariantMap& factorData);
Q_INVOKABLE bool updateFactor(const QString& factorId, const QVariantMap& updates);
Q_INVOKABLE bool removeFactor(const QString& factorId);
Q_INVOKABLE QVariantMap getFactor(const QString& factorId) const;
Q_INVOKABLE void toggleFavorite(const QString& factorId, bool favorite);

// 添加筛选功能
Q_INVOKABLE void filterByCategory(const QString& category);
Q_INVOKABLE void filterByIC(double minIC);
Q_INVOKABLE void filterByTurnover(double maxTurnover);
Q_INVOKABLE void search(const QString& keyword);
```

#### 2.3 迁移数据的具体步骤

1. **创建数据初始化脚本**：
```cpp
// 在GlobalDataService::initialize()中初始化数据
void GlobalDataService::initialize()
{
    // 初始化系统状态
    QVariantMap status;
    status["计算资源"] = QVariantMap{{"status", "🟢"}, {"value", "3核空闲"}, {"color", "#10b981"}};
    status["内存使用"] = QVariantMap{{"status", "📊"}, {"value", "62%"}, {"color", "#3b82f6"}};
    status["数据处理"] = QVariantMap{{"status", "⚡"}, {"value", "实时"}, {"color", "#f59e0b"}};
    m_systemStatus = status;
    
    // 初始化通知
    QVariantList notifications;
    notifications.append(QVariantMap{
        {"type", "info"},
        {"text", "因子MA_20回测完成"},
        {"time", "5分钟前"},
        {"action", "查看报告"}
    });
    // ... 添加更多通知
    m_notifications = notifications;
    
    // 初始化模板
    initializeTemplates();
    
    // 初始化历史记录
    initializeHistory();
    
    // 初始化最近分析
    initializeRecentAnalysis();
}
```

2. **在FactorDataModel中初始化因子数据**：
```cpp
void FactorDataModel::initializeWithSampleData()
{
    beginResetModel();
    
    m_factorData.clear();
    
    // 添加示例因子数据
    FactorData factor1;
    factor1.factorId = "momentum_20d";
    factor1.displayName = "MA_20";
    factor1.majorCategory = "动量类";
    // ... 设置其他属性
    m_factorData.append(factor1);
    
    // 添加更多示例数据
    
    endResetModel();
    emit countChanged();
}
```

### 阶段三：组件完善（3-5天）

#### 3.1 创建因子模块开发

**目录结构**：
```
src/app/Qml/components/FactorWorkbench/Creation/
├── FactorCreationWizard.qml          # 主向导组件
├── TypeSelectionStep.qml            # 类型选择步骤
├── ParameterConfigStep.qml          # 参数配置步骤
├── PreviewPanel.qml                 # 实时预览面板
└── ParameterComponents/             # 参数组件目录
    ├── MomentumParameterPanel.qml
    ├── ValueParameterPanel.qml
    ├── TechnicalParameterPanel.qml
    ├── QualityParameterPanel.qml
    ├── SentimentParameterPanel.qml
    ├── MacroParameterPanel.qml
    ├── RiskParameterPanel.qml
    └── CustomParameterPanel.qml
```

**开发步骤**：
1. **FactorCreationWizard.qml**：实现向导式创建流程
2. **TypeSelectionStep.qml**：复用原始页面中的类型选择逻辑
3. **ParameterConfigStep.qml**：根据选择的类型动态加载对应参数面板
4. **各类型参数面板**：实现具体的参数输入组件
5. **PreviewPanel.qml**：实时预览因子计算效果

#### 3.2 调试模块开发

**目录结构**：
```
src/app/Qml/components/FactorWorkbench/Debug/
├── FactorDebugPanel.qml            # 主调试面板
├── ParameterSlider.qml             # 参数滑块组件
├── ParameterInput.qml              # 参数输入组件
├── RealTimePreview.qml             # 实时预览组件
└── PerformanceMetrics.qml          # 性能指标显示
```

**功能要点**：
- 实时参数调整和预览
- 性能指标实时计算
- 历史参数对比
- 参数优化建议

#### 3.3 分析模块开发

**目录结构**：
```
src/app/Qml/components/FactorWorkbench/Analysis/
├── FactorAnalysisPanel.qml         # 主分析面板
├── AnalysisCard.qml                # 分析卡片组件
├── PerformanceChart.qml            # 性能图表
├── GroupReturnsChart.qml           # 分组收益图表
├── StabilityAnalysis.qml           # 稳定性分析
└── CorrelationMatrix.qml           # 相关性矩阵
```

**功能要点**：
- IC/IR分析
- 分组收益分析
- 稳定性检验
- 相关性分析
- 可视化图表

#### 3.4 回测模块开发

**目录结构**：
```
src/app/Qml/components/FactorWorkbench/Backtest/
├── BacktestPanel.qml               # 主回测面板
├── BacktestConfig.qml              # 回测配置
├── BacktestResultView.qml          # 回测结果展示
├── PerformanceSummary.qml          # 绩效摘要
└── RiskMetrics.qml                 # 风险指标
```

**功能要点**：
- 回测参数配置
- 回测进度监控
- 结果可视化
- 绩效指标计算

### 阶段四：性能优化和集成测试（2-3天）

#### 4.1 性能优化措施

1. **数据绑定优化**：
   - 使用`QAbstractListModel`的`dataChanged`信号
   - 实现分批加载和虚拟滚动
   - 缓存计算结果

2. **渲染性能优化**：
   - 使用`Loader`延迟加载非活动组件
   - 优化QML组件层次结构
   - 减少不必要的属性绑定

3. **内存优化**：
   - 及时释放不使用的组件
   - 优化图像和资源加载
   - 实现数据懒加载

#### 4.2 集成测试清单

| 测试项目 | 测试方法 | 预期结果 | 验证指标 |
|---------|---------|---------|---------|
| 页面切换 | 在6个模式间频繁切换 | 流畅无卡顿 | 切换时间 < 200ms |
| 数据加载 | 加载大量因子数据 | 快速响应 | 加载时间 < 1s |
| 搜索筛选 | 输入搜索关键词 | 实时过滤 | 响应时间 < 100ms |
| 参数调整 | 调整调试参数 | 实时更新预览 | 更新延迟 < 50ms |
| 创建流程 | 完整创建因子流程 | 流程顺畅 | 完成时间 < 2min |
| 内存使用 | 长时间运行 | 内存稳定 | 内存增长 < 50MB/h |

### 阶段五：部署和文档（1天）

#### 5.1 代码部署

1. **替换主文件**：
```bash
# 备份原始文件
copy src\app\Qml\page\FactorWorkbench.qml src\app\Qml\page\FactorWorkbench_original.qml

# 使用重构版本
copy src\app\Qml\page\FactorWorkbench_Refactored.qml src\app\Qml\page\FactorWorkbench.qml
```

2. **清理临时文件**：
   - 删除测试页面
   - 清理调试代码
   - 更新版本号

#### 5.2 文档更新

1. **架构文档**：
   - 更新系统架构图
   - 编写组件接口文档
   - 更新API文档

2. **开发文档**：
   - 编写组件开发指南
   - 更新构建和部署指南
   - 编写故障排除指南

3. **用户文档**：
   - 更新用户手册
   - 编写新功能介绍
   - 更新帮助文档

## 风险评估与缓解措施

### 技术风险

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|---------|
| C++绑定失败 | 低 | 高 | 提前编译验证，准备回滚计划 |
| 数据迁移丢失 | 中 | 中 | 备份原始数据，逐步迁移 |
| 性能不达标 | 中 | 中 | 性能监控，及时优化 |
| 兼容性问题 | 低 | 高 | 充分测试，保持向后兼容 |

### 项目管理风险

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|---------|
| 时间超支 | 中 | 中 | 分阶段实施，优先核心功能 |
| 质量不达标 | 低 | 高 | 代码审查，自动化测试 |
| 团队协作问题 | 低 | 中 | 明确分工，定期沟通 |

## 成功标准

### 技术成功标准
- [ ] 应用程序编译通过，无错误
- [ ] 所有C++绑定正常工作
- [ ] 性能指标达到预期
- [ ] 代码质量符合规范

### 业务成功标准
- [ ] 所有原有功能正常工作
- [ ] 用户体验无明显下降
- [ ] 新架构支持后续功能扩展
- [ ] 团队开发效率提升

### 项目成功标准
- [ ] 按计划时间完成
- [ ] 在预算范围内完成
- [ ] 代码质量达标
- [ ] 文档完整更新

## 资源需求

### 人力资源
- 1名C++开发工程师（数据服务）
- 1名QML开发工程师（UI组件）
- 1名测试工程师（质量保证）

### 时间资源
- 总时长：8-12天
- 阶段划分：5个阶段
- 缓冲时间：2天

### 技术资源
- 开发环境：已具备
- 测试环境：需要搭建
- 文档工具：已具备

## 监控与报告

### 每日进度报告
- 当天完成的工作
- 遇到的问题和解决方案
- 明天的计划
- 风险状态更新

### 关键里程碑
1. **M1**：编译验证通过（第1天）
2. **M2**：数据迁移完成（第3天）
3. **M3**：核心组件开发完成（第7天）
4. **M4**：性能优化完成（第10天）
5. **M5**：完整部署完成（第12天）

## 沟通计划

### 内部沟通
- 每日站会：15分钟
- 每周评审：1小时
- 问题解决：即时沟通

### 外部沟通
- 进度报告：每周一次
- 演示会议：关键里程碑后
- 文档更新：阶段完成后

## 附录

### 附录A：技术依赖清单
- Qt 6.5+
- CMake 3.20+
- Visual Studio 2019+
- C++17标准

### 附录B：参考文档
- FactorWorkbench拆分方案.md
- FactorWorkbench重构验证计划.md
- 原始FactorWorkbench.qml代码
- Qt官方文档

### 附录C：联系人列表
- 项目经理：[姓名]
- 技术负责人：[姓名]
- C++开发：[姓名]
- QML开发：[姓名]
- 测试工程师：[姓名]

---
*计划制定者：AI助手*
*日期：2026年3月10日*
*版本：1.0*