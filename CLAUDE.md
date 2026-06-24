# 项目通用开发规则

## ⚠️ 改代码前必须做的事

1. **定位根因**：理解问题涉及的架构层级和调用链，不是看见症状就动手
2. **回溯变更**：如果功能之前正常现在异常，查 git log 找到变更点
3. **评估影响**：改一处之前，想清楚下游哪些路径会受影响，别修一个坏三个
4. **不确定就确认**：拿不准时间点、架构设计、边界条件，先问
5. **禁止擅自提交**：`git commit` 必须在用户明确要求后才能执行
6. **禁止擅自改代码**：分析出根因后，先向用户确认方案，同意后再动手。不要发现一个问题就改一个

---

## 1. 架构分层与职责边界

### 1.1 底层模块（Core Layer）核心规则

#### 1.1.1 完全面向对象，禁止 C 风格函数堆砌

**规则**：底层代码必须使用类和对象组织功能，不允许在全局作用域或命名空间中定义裸露的函数（`main` 与 C 接口除外），禁止函数堆砌，确保高内聚。
**错误示例**（C 风格全局函数）：

```cpp
// utils.cpp
int calculate(int a, int b) {
    return a + b;
}
void log(const char* msg) {
    // ...
}
```

**正确示例**（面向对象封装）：

```cpp
// Calculator.h
class Calculator {
public:
    int add(int a, int b) const;
};

// Logger.h
class Logger {
public:
    void log(const std::string& msg) const;
};
```

#### 1.1.2 零 Qt 依赖

**规则**：底层模块不得包含任何 Qt 头文件或使用 Qt 类型（如 `QString`、`QObject`），只能使用标准 C++ 或经评审的第三方库，保证跨平台能力和可测试性。
**错误示例**：

```cpp
#include <QString>
class Data {
    QString name;  // 底层耦合 Qt
};
```

**正确示例**：

```cpp
#include <string>
class Data {
    std::string name;
};
```

#### 1.1.3 数据封装，禁止暴露裸成员变量

**规则**：所有数据成员必须为 `private`，只能通过 `const` 成员函数或明确命名的 getter/setter 接口访问，绝不允许出现 `public` 成员变量。
**错误示例**：

```cpp
struct Point {
    double x, y;   // 裸变量，完全开放修改
};
```

**正确示例**：

```cpp
class Point {
public:
    double x() const { return x_; }
    double y() const { return y_; }
    void setX(double x) { x_ = x; }
    void setY(double y) { y_ = y; }
private:
    double x_ = 0.0;
    double y_ = 0.0;
};
```

#### 1.1.4 禁止基于字符串的类型判断或路由

**规则**：禁止使用字符串比较（如 `if (type == "image")`）进行类型判断或功能分支，必须使用枚举、多态或 `std::variant` 等类型安全机制。
**错误示例**：

```cpp
void process(const std::string& type, const Data& data) {
    if (type == "image") { /* ... */ }
    else if (type == "video") { /* ... */ }  // 歧义、低效
}
```

**正确示例**：

```cpp
enum class MediaType { Image, Video };
void process(MediaType type, const Data& data) {
    switch (type) {
        case MediaType::Image: /* ... */ break;
        case MediaType::Video: /* ... */ break;
    }
}
```

#### 1.1.5 消除歧义与兼容回退代码

**规则**：代码接口设计必须清晰唯一，不提供功能重叠的重载或默认参数导致行为二义，废弃接口直接删除而不要保留“兼容回退”。
**错误示例**：

```cpp
class Renderer {
public:
    void draw(int x, int y, bool antiAlias = false);  // 旧版本保留
    void draw(int x, int y, int quality);              // 新版本，调用者易混淆
};
```

**正确示例**：

```cpp
class Renderer {
public:
    void draw(int x, int y, DrawParams params); // 唯一接口，参数结构清晰
};
```

#### 1.1.6 优先考虑效率与资源消耗

