#include "../include/StrategyRuleStatsBridge.h"
#include "../include/StrategyBridge.h"
#include "../include/StrategyPerformanceModel.h"
#include "../../domain/strategy/rules/RuleGate.h"
#include "../../domain/strategy/include/StrategyManager.h"
#include "../../domain/strategy/include/IStrategyService.h"
#include "foundation/json/json_facade.h"
#include "foundation/log/logging.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>

#include <fstream>
#include <map>
#include <sstream>

namespace {

using foundation::json::JsonFacade;

double safeDiv(double num, double den) {
    if (den == 0.0) return -1.0;
    return num / den;
}

/// @brief 唯一入口: 获取规则库。
/// 先走 sharedRuleLibrary()（domain 层已缓存），
/// 若空则桥接层用 binary-adjacent 路径加载（CMake POST_BUILD 已拷贝 compiled.json）。
const domain::strategy::rules::RuleLibrary* getLib() {
    auto* lib = domain::strategy::rules::sharedRuleLibrary();
    if (lib) return lib;

    // domain 层未缓存 → 桥接层从 appDir 加载
    static std::unique_ptr<domain::strategy::rules::RuleLibrary> s_cached;
    static bool s_attempted = false;
    if (s_attempted) return s_cached.get();
    s_attempted = true;

    // 二进制旁 config/rules/ (CMake POST_BUILD 拷贝) + 兜底相对路径
    QStringList paths = {
        QCoreApplication::applicationDirPath() + "/config/rules/compiled.json",
        QCoreApplication::applicationDirPath() + "/../../config/rules/compiled.json",
        QDir::currentPath() + "/config/rules/compiled.json",
    };
    for (const auto& p : paths) {
        auto root = JsonFacade::parseFile(p.toStdString());
        if (root.isNull()) continue;
        auto loaded = domain::strategy::rules::loadRuleLibrary(root);
        if (loaded) {
            INTERNAL_INFO_STREAM << "[StrategyRuleStatsBridge] 规则库加载自: " << p.toStdString()
                                 << " (" << loaded->templates.size() << " 模板)";
            s_cached = std::move(loaded);
            return s_cached.get();
        }
    }
    return nullptr;
}

// ── 用户参数持久化 (foundation::json::JsonFacade + std::fstream) ──
static QString userParamsPath() {
    return QCoreApplication::applicationDirPath() + "/config/rule_params_user.json";
}

static QVariantMap loadUserParamsMap() {
    QVariantMap result;
    std::ifstream f(userParamsPath().toStdString());
    if (!f.is_open()) return result;
    std::stringstream buf; buf << f.rdbuf();
    auto root = JsonFacade::parse(buf.str());
    if (root.isNull() || !root.has("params")) return result;
    auto params = root.get("params");
    for (const auto& tid : params.keys()) {
        auto pmap = params.get(tid);
        QVariantMap overrides;
        for (const auto& pkey : pmap.keys())
            overrides[QString::fromStdString(pkey)] = pmap.get(pkey).asDouble();
        result[QString::fromStdString(tid)] = overrides;
    }
    return result;
}

static void saveUserParamsMap(const QVariantMap& allOverrides) {
    auto root = JsonFacade::createObject();
    root.set("version", JsonFacade::createInt(1));
    auto paramsObj = JsonFacade::createObject();
    for (auto tit = allOverrides.begin(); tit != allOverrides.end(); ++tit) {
        QVariantMap overrides = tit.value().toMap();
        auto tObj = JsonFacade::createObject();
        for (auto pit = overrides.begin(); pit != overrides.end(); ++pit)
            tObj.set(pit.key().toStdString(), JsonFacade::createDouble(pit.value().toDouble()));
        paramsObj.set(tit.key().toStdString(), tObj);
    }
    root.set("params", paramsObj);
    QDir().mkpath(QFileInfo(userParamsPath()).path());
    std::ofstream f(userParamsPath().toStdString());
    f << root.toString();
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
    saveUserParamsMap(all);
    // 通知规则引擎重载（下次回测使用新参数）
    domain::strategy::rules::reloadSharedRuleLibrary();
    emit templateStatsUpdated(templateId);
    return true;
}
