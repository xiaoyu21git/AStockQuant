// ParamRange.cpp — 参数范围实现
#include "../include/ParamRange.h"

#include <cmath>
#include <stdexcept>

namespace domain::optimization {

// ── 工厂方法 ──

ParamRange ParamRange::intRange(std::string name, int min, int max, int step) {
    if (min > max) {
        throw std::invalid_argument("ParamRange::intRange: min > max for '" + name + "'");
    }
    if (step <= 0) {
        throw std::invalid_argument("ParamRange::intRange: step <= 0 for '" + name + "'");
    }
    ParamRange r;
    r.m_name = std::move(name);
    r.m_type = ParamType::Int;
    r.m_intMin = min;
    r.m_intMax = max;
    r.m_intStep = step;
    return r;
}

ParamRange ParamRange::doubleRange(std::string name, double min, double max, double step) {
    if (min > max) {
        throw std::invalid_argument("ParamRange::doubleRange: min > max for '" + name + "'");
    }
    if (step <= 0.0) {
        throw std::invalid_argument("ParamRange::doubleRange: step <= 0 for '" + name + "'");
    }
    ParamRange r;
    r.m_name = std::move(name);
    r.m_type = ParamType::Double;
    r.m_doubleMin = min;
    r.m_doubleMax = max;
    r.m_doubleStep = step;
    return r;
}

ParamRange ParamRange::boolRange(std::string name) {
    ParamRange r;
    r.m_name = std::move(name);
    r.m_type = ParamType::Bool;
    return r;
}

ParamRange ParamRange::enumRange(std::string name, std::vector<EnumOption> options) {
    if (options.empty()) {
        throw std::invalid_argument("ParamRange::enumRange: empty options for '" + name + "'");
    }
    ParamRange r;
    r.m_name = std::move(name);
    r.m_type = ParamType::Enum;
    r.m_enumOptions = std::move(options);
    return r;
}

// ── 候选值 ──

double ParamRange::defaultValue() const {
    switch (m_type) {
    case ParamType::Int:
        return static_cast<double>(m_intMin + (m_intMax - m_intMin) / 2);
    case ParamType::Double:
        return (m_doubleMin + m_doubleMax) / 2.0;
    case ParamType::Bool:
        return 0.0;  // 默认 false
    case ParamType::Enum:
        if (!m_enumOptions.empty()) {
            return static_cast<double>(m_enumOptions.front().value);
        }
        return 0.0;
    }
    return 0.0;
}

std::vector<double> ParamRange::candidateValues() const {
    std::vector<double> result;
    switch (m_type) {
    case ParamType::Int: {
        int count = (m_intMax - m_intMin) / m_intStep + 1;
        result.reserve(static_cast<std::size_t>(count));
        for (int v = m_intMin; v <= m_intMax; v += m_intStep) {
            result.push_back(static_cast<double>(v));
        }
        break;
    }
    case ParamType::Double: {
        // 逐步累加避免浮点误差
        int steps = static_cast<int>(std::round((m_doubleMax - m_doubleMin) / m_doubleStep));
        result.reserve(static_cast<std::size_t>(steps + 1));
        for (int i = 0; i <= steps; ++i) {
            double v = m_doubleMin + static_cast<double>(i) * m_doubleStep;
            if (v <= m_doubleMax + 1e-9) {
                result.push_back(v);
            }
        }
        break;
    }
    case ParamType::Bool:
        result = {0.0, 1.0};
        break;
    case ParamType::Enum:
        result.reserve(m_enumOptions.size());
        for (const auto& opt : m_enumOptions) {
            result.push_back(static_cast<double>(opt.value));
        }
        break;
    }
    return result;
}

int ParamRange::candidateCount() const {
    switch (m_type) {
    case ParamType::Int:
        return (m_intMax - m_intMin) / m_intStep + 1;
    case ParamType::Double:
        return static_cast<int>(std::round((m_doubleMax - m_doubleMin) / m_doubleStep)) + 1;
    case ParamType::Bool:
        return 2;
    case ParamType::Enum:
        return static_cast<int>(m_enumOptions.size());
    }
    return 0;
}

bool ParamRange::contains(double value) const {
    switch (m_type) {
    case ParamType::Int: {
        int iv = static_cast<int>(std::round(value));
        return iv >= m_intMin && iv <= m_intMax;
    }
    case ParamType::Double:
        return value >= m_doubleMin - 1e-9 && value <= m_doubleMax + 1e-9;
    case ParamType::Bool:
        return value == 0.0 || value == 1.0;
    case ParamType::Enum:
        for (const auto& opt : m_enumOptions) {
            if (static_cast<double>(opt.value) == value) return true;
        }
        return false;
    }
    return false;
}

} // namespace domain::optimization
