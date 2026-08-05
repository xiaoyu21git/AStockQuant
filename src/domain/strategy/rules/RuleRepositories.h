#pragma once
// RuleRepositories — 内置/用户/复合规则仓库
// 零 Qt, 纯 C++

#include "RuleTypes.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace domain::strategy::rules {

/// @brief 内置规则仓库 — 从编译进二进制的 JSON 字符串加载
/// 不可变, 只读
class BuiltinRuleRepository {
public:
    /// @brief 从嵌入的 kBuiltinRulesJson 常量字符串构造
    explicit BuiltinRuleRepository(const char* builtinJson,
                                    const ParamOverrides& overrides = {});

    [[nodiscard]] std::shared_ptr<const RuleLibrary> library() const;
    [[nodiscard]] const CompiledRuleTemplate* findTemplate(
        const std::string& templateId) const;
    [[nodiscard]] std::vector<const CompiledRuleTemplate*>
        templatesByStage(const std::string& stage) const;
    [[nodiscard]] const std::vector<CompiledRuleTemplate>& allTemplates() const;

    /// @brief 内置规则查询: 始终返回 true
    [[nodiscard]] bool isBuiltin(const std::string& templateId) const noexcept;
    /// @brief 内置规则查询: 始终返回 false
    [[nodiscard]] bool isUserDefined(const std::string& templateId) const noexcept;

private:
    std::shared_ptr<const RuleLibrary> m_library;
};

/// @brief 用户规则仓库 — 从 config/rules/user/*.json 扫描加载
/// 可重载, 可编辑
class UserRuleRepository {
public:
    UserRuleRepository();
    ~UserRuleRepository();
    /// @return 成功加载的模板数
    int loadFromDirectory(const std::string& dirPath);

    /// @brief 重新加载 (清空后重新扫描)
    int reload();

    [[nodiscard]] std::shared_ptr<const RuleLibrary> library() const;
    [[nodiscard]] const CompiledRuleTemplate* findTemplate(
        const std::string& templateId) const;
    [[nodiscard]] std::vector<const CompiledRuleTemplate*>
        templatesByStage(const std::string& stage) const;

    /// @brief 用户规则查询: 始终返回 false
    [[nodiscard]] bool isBuiltin(const std::string& templateId) const noexcept;
    /// @brief 用户规则查询: 始终返回 true
    [[nodiscard]] bool isUserDefined(const std::string& templateId) const noexcept;

    /// @brief 设置错误回调
    void setErrorCallback(RuleErrorCallback callback);

    /// @brief 单个用户模板有效 ID 集合 (用于冲突检测)
    [[nodiscard]] std::vector<std::string> templateIds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// @brief 复合规则仓库 — 合并内置 + 用户规则
/// ID 冲突时用户规则被拒绝, 内置规则保留
class CompositeRuleRepository {
public:
    CompositeRuleRepository();
    ~CompositeRuleRepository();
    void initialize(const char* builtinJson, const std::string& userDir,
                    const ParamOverrides& overrides = {});

    [[nodiscard]] std::shared_ptr<const RuleLibrary> library() const;
    [[nodiscard]] const CompiledRuleTemplate* findTemplate(
        const std::string& templateId) const;
    [[nodiscard]] std::vector<const CompiledRuleTemplate*>
        templatesByStage(const std::string& stage) const;
    [[nodiscard]] const std::vector<CompiledRuleTemplate>& allTemplates() const;

    /// @brief 查询模板来源
    [[nodiscard]] bool isBuiltin(const std::string& templateId) const noexcept;
    [[nodiscard]] bool isUserDefined(const std::string& templateId) const noexcept;

    /// @brief 设置错误回调
    void setErrorCallback(RuleErrorCallback callback);

    /// @brief 用户规则管理
    bool addUserTemplate(const std::string& json);
    bool removeUserTemplate(const std::string& templateId);
    void reloadUserTemplates();

private:
    void rebuildMergedLibrary();

    std::unique_ptr<BuiltinRuleRepository> m_builtin;
    std::unique_ptr<UserRuleRepository> m_user;
    std::shared_ptr<RuleLibrary> m_mergedLibrary;
    std::unordered_map<std::string, bool> m_sourceMap;  // templateId → isBuiltin
    std::string m_userDir;
    RuleErrorCallback m_errorCallback;
};

} // namespace domain::strategy::rules
