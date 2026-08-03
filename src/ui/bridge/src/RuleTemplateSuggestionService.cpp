#include "RuleTemplateSuggestionService.h"

#include "RuleGate.h"
#include "RuleLibrary.h"
#include "RuleTypes.h"    // RuleLibrary, CompiledRuleTemplate, RuleDecision

#include <yaml-cpp/yaml.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct TermEntry {
    std::string termId;
    std::string displayName;
    std::vector<std::string> aliases;
    std::string defaultTemplateId;
    std::vector<std::string> templateIds;
};

/// 加载 trading_term_catalog.yaml, 搜索多个候选路径 (开发/部署兼容)
std::vector<TermEntry> loadTermCatalog()
{
    static const char* kCandidatePaths[] = {
        "astock_engine/rules/catalogs/trading_term_catalog.yaml",
        "../astock_engine/rules/catalogs/trading_term_catalog.yaml",
        "../../astock_engine/rules/catalogs/trading_term_catalog.yaml",
    };

    std::string yamlText;
    for (const char* path : kCandidatePaths) {
        std::ifstream fin(path);
        if (!fin.is_open()) continue;
        std::string line;
        while (std::getline(fin, line)) {
            yamlText += line;
            yamlText += '\n';
        }
        break;
    }

    std::vector<TermEntry> terms;
    if (yamlText.empty()) return terms;

    try {
        YAML::Node root = YAML::Load(yamlText);
        if (!root["terms"] || !root["terms"].IsSequence()) return terms;

        for (const auto& termNode : root["terms"]) {
            TermEntry entry;
            entry.termId        = termNode["term_id"]        ? termNode["term_id"].as<std::string>()        : "";
            entry.displayName   = termNode["display_name"]   ? termNode["display_name"].as<std::string>()  : "";
            entry.defaultTemplateId = termNode["default_template_id"]
                ? termNode["default_template_id"].as<std::string>() : "";

            if (termNode["aliases"] && termNode["aliases"].IsSequence()) {
                for (const auto& a : termNode["aliases"])
                    entry.aliases.push_back(a.as<std::string>());
            }
            if (termNode["template_ids"] && termNode["template_ids"].IsSequence()) {
                for (const auto& tid : termNode["template_ids"])
                    entry.templateIds.push_back(tid.as<std::string>());
            }
            if (!entry.termId.empty() && !entry.displayName.empty())
                terms.push_back(std::move(entry));
        }
    } catch (...) {
        // YAML 解析失败 → 返回空, 术语匹配不可用但不影响模板搜索
    }
    return terms;
}

/// 匹配查询文本 → 术语; 返回最佳匹配索引, -1 表示无匹配
int matchTerm(const std::string& queryLower, const std::vector<TermEntry>& terms)
{
    int best = -1;
    std::size_t bestLen = 0;
    for (std::size_t i = 0; i < terms.size(); ++i) {
        const auto& t = terms[i];
        // 检查 display_name
        if (queryLower.find(t.displayName) != std::string::npos
            || t.displayName.find(queryLower) != std::string::npos) {
            if (t.displayName.size() > bestLen) { best = static_cast<int>(i); bestLen = t.displayName.size(); }
            continue;
        }
        // 检查 aliases
        for (const auto& alias : t.aliases) {
            if (queryLower.find(alias) != std::string::npos
                || alias.find(queryLower) != std::string::npos) {
                if (alias.size() > bestLen) { best = static_cast<int>(i); bestLen = alias.size(); }
                break;
            }
        }
    }
    return best;
}

} // namespace

using namespace domain::strategy::rules;

