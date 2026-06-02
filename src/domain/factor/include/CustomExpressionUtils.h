#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <regex>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace factor::custom_expression {

struct VariableBinding {
    std::string name;
    std::string field;
    bool hasDefaultValue{false};
    double defaultValue{0.0};
};

struct FieldRequirements {
    std::vector<std::string> requiredFields;
    std::vector<std::string> optionalFields;
};

inline std::string trimAsciiWhitespace(const std::string& text)
{
    const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    const auto begin = std::find_if_not(text.begin(), text.end(), isSpace);
    const auto end = std::find_if_not(text.rbegin(), text.rend(), isSpace).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

inline std::string toLowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

inline std::string normalizeBindingName(const std::string& text)
{
    return toLowerAscii(trimAsciiWhitespace(text));
}

inline const VariableBinding* findBinding(const std::vector<VariableBinding>& bindings, const std::string& variable)
{
    const std::string normalizedVariable = normalizeBindingName(variable);
    for (const auto& binding : bindings) {
        if (normalizeBindingName(binding.name) == normalizedVariable) {
            return &binding;
        }
    }
    return nullptr;
}

inline std::string resolveBoundField(const VariableBinding& binding)
{
    const std::string explicitField = normalizeBindingName(binding.field);
    if (!explicitField.empty()) {
        return explicitField;
    }
    return normalizeBindingName(binding.name);
}

inline bool isOperator(std::string_view token)
{
    return token == "+" || token == "-" || token == "*" || token == "/";
}

inline bool isSupportedFunction(std::string_view token)
{
    return token == "sqrt" || token == "abs" || token == "min" || token == "max";
}

inline int functionArity(std::string_view token)
{
    if (token == "sqrt" || token == "abs") {
        return 1;
    }
    if (token == "min" || token == "max") {
        return 2;
    }
    return 0;
}

inline int precedence(std::string_view token)
{
    if (token == "+" || token == "-") {
        return 1;
    }
    if (token == "*" || token == "/") {
        return 2;
    }
    return 0;
}

inline bool appendIfAbsent(std::vector<std::string>& values, const std::string& candidate)
{
    if (candidate.empty()) {
        return false;
    }
    if (std::find(values.begin(), values.end(), candidate) != values.end()) {
        return false;
    }
    values.push_back(candidate);
    return true;
}

inline std::vector<std::string> extractVariables(const std::string& expression)
{
    std::vector<std::string> variables;
    static const std::regex regexPattern("\\b[a-zA-Z_][a-zA-Z0-9_]*\\b");
    for (std::sregex_iterator it(expression.begin(), expression.end(), regexPattern), end; it != end; ++it) {
        const std::string token = toLowerAscii(trimAsciiWhitespace((*it)[0].str()));
        if (token.empty() || isSupportedFunction(token)) {
            continue;
        }
        appendIfAbsent(variables, token);
    }
    return variables;
}

inline FieldRequirements resolveFieldRequirements(const std::string& expression,
                                                  const std::vector<VariableBinding>& bindings)
{
    FieldRequirements requirements;
    const std::vector<std::string> variables = extractVariables(toLowerAscii(expression));
    for (const std::string& variable : variables) {
        const VariableBinding* binding = findBinding(bindings, variable);
        if (!binding) {
            appendIfAbsent(requirements.requiredFields, variable);
            continue;
        }

        const std::string field = resolveBoundField(*binding);
        if (field.empty()) {
            continue;
        }

        if (binding->hasDefaultValue) {
            appendIfAbsent(requirements.optionalFields, field);
        } else {
            appendIfAbsent(requirements.requiredFields, field);
        }
    }
    return requirements;
}

inline std::string formatUnsupportedTokenError(const std::string& token)
{
    return std::string("表达式包含不支持的字符: ") + token;
}

inline std::vector<std::string> toRpn(const std::string& expression, std::string* errorMessage)
{
    std::vector<std::string> output;
    std::stack<std::string> operators;
    std::string token;

    auto flushToken = [&]() {
        if (!token.empty()) {
            output.push_back(token);
            token.clear();
        }
    };

    for (size_t index = 0; index < expression.size(); ++index) {
        const unsigned char ch = static_cast<unsigned char>(expression[index]);
        if (std::isspace(ch) != 0) {
            flushToken();
            continue;
        }

        if (std::isalnum(ch) != 0 || ch == '_' || ch == '.') {
            token.push_back(static_cast<char>(ch));
            continue;
        }

        const std::string current(1, static_cast<char>(ch));
        if (current == "(") {
            const std::string loweredToken = toLowerAscii(token);
            if (isSupportedFunction(loweredToken)) {
                operators.push(loweredToken);
                token.clear();
            } else {
                flushToken();
            }
            operators.push(current);
            continue;
        }

        flushToken();
        if (current == ",") {
            while (!operators.empty() && operators.top() != "(") {
                output.push_back(operators.top());
                operators.pop();
            }
            if (operators.empty()) {
                if (errorMessage) {
                    *errorMessage = "表达式参数分隔符位置不正确";
                }
                return {};
            }
            continue;
        }
        if (current == ")") {
            while (!operators.empty() && operators.top() != "(") {
                output.push_back(operators.top());
                operators.pop();
            }
            if (operators.empty()) {
                if (errorMessage) {
                    *errorMessage = "表达式括号不匹配";
                }
                return {};
            }
            operators.pop();
            if (!operators.empty() && isSupportedFunction(operators.top())) {
                output.push_back(operators.top());
                operators.pop();
            }
            continue;
        }
        if (!isOperator(current)) {
            if (errorMessage) {
                *errorMessage = formatUnsupportedTokenError(current);
            }
            return {};
        }

        while (!operators.empty() && isOperator(operators.top())
               && precedence(operators.top()) >= precedence(current)) {
            output.push_back(operators.top());
            operators.pop();
        }
        operators.push(current);
    }

    flushToken();
    while (!operators.empty()) {
        if (operators.top() == "(") {
            if (errorMessage) {
                *errorMessage = "表达式括号不匹配";
            }
            return {};
        }
        output.push_back(operators.top());
        operators.pop();
    }

    return output;
}

inline bool parseNumberToken(const std::string& token, double& value)
{
    if (token.empty()) {
        return false;
    }
    char* end = nullptr;
    value = std::strtod(token.c_str(), &end);
    return end == token.c_str() + static_cast<std::ptrdiff_t>(token.size());
}

inline std::optional<double> evaluateRpn(const std::vector<std::string>& rpn,
                                         const std::unordered_map<std::string, double>& variables,
                                         std::string* errorMessage)
{
    std::stack<double> stack;
    for (const std::string& token : rpn) {
        double number = 0.0;
        const bool isNumber = parseNumberToken(token, number);
        if (isNumber) {
            stack.push(number);
            continue;
        }

        if (isOperator(token)) {
            if (stack.size() < 2) {
                if (errorMessage) {
                    *errorMessage = "表达式语法错误";
                }
                return std::nullopt;
            }
            const double rhs = stack.top();
            stack.pop();
            const double lhs = stack.top();
            stack.pop();
            if (token == "+") stack.push(lhs + rhs);
            else if (token == "-") stack.push(lhs - rhs);
            else if (token == "*") stack.push(lhs * rhs);
            else if (token == "/") stack.push(std::abs(rhs) < 1e-12 ? 0.0 : lhs / rhs);
            continue;
        }

        if (isSupportedFunction(token)) {
            const int arity = functionArity(token);
            if (static_cast<int>(stack.size()) < arity) {
                if (errorMessage) {
                    *errorMessage = std::string("表达式函数参数不足: ") + token;
                }
                return std::nullopt;
            }

            if (arity == 1) {
                const double value = stack.top();
                stack.pop();
                if (token == "sqrt") {
                    stack.push(value < 0.0 ? 0.0 : std::sqrt(value));
                } else if (token == "abs") {
                    stack.push(std::abs(value));
                }
                continue;
            }

            const double rhs = stack.top();
            stack.pop();
            const double lhs = stack.top();
            stack.pop();
            if (token == "min") {
                stack.push((std::min)(lhs, rhs));
            } else if (token == "max") {
                stack.push((std::max)(lhs, rhs));
            }
            continue;
        }

        const auto variableIt = variables.find(token);
        if (variableIt == variables.end()) {
            if (errorMessage) {
                *errorMessage = std::string("表达式变量缺失: ") + token;
            }
            return std::nullopt;
        }
        stack.push(variableIt->second);
    }

    if (stack.size() != 1) {
        if (errorMessage) {
            *errorMessage = "表达式语法错误";
        }
        return std::nullopt;
    }
    return stack.top();
}

} // namespace factor::custom_expression