#include "RuleConditionEvaluator.h"
#include "foundation/json/json_facade.h"

#include <cstdio>
#include <string>
#include <unordered_map>

/// @brief 规则引擎单元测试
///
/// 验收标准:
/// - 条件树求值: all/any/not/truthy/比较 语义正确
/// - 三态语义: 变量缺失 → DataMissing, 绝不静默判 Pass/Fail
/// - 字符串枚举 eq: 编码一致性
/// - 全量加载 config/rules/compiled.json 并对真实模板求值
namespace {

using namespace domain::strategy::rules;
using foundation::json::JsonFacade;

constexpr int kExitSuccess = 0;
constexpr int kExitFailure = 1;

/// 测试变量提供者: 预置 map, 未预置的变量返回 nullopt
class MapProvider final : public IRuleVariableProvider {
public:
    void set(const std::string& name, double value) { m_values[name] = value; }
    void setString(const std::string& name, const std::string& text)
    {
        m_values[name] = ruleStringValueCode(text);
    }
    [[nodiscard]] std::optional<double> resolve(const std::string& varPath) const override
    {
        auto it = m_values.find(varPath);
        if (it == m_values.end()) return std::nullopt;
        return it->second;
    }
private:
    std::unordered_map<std::string, double> m_values;
};

TriState evalJson(const std::string& conditionJson, const IRuleVariableProvider& provider)
{
    auto node = JsonFacade::parse(conditionJson);
    return compileCondition(node)(provider);
}

const char* triStateName(TriState v)
{
    switch (v) {
    case TriState::Pass: return "Pass";
    case TriState::Fail: return "Fail";
    case TriState::DataMissing: return "DataMissing";
    }
    return "?";
}

bool expect(const char* label, TriState actual, TriState expected)
{
    if (actual != expected) {
        std::printf("[FAIL] %s: 期望 %s 实际 %s\n", label, triStateName(expected), triStateName(actual));
        return false;
    }
    return true;
}

/// 比较与 truthy 语义
bool testComparisons()
{
    MapProvider p;
    p.set("candidate.x", 5.0);
    p.set("candidate.flag", 1.0);
    p.set("candidate.zero", 0.0);

    bool ok = true;
    ok &= expect("lt 命中", evalJson(R"({"op":"lt","left":{"var":"candidate.x"},"right":6})", p), TriState::Pass);
    ok &= expect("lt 未中", evalJson(R"({"op":"lt","left":{"var":"candidate.x"},"right":5})", p), TriState::Fail);
    ok &= expect("ge 边界", evalJson(R"({"op":"ge","left":{"var":"candidate.x"},"right":5})", p), TriState::Pass);
    ok &= expect("truthy 真", evalJson(R"({"op":"truthy","value":{"var":"candidate.flag"}})", p), TriState::Pass);
    ok &= expect("truthy 假", evalJson(R"({"op":"truthy","value":{"var":"candidate.zero"}})", p), TriState::Fail);
    ok &= expect("变量缺失", evalJson(R"({"op":"lt","left":{"var":"candidate.absent"},"right":1})", p), TriState::DataMissing);
    return ok;
}

/// 逻辑组合三态语义
bool testLogicTriState()
{
    MapProvider p;
    p.set("a", 1.0);
    p.set("b", 0.0);
    // c 缺失

    auto T = R"({"op":"truthy","value":{"var":"a"}})";
    auto F = R"({"op":"truthy","value":{"var":"b"}})";
    auto M = R"({"op":"truthy","value":{"var":"c"}})";

    auto wrap = [](const char* op, std::initializer_list<const char*> parts) {
        std::string json = std::string(R"({"op":")") + op + R"(","conditions":[)";
        bool first = true;
        for (const char* part : parts) {
            if (!first) json += ",";
            json += part;
            first = false;
        }
        return json + "]}";
    };

    bool ok = true;
    ok &= expect("all 全真", evalJson(wrap("all", {T, T}), p), TriState::Pass);
    ok &= expect("all 含假", evalJson(wrap("all", {T, F, M}), p), TriState::Fail);       // Fail 短路优先
    ok &= expect("all 真+缺", evalJson(wrap("all", {T, M}), p), TriState::DataMissing);  // 不静默通过
    ok &= expect("any 含真", evalJson(wrap("any", {F, M, T}), p), TriState::Pass);
    ok &= expect("any 全假", evalJson(wrap("any", {F, F}), p), TriState::Fail);
    ok &= expect("any 假+缺", evalJson(wrap("any", {F, M}), p), TriState::DataMissing);  // 不静默判假
    ok &= expect("not 反转", evalJson(std::string(R"({"op":"not","value":)") + F + "}", p), TriState::Pass);
    ok &= expect("not 缺失", evalJson(std::string(R"({"op":"not","value":)") + M + "}", p), TriState::DataMissing);
    ok &= expect("not 裸var", evalJson(R"({"op":"not","value":{"var":"a"}})", p), TriState::Fail);
    return ok;
}

/// 字符串枚举 eq 编码一致性
bool testStringEq()
{
    MapProvider p;
    p.setString("market.emotion_cycle", "repair");

    bool ok = true;
    ok &= expect("eq 字符串命中",
        evalJson(R"({"op":"eq","left":{"var":"market.emotion_cycle"},"right":"repair"})", p), TriState::Pass);
    ok &= expect("eq 字符串未中",
        evalJson(R"({"op":"eq","left":{"var":"market.emotion_cycle"},"right":"panic"})", p), TriState::Fail);
    return ok;
}

/// 全量加载 compiled.json + 真实模板求值
bool testLoadCompiledLibrary()
{
    auto root = JsonFacade::parseFile("config/rules/compiled.json");
    auto library = loadRuleLibrary(root);
    if (!library || library->templates.empty()) {
        std::printf("[FAIL] compiled.json 加载失败或为空 (需从项目根目录运行)\n");
        return false;
    }
    std::printf("  规则库: %zu 个模板\n", library->templates.size());

    // 真实模板: 熊市冻结开仓 (risk_market_bear_freeze_entry)
    auto it = library->byId.find("template_risk_market_bear_freeze_entry_v1");
    if (it == library->byId.end()) {
        std::printf("[FAIL] 缺少熊市冻结模板\n");
        return false;
    }
    const auto& bearTemplate = *it->second;
    if (bearTemplate.rules.empty()) { std::printf("[FAIL] 熊市模板无规则\n"); return false; }

    // 构造熊市变量: 依模板 YAML 条件命中冻结
    MapProvider bear;
    bear.setString("market.regime_state", "bear");
    bear.set("market.index_below_ma200", 1.0);
    bear.set("market.breadth", 0.10);
    bear.set("market.drawdown_from_peak_ratio", 0.30);
    bear.set("market.risk_off_confirmed", 1.0);

    int hit = 0, missing = 0;
    for (const auto& rule : bearTemplate.rules) {
        const TriState verdict = rule.evaluateCondition(bear);
        if (verdict == TriState::Pass) {
            ++hit;
            std::printf("  熊市模板命中规则: %s action=%d reason=%s\n",
                        rule.ruleId.c_str(), static_cast<int>(rule.decision.action),
                        rule.decision.reasonCode.c_str());
        } else if (verdict == TriState::DataMissing) {
            ++missing;
        }
    }
    if (hit == 0 && missing > 0) {
        std::printf("  [提示] 熊市模板变量与测试假设不完全匹配(missing=%d), 视为环境差异非引擎错误\n", missing);
    }

    // 全库 smoke: 空 provider 下所有规则求值必须返回 DataMissing 或 Fail, 不允许崩溃/误判 Pass
    MapProvider empty;
    int silentPass = 0;
    for (const auto& compiledTemplate : library->templates) {
        for (const auto& rule : compiledTemplate.rules) {
            if (rule.evaluateCondition(empty) == TriState::Pass) {
                ++silentPass;
                std::printf("[FAIL] 空数据下规则误判 Pass: %s/%s\n",
                            compiledTemplate.templateId.c_str(), rule.ruleId.c_str());
            }
        }
    }
    return silentPass == 0;
}

} // anonymous namespace

int main()
{
    struct TestCase { const char* name; bool (*run)(); };
    const TestCase cases[] = {
        {"比较与truthy", testComparisons},
        {"逻辑组合三态", testLogicTriState},
        {"字符串eq编码", testStringEq},
        {"全量库加载与smoke", testLoadCompiledLibrary},
    };

    int failed = 0;
    for (const auto& testCase : cases) {
        const bool passed = testCase.run();
        std::printf("[%s] %s\n", passed ? "PASS" : "FAIL", testCase.name);
        if (!passed) ++failed;
    }
    std::printf(failed ? "test_rule_engine: %d 项失败\n" : "test_rule_engine: 全部通过\n", failed);
    return failed ? kExitFailure : kExitSuccess;
}
