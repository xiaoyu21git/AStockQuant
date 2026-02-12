# API接口冻结声明

## 声明日期
2026年2月12日

## 冻结范围

### 1. 公共API接口
以下公共API接口已冻结，保证向后兼容性：

#### C++接口
- `thirdparty::ThirdPartyApi` - 第三方API统一接口
- `thirdparty::JujinApi` - 掘金量化平台接口
- `database::QtMySQLDatabase` - MySQL数据库接口
- `database::ConnectionPool` - 数据库连接池接口
- `database::QueryBuilder` - SQL查询构建器接口
- `DataService` - 数据服务接口
- `DataFetchController` - 数据获取控制器接口
- `DataCleaningEngine` - 数据清洗引擎接口
- `DataManager` - 数据管理接口
- `CleaningResultModel` - 清洗结果模型接口

#### Python接口
- `astock_engine.core.data_fetch_handler` - 数据获取处理器
- `astock_engine.core.eventbus_simple` - 简单事件总线
- `astock_engine.data.juejin_data_source` - 掘金数据源
- `astock_engine.data.database_wrapper` - 数据库包装器

#### QML组件接口
- `DataAnalysis/Datamain.qml` - 数据分析主页面
- `DataAnalysis/DataSourceModal.qml` - 数据源选择模态框
- `DataAnalysis/DataPreviewModal.qml` - 数据预览模态框
- `DataAnalysis/DataCleaningModal.qml` - 数据清洗模态框
- `DataAnalysis/RulesConfigModal.qml` - 规则配置模态框
- `strategies/StrategyLibraryPage.qml` - 策略库页面

### 2. 数据格式
- 市场数据格式 (`MarketData` 结构体)
- 清洗规则配置格式
- 数据库表结构
- 配置文件格式

### 3. 构建系统
- CMake配置接口
- 编译脚本参数
- 依赖库版本

## 冻结规则

### 1. 向后兼容性保证
- 现有API接口签名保持不变
- 现有功能行为保持不变
- 现有数据格式兼容性保证

### 2. 允许的修改
- 内部实现优化（不改变外部行为）
- bug修复（保持接口不变）
- 性能优化（保持功能不变）
- 文档完善和错误信息改进

### 3. 禁止的修改
- 公共API接口签名变更
- 数据格式的重大变更
- 构建系统的破坏性变更
- 功能行为的重大变更

## 例外情况

### 1. 安全修复
涉及安全漏洞的修复可以突破冻结限制，但需要：
1. 记录变更原因
2. 提供迁移指南
3. 通知所有使用者

### 2. 重大bug修复
影响核心功能的重大bug可以修复，但需要：
1. 保持接口兼容性
2. 提供回滚方案
3. 更新相关文档

## 解冻流程

### 1. 解冻条件
满足以下条件之一可以申请解冻：
1. 新版本规划开始
2. 架构重大升级
3. 技术栈迁移

### 2. 解冻流程
1. 提交解冻申请
2. 团队评审通过
3. 制定迁移计划
4. 通知所有使用者
5. 执行解冻操作

## 版本管理

### 1. 当前版本
- 版本号: v1.0.0
- 状态: 接口冻结版
- 发布日期: 2026年2月12日

### 2. 版本策略
- 主版本号: 接口不兼容的重大变更
- 次版本号: 向下兼容的功能性新增
- 修订号: 向下兼容的问题修正

### 3. 发布周期
- 主版本: 6-12个月
- 次版本: 1-3个月
- 修订版: 按需发布

## 维护承诺

### 1. 维护期限
- 主动维护: 12个月
- 安全维护: 24个月
- 社区支持: 长期

### 2. 支持级别
- **一级支持**: 安全漏洞和严重bug
- **二级支持**: 功能bug和性能问题
- **三级支持**: 使用咨询和文档问题

### 3. 响应时间
- 一级问题: 24小时内响应
- 二级问题: 3个工作日内响应
- 三级问题: 7个工作日内响应

## 联系方式

### 1. 问题报告
- GitHub Issues: [项目地址]/issues
- 邮件支持: support@astockquantengine.com
- 文档: [项目地址]/docs

### 2. 社区支持
- 论坛: [社区论坛地址]
- Discord: [Discord频道]
- 微信群: [微信群二维码]

### 3. 商业支持
- 企业版: enterprise@astockquantengine.com
- 培训服务: training@astockquantengine.com
- 定制开发: custom@astockquantengine.com

## 法律声明

### 1. 版权
© 2025-2026 AStockQuantEngine开发团队
保留所有权利。

### 2. 许可证
本项目采用MIT许可证，详情见LICENSE文件。

### 3. 免责声明
本软件按"原样"提供，不提供任何明示或暗示的担保，包括但不限于对适销性、特定用途适用性和非侵权的担保。

---

**签署人**: AStockQuantEngine开发团队  
**签署日期**: 2026年2月12日  
**生效日期**: 2026年2月12日