// HighPositionModel.cpp
#include "HighPositionModel.h"

#include <QDebug>
#include <QPointer>

#include <QtConcurrent>

#include <algorithm>
#include <ctime>

#include "database/DatabaseConfig.h"
#include "database/ConnectionPool.h"
#include "database/MarketDataRepository.h"
#include "database/MarketDataModels.h"

// 避免 Windows 头文件定义的 min/max 宏与 std::min/std::max 冲突
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

using astock::database::ConnectionPool;
using astock::database::DatabaseConfig;
using astock::database::MarketDataRepository;
using astock::database::DailyBar;
using astock::database::MoneyFlowDaily;
using astock::database::DragonTigerRecord;

HighPositionModel::HighPositionModel(QObject* parent)
    : QAbstractListModel(parent)
{
    m_roleNames[SymbolRole]          = "symbol";
    m_roleNames[NameRole]           = "name";
    m_roleNames[LastPriceRole]      = "lastPrice";
    m_roleNames[PreviousChangeRole] = "previousChange";
    m_roleNames[RiseFromLowRole]    = "riseFromLow";
    m_roleNames[DrawdownFromHighRole] = "drawdownFromHigh";
    m_roleNames[ExtremeGapRole]     = "extremeGap";
    m_roleNames[NetMainInflow5dRole] = "netMainInflow5d";
    m_roleNames[RecentOnLhbRole]     = "recentOnLhb";
}

int HighPositionModel::rowCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return m_rows.size();
}

QVariant HighPositionModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const HighRow& row = m_rows.at(index.row());

    switch (role) {
    case SymbolRole:           return row.symbol;
    case NameRole:             return row.name;
    case LastPriceRole:        return row.lastPrice;
    case PreviousChangeRole:   return row.previousChange;
    case RiseFromLowRole:      return row.riseFromLow;
    case DrawdownFromHighRole: return row.drawdownFromHigh;
    case ExtremeGapRole:       return row.extremeGap;
    case NetMainInflow5dRole:  return row.netMainInflow5d;
    case RecentOnLhbRole:      return row.recentOnLhb;
    case Qt::DisplayRole:
        return QStringLiteral("%1 %2").arg(row.symbol).arg(row.lastPrice, 0, 'f', 2);
    default:
        return {};
    }
}

QHash<int, QByteArray> HighPositionModel::roleNames() const
{
    return m_roleNames;
}

