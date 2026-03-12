# FactorWorkbench.qml 拆分方案

## 问题分析

当前 `FactorWorkbench.qml` 文件存在以下问题：
1. **文件过大**：超过6000行，包含大量重复代码和内联组件
2. **数据与UI混合**：大量数据逻辑直接写在QML中，难以维护
3. **组件复用性差**：内联组件无法在其他文件中重用
4. **性能问题**：大量内联数据和逻辑导致渲染和计算性能下降

## 拆分原则

1. **数据与UI分离**：所有数据相关逻辑移到C++端，通过绑定方式初始化
2. **组件化**：将大型UI拆分为可重用的独立组件
3. **模块化**：按功能划分独立模块，便于维护和扩展
4. **性能优化**：减少QML中的数据操作，利用C++高性能优势

## C++绑定初始化方案

### 1. 数据层 C++ 绑定

**已实现组件：**
- ✅ `GlobalDataService` - 全局数据服务（单例模式）
- ✅ `FactorDataModel` - 因子数据模型（QAbstractListModel）
- ✅ `FactorParamController` - 因子参数控制器

**可用C++初始化的数据：**

| 数据类别 | 当前状态 | C++绑定方案 |
|---------|---------|------------|
| **因子数据** | QML硬编码 | 使用 `FactorDataModel` |
| **系统状态** | QML属性 | 使用 `GlobalDataService` |
| **通知消息** | QML数组 | 使用 `GlobalDataService` |
| **创建模板** | ListModel | 使用 `GlobalDataService` |
| **历史记录** | ListModel | 使用 `GlobalDataService` |
| **最近分析** | ListModel | 使用 `GlobalDataService` |
| **参数配置** | QML内联逻辑 | 使用 `FactorParamController` |

### 2. 数据结构映射

**FactorDataModel 数据结构：**
```cpp
struct FactorData {
    QString factorId;           // 因子ID
    QString factorName;         // 因子内部名称
    QString displayName;        // 显示名称
    QString majorCategory;      // 主分类（动量类、价值类等）
    QString subCategory;        // 子分类
    QString description;        // 描述
    double icValue;             // IC值
    double irValue;             // IR值
    int validityDays;           // 有效期
    double turnoverRate;        // 换手率
    bool isRecommended;         // 是否推荐
    bool isFavorite;            // 是否收藏
    QString status;             // 状态
    QStringList tags;           // 标签
    QString creator;            // 创建者
    QString createDate;         // 创建日期
    QVector<double> groupReturns; // 分组收益
};
```

**GlobalDataService 数据结构：**
```cpp
// 系统状态
QVariantMap m_systemStatus;

// 通知消息
QVariantList m_notifications;

// 创建模板
QVariantList m_templates;

// 历史记录
QVariantList m_historyRecords;

// 最近分析
QVariantList m_recentAnalysis;
```

## 组件拆分方案

### 1. 导航组件模块
```
src/app/Qml/components/FactorWorkbench/
├── Navigation/
│   ├── ModeSelector.qml              // 模式选择器
│   ├── ModeTitleBar.qml              // 模式标题栏
│   └── BottomNotificationBar.qml     // 底部通知栏
```

### 2. 首页模块
```
src/app/Qml/components/FactorWorkbench/
├── Home/
│   ├── HomePage.qml                  // 首页主组件
│   ├── FeatureCard.qml               // 功能卡片（独立组件）
│   └── WelcomeSection.qml            // 欢迎区域
```

### 3. 因子库模块
```
src/app/Qml/components/FactorWorkbench/
├── Library/
│   ├── FactorLibraryPage.qml         // 因子库主页面
│   ├── FactorCardEnhanced.qml        // 因子卡片（已有）
│   ├── FactorListRow.qml             // 因子列表行（已有）
│   ├── FilterBar.qml                 // 筛选栏组件
│   └── ViewModeSelector.qml          // 视图模式选择器
```

### 4. 创建因子模块
```
src/app/Qml/components/FactorWorkbench/
├── Creation/
│   ├── FactorCreationWizard.qml      // 因子创建向导
│   ├── TypeSelectionStep.qml         // 类型选择步骤
│   ├── ParameterConfigStep.qml       // 参数配置步骤
│   ├── ParameterComponents/
│   │   ├── MomentumParameterPanel.qml
│   │   ├── ValueParameterPanel.qml
│   │   ├── TechnicalParameterPanel.qml
│   │   └── GenericParameterPanel.qml
│   └── PreviewPanel.qml              // 实时预览面板
```

### 5. 调试与分析模块
```
src/app/Qml/components/FactorWorkbench/
├── Debug/
│   ├── FactorDebugPanel.qml          // 因子调试面板
│   └── ParameterSlider.qml           // 参数滑块组件
├── Analysis/
│   ├── FactorAnalysisPanel.qml       // 因子分析面板
│   ├── AnalysisCard.qml              // 分析卡片（独立组件）
│   └── PerformanceChart.qml          // 性能图表组件
└── Backtest/
    ├── BacktestPanel.qml             // 回测面板
    └── BacktestResultView.qml        // 回测结果视图
```