**规则**：重要模块设计时必须评估内存、CPU 使用效率，合理使用移动语义、避免不必要拷贝，谨慎选择容器与算法，必要时引入经过验证的开源库（如 fmt, abseil 等）。
**错误示例**（无意义拷贝）：

```cpp
std::vector<BigObject> createObjects() {
    std::vector<BigObject> temp;
    // ...填充
    return temp;   // 可能触发拷贝（取决于编译器优化，但意图不明）
}
```

**正确示例**（明确使用移动语义）：

```cpp
std::vector<BigObject> createObjects() {
    std::vector<BigObject> temp;
    // ...填充
    return std::move(temp); // 明确转移所有权，避免拷贝
}
// 或在 C++17 后依赖 NRVO，但确保内部不产生额外拷贝
```

---

### 1.2 桥接层（Bridge Layer）规则

#### 1.2.1 桥接层只负责转发与调度，禁止业务逻辑

**规则**：桥接层（如 ViewModel 或适配器）的任务仅限于：接收 UI 输入 → 校验参数 → 调用底层接口 → 返回结果/状态给 UI。绝对不可内含算法、决策或数据处理逻辑。
**错误示例**：

```cpp
// Bridge.cpp (包含业务判断)
void Bridge::onUserPressed() {
    if (currentUser.age > 18 && currentUser.country == "US") {
        // 复杂业务规则写在了桥接层
        double tax = calculateTax(income);
        ui_->showTax(tax);
    }
}
```

**正确示例**：

```cpp
void Bridge::onUserPressed() {
    // 仅传递参数并调度底层
    auto result = coreService_->getTaxInfo(currentUser.id);
    if (result.valid) {
        ui_->showTax(result.taxValue);
    } else {
        ui_->showError(result.errorMessage);
    }
}
```

#### 1.2.2 数据转换集中处理，避免散落

**规则**：底层与 UI 数据类型不一致时，转换逻辑应集中在专门辅助类或转换函数中，桥接层仅调用转换器，不能到处内嵌转换代码。
**错误示例**：

```cpp
void Bridge::showItems() {
    auto coreItems = core_->fetchItems();
    for (auto& item : coreItems) {
        QString name = QString::fromStdString(item.name); // 散落的转换
        ui_->addItem(name, item.id);
    }
}
```

**正确示例**：

```cpp
// Converter.h
UIModel convert(const CoreItem& item) {
    return { QString::fromStdString(item.name), item.id };
}

void Bridge::showItems() {
    auto coreItems = core_->fetchItems();
    for (auto& c : coreItems) {
        ui_->addItem(convert(c));
    }
}
```

---

### 1.3 UI 层（QML）规则

#### 1.3.1 函数行数限制与组件拆分

**规则**：每个 QML 函数/代码块（含 `function`、`onXxx` 中的逻辑）不得超过 30 行（不含纯声明）。超出必须拆分为更小的函数或提取自定义组件。
**错误示例**（超长 onClicked 处理）：

```qml
Button {
    onClicked: {
        // 30+ 行数据解析、布局计算...
    }
}
```

**正确示例**：

```qml
Button {
    onClicked: handleClick(data)
}
function handleClick(d) {
    if (!validate(d)) return;
    process(d);
}
function validate(d) { /* < 30 行 */ }
function process(d) { /* < 30 行 */ }
```

#### 1.3.2 通用 UI 部分必须封装为可复用组件

**规则**：任何在两处或以上出现的 UI 模式（按钮样式、卡片、对话框等），必须提取为独立 `.qml` 组件并暴露属性接口，禁止复制粘贴代码。
**错误示例**（多处重复卡片）：

```qml
// PageA.qml
Rectangle { ... Text { text: title } ... MouseArea { ... } }

// PageB.qml
Rectangle { ... Text { text: title } ... MouseArea { ... } } // 完全重复
```

**正确示例**：

```qml
// Card.qml
Rectangle {
    property string title
    signal clicked
    Text { text: title }
    MouseArea { onClicked: parent.clicked() }
}
// 使用
Card { title: "Home"; onClicked: { ... } }
```

