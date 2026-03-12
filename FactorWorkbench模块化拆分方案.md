# FactorWorkbench 模块化拆分方案

## 1. 现有架构分析

当前FactorWorkbench.qml是一个大型文件（约350行），包含以下主要部分：
- 主页面布局（ColumnLayout + Loader）
- 六种模式（home, library, create, debug, analyze, backtest）
- C++数据绑定（GlobalDataService, FactorDataModel, FactorParamController）
- 内联组件（BottomNotificationBar）
- 大量业务逻辑函数

## 2. C++绑定初始化建议

### 2.1 已实现的C++绑定
1. **GlobalDataService** - 全局数据服务（单例）
   - 用途：提供全局配置、通知、状态管理
   - 建议：已完成，功能完善

2. **FactorDataModel** - 因子数据模型
   - 用途：因子列表、CRUD操作、过滤排序
   - 建议：已完成，需要测试数据库连接

3. **FactorParamController** - 因子参数控制器
   - 用途：因子参数加载、验证、因子创建
   - 建议：已完成，需要与domain_model集成

### 2.2 建议新增的C++绑定
1. **FactorAnalysisService** - 因子分析服务
   - 功能：IC计算、分组收益、绩效指标计算
   - 数据：需要访问历史价格数据、因子值
   - 绑定方式：QObject派生类，暴露Q_INVOKABLE方法

2. **FactorBacktestService** - 因子回测服务
   - 功能：回测引擎、组合构建、绩效评估
   - 数据：需要完整的回测框架
   - 绑定方式：QObject派生类，支持异步操作

3. **DataProviderService** - 数据提供服务
   - 功能：股票池、价格数据、基本面数据
   - 数据：连接数据库和外部API
   - 绑定方式：单例模式，提供数据获取接口

## 3. 组件拆分方案

### 3.1 导航组件（已拆分）
- **ModeTitleBar.qml** - 顶部导航栏 ✓
  - 功能：模式切换、标题显示、返回按钮
  - 状态：已完成

### 3.2 页面组件（部分拆分）
- **HomePage.qml** - 首页 ✓
  - 功能：欢迎界面、功能导航
  - 状态：已完成

- **FactorLibraryPage.qml** - 因子库页面
  - 需要拆分：因子列表、筛选器、详情视图
  - 建议组件：
    - FactorListView.qml - 因子列表视图
    - FactorFilterPanel.qml - 筛选面板
    - FactorDetailCard.qml - 详情卡片

- **FactorCreatePage.qml** - 因子创建页面
  - 需要创建：参数表单、模板选择、预览
  - 建议组件：
    - FactorTypeSelector.qml - 因子类型选择器
    - ParameterForm.qml - 参数表单
    - FactorPreview.qml - 实时预览

- **FactorDebugPage.qml** - 因子调试页面
  - 需要创建：参数调整、实时计算、可视化
  - 建议组件：
    - ParameterSlider.qml - 参数滑块组件
    - CalculationDebugger.qml - 计算调试器
    - VisualizationPanel.qml - 可视化面板

- **FactorAnalysisPage.qml** - 因子分析页面
  - 需要创建：绩效图表、统计表格、IC分析
  - 建议组件：
    - PerformanceChart.qml - 绩效图表
    - StatisticsTable.qml - 统计表格
    - ICAnalysisPanel.qml - IC分析面板

- **FactorBacktestPage.qml** - 因子回测页面
  - 需要创建：回测配置、结果展示、对比分析
  - 建议组件：
    - BacktestConfigForm.qml - 回测配置表单
    - ResultDashboard.qml - 结果仪表盘
    - ComparisonPanel.qml - 对比分析面板

### 3.3 通用组件（需要创建）
1. **ToastNotification.qml** - 提示通知组件
   - 功能：显示临时消息、成功/错误提示
   - 位置：右上角浮层

2. **LoadingSpinner.qml** - 加载指示器
   - 功能：异步操作时的加载状态
   - 位置：居中或内联

3. **DataTable.qml** - 数据表格组件
   - 功能：可排序、可筛选的数据表格
   - 特性：分页、导出、列配置

4. **ChartComponent.qml** - 图表组件
   - 功能：折线图、柱状图、散点图
   - 数据绑定：支持动态数据更新

5. **ParameterEditor.qml** - 参数编辑器
   - 功能：通用参数编辑组件
   - 支持：整数、浮点数、枚举、布尔值

## 4. 数据流设计

### 4.1 数据流向
```
C++数据层 → QML绑定 → 组件属性 → UI显示
```

### 4.2 状态管理
1. **全局状态** - GlobalDataService
   - 应用配置、用户偏好、通知队列

2. **因子状态** - FactorDataModel
   - 因子列表、选中状态、过滤条件

3. **分析状态** - FactorAnalysisService
   - 分析参数、计算结果、图表数据

### 4.3 通信机制
1. **信号槽** - QObject的信号槽机制
   - 用于C++ ↔ QML通信
   - 异步操作的回调

2. **属性绑定** - QML属性绑定
   - 实时数据更新
   - 自动UI刷新

3. **函数调用** - Q_INVOKABLE方法
   - 同步操作
   - 复杂计算

## 5. 实施计划

### 阶段1：基础架构完善（1-2天）
1. 修复编译问题（Factor.h包含路径）
2. 测试现有C++绑定功能
3. 验证GlobalDataService通信

### 阶段2：组件库建设（3-4天）
1. 创建通用组件（Toast, Loading, Table）
2. 完善导航组件
3. 构建数据表格和图表组件

### 阶段3：页面开发（5-7天）
1. 完成FactorLibraryPage
2. 开发FactorCreatePage
3. 构建FactorAnalysisPage

### 阶段4：高级功能（7-10天）
1. 实现FactorDebugPage
2. 完成FactorBacktestPage
3. 优化性能和数据缓存

## 6. 技术要点

### 6.1 C++绑定最佳实践
1. **使用智能指针** - shared_ptr管理资源
2. **线程安全** - 数据访问的线程保护
3. **错误处理** - 统一的错误返回机制
4. **性能优化** - 避免频繁的C++ ↔ QML通信

### 6.2 QML组件设计原则
1. **单一职责** - 每个组件只做一件事
2. **松耦合** - 通过属性传递数据
3. **可重用** - 通用组件设计
4. **可测试** - 独立的组件测试

### 6.3 性能考虑
1. **懒加载** - 使用Loader延迟加载
2. **虚拟化** - 大型列表使用虚拟化
3. **缓存** - 数据计算结果缓存
4. **批处理** - 减少UI更新频率

## 7. 风险评估

### 高风险
1. C++绑定稳定性
2. 数据库连接性能
3. 大型数据集处理

### 中风险
1. 组件间通信复杂度
2. 异步操作状态管理
3. 内存泄漏

### 低风险
1. UI样式一致性
2. 用户交互体验
3. 文档完整性

## 8. 成功标准

1. **功能完整性** - 所有六种模式正常运行
2. **性能指标** - 页面加载<1s，操作响应<200ms
3. **代码质量** - 组件化程度>80%，C++绑定覆盖率>70%
4. **用户体验** - 操作流畅，数据实时更新
5. **维护性** - 新功能开发效率提升50%

## 9. 下一步行动

1. **立即执行**：修复编译错误，测试现有绑定
2. **短期计划**：开始通用组件开发
3. **中期目标**：完成核心页面实现
4. **长期愿景**：建立完整的量化因子工作台生态系统