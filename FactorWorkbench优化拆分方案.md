# FactorWorkbench.qml 优化拆分方案

基于反馈要求重新设计，重点解决：
1. C++ model绑定统一管理重复数据
2. 全局系统状态共享
3. 参数配置组件继承结构
4. 合理拆分粒度

## 核心架构设计

### 1. 全局数据服务（C++绑定）

#### GlobalDataService (单例模式)
```cpp
// src/ui/bridge/include/GlobalDataService.h
#pragma once

#include <QObject>
#include <QAbstractListModel>
#include <QVector>

class GlobalDataService : public QObject {
    Q_OBJECT
    
    // 系统状态属性
    Q_PROPERTY(QVariantMap systemStatus READ systemStatus NOTIFY systemStatusChanged)
    Q_PROPERTY(QVariantList notifications READ notifications NOTIFY notificationsChanged)
    
    // 模板数据
    Q_PROPERTY(QVariantList templates READ templates NOTIFY templatesChanged)
    
public:
    static GlobalDataService* instance();
    
    // QML可调用方法
    Q_INVOKABLE void updateSystemStatus(const QString& key, const QVariant& value);
    Q_INVOKABLE void addNotification(const QVariantMap& notification);
    Q_INVOKABLE void clearNotifications();
    Q_INVOKABLE QVariantMap getTemplateById(const QString& templateId);
    
    // 属性访问器
    QVariantMap systemStatus() const;
    QVariantList notifications() const;
    QVariantList templates() const;
    
signals:
    void systemStatusChanged();
    void notificationsChanged();
    void templatesChanged();
    
private:
    GlobalDataService(QObject* parent = nullptr);
    void initializeDefaultData();
    
private:
    QVariantMap m_systemStatus;
    QVariantList m_notifications;
    QVariantList m_templates;
};
```

#### FactorDataModel (C++ QAbstractListModel)
```cpp
// src/ui/bridge/include/FactorDataModel.h
#pragma once

#include <QAbstractListModel>
#include <QVector>

struct FactorData {
    QString factorId;
    QString factorName;
    QString displayName;
    QString majorCategory;
    QString subCategory;
    QString description;
    double icValue;
    double irValue;
    int validityDays;
    double turnoverRate;
    bool isRecommended;
    bool isFavorite;
    QString status;
    QStringList tags;
    QString creator;
    QString createDate;
    QVector<double> groupReturns;
};

class FactorDataModel : public QAbstractListModel {
    Q_OBJECT
    
public:
    enum RoleNames {
        FactorIdRole = Qt::UserRole + 1,
        FactorNameRole,
        DisplayNameRole,
        MajorCategoryRole,
        SubCategoryRole,
        DescriptionRole,
        IcValueRole,
        IrValueRole,
        ValidityDaysRole,
        TurnoverRateRole,
        IsRecommendedRole,
        IsFavoriteRole,
        StatusRole,
        TagsRole,
        CreatorRole,
        CreateDateRole,
        GroupReturnsRole
    };
    
    explicit FactorDataModel(QObject* parent = nullptr);
    
    // QAbstractListModel接口
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    
    // 数据操作方法
    Q_INVOKABLE void loadFactors();
    Q_INVOKABLE void addFactor(const QVariantMap& factorData);
    Q_INVOKABLE void updateFactor(const QString& factorId, const QVariantMap& factorData);
    Q_INVOKABLE void deleteFactor(const QString& factorId);
    Q_INVOKABLE QVariantMap getFactorById(const QString& factorId);
    Q_INVOKABLE QVariantList getFactorsByType(const QString& type);
    Q_INVOKABLE void toggleFavorite(const QString& factorId);
    
private:
    QVector<FactorData> m_factors;
};
```

### 2. 继承式参数配置组件设计

#### 基类：BaseParameterConfig.qml
```qml
// src/app/Qml/components/parameters/BaseParameterConfig.qml
import QtQuick 2.15

Item {
    id: root
    
    // 公共属性
    property string factorType: ""
    property var parameterValues: ({})
    
    // 信号
    signal parameterChanged(string paramName, variant value)
    signal parametersValidated(bool isValid, string message)
    
    // 虚方法 - 子类需要重写
    function validateParameters() {
        return { valid: true, message: "" }
    }
    
    function getParameters() {
        return {}
    }
    
    function resetParameters() {
        // 子类实现
    }
}
```