namespace {

/// 递归格式化条件 JSON → QML display lines [{text, level}]
void formatConditionLines(const QJsonValue& node, int level, QVariantList& out)
{
    if (node.isObject()) {
        const QJsonObject obj = node.toObject();
        const QString op = obj.value("op").toString();

        if (op == "all" || op == "any") {
            QVariantMap header;
            header["text"]  = (op == "all") ? QStringLiteral("全部满足:") : QStringLiteral("任一满足:");
            header["level"] = level;
            out.append(header);
            const QJsonArray conds = obj.value("conditions").toArray();
            for (const auto& c : conds)
                formatConditionLines(c, level + 1, out);
            return;
        }

        if (op == "not") {
            QVariantMap header;
            header["text"]  = QStringLiteral("不满足:");
            header["level"] = level;
            out.append(header);
            formatConditionLines(obj.value("value"), level + 1, out);
            return;
        }

        if (op == "truthy") {
            const QString varPath = obj.value("value").toObject().value("var").toString();
            QVariantMap line;
            line["text"]  = varPath.isEmpty() ? QStringLiteral("(条件为真)") : varPath + QStringLiteral(" ≠ 0");
            line["level"] = level;
            out.append(line);
            return;
        }

        // 二元比较: eq/ne/lt/le/gt/ge
        const QString leftVar  = obj.value("left").toObject().value("var").toString();
        const QString rightVar = obj.value("right").isObject()
            ? obj.value("right").toObject().value("var").toString() : QString();
        const double rightNum  = obj.value("right").isDouble() ? obj.value("right").toDouble() : 0.0;
        const bool hasRightNum = obj.value("right").isDouble();

        QString opSymbol;
        if (op == "eq") opSymbol = "=";
        else if (op == "ne") opSymbol = "≠";
        else if (op == "lt") opSymbol = "<";
        else if (op == "le") opSymbol = "≤";
        else if (op == "gt") opSymbol = ">";
        else if (op == "ge") opSymbol = "≥";
        else opSymbol = op;

        QVariantMap line;
        if (hasRightNum)
            line["text"] = leftVar + " " + opSymbol + " " + QString::number(rightNum);
        else if (!rightVar.isEmpty())
            line["text"] = leftVar + " " + opSymbol + " " + rightVar;
        else
            line["text"] = leftVar + " " + opSymbol + " ?";
        line["level"] = level;
        out.append(line);
    }
}

/// 根据 then 子句构建 action/payload/state lines
void formatThenLines(const CompiledRule& rule, QVariantList& actionLines,
                     QVariantList& payloadLines, QVariantList& stateLines)
{
    // 动作结果
    const char* actionText = nullptr;
    switch (rule.decision.action) {
    case RuleAction::Block:       actionText = "阻断 (block)"; break;
    case RuleAction::Exit:        actionText = "退出 (exit)"; break;
    case RuleAction::Reduce:      actionText = "减仓 (reduce)"; break;
    case RuleAction::StateSwitch: actionText = "切换状态 (state_switch)"; break;
    case RuleAction::Freeze:      actionText = "冻结 (freeze)"; break;
    default:                      actionText = "通过 (pass)"; break;
    }
    QVariantMap al;
    al["text"] = QString::fromStdString(std::string(actionText) + " — " + rule.decision.message);
    actionLines.append(al);

    // Payload
    if (!rule.decision.statePayload.empty()) {
        QVariantMap pl;
        pl["text"] = QString::fromStdString("state: " + rule.decision.statePayload);
        payloadLines.append(pl);
    }
    if (rule.decision.scoreBoost != 0.0) {
        QVariantMap pl;
        pl["text"] = QString::fromLatin1("score_boost: %1").arg(rule.decision.scoreBoost);
        payloadLines.append(pl);
    }

    // 状态写入
    QJsonDocument thenDoc = QJsonDocument::fromJson(
        QString::fromStdString("{ \"dummy\": true }").toUtf8());
    // 简单: 如果 action == StateSwitch, 添加 statePayload
    if (rule.decision.action == RuleAction::StateSwitch
        && !rule.decision.statePayload.empty()) {
        QVariantMap sl;
        sl["text"] = QString::fromLatin1("→ %1").arg(
            QString::fromStdString(rule.decision.statePayload));
        stateLines.append(sl);
    }
}

/// 构建单条建议条目 (共享于术语匹配和全文本搜索)
QVariantMap buildSuggestionItem(const CompiledRuleTemplate& tmpl)
{
    QVariantMap item;
    const QString tid = QString::fromStdString(tmpl.templateId);
    const QString name = QString::fromStdString(tmpl.displayName);
    item["templateId"]        = tid;
    item["template_id"]       = tid;
    item["templateName"]      = name;
    item["template_name"]     = name;
    item["displayName"]       = name;
    item["display_name"]      = name;
    item["templateDisplayName"] = name;
    item["phase"]             = QString::fromStdString(tmpl.phase);
    item["summary"]           = QString::fromStdString(tmpl.summary);
    item["ns"]                = QString::fromStdString(tmpl.ns);
    item["file_name"]         = QString::fromStdString(tmpl.fileName);
    item["fileName"]          = QString::fromStdString(tmpl.fileName);

    QVariantList tagList;
    for (const auto& t : tmpl.tags) tagList.append(QString::fromStdString(t));
    item["tags"] = tagList;

    QVariantList actionList;
    for (const auto& a : tmpl.actions) actionList.append(QString::fromStdString(a));
    item["actions"]             = actionList;
    item["recommended_actions"] = actionList;

    // 规则详情 — RuleTemplateStructureView 直接消费
    QVariantList ruleItems;
    for (const auto& rule : tmpl.rules) {
        QVariantMap ri;
        ri["id"]          = QString::fromStdString(rule.ruleId);
        ri["name"]        = QString::fromStdString(rule.decision.message.empty()
                                ? rule.ruleId : rule.decision.message);
        ri["priority"]    = rule.priority;
        ri["description"] = QString(); // 模板层面描述已在 summary

        // 解析 conditionJson → display lines
        QVariantList conditionLines;
        QJsonDocument condDoc = QJsonDocument::fromJson(
            QString::fromStdString(rule.conditionJson).toUtf8());
        if (!condDoc.isNull())
            formatConditionLines(condDoc.object(), 0, conditionLines);
        ri["conditionLines"] = conditionLines;

        // then → action/payload/state lines
        QVariantList actionLines, payloadLines, stateLines;
        formatThenLines(rule, actionLines, payloadLines, stateLines);
        ri["actionLines"]  = actionLines;
        ri["payloadLines"] = payloadLines;
        ri["stateLines"]   = stateLines;

        ruleItems.append(ri);
    }
    item["rules"] = ruleItems;

    item["matched_aliases"]        = QVariantList();
    item["missing_feature_labels"] = QVariantList();
    item["fileModifiedAt"]         = QString();
    item["file_modified_at"]       = QString();

    return item;
}

} // namespace