void HighPositionModel::refresh(int lookbackDays)
{
    if (m_busy) {
        qWarning() << "[HighPositionModel] refresh already running, ignore";
        return;
    }

    m_busy = true;
    emit busyChanged();
    if (lookbackDays <= 0)
        lookbackDays = 60;

    QPointer<HighPositionModel> self(this);

    QtConcurrent::run([self, lookbackDays]() {
        if (!self)
            return;

        DatabaseConfig config;
        config.host     = "127.0.0.1";
        config.port     = 3306;
        config.database = "astock_quant";
        config.username = "root";
        config.password = "123456a";

        if (!config.validate()) {
            qWarning() << "[HighPositionModel] Invalid database config";
            QMetaObject::invokeMethod(self, [self]() {
                if (!self) return;
                self->m_busy = false;
                emit self->busyChanged();
            }, Qt::QueuedConnection);
            return;
        }

        auto pool = std::make_shared<ConnectionPool>(config);
        if (!pool->initialize()) {
            qWarning() << "[HighPositionModel] Failed to initialize ConnectionPool";
            QMetaObject::invokeMethod(self, [self]() {
                if (!self) return;
                self->m_busy = false;
                emit self->busyChanged();
            }, Qt::QueuedConnection);
            return;
        }

        auto repo = std::make_shared<MarketDataRepository>(pool);

        // 获取所有活跃标的（数据库中状态为 'ACTIVE'）
        auto symbols = repo->getAllSymbols(std::nullopt, "ACTIVE");

        struct Candidate {
            std::string symbol;
            std::string name;
            double lastClose{0.0};
            double rise{0.0};
            double draw{0.0};
            double recentChange{0.0};   // 最新一个交易日涨幅（%）
            double prevChange{0.0};     // 上一个交易日涨幅（%）
            double recentRise20{0.0};
            double volumeSpike{0.0};
            double netMainInflow5d{0.0};
            bool   recentOnLhb{false};
        };

        std::vector<Candidate> candidates;

        for (const auto& sym : symbols) {
            auto latestOpt = repo->getLatestBar(sym.symbol);
            if (!latestOpt.has_value())
                continue;

            std::time_t end_ts   = latestOpt->trade_date;
            std::time_t start_ts = end_ts - static_cast<std::time_t>(lookbackDays) * 24 * 60 * 60;

            auto bars = repo->getDailyBars(sym.symbol, start_ts, end_ts);
            if (bars.size() < 15)
                continue;

            double minClose = 0.0;
            double maxClose = 0.0;
            double lastClose = 0.0;
            double lastChangePct = 0.0;   // 最新一个交易日涨幅（%）
            double prevChangePct = 0.0;   // 上一个交易日涨幅（%）

            const std::size_t n = bars.size();
            const std::size_t window20 = std::min<std::size_t>(20, n);

            bool first = true;
            for (const DailyBar& bar : bars) {
                if (bar.close <= 0.0 || bar.pre_close <= 0.0)
                    continue;
                if (first) {
                    minClose = maxClose = bar.close;
                    first = false;
                } else {
                    minClose = std::min(minClose, bar.close);
                    maxClose = std::max(maxClose, bar.close);
                }
                lastClose = bar.close;

                // 用收盘价和前收盘价即时计算当日涨跌幅，而不是依赖
                // daily_bar.change_pct 字段（目前导入的数据里该字段为 0）
                double dayChangePct = (bar.close / bar.pre_close - 1.0) * 100.0;

                // 在更新最新涨幅前，先把旧值保存为“前一日涨幅”
                prevChangePct = lastChangePct;
                lastChangePct = dayChangePct;
            }

            if (first || minClose <= 0.0 || maxClose <= 0.0)
                continue;

            double recentRise20 = 0.0;
            if (window20 > 1) {
                const DailyBar& refBar = bars[n - window20];
                if (refBar.close > 0.0)
                    recentRise20 = lastClose / refBar.close - 1.0;
            }

            double avgVol3 = 0.0;
            double avgVol10 = 0.0;
            std::size_t cnt3 = 0, cnt10 = 0;
            for (std::size_t i = 0; i < n; ++i) {
                const auto& b = bars[i];
                if (b.volume <= 0.0)
                    continue;
                if (i >= n - 3) {
                    avgVol3 += b.volume;
                    ++cnt3;
                }
                if (i >= n - 13 && i < n - 3) {
                    avgVol10 += b.volume;
                    ++cnt10;
                }
            }
            if (cnt3 > 0) avgVol3 /= static_cast<double>(cnt3);
            if (cnt10 > 0) avgVol10 /= static_cast<double>(cnt10);
            double volumeSpike = (avgVol10 > 0.0 ? avgVol3 / avgVol10 : 0.0);

            double rise  = lastClose / minClose - 1.0;
            double draw  = 1.0 - lastClose / maxClose;

            // 近5日主力净流入
            double netMainInflow5d = 0.0;
            {
                auto flows = repo->getMoneyFlowDaily(sym.symbol, start_ts, end_ts);
                if (!flows.empty()) {
                    std::time_t fiveDayStart = end_ts - static_cast<std::time_t>(4) * 24 * 60 * 60;
                    for (const MoneyFlowDaily& mf : flows) {
                        if (mf.trade_date >= fiveDayStart && mf.trade_date <= end_ts) {
                            netMainInflow5d += mf.net_main_inflow;
                        }
                    }
                }
            }

            // 最近10日是否上过龙虎榜
            bool recentOnLhb = false;
            {
                std::time_t lhbStart = end_ts - static_cast<std::time_t>(9) * 24 * 60 * 60;
                auto lhbRecords = repo->getDragonTigerRecords(sym.symbol, lhbStart, end_ts);
                recentOnLhb = !lhbRecords.empty();
            }

            Candidate c;
            c.symbol       = sym.symbol;
            c.name         = sym.name;
            c.lastClose    = lastClose;
            c.rise         = rise;
            c.draw         = draw;
            c.recentChange = lastChangePct;
            c.prevChange   = prevChangePct;
            c.recentRise20 = recentRise20;
            c.volumeSpike  = volumeSpike;
            c.netMainInflow5d = netMainInflow5d;
            c.recentOnLhb     = recentOnLhb;
            candidates.push_back(std::move(c));
        }

        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& a, const Candidate& b) {
                      if (a.recentRise20 == b.recentRise20)
                          return a.rise > b.rise;
                      return a.recentRise20 > b.recentRise20;
                  });

        const std::size_t maxCount = 50;
        std::size_t count = 0;
        QVector<HighRow> rows;
        rows.reserve(static_cast<int>(std::min(maxCount, candidates.size())));

        for (const auto& c : candidates) {
            if (c.recentRise20 <= 0.25)
                continue;
            if (c.rise <= 0.40)
                continue;
            if (c.draw >= 0.15)
                continue;
            if (c.recentChange < -3.0)
                continue;
            if (c.volumeSpike <= 1.5)
                continue;
            // 剔除近5日主力净流入为负的标的
            if (c.netMainInflow5d < 0.0)
                continue;

            const double severeThreshold = 0.50;
            double extremeGap = severeThreshold - c.recentRise20;
            if (extremeGap < 0.0)
                extremeGap = 0.0;

            HighRow row;
            row.symbol           = QString::fromStdString(c.symbol);
            row.name             = QString::fromStdString(c.name);
            row.lastPrice        = c.lastClose;
            // previousChange 显示“上一个交易日”的涨幅
            row.previousChange   = c.prevChange;
            row.riseFromLow      = c.rise;
            row.drawdownFromHigh = c.draw;
            row.extremeGap       = extremeGap;
            row.netMainInflow5d  = c.netMainInflow5d;
            row.recentOnLhb      = c.recentOnLhb;
            rows.push_back(row);
            if (++count >= maxCount)
                break;
        }

        const int scanned   = static_cast<int>(symbols.size());
        const int candCount = static_cast<int>(candidates.size());
        const int selected  = rows.size();

        QMetaObject::invokeMethod(self, [self, rows, scanned, candCount, selected]() mutable {
            if (!self)
                return;

            self->beginResetModel();
            self->m_rows = std::move(rows);
            qDebug() << "[HighPositionModel] scanned" << scanned
                     << "symbols, candidates:" << candCount
                     << "selected:" << selected;
            self->endResetModel();
            emit self->countChanged();

            self->m_busy = false;
            emit self->busyChanged();
        }, Qt::QueuedConnection);
    });
}

#include "moc_HighPositionModel.cpp"