## 实施步骤

### 第一阶段：基础架构
1. ✅ 创建 C++ 数据服务组件
2. ✅ 创建 QML 类型注册
3. ✅ 更新 CMake 配置
4. 重构主页面结构

### 第二阶段：组件拆分
1. 提取导航组件
2. 提取首页模块
3. 提取因子库模块
4. 提取创建因子模块
5. 提取调试与分析模块

### 第三阶段：数据迁移
1. 将 QML 数据迁移到 C++ 模型
2. 更新组件绑定
3. 优化性能
4. 测试验证

## 重构后的主文件结构

```qml
// FactorWorkbench.qml (重构后)
import QtQuick 2.15
import "../components/FactorWorkbench/Home" as Home
import "../components/FactorWorkbench/Library" as Library
import "../components/FactorWorkbench/Creation" as Creation
import "../components/FactorWorkbench/Debug" as Debug
import "../components/FactorWorkbench/Analysis" as Analysis
import "../components/FactorWorkbench/Backtest" as Backtest
import "../components/FactorWorkbench/Navigation" as Navigation

Item {
    id: root
    
    // C++ 数据绑定
    GlobalDataService {
        id: globalDataService
    }
    
    FactorDataModel {
        id: factorDataModel
    }
    
    FactorParamController {
        id: factorParamController
    }
    
    // 主布局
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // 导航栏
        Navigation.ModeTitleBar {
            currentMode: currentMode
            onModeSelected: switchMode(mode)
        }
        
        // 主内容区
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            
            Loader {
                id: contentLoader
                anchors.fill: parent
                sourceComponent: getComponentForMode(currentMode)
            }
        }
        
        // 底部通知栏
        Navigation.BottomNotificationBar {
            notifications: globalDataService.notifications
        }
    }
    
    // 模式切换逻辑
    function switchMode(mode) {
        currentMode = mode
        contentLoader.sourceComponent = getComponentForMode(mode)
    }
    
    function getComponentForMode(mode) {
        switch(mode) {
            case "home": return homeComponent
            case "library": return libraryComponent
            case "create": return creationComponent
            case "debug": return debugComponent
            case "analyze": return analysisComponent
            case "backtest": return backtestComponent
            default: return homeComponent
        }
    }
    
    // 组件定义
    Component {
        id: homeComponent
        Home.HomePage {
            onStartCreation: switchMode("create")
            onOpenLibrary: switchMode("library")
            onOpenAnalysis: switchMode("analyze")
        }
    }
    
    Component {
        id: libraryComponent
        Library.FactorLibraryPage {
            factorModel: factorDataModel
            onFactorSelected: handleFactorSelected(factorId)
        }
    }
    
    Component {
        id: creationComponent
        Creation.FactorCreationWizard {
            paramController: factorParamController
            onFactorCreated: handleFactorCreated(factorData)
        }
    }
    
    // 其他组件...
}
```

## 性能优化建议

### 1. 数据绑定优化
- 使用 `QAbstractListModel` 替代 QML `ListModel`
- 使用 `QQmlPropertyMap` 动态属性绑定
- 实现数据缓存和懒加载

### 2. 渲染性能优化
- 使用 `Loader` 动态加载组件
- 实现虚拟列表（对于大数据集）
- 优化图片和资源加载

### 3. 计算性能优化
- 复杂计算移到 C++ 线程池
- 实现计算结果缓存
- 使用异步操作避免UI阻塞

## 预期效果

### 代码结构改善
- **文件大小**：从6000+行减少到500行以内
- **组件复用**：可复用组件增加到80%以上
- **维护性**：单个文件维护，降低耦合度

### 性能提升
- **内存使用**：减少50%以上QML内存占用
- **启动速度**：提升30%以上
- **渲染性能**：提升20%以上

### 开发效率
- **新功能开发**：减少70%代码量
- **Bug修复**：定位问题时间减少80%
- **测试覆盖率**：提高至90%以上

## 风险评估

### 技术风险
1. **QML/C++交互复杂性**：需要熟悉Qt的MOC机制
2. **数据绑定性能**：大量绑定可能影响性能
3. **线程安全问题**：C++多线程操作需要同步

### 缓解措施
1. **分阶段实施**：先迁移部分数据，验证可行性
2. **性能监控**：实现性能监控和测试
3. **代码审查**：严格审查C++/QML交互代码

## 时间预估

| 阶段 | 任务 | 时间估算 |
|------|------|---------|
| 第一阶段 | 基础架构搭建 | 2天 |
| 第二阶段 | 组件提取 | 3天 |
| 第三阶段 | 数据迁移 | 2天 |
| 第四阶段 | 性能优化 | 1天 |
| 第五阶段 | 测试验证 | 1天 |
| **总计** | | **9天** |

## 下一步行动

1. **立即开始**：创建组件目录结构和基础架构
2. **优先级**：先迁移系统状态、通知等简单数据
3. **验证**：构建测试验证C++绑定正常工作
4. **迭代**：逐步替换现有功能，保持系统可用性