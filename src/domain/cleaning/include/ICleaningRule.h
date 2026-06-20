// ICleaningRule.h — 纯 C++ 数据清洗规则接口（零 Qt 依赖）
// 所有规则通过此接口实现，使用 foundation::json::JsonFacade 作为行数据
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace foundation::json { class JsonFacade; }

namespace cleaning {

// ── 清洗动作 ──
enum class CleaningAction : uint8_t {
    Keep,
    Remove,
    TagOnly
};

// ── 规则执行顺序 ──
enum class RuleExecutionOrder : uint8_t {
    FieldStandardization = 0,
    Completeness = 5,
    DuplicateRemoval = 10,
    FinancialDateValidity = 15,
    FinancialMetricSanitize = 20,
    ReportDateAlignment = 25,
    SurvivorBias = 30,
    SuspensionFill = 35,
    MissingValueFill = 40,
    AdjustedPrice = 45,
    NewStockFilter = 50,
    STFilter = 55,
    PriceValidity = 60,
    VolumeFilter = 65,
    LimitMoveTag = 70,
    ValuationSanitize = 75
};

// ── 规则级统计 ──
struct RuleStat {
    std::string ruleName;
    int passed{0};
    int removed{0};
};

// ── 清洗统计 ──
struct CleaningStats {
    int totalRecords{0};
    int keptRecords{0};
    int removedRecords{0};
    int64_t durationMs{0};
    std::vector<RuleStat> ruleStats;  // 每规则的通过/移除计数
};

// ── 纯 C++ 清洗规则基类 ──
class ICleaningRule {
public:
    virtual ~ICleaningRule() = default;

    /// @brief 规则唯一名称（与 CleaningRuleContract.h 保持一致）
    virtual const char* ruleName() const = 0;

    /// @brief 执行顺序，越小越先执行
    virtual uint8_t executionOrder() const = 0;

    /// @brief 预处理：对全部数据执行一次（可用于批量上下文构建）
    virtual void cleanCrossSectional(std::vector<foundation::json::JsonFacade>& rows) {}

    /// @brief 判断规则是否适用于此数据行（默认总是适用，可重写以按行类型跳过）
    /// @return true 适用，false 跳过此规则对此行的检查
    virtual bool appliesTo(const foundation::json::JsonFacade& row) const { return true; }

    /// @brief 逐行过滤：返回 true 保留，false 移除
    virtual bool clean(foundation::json::JsonFacade& row) = 0;

    /// @brief 后处理：对过滤后的数据执行一次
    virtual void postCrossSectional(std::vector<foundation::json::JsonFacade>& rows) {}

    /// @brief 验证规则配置是否有效（返回空字符串表示通过，否则返回错误消息）
    virtual std::string validateConfig() const { return ""; }
};

// ── 引擎配置：从 JSON 字符串解析规则集 ──
struct CleaningRuleConfig {
    std::string ruleId;
    bool enabled{true};
    std::string configJson;  // 规则特定参数的 JSON
};

/// @brief 根据配置创建规则实例（工厂函数，由各规则实现注册）
using RuleFactory = std::unique_ptr<ICleaningRule>(*)(const std::string& configJson);

// ── 规则工厂注册表（便于扩展，替代 if-else 链）──
class RuleFactoryRegistry {
public:
    using FactoryFn = std::function<std::unique_ptr<ICleaningRule>(const std::string& configJson)>;

    static RuleFactoryRegistry& instance() {
        static RuleFactoryRegistry s_registry;
        return s_registry;
    }

    /// @brief 注册规则工厂
    void registerFactory(const std::string& ruleName, FactoryFn factory) {
        m_factories[ruleName] = std::move(factory);
    }

    /// @brief 根据规则名创建实例，未注册返回 nullptr
    std::unique_ptr<ICleaningRule> create(const std::string& ruleName,
                                          const std::string& configJson = {}) const {
        auto it = m_factories.find(ruleName);
        if (it == m_factories.end()) return nullptr;
        return it->second(configJson);
    }

    /// @brief 获取所有已注册的规则名
    std::vector<std::string> registeredRuleNames() const {
        std::vector<std::string> names;
        names.reserve(m_factories.size());
        for (const auto& [name, _] : m_factories) names.push_back(name);
        return names;
    }

private:
    RuleFactoryRegistry() = default;
    std::map<std::string, FactoryFn> m_factories;
};

} // namespace cleaning
