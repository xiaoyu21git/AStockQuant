// RuleLibrary — 规则仓库实现

#include "RuleLibrary.h"
#include "RuleConditionEvaluator.h"
#include "RuleRepositories.h"

#include "foundation/json/json_facade.h"
#include "foundation/log/logging.hpp"
#include "../../../infrastructure/include/database/NativePgConnectionPool.h"
#include "../../../infrastructure/include/database/ISqlDatabase.h"

namespace domain::strategy::rules {

static void initCompositeRepo();

void reloadSharedRuleLibrary()
{
    initCompositeRepo();
    INTERNAL_INFO_STREAM << "[RuleLib] 规则库已重新加载";
}

void setSharedParamOverrides(const ParamOverrides& overrides)
{
    try {
        auto& pool = astock::database::NativePgConnectionPool::instance();
        auto db = pool.getConnection();
        if (db && db->isOpen()) {
            db->executeQuery("DELETE FROM live.rule_param_overrides");
            for (const auto& [tid, kv] : overrides) {
                for (const auto& [key, val] : kv) {
                    db->executeQuery(
                        "INSERT INTO live.rule_param_overrides VALUES($1,$2,$3) ON CONFLICT (template_id,param_key) DO UPDATE SET param_value=$3",
                        {astock::database::SqlParam{std::string(tid)},
                         astock::database::SqlParam{std::string(key)},
                         astock::database::SqlParam{val}});
                }
            }
            INTERNAL_INFO_STREAM << "[RuleLib] 参数覆盖已保存: " << overrides.size() << " 个模板";
        }
    } catch (...) {}
}

// 由 embed_rules.py 生成, 定义在 rules_builtin.cpp 中
namespace builtin {
    const char* getBuiltinRulesJson();
}

namespace {
std::unique_ptr<CompositeRuleRepository> s_compositeRepo;
std::mutex s_compositeMutex;
RuleStateMap s_ruleStates;
std::mutex s_ruleStatesMutex;
}

void initCompositeRepo()
{
    auto repo = std::make_unique<CompositeRuleRepository>();
    repo->setErrorCallback([](RuleErrorCode code, const std::string& detail) {
        INTERNAL_WARN_STREAM << "[RuleRepo] 错误: code=" << static_cast<int>(code)
                             << " detail=" << detail;
    });
    ParamOverrides overrides;
    RuleStateMap ruleStates;
    try {
        auto& pool = astock::database::NativePgConnectionPool::instance();
        auto db = pool.getConnection();
        if (db && db->isOpen()) {
            auto result = db->executeQuery(
                "SELECT template_id, param_key, param_value FROM live.rule_param_overrides");
            for (auto& row : result.getRows()) {
                overrides[row.getString("template_id")]
                         [row.getString("param_key")] = row.getDouble("param_value");
            }
            // 加载规则运行时状态
            auto stateResult = db->executeQuery(
                "SELECT rule_id, template_id, enabled, severity FROM live.rule_state");
            for (auto& row : stateResult.getRows()) {
                RuleRuntimeState rs;
                rs.enabled = row.getInt("enabled") != 0;
                rs.severity = row.getString("severity", "active");
                ruleStates[row.getString("rule_id")] = rs;
            }
            INTERNAL_INFO_STREAM << "[RuleRepo] 规则状态加载: " << ruleStates.size() << " 条";
        }
    } catch (...) {}
    {
        std::lock_guard<std::mutex> lock(s_ruleStatesMutex);
        s_ruleStates = std::move(ruleStates);
    }
    repo->initialize(builtin::getBuiltinRulesJson(), "config/rules/user", overrides);
    std::lock_guard<std::mutex> lock(s_compositeMutex);
    s_compositeRepo = std::move(repo);
}

CompositeRuleRepository& sharedCompositeRepo()
{
    if (!s_compositeRepo) initCompositeRepo();
    return *s_compositeRepo;
}

const RuleLibrary* sharedRuleLibrary()
{
    auto lib = sharedCompositeRepo().library();
    return lib.get();
}

const RuleStateMap& getRuleStates() {
    std::lock_guard<std::mutex> lock(s_ruleStatesMutex);
    return s_ruleStates;
}

} // namespace domain::strategy::rules
