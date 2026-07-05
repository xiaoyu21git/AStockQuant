#include "BacktestAnalyticsService.h"
#include "../../../infrastructure/include/database/NativePgConnectionPool.h"
#include "foundation/log/logging.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace ui::bridge {

BacktestAnalyticsService::BacktestAnalyticsService(QObject* parent) : QObject(parent) {}

void BacktestAnalyticsService::refreshRunList() {
    m_runList.clear();
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) { emit runListChanged(); return; }

    auto rows = db->executeQuery(
        "SELECT r.id, r.factor_id, r.config_json, r.summary_json, r.groups_json, r.created_at,"
        "  (SELECT COUNT(*) FROM alpha.factor_backtest_daily d WHERE d.run_id = r.id) AS daily_count,"
        "  (SELECT COUNT(*) FROM alpha.factor_backtest_trades t WHERE t.run_id = r.id) AS trade_count "
        "FROM alpha.factor_backtest_runs r ORDER BY r.created_at DESC LIMIT 500",
        {});

    for (const auto& row : rows.getRows()) {
        QVariantMap item;
        item["runId"]    = QString::fromStdString(row.getString("id"));
        item["factorId"] = QString::fromStdString(row.getString("factor_id"));
        item["createdAt"]= QString::fromStdString(row.getString("created_at"));
        item["dailyCount"] = row.getInt("daily_count");
        item["tradeCount"] = row.getInt("trade_count");

        // 解析 summary_json 提取关键指标
        std::string summaryStr = row.getString("summary_json");
        if (!summaryStr.empty()) {
            QJsonParseError err;
            auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(summaryStr), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                auto root = doc.object();
                auto fq = root.value("factorQuality").toObject();
                auto ic = root.value("ic").toObject();
                auto ex = root.value("execution").toObject();
                auto tr = root.value("trading").toObject();

                item["sharpe"]     = tr.value("sharpe").toDouble();
                item["icMean"]     = ic.value("value").toDouble();
                item["icIR"]       = ic.value("ir").toDouble();
                item["totalReturn"]= tr.value("totalReturn").toDouble();
                item["maxDrawdown"]= tr.value("maxDrawdown").toDouble();
                item["turnover"]   = ex.value("turnoverRatio").toDouble();
                item["rating"]     = fq.value("coreRating").toInt();
                item["ratingLabel"]= fq.value("coreRatingLabel").toString();

                auto coreMetrics = fq.value("coreMetrics").toArray();
                for (const auto& cm : coreMetrics) {
                    auto obj = cm.toObject();
                    item[obj["key"].toString()] = obj["value"].toDouble();
                }
            }
        }

        // 解析 config_json 提取参数
        std::string cfgStr = row.getString("config_json");
        if (!cfgStr.empty()) {
            QJsonParseError err;
            auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(cfgStr), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                auto cfg = doc.object();
                item["benchmark"]   = cfg.value("benchmarkSymbol").toString();
                item["startDate"]   = cfg.value("startDate").toString();
                item["endDate"]     = cfg.value("endDate").toString();
                item["initialCapital"] = cfg.value("initialCapital").toDouble();
            }
        }

        m_runList.append(item);
    }
    emit runListChanged();
}