#### 1.3.3 编写前必须确认方案，避免重复造轮子

**规则**：在实现 UI 界面或功能前，必须检查现有公共组件库和已实现页面，确认没有可直接使用或稍作扩展即可满足需求的组件，否则先沟通重构。

---

## 2. 命名与代码风格

### 2.1 统一命名规范

**规则**：项目必须遵守如下命名约定（示例），无例外：

- 类/结构体：`PascalCase`（如 `VideoDecoder`）
- 函数/方法：`camelCase`（如 `decodeFrame`）
- 成员变量：`m_` 前缀 + `camelCase`（如 `m_frameCount`）
- 常量/枚举值：`kPascalCase` 或 `ALL_CAPS`（如 `kMaxRetries` 或 `MAX_RETRIES`）
- QML 对象 id：`camelCase`，组件文件：`PascalCase`
  **错误示例**：

```cpp
class video_decoder {
    int frameCnt;
    void DecodeFrame();
};
```

**正确示例**：

```cpp
class VideoDecoder {
    int m_frameCount;
    void decodeFrame();
};
```

### 2.2 禁止 C 风格类型别名

**规则**：使用 `using` 代替 `typedef`，保持模板友好和可读性。
**错误示例**：

```cpp
typedef std::map<std::string, int> NameMap;
```

**正确示例**：

```cpp
using NameMap = std::map<std::string, int>;
```

---

## 3. 内存与性能安全

### 3.1 智能指针管理所有权，禁止裸指针传递所有权

**规则**：任何需要拥有对象生命周期的指针，一律使用 `std::unique_ptr` 或 `std::shared_ptr`，函数参数和返回中若涉及所有权转移，必须使用智能指针；观察者用裸指针或引用。
**错误示例**：

```cpp
Shape* createShape() {
    return new Circle();  // 调用方不知道需不需要 delete
}
```

**正确示例**：

```cpp
std::unique_ptr<Shape> createShape() {
    return std::make_unique<Circle>();
}
```

### 3.2 传参和返回应避免不必要拷贝

**规则**：大对象（`std::string`, 容器等）作为参数时，优先使用 `const &`；输出参数通过指针或引用；返回局部对象时依赖移动语义或 NRVO。
**错误示例**：

```cpp
std::vector<int> getData(std::string key) { // 拷贝 key
    std::vector<int> v;
    return v; // 可能拷贝
}
```

**正确示例**：

```cpp
std::vector<int> getData(const std::string& key) {
    std::vector<int> v;
    return v; // C++11 起自动移动（或 NRVO）
}
```

### 3.3 禁止使用魔法数字，必须定义常量

**错误示例**：

```cpp
if (errorCode == 108) { /* ... */ }
```

**正确示例**：

```cpp
constexpr int kTimeoutError = 108;
if (errorCode == kTimeoutError) { /* ... */ }
```

---

## 4. 代码复用与开源库

### 4.1 坚决避免重复造轮子

**规则**：编写新功能前必须检索代码库中是否已有相似公共功能类；若需实现复杂基础功能（JSON解析、加密、日志等），优先评估引入成熟开源库，评审后集成。
**示例体现**：发现两个模块分别实现 `LogManager` → 重构成单一 `LogManager` 并移除重复。

### 4.2 复用检测

若发现存在重复代码，必须立即重构。
**错误示例**（重复实现）：

```cpp
// file_a.cpp
class TempLogger {
    void log(const std::string& msg) { /* 写入文件 */ }
};
// file_b.cpp
class MyLogger {
    void log(const std::string& msg) { /* 写入文件，逻辑几乎相同 */ }
};
```

**正确示例**：

```cpp
// log_manager.h
class LogManager {
public:
    static void log(const std::string& msg);
};
// file_a.cpp & file_b.cpp 均调用 LogManager::log()
```

---

以上规则从分层职责、封装、性能、复用与风格等维度全面约束项目开发，所有成员必须遵守。代码评审时若发现违规，应立即修正。
