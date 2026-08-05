#include "../include/StrategyRuleStatsBridge.h"
#include "../include/StrategyBridge.h"
#include "../include/StrategyPerformanceModel.h"
#include "../../domain/strategy/rules/RuleGate.h"
#include "../../domain/strategy/rules/RuleLibrary.h"
#include "../../domain/strategy/rules/RuleConditionEvaluator.h"
#include "../../domain/strategy/rules/RuleAttribution.h"
#include "../../../infrastructure/include/database/NativePgConnectionPool.h"
#include "../../../infrastructure/include/database/ISqlDatabase.h"
#include "../../domain/strategy/include/StrategyManager.h"
#include "../../domain/strategy/include/IStrategyService.h"
#include "foundation/json/json_facade.h"
#include "foundation/log/logging.hpp"

#include <QMutexLocker>
#include <QtConcurrent>

#include <map>

namespace {

using foundation::json::JsonFacade;

double safeDiv(double num, double den) {
    if (den == 0.0) return -1.0;
    return num / den;
}

const domain::strategy::rules::RuleLibrary* getLib() {
    return domain::strategy::rules::sharedRuleLibrary();
}

static QVariantMap loadUserParamsMap() {
    QVariantMap result;
    try {
        auto& pool = astock::database::NativePgConnectionPool::instance();
        auto db = pool.getConnection();
        if (db && db->isOpen()) {
            auto rows = db->executeQuery(
                "SELECT template_id, param_key, param_value FROM live.rule_param_overrides");
            for (auto& row : rows.getRows()) {
                QString tid = QString::fromStdString(row.getString("template_id"));
                QVariantMap overrides = result.value(tid).toMap();
                overrides[QString::fromStdString(row.getString("param_key"))] = row.getDouble("param_value");
                result[tid] = overrides;
            }
        }
    } catch (...) {}
    return result;
}

static QString translateSuffix(const std::string& suffix);

/// @brief 将英文字段路径转为中文显示名
static QString toChineseLabel(const std::string& varPath) {
    if (varPath.empty()) return "";
    // 精确匹配优先
    static const std::map<std::string, std::string> kMap = {
        {"market.emotion_cycle", "市场情绪周期"},
        {"market.index_trend_score", "大盘趋势评分"},
        {"market.breadth_ratio", "涨跌比"},
        {"market.limit_up_ratio", "涨停率"},
        {"market.index_ma_trend", "大盘均线趋势"},
        {"candidate.volume_ratio_to_5d_avg", "相对5日均量比"},
        {"candidate.reclaim_reference_ratio", "关键价位修复率"},
        {"candidate.previous_weakness_confirmed", "前期弱势确认"},
        {"candidate.intraday_strength_score", "日内强度评分"},
        {"candidate.gap_ratio", "跳空幅度"},
        {"candidate.open_range_ratio", "开盘区间比"},
        {"candidate.relative_strength_vs_market", "相对大盘强度"},
        {"candidate.turnover_acceleration", "换手加速"},
        {"candidate.close_position_ratio", "收盘位置比"},
        {"candidate.buy_volume_ratio", "买入量比"},
        {"position.hold_days", "持仓天数"},
        {"position.pnl_percent", "盈亏百分比"},
        {"position.weight_percent", "权重百分比"},
        {"position.stop_loss_pct", "止损比例"},
        {"position.take_profit_pct", "止盈比例"},
    };
    auto it = kMap.find(varPath);
    if (it != kMap.end()) return QString::fromStdString(it->second);
    // 前缀映射兜底（后缀也翻译）
    if (varPath.find("candidate.") == 0)
        return "候选." + translateSuffix(varPath.substr(10));
    if (varPath.find("market.") == 0)
        return "市场." + translateSuffix(varPath.substr(7));
    if (varPath.find("position.") == 0)
        return "持仓." + translateSuffix(varPath.substr(9));
    // 无前缀 → 尝试全路径翻译
    return translateSuffix(varPath);
}

/// @brief 常见变量名后缀英译中
static QString translateSuffix(const std::string& suffix) {
    static const std::map<std::string, std::string> kWordMap = {
        {"volume", "量"}, {"ratio", "比"}, {"score", "评分"}, {"strength", "强度"},
        {"price", "价格"}, {"trend", "趋势"}, {"weakness", "弱势"}, {"confirmed", "已确认"},
        {"turnover", "换手"}, {"acceleration", "加速"}, {"close", "收盘"}, {"open", "开盘"},
        {"high", "最高"}, {"low", "最低"}, {"gap", "缺口"}, {"range", "区间"},
        {"intraday", "日内"}, {"relative", "相对"}, {"market", "大盘"}, {"index", "指数"},
        {"position", "位置"}, {"hold", "持有"}, {"days", "天数"}, {"pnl", "盈亏"},
        {"percent", "百分比"}, {"weight", "权重"}, {"stop", "止损"}, {"loss", "亏损"},
        {"take", "止盈"}, {"profit", "盈利"}, {"buy", "买入"}, {"sell", "卖出"},
        {"reclaim", "修复"}, {"reference", "参考"}, {"previous", "前期"}, {"current", "当前"},
        {"breadth", "宽度"}, {"limit", "涨跌停"}, {"up", "上涨"}, {"down", "下跌"},
        {"emotion", "情绪"}, {"cycle", "周期"}, {"avg", "均值"}, {"ma", "均线"},
        {"daily", "日"}, {"weekly", "周"}, {"monthly", "月"},
    };
    // 按下划线拆分，逐词翻译
    QString result;
    std::string word;
    for (char c : suffix) {
        if (c == '_') {
            if (!word.empty()) {
                auto it = kWordMap.find(word);
                result += it != kWordMap.end() ? QString::fromStdString(it->second) : QString::fromStdString(word);
                word.clear();
            }
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        auto it = kWordMap.find(word);
        result += it != kWordMap.end() ? QString::fromStdString(it->second) : QString::fromStdString(word);
    }
    return result;
}

static QString opToChinese(const std::string& op) {
    if (op == "ge") return "≥";
    if (op == "le") return "≤";
    if (op == "gt") return ">";
    if (op == "lt") return "<";
    if (op == "eq") return "=";
    return QString::fromStdString(op);
}

/// @brief 递归提取可调参数
void collectTunableParams(const JsonFacade& node, QVariantList& out, int depth = 0) {
    if (depth > 20 || !node.has("op")) return;
    std::string op = node.get("op").asString();
    if (op == "lt" || op == "gt" || op == "le" || op == "ge" || op == "eq") {
        if (node.has("right")) {
            auto right = node.get("right");
            if (right.isNumber()) {
                std::string varPath;
                if (node.has("left") && node.get("left").isObject()) {
                    auto left = node.get("left");
                    varPath = left.has("var") ? left.get("var").asString() : "";
                }
                QString key = varPath.empty()
                    ? QString("param_%1__%2").arg(QString::fromStdString(op)).arg(out.size())
                    : QString("%1__%2").arg(QString::fromStdString(varPath), QString::fromStdString(op));
                QVariantMap param;
                param["key"] = key;
                param["currentValue"] = right.asDouble();
                param["displayLabel"] = toChineseLabel(varPath);
                param["type"] = "double";
                param["_rawOp"] = QString::fromStdString(op);  // 保留原始操作符用于范围计算
                param["op"] = opToChinese(op);
                out.append(param);
            }
        }
    }
    if (node.has("conditions")) {
        auto conds = node.get("conditions");
        for (std::size_t i = 0; i < conds.size(); ++i)
            collectTunableParams(conds.at(i), out, depth + 1);
    }
}

QVariantList paramsFromJson(const std::string& json) {
    QVariantList params;
    if (json.empty()) return params;
    try {
        auto root = JsonFacade::parse(json);
        collectTunableParams(root, params, 0);
    } catch (...) {}
    return params;
}

QString actionToString(domain::strategy::rules::RuleAction a) {
    using domain::strategy::rules::RuleAction;
    switch (a) {
    case RuleAction::Pass:           return "pass";
    case RuleAction::Block:          return "block";
    case RuleAction::CandidateEntry: return "candidate_entry";
    case RuleAction::Exit:           return "exit";
    case RuleAction::Reduce:         return "reduce";
    case RuleAction::StateSwitch:    return "state_switch";
    case RuleAction::Freeze:         return "freeze";
    }
    return "pass";
}

} // anonymous namespace

// ── StrategyRuleStatsBridge ──

StrategyRuleStatsBridge::StrategyRuleStatsBridge(QObject* parent)
    : QObject(parent) { preload(); }

void StrategyRuleStatsBridge::preload() {
    // 构造时直接加载，不经过 loadAllTemplates 的 loading 状态机
    auto* lib = getLib();
    if (!lib) return;
    QVariantList result;
    for (const auto& tmpl : lib->templates) {
        QVariantMap e;
        e["templateId"] = QString::fromStdString(tmpl.templateId);
        e["displayName"] = QString::fromStdString(tmpl.displayName);
        e["phase"] = QString::fromStdString(tmpl.phase);
        QString s = QString::fromStdString(tmpl.summary);
        if (s.length() > 100) s = s.left(97) + "...";
        e["summary"] = s;
        e["rulesCount"] = static_cast<int>(tmpl.rules.size());
        e["namespace"] = QString::fromStdString(tmpl.ns);
        QVariantList tags, acts;
        for (const auto& t : tmpl.tags) tags.append(QString::fromStdString(t));
        for (const auto& a : tmpl.actions) acts.append(QString::fromStdString(a));
        e["tags"] = tags;
        e["actions"] = acts;
        result.append(e);
    }
    m_cachedTemplates = result;
    m_hasData = !result.isEmpty();
}

void StrategyRuleStatsBridge::refresh() {
    preload();
    emit dataChanged();
}

void StrategyRuleStatsBridge::setLoading(bool loading) {
    QMutexLocker lock(&m_mutex);
    if (m_isLoading != loading) {
        m_isLoading = loading;
        lock.unlock();
        emit loadingChanged();
    }
}

void StrategyRuleStatsBridge::setError(const QString& msg) {
    QMutexLocker lock(&m_mutex);
    m_lastError = msg;
    lock.unlock();
    emit errorOccurred(msg);
}

double StrategyRuleStatsBridge::safeDiv(double n, double d) const { return ::safeDiv(n, d); }
QString StrategyRuleStatsBridge::formatWinRateSource(const QString& s) const { return s; }

// ── 模板元数据 ──

QVariantList StrategyRuleStatsBridge::loadAllTemplates() {
    QMutexLocker lock(&m_mutex);
    return m_cachedTemplates;
}

QVariantList StrategyRuleStatsBridge::getTemplatesByPhase(const QString& phase) {
    auto* lib = getLib();
    if (!lib) return {};
    QVariantList result;
    for (const auto& tmpl : lib->templates) {
        if (QString::fromStdString(tmpl.phase) != phase) continue;
        QVariantMap e;
        e["templateId"] = QString::fromStdString(tmpl.templateId);
        e["displayName"] = QString::fromStdString(tmpl.displayName);
        e["phase"] = QString::fromStdString(tmpl.phase);
        QString s = QString::fromStdString(tmpl.summary);
        if (s.length() > 100) s = s.left(97) + "...";
        e["summary"] = s;
        e["rulesCount"] = static_cast<int>(tmpl.rules.size());
        result.append(e);
    }
    return result;
}

QVariantMap StrategyRuleStatsBridge::getTemplateDetail(const QString& templateId) {
    auto* lib = getLib();
    if (!lib) return {};
    auto it = lib->byId.find(templateId.toStdString());
    if (it == lib->byId.end()) return {};

    const auto& tmpl = *it->second;
    QVariantMap d;
    d["templateId"] = QString::fromStdString(tmpl.templateId);
    d["displayName"] = QString::fromStdString(tmpl.displayName);
    d["phase"] = QString::fromStdString(tmpl.phase);
    d["summary"] = QString::fromStdString(tmpl.summary);
    d["namespace"] = QString::fromStdString(tmpl.ns);

    QVariantList tags, acts;
    for (const auto& t : tmpl.tags) tags.append(QString::fromStdString(t));
    for (const auto& a : tmpl.actions) acts.append(QString::fromStdString(a));
    d["tags"] = tags;
    d["actions"] = acts;

    QVariantList rulesList;
    QVariantList allParams;
    for (const auto& rule : tmpl.rules) {
        QVariantMap r;
        r["id"] = QString::fromStdString(rule.ruleId);
        r["stage"] = QString::fromStdString(rule.stage);
        r["priority"] = rule.priority;
        r["conditionJson"] = QString::fromStdString(rule.conditionJson);
        r["action"] = actionToString(rule.decision.action);
        r["reasonCode"] = QString::fromStdString(rule.decision.reasonCode);
        r["message"] = QString::fromStdString(rule.decision.message);
        rulesList.append(r);
        auto params = paramsFromJson(rule.conditionJson);
        for (const auto& p : params) allParams.append(p);
    }
    d["rules"] = rulesList;
    if (!tmpl.rules.empty())
        d["conditionTreeJson"] = QString::fromStdString(tmpl.rules.front().conditionJson);
    d["params"] = allParams;
    return d;
}

// ── 统计 ──

QVariantMap StrategyRuleStatsBridge::getTemplateStats(const QString& templateId,
                                                       const QString& strategyId) {
    QVariantMap s;
    s["evaluated"] = 0; s["hits"] = 0; s["blockedSignals"] = 0; s["dataMissing"] = 0;
    s["blockRate"] = -1.0; s["hitRate"] = -1.0;
    s["winRate"] = -1.0; s["winRateSource"] = "none";

    try {
        std::string sid = strategyId.isEmpty() ? "" : strategyId.toStdString();
        domain::strategy::StrategyEngine* engine = nullptr;
        if (!sid.empty()) engine = domain::strategy::StrategyManager::instance().get(sid);

        if (engine) {
            const auto& gs = engine->ruleGateStats();
            auto it = gs.byTemplate.find(templateId.toStdString());
            if (it != gs.byTemplate.end()) {
                s["evaluated"] = it->second.evaluated;
                s["hits"] = it->second.hits;
                s["blockedSignals"] = it->second.blockedSignals;
                s["dataMissing"] = it->second.dataMissing;
                s["blockRate"] = ::safeDiv((double)it->second.blockedSignals, (double)it->second.evaluated);
                s["hitRate"] = ::safeDiv((double)it->second.hits, (double)it->second.evaluated);
            }
        }
        if (!sid.empty()) {
            auto* pm = new StrategyPerformanceModel();
            pm->setStrategyId(strategyId);
            pm->refresh();
            if (pm->rowCount() > 0) {
                auto d = pm->loadResultDetail(0);
                s["winRate"] = d.value("winRate", -1.0).toDouble();
                s["winRateSource"] = QString("strategy:%1").arg(strategyId);
            }
            delete pm;
        }
    } catch (const std::exception& ex) {
        INTERNAL_WARN_STREAM << "[StatsBridge] getTemplateStats: " << ex.what();
    }
    return s;
}

QVariantMap StrategyRuleStatsBridge::getAggregateStats(const QString& strategyId) {
    QVariantMap a;
    a["totalTemplates"] = 0; a["boundTemplates"] = 0;
    a["avgBlockRate"] = -1.0; a["avgWinRate"] = -1.0;
    try {
        auto templates = loadAllTemplates();
        a["totalTemplates"] = templates.size();
        double sumBr = 0, sumWr = 0;
        int brN = 0, wrN = 0, bound = 0;
        for (const auto& t : templates) {
            auto s = getTemplateStats(t.toMap()["templateId"].toString(), strategyId);
            if (s["evaluated"].toInt() > 0) { ++bound; double br = s["blockRate"].toDouble(); if (br >= 0) { sumBr += br; ++brN; } }
            double wr = s["winRate"].toDouble(); if (wr >= 0) { sumWr += wr; ++wrN; }
        }
        a["boundTemplates"] = bound;
        a["avgBlockRate"] = ::safeDiv(sumBr, (double)brN);
        a["avgWinRate"] = ::safeDiv(sumWr, (double)wrN);
    } catch (...) {}
    return a;
}

QVariantList StrategyRuleStatsBridge::extractTunableParams(const QString& templateId) {
    auto* lib = getLib();
    if (!lib) return {};
    auto it = lib->byId.find(templateId.toStdString());
    if (it == lib->byId.end()) return {};
    // 加载用户覆盖值
    auto userOverrides = loadUserParamsMap();
    QVariantMap tmplOverrides = userOverrides.value(templateId).toMap();
    QVariantList all;
    for (const auto& rule : it->second->rules) {
        auto raw = paramsFromJson(rule.conditionJson);
        for (const auto& variant : raw) {
            QVariantMap m = variant.toMap();
            QString key = m["key"].toString();
            if (tmplOverrides.contains(key))
                m["currentValue"] = tmplOverrides[key];
            all.append(m);
        }
    }
    return all;
}

bool StrategyRuleStatsBridge::updateTemplateParams(const QString& templateId,
                                                    const QVariantMap& params,
                                                    const QString&) {
    if (templateId.isEmpty()) return false;
    auto all = loadUserParamsMap();
    QVariantMap tmplOverrides = all.value(templateId).toMap();
    for (auto pit = params.begin(); pit != params.end(); ++pit)
        tmplOverrides[pit.key()] = pit.value();
    all[templateId] = tmplOverrides;
    // 注入内存 + 通知规则引擎重载
    domain::strategy::rules::ParamOverrides cppOverrides;
    for (auto tit = all.begin(); tit != all.end(); ++tit) {
        QVariantMap m = tit.value().toMap();
        std::map<std::string, double> inner;
        for (auto pit = m.begin(); pit != m.end(); ++pit)
            inner[pit.key().toStdString()] = pit.value().toDouble();
        cppOverrides[tit.key().toStdString()] = inner;
    }
    domain::strategy::rules::setSharedParamOverrides(cppOverrides);
    domain::strategy::rules::reloadSharedRuleLibrary();
    emit templateStatsUpdated(templateId);
    return true;
}

QVariantMap StrategyRuleStatsBridge::testSingleRule(
    const QString& templateId, int ruleIndex,
    const QString& symbol, const QString& date)
{
    QVariantMap result;
    result["verdict"] = "Error";

    auto lib = domain::strategy::rules::sharedRuleLibrary();
    if (!lib) { result["reason"] = "规则库未加载"; return result; }
    auto it = lib->byId.find(templateId.toStdString());
    if (it == lib->byId.end()) { result["reason"] = "模板不存在"; return result; }
    const auto& rules = it->second->rules;
    if (ruleIndex < 0 || ruleIndex >= static_cast<int>(rules.size())) {
        result["reason"] = "规则索引越界"; return result;
    }
    const auto& rule = rules[static_cast<std::size_t>(ruleIndex)];

    int y = date.left(4).toInt(), m = date.mid(4,2).toInt(), d = date.right(2).toInt();
    char ds[32]; snprintf(ds, sizeof(ds), "%04d-%02d-%02d", y, m, d);

    std::vector<double> closes;
    try {
        auto& pool = astock::database::NativePgConnectionPool::instance();
        auto db = pool.getConnection();
        if (db && db->isOpen()) {
            auto rows = db->executeQuery(
                "SELECT d.close FROM mkt.daily_bar d JOIN ref.symbol_info si ON d.symbol_id=si.id "
                "WHERE si.symbol=$1 AND d.trade_date<=$2::date ORDER BY d.trade_date DESC LIMIT 60",
                {astock::database::SqlParam{symbol.toStdString()},
                 astock::database::SqlParam{std::string(ds)}});
            for (auto& row : rows.getRows()) closes.push_back(row.getDouble("close"));
            std::reverse(closes.begin(), closes.end());
        }
    } catch (...) {}

    if (closes.size() < 20) { result["reason"] = "数据不足"; return result; }

    struct SP : domain::strategy::rules::IRuleVariableProvider {
        const std::vector<double>& c; int last;
        SP(const std::vector<double>& cl, int l) : c(cl), last(l) {}
        std::optional<double> resolve(const std::string& vp) const override {
            if (last < 0 || c[last] <= 0) return std::nullopt;
            auto ma = [&](int w) -> std::optional<double> {
                if (last + 1 < w) return std::nullopt;
                double s = 0; for (int i = last; i > last - w; --i) s += c[i];
                return s / w;
            };
            if (vp == "candidate.close_to_ma20_ratio") { auto m20 = ma(20); return m20 ? std::optional(c[last] / *m20) : std::nullopt; }
            if (vp == "candidate.close_to_ma60_ratio") { auto m60 = ma(60); return m60 ? std::optional(c[last] / *m60) : std::nullopt; }
            if (vp == "candidate.close_to_ma120_ratio") { auto m120 = ma(120); return m120 ? std::optional(c[last] / *m120) : std::nullopt; }
            if (vp == "candidate.volume_ratio_to_5d_avg") return 1.5;
            if (vp == "candidate.change_percent") {
                if (last < 1 || c[last-1] <= 0) return std::nullopt;
                return (c[last]/c[last-1] - 1.0) * 100.0;
            }
            return std::nullopt;
        }
    };
    SP provider(closes, static_cast<int>(closes.size()) - 1);

    auto fn = domain::strategy::rules::compileCondition(
        foundation::json::JsonFacade::parse(rule.conditionJson), nullptr);
    auto verdict = fn(provider);
    result["verdict"] = verdict == domain::strategy::rules::TriState::Pass ? "Pass"
        : (verdict == domain::strategy::rules::TriState::Fail ? "Fail" : "DataMissing");
    result["ruleId"] = QString::fromStdString(rule.ruleId);
    result["stage"] = QString::fromStdString(rule.stage);
    return result;
}

void StrategyRuleStatsBridge::startCoverageCalc(
    const QString& templateId, int ruleIndex,
    const QString& symbol, int lookbackDays)
{
    QtConcurrent::run([this, templateId, ruleIndex, symbol, lookbackDays]() {
        QVariantMap result;
        result["totalDays"] = lookbackDays;
        result["passCount"] = 0;

        auto lib = domain::strategy::rules::sharedRuleLibrary();
        if (!lib) { emit coverageReady(result); return; }
        auto it = lib->byId.find(templateId.toStdString());
        if (it == lib->byId.end()) { emit coverageReady(result); return; }
        if (ruleIndex < 0 || ruleIndex >= static_cast<int>(it->second->rules.size())) {
            emit coverageReady(result); return;
        }
        const auto& rule = it->second->rules[static_cast<std::size_t>(ruleIndex)];

        std::vector<double> closes;
        try {
            auto& pool = astock::database::NativePgConnectionPool::instance();
            auto db = pool.getConnection();
            if (db && db->isOpen()) {
                auto rows = db->executeQuery(
                    "SELECT d.close FROM mkt.daily_bar d JOIN ref.symbol_info si ON d.symbol_id=si.id "
                    "WHERE si.symbol=$1 ORDER BY d.trade_date DESC LIMIT $2",
                    {astock::database::SqlParam{symbol.toStdString()},
                     astock::database::SqlParam{lookbackDays + 60}});
                for (auto& row : rows.getRows()) closes.push_back(row.getDouble("close"));
                std::reverse(closes.begin(), closes.end());
            }
        } catch (...) { emit coverageReady(result); return; }
        if (closes.size() < 60) { emit coverageReady(result); return; }

        struct SP : domain::strategy::rules::IRuleVariableProvider {
            const std::vector<double>& c; int last;
            SP(const std::vector<double>& cl, int l) : c(cl), last(l) {}
            std::optional<double> resolve(const std::string& vp) const override {
                if (last < 0 || c[last] <= 0) return std::nullopt;
                auto ma = [&](int w) -> std::optional<double> {
                    if (last + 1 < w) return std::nullopt;
                    double s = 0; for (int i = last; i > last - w; --i) s += c[i];
                    return s / w;
                };
                if (vp == "candidate.close_to_ma20_ratio") { auto m20 = ma(20); return m20 ? std::optional(c[last] / *m20) : std::nullopt; }
                if (vp == "candidate.change_percent") {
                    if (last < 1 || c[last-1] <= 0) return std::nullopt;
                    return (c[last]/c[last-1] - 1.0) * 100.0;
                }
                return std::nullopt;
            }
        };

        auto fn = domain::strategy::rules::compileCondition(
            foundation::json::JsonFacade::parse(rule.conditionJson), nullptr);
        int passCount = 0, evalStart = std::max(0, static_cast<int>(closes.size()) - 1 - lookbackDays);
        for (int i = evalStart; i < static_cast<int>(closes.size()); ++i) {
            SP provider(closes, i);
            if (fn(provider) == domain::strategy::rules::TriState::Pass) ++passCount;
        }
        result["passCount"] = passCount;
        result["passRate"] = lookbackDays > 0 ? static_cast<double>(passCount) / lookbackDays : 0.0;
        emit coverageReady(result);
    });
}

QVariantList StrategyRuleStatsBridge::getCrossStrategyRuleStats(const QString& templateId)
{
    QVariantList result;
    auto& mgr = domain::strategy::StrategyManager::instance();
    for (std::size_t i = 0; i < mgr.count(); ++i) {
        // StrategyManager 不支持索引遍历，改用 DB 查询
    }
    // 从 DB 查所有使用该模板的策略及其回测结果
    try {
        auto& pool = astock::database::NativePgConnectionPool::instance();
        auto db = pool.getConnection();
        if (db && db->isOpen()) {
            auto rows = db->executeQuery(
                "SELECT s.strategy_id, s.parameters FROM live.strategy s "
                "WHERE s.parameters IS NOT NULL");
            for (auto& row : rows.getRows()) {
                std::string paramsJson;
                // parameters 是 text, 需要转成字符串
                try { paramsJson = row.getString("parameters"); } catch (...) { continue; }
                // 简单字符串匹配: 检查是否包含 templateId
                if (paramsJson.find(templateId.toStdString()) == std::string::npos) continue;

                QString sid = QString::fromStdString(row.getString("strategy_id"));
                QVariantMap item;
                item["strategyId"] = sid;
                item["strategyName"] = sid.left(8);

                // 读该策略的归因
                auto* engine = mgr.get(row.getString("strategy_id"));
                if (engine) {
                    const auto& attr = engine->ruleAttribution();
                    auto it = attr.find(templateId.toStdString());
                    if (it != attr.end()) {
                        item["preventedTrades"] = it->second.preventedTrades;
                        item["preventedPnL"] = it->second.preventedHypotheticalPnL;
                        item["triggeredExits"] = it->second.triggeredExits;
                        item["exitPnL"] = it->second.exitRealizedPnL;
                        double net = it->second.preventedHypotheticalPnL + it->second.exitRealizedPnL;
                        item["netContribution"] = net;
                    }
                }
                // 获取命中统计
                if (engine) {
                    const auto& gs = engine->ruleGateStats();
                    auto tIt = gs.byTemplate.find(templateId.toStdString());
                    if (tIt != gs.byTemplate.end()) {
                        item["hits"] = tIt->second.hits;
                        item["evaluated"] = tIt->second.evaluated;
                    }
                }
                if (!item.contains("hits")) continue;
                result.append(item);
            }
        }
    } catch (...) {}
    return result;
}

QVariantList StrategyRuleStatsBridge::getRuleAttribution(
    const QString& strategyId, const QString& templateId)
{
    QVariantList result;

    auto& mgr = domain::strategy::StrategyManager::instance();
    domain::strategy::StrategyEngine* engine = mgr.get(strategyId.toStdString());
    if (!engine) return result;

    const auto& gateStats = engine->ruleGateStats();
    const auto& attrMap = engine->ruleAttribution();
    auto tmplIt = gateStats.byTemplate.find(templateId.toStdString());
    auto attrIt = attrMap.find(templateId.toStdString());

    if (tmplIt == gateStats.byTemplate.end() && attrIt == attrMap.end()) return result;

    QVariantMap item;
    item["ruleId"] = QString::fromStdString(templateId.toStdString());
    item["dateRange"] = QString::fromStdString(engine->backtestDateRange());

    if (tmplIt != gateStats.byTemplate.end()) {
        const auto& stats = tmplIt->second;
        item["evaluated"] = stats.evaluated;
        item["hits"] = stats.hits;
        item["blockedSignals"] = stats.blockedSignals;
        item["dataMissing"] = stats.dataMissing;
    } else {
        item["evaluated"] = 0;
        item["hits"] = 0;
        item["blockedSignals"] = 0;
        item["dataMissing"] = 0;
    }

    // P&L 归因 + 交易质量
    if (attrIt != attrMap.end()) {
        item["preventedTrades"] = attrIt->second.preventedTrades;
        item["preventedPnL"] = attrIt->second.preventedHypotheticalPnL;
        item["preventedWinRate"] = attrIt->second.preventedWinRate;
        item["triggeredExits"] = attrIt->second.triggeredExits;
        item["exitPnL"] = attrIt->second.exitRealizedPnL;
        double net = attrIt->second.preventedHypotheticalPnL + attrIt->second.exitRealizedPnL;
        item["netContribution"] = net;
        item["topBoughtCount"] = attrIt->second.topBoughtCount;
        item["missedGainCount"] = attrIt->second.missedGainCount;
        item["stopLossCount"] = attrIt->second.stopLossCount;
    } else {
        item["preventedTrades"] = 0; item["preventedPnL"] = 0.0; item["preventedWinRate"] = 0.0;
        item["triggeredExits"] = 0; item["exitPnL"] = 0.0; item["netContribution"] = 0.0;
        item["topBoughtCount"] = 0; item["missedGainCount"] = 0; item["stopLossCount"] = 0;
    }
    result.append(item);

    return result;
}