void BacktestAnalyticsService::loadRunDetail(const QString& runId) {
    m_activeDetail.clear();
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) { emit activeRunDetailChanged(); return; }

    auto rows = db->executeQuery(
        "SELECT r.*,"
        "  (SELECT AVG(rank_ic) FROM alpha.factor_backtest_ic_daily ic WHERE ic.run_id = r.id) AS avg_ic,"
        "  (SELECT STDDEV(rank_ic) FROM alpha.factor_backtest_ic_daily ic WHERE ic.run_id = r.id) AS std_ic,"
        "  (SELECT AVG(raw_long_short) FROM alpha.factor_backtest_daily d WHERE d.run_id = r.id) AS avg_raw_ret,"
        "  (SELECT AVG(cost_adj_long_short) FROM alpha.factor_backtest_daily d WHERE d.run_id = r.id) AS avg_cost_ret,"
        "  (SELECT SUM(long_held+short_held) FROM alpha.factor_backtest_periods p WHERE p.run_id = r.id) AS total_held "
        "FROM alpha.factor_backtest_runs r WHERE r.id = $1",
        {astock::database::SqlParam{runId.toStdString()}});

    if (!rows.getRows().empty()) {
        const auto& row = rows.getRows()[0];
        m_activeDetail["runId"]     = runId;
        m_activeDetail["factorId"]  = QString::fromStdString(row.getString("factor_id"));
        m_activeDetail["createdAt"] = QString::fromStdString(row.getString("created_at"));
        m_activeDetail["avgIc"]     = row.getDouble("avg_ic");
        m_activeDetail["stdIc"]     = row.getDouble("std_ic");
        m_activeDetail["avgRawRet"] = row.getDouble("avg_raw_ret");
        m_activeDetail["avgCostRet"]= row.getDouble("avg_cost_ret");
        m_activeDetail["totalHeld"] = row.getInt("total_held");

        std::string cfgStr = row.getString("config_json");
        if (!cfgStr.empty()) {
            auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(cfgStr));
            if (doc.isObject())
                m_activeDetail["config"] = doc.object().toVariantMap();
        }
        std::string summaryStr = row.getString("summary_json");
        if (!summaryStr.empty()) {
            auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(summaryStr));
            if (doc.isObject())
                m_activeDetail["summary"] = doc.object().toVariantMap();
        }
    }
    emit activeRunDetailChanged();
    emit runDetailLoaded(runId);
}

QVariantList BacktestAnalyticsService::loadDailyReturns(const QString& runId) {
    QVariantList result;
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) return result;

    auto rows = db->executeQuery(
        "SELECT trade_date, raw_long_short, cost_adj_long_short, risk_adj_long_short, group_returns_json "
        "FROM alpha.factor_backtest_daily WHERE run_id = $1 ORDER BY trade_date",
        {astock::database::SqlParam{runId.toStdString()}});

    double cumRaw = 1.0, cumCost = 1.0, cumRisk = 1.0;
    for (const auto& row : rows.getRows()) {
        QVariantMap m;
        m["date"]         = QString::fromStdString(row.getString("trade_date"));
        m["rawReturn"]    = row.getDouble("raw_long_short");
        m["costReturn"]   = row.getDouble("cost_adj_long_short");
        m["riskReturn"]   = row.getDouble("risk_adj_long_short");

        cumRaw  *= (1.0 + m["rawReturn"].toDouble());
        cumCost *= (1.0 + m["costReturn"].toDouble());
        cumRisk *= (1.0 + m["riskReturn"].toDouble());
        m["cumRaw"]  = cumRaw;
        m["cumCost"] = cumCost;
        m["cumRisk"] = cumRisk;

        result.append(m);
    }
    return result;
}

QVariantList BacktestAnalyticsService::loadIcSeries(const QString& runId) {
    QVariantList result;
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) return result;

    auto rows = db->executeQuery(
        "SELECT trade_date, rank_ic FROM alpha.factor_backtest_ic_daily "
        "WHERE run_id = $1 ORDER BY trade_date",
        {astock::database::SqlParam{runId.toStdString()}});

    std::vector<double> buf;
    for (const auto& row : rows.getRows()) {
        QVariantMap m;
        m["date"] = QString::fromStdString(row.getString("trade_date"));
        m["ic"]   = row.getDouble("rank_ic");
        buf.push_back(m["ic"].toDouble());
        if (buf.size() > 20) buf.erase(buf.begin());
        double sum = 0; for (double v : buf) sum += v;
        m["icMA20"] = buf.empty() ? 0.0 : sum / buf.size();
        result.append(m);
    }
    return result;
}

QVariantList BacktestAnalyticsService::loadTrades(const QString& runId, int offset, int limit) {
    QVariantList result;
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) return result;

    std::ostringstream sql;
    sql << "SELECT trade_date, symbol, side, basket, price, cost_rate "
        << "FROM alpha.factor_backtest_trades WHERE run_id = $1 "
        << "ORDER BY trade_date, symbol LIMIT " << limit << " OFFSET " << offset;

    auto rows = db->executeQuery(sql.str(),
        {astock::database::SqlParam{runId.toStdString()}});

    for (const auto& row : rows.getRows()) {
        QVariantMap m;
        m["date"]     = QString::fromStdString(row.getString("trade_date"));
        m["symbol"]   = QString::fromStdString(row.getString("symbol"));
        m["side"]     = QString::fromStdString(row.getString("side"));
        m["basket"]   = QString::fromStdString(row.getString("basket"));
        m["price"]    = row.getDouble("price");
        m["costRate"] = row.getDouble("cost_rate");
        result.append(m);
    }
    return result;
}