#### 通用参数配置器：GenericParameterConfig.qml
```qml
// src/app/Qml/components/parameters/GenericParameterConfig.qml
BaseParameterConfig {
    id: root
    
    // 根据factorType显示不同参数
    Loader {
        anchors.fill: parent
        sourceComponent: getParameterComponent(factorType)
        
        function getParameterComponent(type) {
            switch(type) {
                case "momentum": return momentumParams
                case "value": return valueParams
                case "technical": return technicalParams
                case "quality": return qualityParams
                case "sentiment": return sentimentParams
                case "macro": return macroParams
                case "risk": return riskParams
                case "custom": return customParams
                default: return genericParams
            }
        }
    }
    
    // 参数组件定义
    Component {
        id: momentumParams
        MomentumParameters {}
    }
    
    Component {
        id: valueParams
        ValueParameters {}
    }
    
    // ... 其他参数组件
}
```

### 3. 组件拆分规划（优化版）

#### 必须拆分的大组件
1. **LibraryMode.qml** (200+行) - 因子库浏览模式
2. **CreateMode.qml** (300+行) - 因子创建模式
3. **ParameterConfigurator.qml** (150+行) - 参数配置器（使用继承结构）

#### 可以合并的小组件
1. 首页的5个功能卡片（每个约50行） - 合并到HomeMode.qml中
2. 通知栏组件（约80行） - 使用全局服务，不单独拆分
3. 分析卡片组件（约40行） - 合并到AnalyzeMode.qml中

#### 保留原文件但简化
1. **FactorWorkbench.qml** - 主文件，精简为布局和模式切换逻辑
2. **HomeMode.qml** - 包含首页所有卡片

### 4. 实施步骤

#### 第一阶段：C++数据服务实现
1. 创建GlobalDataService单例类
2. 创建FactorDataModel列表模型
3. 注册QML类型
4. 测试C++到QML的数据绑定

#### 第二阶段：参数组件重构
1. 创建BaseParameterConfig基类
2. 创建GenericParameterConfig通用配置器
3. 创建各类型参数子组件
4. 替换现有的参数配置代码

#### 第三阶段：组件拆分与集成
1. 拆分LibraryMode.qml
2. 拆分CreateMode.qml  
3. 精简FactorWorkbench.qml主文件
4. 集成所有组件并测试

### 5. 文件结构

```
src/app/Qml/page/FactorWorkbench/
├── FactorWorkbench.qml                    # 主文件（精简版，50行）
├── modes/
│   ├── HomeMode.qml                      # 首页模式（包含5个卡片，150行）
│   ├── LibraryMode.qml                   # 因子库模式（200行）
│   ├── CreateMode.qml                    # 创建模式（250行）
│   ├── DebugMode.qml                     # 调试模式（80行）
│   ├── AnalyzeMode.qml                   # 分析模式（100行）
│   └── BacktestMode.qml                  # 回测模式（80行）
└── components/
    └── parameters/
        ├── BaseParameterConfig.qml       # 参数基类（30行）
        ├── GenericParameterConfig.qml    # 通用配置器（50行）
        ├── MomentumParameters.qml        # 动量参数（40行）
        ├── ValueParameters.qml           # 价值参数（40行）
        └── ...                           # 其他参数类型
```

### 6. 收益分析

#### 代码量减少
- 主文件从1000+行减少到50行
- 避免数据重复定义（系统状态、通知等）
- 参数配置代码复用率提高

#### 性能提升
- C++数据模型比QML硬编码更高效
- 全局数据共享减少内存占用
- 数据更新统一触发UI刷新

#### 维护性提升
- 组件职责单一，易于理解和修改
- 参数配置统一架构，新增类型容易
- 数据管理集中，避免不一致

### 7. 预期拆分效果

| 组件 | 原行数 | 新行数 | 说明 |
|------|--------|--------|------|
| FactorWorkbench.qml | 1000+ | ~50 | 仅保留布局和模式切换 |
| HomeMode.qml | 200 | ~150 | 合并5个功能卡片 |
| LibraryMode.qml | 300 | ~200 | 使用C++ FactorDataModel |
| CreateMode.qml | 400 | ~250 | 使用GenericParameterConfig |
| 参数配置代码 | 500+ | ~300 | 继承式设计，减少重复 |
| **总计** | **2400+** | **~950** | **减少约60%代码量** |

### 8. 注意事项

1. **向后兼容**：保持现有API接口不变
2. **逐步迁移**：先实现C++服务，再拆分组件
3. **测试覆盖**：每个阶段都要充分测试
4. **性能监控**：监控C++到QML的数据传输性能

此方案在保持功能完整性的同时，最大程度减少了代码量，提高了数据管理和组件复用的效率。