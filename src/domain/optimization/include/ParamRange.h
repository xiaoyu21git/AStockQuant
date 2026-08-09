// ParamRange.h — 单个参数维度的搜索范围定义
// 支持 int / double / bool / enum 四种类型
#pragma once

#include "ParamDef.h"

#include <string>
#include <vector>

namespace domain::optimization {

/// 单个参数维度的搜索范围
class ParamRange final {
public:
    // ── 工厂方法 ──

    static ParamRange intRange(std::string name, int min, int max, int step);
    static ParamRange doubleRange(std::string name, double min, double max, double step);
    static ParamRange boolRange(std::string name);
    static ParamRange enumRange(std::string name, std::vector<EnumOption> options);

    // ── 只读访问 ──

    [[nodiscard]] const std::string& name() const noexcept { return m_name; }
    [[nodiscard]] ParamType type() const noexcept { return m_type; }

    [[nodiscard]] int intMin() const noexcept { return m_intMin; }
    [[nodiscard]] int intMax() const noexcept { return m_intMax; }
    [[nodiscard]] int intStep() const noexcept { return m_intStep; }

    [[nodiscard]] double doubleMin() const noexcept { return m_doubleMin; }
    [[nodiscard]] double doubleMax() const noexcept { return m_doubleMax; }
    [[nodiscard]] double doubleStep() const noexcept { return m_doubleStep; }

    [[nodiscard]] const std::vector<EnumOption>& enumOptions() const noexcept { return m_enumOptions; }

    /// 默认值 (范围的中间值或第一个枚举项)
    [[nodiscard]] double defaultValue() const;

    /// 生成所有候选值 (统一为 double)
    /// int/double: min 到 max 按 step 步进
    /// bool: {0.0, 1.0}
    /// enum: 每个 option.value 转为 double
    [[nodiscard]] std::vector<double> candidateValues() const;

    /// 候选值数量
    [[nodiscard]] int candidateCount() const;

    /// 检查值是否在合法范围内
    [[nodiscard]] bool contains(double value) const;

private:
    ParamRange() = default;

    std::string m_name;
    ParamType m_type{ParamType::Int};

    // Numeric ranges
    int m_intMin{0};
    int m_intMax{0};
    int m_intStep{1};
    double m_doubleMin{0.0};
    double m_doubleMax{0.0};
    double m_doubleStep{1.0};

    // Enum options
    std::vector<EnumOption> m_enumOptions;
};

} // namespace domain::optimization