QVariantList BacktestAnalyticsService::loadPeriods(const QString& runId) {
    QVariantList result;
    auto db = astock::database::NativePgConnectionPool::instance().getConnection();
    if (!db || !db->isOpen()) return result;

    auto rows = db->executeQuery(
        "SELECT * FROM alpha.factor_backtest_periods WHERE run_id = $1 ORDER BY trade_date",
        {astock::database::SqlParam{runId.toStdString()}});

    for (const auto& row : rows.getRows()) {
        QVariantMap m;
        m["date"]             = QString::fromStdString(row.getString("trade_date"));
        m["longHeld"]         = row.getInt("long_held");
        m["shortHeld"]        = row.getInt("short_held");
        m["longBought"]       = row.getInt("long_bought");
        m["longSold"]         = row.getInt("long_sold");
        m["shortBought"]      = row.getInt("short_bought");
        m["shortSold"]        = row.getInt("short_sold");
        m["longTurnover"]     = row.getDouble("long_turnover");
        m["shortTurnover"]    = row.getDouble("short_turnover");
        m["longRawReturn"]    = row.getDouble("long_raw_return");
        m["shortRawReturn"]   = row.getDouble("short_raw_return");
        m["strategyNetReturn"]= row.getDouble("strategy_net_return");
        result.append(m);
    }
    return result;
}

QVariantMap BacktestAnalyticsService::loadRunComparison(const QStringList& runIds) {
    QVariantMap result;
    for (const auto& rid : runIds) {
        loadRunDetail(rid);
        result[rid] = m_activeDetail;
    }
    return result;
}

QVariantMap BacktestAnalyticsService::aggregateStats(const QVariantList& data, const QString& field) {
    QVariantMap stats;
    if (data.isEmpty()) return stats;

    std::vector<double> vals;
    for (const auto& item : data) {
        QVariantMap m = item.toMap();
        double v = m.value(field).toDouble();
        if (std::isfinite(v)) vals.push_back(v);
    }
    if (vals.empty()) return stats;

    std::sort(vals.begin(), vals.end());
    double sum = 0;
    for (double v : vals) sum += v;
    double mean = sum / vals.size();

    double var = 0;
    for (double v : vals) { double d = v - mean; var += d * d; }
    var /= vals.size();

    stats["count"] = static_cast<int>(vals.size());
    stats["mean"]  = mean;
    stats["std"]   = std::sqrt(var);
    stats["min"]   = vals.front();
    stats["max"]   = vals.back();
    stats["p25"]   = vals[vals.size() / 4];
    stats["p50"]   = vals[vals.size() / 2];
    stats["p75"]   = vals[vals.size() * 3 / 4];
    stats["skew"]  = var > 1e-12 ? (vals.size() * std::pow(var, -1.5) *
        std::accumulate(vals.begin(), vals.end(), 0.0,
            [mean](double a, double v) { double d = v - mean; return a + d*d*d; }) / vals.size()) : 0.0;
    return stats;
}

QVariantList BacktestAnalyticsService::computeDistribution(const QVariantList& data, const QString& field, int bins) {
    QVariantList result;
    if (data.isEmpty() || bins <= 0) return result;

    std::vector<double> vals;
    for (const auto& item : data) {
        double v = item.toMap().value(field).toDouble();
        if (std::isfinite(v)) vals.push_back(v);
    }
    if (vals.empty()) return result;

    double vmin = *std::min_element(vals.begin(), vals.end());
    double vmax = *std::max_element(vals.begin(), vals.end());
    double width = (vmax - vmin) / bins;
    if (width < 1e-12) width = 1.0;

    std::vector<int> counts(bins, 0);
    for (double v : vals) {
        int idx = static_cast<int>((v - vmin) / width);
        if (idx >= bins) idx = bins - 1;
        if (idx < 0) idx = 0;
        counts[idx]++;
    }

    for (int i = 0; i < bins; ++i) {
        QVariantMap bin;
        bin["x"] = vmin + width * (i + 0.5);
        bin["count"] = counts[i];
        bin["rangeLow"]  = vmin + width * i;
        bin["rangeHigh"] = vmin + width * (i + 1);
        result.append(bin);
    }
    return result;
}

} // namespace ui::bridge
