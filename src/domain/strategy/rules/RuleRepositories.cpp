// RuleRepositories — 内置/用户/复合规则仓库实现

#include "RuleRepositories.h"
#include "RuleConditionEvaluator.h"

#include "foundation/json/json_facade.h"
#include "foundation/log/logging.hpp"

#include <algorithm>

namespace domain::strategy::rules {

// ═══════════════════════════════════════════════════════════════════════
// BuiltinRuleRepository
// ═══════════════════════════════════════════════════════════════════════

BuiltinRuleRepository::BuiltinRuleRepository(const char* builtinJson,
                                               const ParamOverrides& overrides)
{
    if (!builtinJson || !builtinJson[0]) return;  // 空指针或空字符串 → 延迟初始化
    auto root = foundation::json::JsonFacade::parse(builtinJson);
    if (root.isNull()) {
        INTERNAL_ERROR_STREAM << "[BuiltinRepo] 内置规则 JSON 解析失败";
        return;
    }
    m_library = loadRuleLibrary(root, overrides);
    if (m_library) {
        INTERNAL_INFO_STREAM << "[BuiltinRepo] 内置规则加载完成: "
                             << m_library->templates.size() << " 个模板";
    }
}

std::shared_ptr<const RuleLibrary> BuiltinRuleRepository::library() const
{
    return m_library;
}

const CompiledRuleTemplate* BuiltinRuleRepository::findTemplate(
    const std::string& templateId) const
{
    if (!m_library) return nullptr;
    auto it = m_library->byId.find(templateId);
    return (it != m_library->byId.end()) ? it->second : nullptr;
}

std::vector<const CompiledRuleTemplate*>
BuiltinRuleRepository::templatesByStage(const std::string& stage) const
{
    std::vector<const CompiledRuleTemplate*> result;
    if (!m_library) return result;
    for (const auto& tmpl : m_library->templates) {
        if (tmpl.phase == stage)
            result.push_back(&tmpl);
    }
    return result;
}

const std::vector<CompiledRuleTemplate>& BuiltinRuleRepository::allTemplates() const
{
    static const std::vector<CompiledRuleTemplate> s_empty;
    return m_library ? m_library->templates : s_empty;
}

bool BuiltinRuleRepository::isBuiltin(const std::string&) const noexcept
{
    return true;
}

bool BuiltinRuleRepository::isUserDefined(const std::string&) const noexcept
{
    return false;
}

// ═══════════════════════════════════════════════════════════════════════
// UserRuleRepository
// ═══════════════════════════════════════════════════════════════════════

struct UserRuleRepository::Impl {
    std::shared_ptr<RuleLibrary> library;
    std::string lastDirPath;
    RuleErrorCallback errorCallback;
};

UserRuleRepository::UserRuleRepository()
    : m_impl(std::make_unique<Impl>()) {}

UserRuleRepository::~UserRuleRepository() = default;

void UserRuleRepository::setErrorCallback(RuleErrorCallback callback)
{
    m_impl->errorCallback = std::move(callback);
}

int UserRuleRepository::loadFromDirectory(const std::string& dirPath)
{
    m_impl->lastDirPath = dirPath;
    auto lib = std::make_shared<RuleLibrary>();
    int successCount = 0;
    int skipCount = 0;

    INTERNAL_INFO_STREAM << "[UserRepo] 扫描用户规则目录: " << dirPath;

    // TODO: 实现目录遍历加载用户模板

    m_impl->library = lib;
    INTERNAL_INFO_STREAM << "[UserRepo] 用户规则加载: 成功=" << successCount
                         << " 跳过=" << skipCount;
    return successCount;
}

int UserRuleRepository::reload()
{
    return loadFromDirectory(m_impl->lastDirPath);
}

std::shared_ptr<const RuleLibrary> UserRuleRepository::library() const
{
    return m_impl->library;
}

const CompiledRuleTemplate* UserRuleRepository::findTemplate(
    const std::string& templateId) const
{
    if (!m_impl->library) return nullptr;
    auto it = m_impl->library->byId.find(templateId);
    return (it != m_impl->library->byId.end()) ? it->second : nullptr;
}

std::vector<const CompiledRuleTemplate*>
UserRuleRepository::templatesByStage(const std::string& stage) const
{
    std::vector<const CompiledRuleTemplate*> result;
    if (!m_impl->library) return result;
    for (const auto& tmpl : m_impl->library->templates) {
        if (tmpl.phase == stage)
            result.push_back(&tmpl);
    }
    return result;
}

bool UserRuleRepository::isBuiltin(const std::string&) const noexcept
{
    return false;
}

bool UserRuleRepository::isUserDefined(const std::string&) const noexcept
{
    return true;
}

std::vector<std::string> UserRuleRepository::templateIds() const
{
    std::vector<std::string> ids;
    if (!m_impl->library) return ids;
    for (const auto& tmpl : m_impl->library->templates)
        ids.push_back(tmpl.templateId);
    return ids;
}

// ═══════════════════════════════════════════════════════════════════════
// CompositeRuleRepository
// ═══════════════════════════════════════════════════════════════════════

CompositeRuleRepository::CompositeRuleRepository()
    : m_builtin(std::make_unique<BuiltinRuleRepository>("", ParamOverrides{}))
    , m_user(std::make_unique<UserRuleRepository>()) {}

CompositeRuleRepository::~CompositeRuleRepository() = default;

void CompositeRuleRepository::initialize(
    const char* builtinJson, const std::string& userDir,
    const ParamOverrides& overrides)
{
    m_userDir = userDir;

    // 加载内置规则 (带参数覆盖)
    m_builtin = std::make_unique<BuiltinRuleRepository>(builtinJson, overrides);

    // 加载用户规则
    m_user->setErrorCallback(m_errorCallback);
    m_user->loadFromDirectory(userDir);

    rebuildMergedLibrary();

    INTERNAL_INFO_STREAM << "[CompositeRepo] 初始化完成: "
                         << m_builtin->allTemplates().size() << " 内置 + "
                         << m_user->templateIds().size() << " 用户";
}

void CompositeRuleRepository::rebuildMergedLibrary()
{
    auto merged = std::make_shared<RuleLibrary>();
    m_sourceMap.clear();

    // 先入内置
    for (const auto& tmpl : m_builtin->allTemplates()) {
        merged->templates.push_back(tmpl);
        m_sourceMap[tmpl.templateId] = true;
    }

    // 再入用户
    for (const auto& tmpl : m_user->library() ? m_user->library()->templates
                                               : std::vector<CompiledRuleTemplate>{}) {
        merged->templates.push_back(tmpl);
        m_sourceMap[tmpl.templateId] = false;
    }

    // 重建 byId 索引
    merged->byId.clear();
    for (const auto& tmpl : merged->templates)
        merged->byId[tmpl.templateId] = &tmpl;

    m_mergedLibrary = merged;
}

void CompositeRuleRepository::setErrorCallback(RuleErrorCallback callback)
{
    m_errorCallback = std::move(callback);
    m_user->setErrorCallback(m_errorCallback);
}

std::shared_ptr<const RuleLibrary> CompositeRuleRepository::library() const
{
    return m_mergedLibrary;
}

const CompiledRuleTemplate* CompositeRuleRepository::findTemplate(
    const std::string& templateId) const
{
    auto it = m_mergedLibrary->byId.find(templateId);
    return (it != m_mergedLibrary->byId.end()) ? it->second : nullptr;
}

std::vector<const CompiledRuleTemplate*>
CompositeRuleRepository::templatesByStage(const std::string& stage) const
{
    std::vector<const CompiledRuleTemplate*> result;
    for (const auto& tmpl : m_mergedLibrary->templates) {
        if (tmpl.phase == stage)
            result.push_back(&tmpl);
    }
    return result;
}

const std::vector<CompiledRuleTemplate>& CompositeRuleRepository::allTemplates() const
{
    return m_mergedLibrary->templates;
}

bool CompositeRuleRepository::isBuiltin(const std::string& templateId) const noexcept
{
    auto it = m_sourceMap.find(templateId);
    return it != m_sourceMap.end() ? it->second : false;
}

bool CompositeRuleRepository::isUserDefined(const std::string& templateId) const noexcept
{
    auto it = m_sourceMap.find(templateId);
    return it != m_sourceMap.end() ? !it->second : false;
}

bool CompositeRuleRepository::addUserTemplate(const std::string& /*json*/)
{
    // 用户规则添加: 解析 JSON → 检测冲突 → 追加到 UserRuleRepository → rebuild
    // 完整实现需要文件持久化, 当前保留接口
    return false;
}

bool CompositeRuleRepository::removeUserTemplate(const std::string& /*templateId*/)
{
    return false;
}

void CompositeRuleRepository::reloadUserTemplates()
{
    m_user->reload();
    rebuildMergedLibrary();
}

} // namespace domain::strategy::rules
