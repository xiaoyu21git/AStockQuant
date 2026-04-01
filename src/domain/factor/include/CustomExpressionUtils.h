#pragma once

#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <optional>
#include <stack>
#include <string>
#include <unordered_map>

namespace factor::custom_expression {

struct VariableBinding {
    QString name;
    QString field;
    bool hasDefaultValue{false};
    double defaultValue{0.0};
};

struct FieldRequirements {
    QStringList requiredFields;
    QStringList optionalFields;
};

inline QString normalizeBindingName(const QString& text)
{
    return text.trimmed().toLower();
}

inline const VariableBinding* findBinding(const std::vector<VariableBinding>& bindings, const QString& variable)
{
    const QString normalizedVariable = normalizeBindingName(variable);
    for (const auto& binding : bindings) {
        if (normalizeBindingName(binding.name) == normalizedVariable) {
            return &binding;
        }
    }
    return nullptr;
}

inline QString resolveBoundField(const VariableBinding& binding)
{
    const QString explicitField = binding.field.trimmed().toLower();
    if (!explicitField.isEmpty()) {
        return explicitField;
    }
    if (binding.hasDefaultValue) {
        return {};
    }
    return normalizeBindingName(binding.name);
}

inline bool isOperator(const QString& token)
{
    return token == "+" || token == "-" || token == "*" || token == "/";
}

inline bool isSupportedFunction(const QString& token)
{
    return token == "sqrt" || token == "abs" || token == "min" || token == "max";
}

inline int functionArity(const QString& token)
{
    if (token == "sqrt" || token == "abs") {
        return 1;
    }
    if (token == "min" || token == "max") {
        return 2;
    }
    return 0;
}

inline int precedence(const QString& token)
{
    if (token == "+" || token == "-") {
        return 1;
    }
    if (token == "*" || token == "/") {
        return 2;
    }
    return 0;
}

inline QStringList extractVariables(const QString& expression)
{
    QStringList variables;
    QRegularExpression regex(QStringLiteral("\\b[a-zA-Z_][a-zA-Z0-9_]*\\b"));
    auto it = regex.globalMatch(expression);
    while (it.hasNext()) {
        const QString token = it.next().captured(0).trimmed();
        const QString lower = token.toLower();
        if (isSupportedFunction(lower)) {
            continue;
        }
        if (!variables.contains(lower)) {
            variables.append(lower);
        }
    }
    return variables;
}

inline FieldRequirements resolveFieldRequirements(const QString& expression,
                                                  const std::vector<VariableBinding>& bindings)
{
    FieldRequirements requirements;
    const QStringList variables = extractVariables(expression.toLower());
    for (const QString& variable : variables) {
        const VariableBinding* binding = findBinding(bindings, variable);
        if (!binding) {
            if (!requirements.requiredFields.contains(variable)) {
                requirements.requiredFields.append(variable);
            }
            continue;
        }

        const QString field = resolveBoundField(*binding);
        if (field.isEmpty()) {
            continue;
        }

        QStringList& target = binding->hasDefaultValue ? requirements.optionalFields : requirements.requiredFields;
        if (!target.contains(field)) {
            target.append(field);
        }
    }
    return requirements;
}

inline QStringList toRpn(const QString& expression, QString* errorMessage)
{
    QStringList output;
    std::stack<QString> operators;
    QString token;

    auto flushToken = [&]() {
        if (!token.isEmpty()) {
            output.append(token);
            token.clear();
        }
    };

    for (int index = 0; index < expression.size(); ++index) {
        const QChar ch = expression.at(index);
        if (ch.isSpace()) {
            flushToken();
            continue;
        }

        if (ch.isLetterOrNumber() || ch == '_' || ch == '.') {
            token.append(ch);
            continue;
        }

        const QString current(ch);
        if (current == "(") {
            if (isSupportedFunction(token.toLower())) {
                operators.push(token.toLower());
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
                output.append(operators.top());
                operators.pop();
            }
            if (operators.empty()) {
                if (errorMessage) {
                    *errorMessage = QString::fromUtf8("表达式参数分隔符位置不正确");
                }
                return {};
            }
            continue;
        }
        if (current == ")") {
            while (!operators.empty() && operators.top() != "(") {
                output.append(operators.top());
                operators.pop();
            }
            if (operators.empty()) {
                if (errorMessage) {
                    *errorMessage = QString::fromUtf8("表达式括号不匹配");
                }
                return {};
            }
            operators.pop();
            if (!operators.empty() && isSupportedFunction(operators.top())) {
                output.append(operators.top());
                operators.pop();
            }
            continue;
        }
        if (!isOperator(current)) {
            if (errorMessage) {
                *errorMessage = QString::fromUtf8("表达式包含不支持的字符: %1").arg(current);
            }
            return {};
        }

        while (!operators.empty() && isOperator(operators.top())
               && precedence(operators.top()) >= precedence(current)) {
            output.append(operators.top());
            operators.pop();
        }
        operators.push(current);
    }

    flushToken();
    while (!operators.empty()) {
        if (operators.top() == "(") {
            if (errorMessage) {
                *errorMessage = QString::fromUtf8("表达式括号不匹配");
            }
            return {};
        }
        output.append(operators.top());
        operators.pop();
    }

    return output;
}

inline std::optional<double> evaluateRpn(const QStringList& rpn,
                                         const std::unordered_map<std::string, double>& variables,
                                         QString* errorMessage)
{
    std::stack<double> stack;
    for (const QString& token : rpn) {
        bool isNumber = false;
        const double number = token.toDouble(&isNumber);
        if (isNumber) {
            stack.push(number);
            continue;
        }

        if (isOperator(token)) {
            if (stack.size() < 2) {
                if (errorMessage) {
                    *errorMessage = QString::fromUtf8("表达式语法错误");
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
                    *errorMessage = QString::fromUtf8("表达式函数参数不足: %1").arg(token);
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

        const auto variableIt = variables.find(token.toStdString());
        if (variableIt == variables.end()) {
            if (errorMessage) {
                *errorMessage = QString::fromUtf8("表达式变量缺失: %1").arg(token);
            }
            return std::nullopt;
        }
        stack.push(variableIt->second);
    }

    if (stack.size() != 1) {
        if (errorMessage) {
            *errorMessage = QString::fromUtf8("表达式语法错误");
        }
        return std::nullopt;
    }
    return stack.top();
}

} // namespace factor::custom_expression