RuleTemplateSuggestionService::RuleTemplateSuggestionService(QObject* parent)
    : QObject(parent) {}

void RuleTemplateSuggestionService::initialize()
{
    const auto* lib = domain::strategy::rules::sharedRuleLibrary();
    m_initialized = (lib != nullptr);
}

void RuleTemplateSuggestionService::suggestTemplatesRequestAsync(
    const QString& requestId, const QVariantMap& request)
{
    if (!m_initialized) {
        QVariantMap err;
        err["requestId"] = requestId;
        err["error"] = QStringLiteral("规则库未加载，请先确保 config/rules/compiled.json 存在");
        emit suggestionFailed(err);
        return;
    }

    const auto* lib = domain::strategy::rules::sharedRuleLibrary();
    if (!lib || lib->templates.empty()) {
        QVariantMap err;
        err["requestId"] = requestId;
        err["error"] = QStringLiteral("规则库为空");
        emit suggestionFailed(err);
        return;
    }

    // ── 术语匹配 ──
    const QString queryText = request.value("text").toString().trimmed();
    QString resolvedTermId;
    QString resolvedTermDisplayName;

    if (!queryText.isEmpty()) {
        static const std::vector<TermEntry> s_terms = loadTermCatalog();
        const std::string queryLower = queryText.toLower().toStdString();
        const int termIdx = matchTerm(queryLower, s_terms);
        if (termIdx >= 0) {
            resolvedTermId          = QString::fromStdString(s_terms[static_cast<std::size_t>(termIdx)].termId);
            resolvedTermDisplayName = QString::fromStdString(s_terms[static_cast<std::size_t>(termIdx)].displayName);
        }
    }

    // ── 模板搜索 ──
    const QString phaseFilter = request.value("phase").toString().trimmed().toLower();
    const int limit = std::max(1, std::min(request.value("limit", 72).toInt(), 200));

    QVariantList suggestions;

    // 术语命中时: 优先推送关联模板
    if (!resolvedTermId.isEmpty()) {
        for (const auto& tmpl : lib->templates) {
            if (suggestions.size() >= limit) break;
            const QString tid = QString::fromStdString(tmpl.templateId);
            if (!tid.contains(resolvedTermId)) continue;
            if (!phaseFilter.isEmpty()
                && QString::fromStdString(tmpl.phase).trimmed().toLower() != phaseFilter)
                continue;
            suggestions.append(buildSuggestionItem(tmpl));
        }
    }

    // 补充: 文本搜索所有匹配模板 (术语关联之外)
    {
        const QString q = queryText.isEmpty() ? QString() : queryText.toLower();
        for (const auto& tmpl : lib->templates) {
            if (suggestions.size() >= limit) break;
            if (!phaseFilter.isEmpty()
                && QString::fromStdString(tmpl.phase).trimmed().toLower() != phaseFilter)
                continue;

            const QString tid = QString::fromStdString(tmpl.templateId);
            // 跳过已在术语关联列表中的模板
            bool alreadyIn = false;
            for (const auto& existing : suggestions) {
                if (existing.toMap().value("templateId").toString() == tid) {
                    alreadyIn = true; break;
                }
            }
            if (alreadyIn) continue;

            // 文本匹配
            if (!q.isEmpty()) {
                bool match = tid.toLower().contains(q)
                          || QString::fromStdString(tmpl.displayName).toLower().contains(q)
                          || QString::fromStdString(tmpl.summary).toLower().contains(q);
                if (!match) {
                    for (const auto& tag : tmpl.tags) {
                        if (QString::fromStdString(tag).toLower().contains(q)) { match = true; break; }
                    }
                }
                if (!match) continue;
            }

            suggestions.append(buildSuggestionItem(tmpl));
        }
    }

    QVariantMap result;
    result["requestId"]               = requestId;
    result["resolvedTermId"]          = resolvedTermId;
    result["resolvedTermDisplayName"] = resolvedTermDisplayName;
    result["suggestions"]             = suggestions;

    emit suggestionReady(result);
}

QVariantList RuleTemplateSuggestionService::suggestAllTemplates()
{
    QVariantList result;
    if (!m_initialized) {
        const auto* lib = domain::strategy::rules::sharedRuleLibrary();
        if (!lib) return result;
    }
    const auto* lib = domain::strategy::rules::sharedRuleLibrary();
    if (!lib) return result;

    result.reserve(static_cast<int>(lib->templates.size()));
    for (const auto& tmpl : lib->templates) {
        result.append(buildSuggestionItem(tmpl));
    }
    return result;
}

#include "moc_RuleTemplateSuggestionService.cpp"
