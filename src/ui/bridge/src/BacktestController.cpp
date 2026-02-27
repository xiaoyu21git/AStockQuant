#include "BacktestController.h"

#include <QDebug>
#include <QDate>
#include <QDateTime>
#include <QPointer>

#include <QtConcurrent>

#include <chrono>
#include <cmath>
#include <algorithm>

#include "market/core/MarketData.h"
#include "BacktestEngine.h"
#include "Bar.h"

using astock::market::DataProviderFactory;
using astock::market::MarketDataManager;

namespace {

// 在后台线程中执行一次完整回测，返回是否成功以及结果
struct BacktestJobResult {
        bool ok{false};
        engine::BacktestResult result;
};

BacktestJobResult runBacktestJob(const QString& dataSource,
                                                                 const QString& symbolCode,
                                                                 const QString& startDateStr,
                                                                 const QString& endDateStr,
                                                                 double capital,
                                                                 const QString& strategyName,
                                                                 double maxPositionRatio,
                                                                 double commissionRate,
                                                                 double slippageRate,
                                                                 double minVolume)
{
        BacktestJobResult job;

        qDebug() << "[BacktestController] async run with dataSource =" << dataSource;

        auto resolveSymbolId = [](const QString& code) -> std::uint32_t {
                if (code == QStringLiteral("000001.SZ")) return 1u;
                if (code == QStringLiteral("000002.SZ")) return 2u;
                if (code == QStringLiteral("000300.SH")) return 3u;
                if (code == QStringLiteral("000905.SH")) return 4u;
                if (code == QStringLiteral("600000.SH")) return 5u;
                if (code == QStringLiteral("600036.SH")) return 6u;
                if (code == QStringLiteral("AAPL"))      return 7u;
                if (code == QStringLiteral("MSFT"))      return 8u;
                return 1u;
        };

        const std::uint32_t symbol_id = resolveSymbolId(symbolCode);

        std::uint64_t start_time = 0;
        std::uint64_t end_time   = 0;

        const bool hasDateRange = !startDateStr.isEmpty() && !endDateStr.isEmpty();
        if (hasDateRange) {
                QDate s = QDate::fromString(startDateStr, "yyyy-MM-dd");
                QDate e = QDate::fromString(endDateStr,   "yyyy-MM-dd");
                if (!s.isValid() || !e.isValid() || s > e) {
                        qWarning() << "[BacktestController] 无效的开始/结束日期:" << startDateStr << endDateStr;
                        return job;
                }

                QDateTime sdt(s, QTime(0, 0, 0), Qt::LocalTime);
                QDateTime edt(e, QTime(23, 59, 59), Qt::LocalTime);
                start_time = static_cast<std::uint64_t>(sdt.toSecsSinceEpoch());
                end_time   = static_cast<std::uint64_t>(edt.toSecsSinceEpoch());
        } else {
                auto now_sec = static_cast<std::uint64_t>(
                                std::chrono::duration_cast<std::chrono::seconds>(
                                                std::chrono::system_clock::now().time_since_epoch()).count());
                end_time   = now_sec;
                start_time = (end_time > 3600 ? end_time - 3600 : 0);
        }

        std::vector<domain::model::Bar> bars;

        auto generateSimulatedBars = [&](std::uint64_t &win_start,
                                                                         std::uint64_t &win_end,
                                                                         std::vector<domain::model::Bar> &outBars) {
                qDebug() << "[BacktestController] using simulated/local data source";

                if (win_end <= win_start) {
                        auto now_sec = static_cast<std::uint64_t>(
                                        std::chrono::duration_cast<std::chrono::seconds>(
                                                        std::chrono::system_clock::now().time_since_epoch()).count());
                        win_end   = now_sec;
                        win_start = (win_end > 7200 ? win_end - 7200 : 0);
                }

                const std::uint64_t total_sec = win_end - win_start;
                const std::size_t max_bars = 200;
                std::size_t step_sec = total_sec / max_bars;
                if (step_sec == 0)
                        step_sec = 60;

                double price = 100.0;
                outBars.clear();
                outBars.reserve(max_bars);

                std::size_t idx = 0;
                for (std::uint64_t ts = win_start; ts < win_end && idx < max_bars; ts += step_sec, ++idx) {
                        double wave  = std::sin(static_cast<double>(idx) / 10.0) * 0.5;
                        double trend = 0.05;

                        double open  = price;
                        double close = price + trend + wave;
                        double high  = (std::max)(open, close) + 0.1;
                        double low   = (std::min)(open, close) - 0.1;

                        domain::model::Bar b;
                        b.symbol = symbolCode.toStdString();
                        b.time   = static_cast<std::int64_t>(ts) * 1000;
                        b.open   = open;
                        b.high   = high;
                        b.low    = low;
                        b.close  = close;
                        b.volume = 1000.0 + static_cast<double>(idx) * 10.0;
                        outBars.push_back(b);

                        price = close;
                }

                qDebug() << "[BacktestController] generated" << static_cast<int>(outBars.size())
                                 << "simulated bars between" << static_cast<qlonglong>(win_start)
                                 << "and" << static_cast<qlonglong>(win_end);
        };

        if (dataSource != QStringLiteral("模拟数据")) {
                std::string config =
                                "host=127.0.0.1;port=3306;database=astock_quant;"
                                "username=root;password=123456a;charset=utf8mb4";

                auto &manager = MarketDataManager::instance();

                static bool initialized = false;
                if (!initialized) {
                        bool ok = manager.initialize(DataProviderFactory::ProviderType::DATABASE, config);
                        if (!ok) {
                                qWarning() << "[BacktestController] Failed to initialize MarketDataManager with DATABASE provider";
                                return job;
                        }
                        initialized = true;
                }

                std::uint16_t period    = 60;
                std::size_t   limit     = 500;

                auto batch = manager.get_history_klines(symbol_id, period, start_time, end_time, limit);

                qDebug() << "[BacktestController] fetched" << static_cast<int>(batch.size())
                                 << "KLines from database for symbol_id" << symbol_id;

                bars.reserve(batch.size());
                for (const auto &k : batch) {
                        domain::model::Bar b;
                        b.symbol = symbolCode.toStdString();
                        b.time   = static_cast<std::int64_t>(k.timestamp) * 1000;
                        b.open   = k.open;
                        b.high   = k.high;
                        b.low    = k.low;
                        b.close  = k.close;
                        b.volume = k.volume;
                        bars.push_back(b);
                }
                if (bars.empty()) {
                        qWarning() << "[BacktestController] No KLines from database, falling back to simulated data";
                        generateSimulatedBars(start_time, end_time, bars);
                }
        } else {
                generateSimulatedBars(start_time, end_time, bars);
        }

        if (bars.empty()) {
                qWarning() << "[BacktestController] No bars available for backtest";
                return job;
        }

        engine::BacktestEngine be;
        job.result = be.run(bars,
                            capital,
                            strategyName.toStdString(),
                            maxPositionRatio,
                            commissionRate,
                            slippageRate,
                            minVolume);
        job.ok = true;
        return job;
}

} // namespace

void BacktestController::setDataSource(const QString& source) {
        if (m_dataSource == source)
                return;
        m_dataSource = source;
        emit dataSourceChanged();
}

void BacktestController::setSymbol(const QString& s) {
        if (m_symbol == s)
                return;
        m_symbol = s;
        emit symbolChanged();
}

void BacktestController::setStartDate(const QString& date) {
        if (m_startDate == date)
                return;
        m_startDate = date;
        emit startDateChanged();
}

void BacktestController::setEndDate(const QString& date) {
        if (m_endDate == date)
                return;
        m_endDate = date;
        emit endDateChanged();
}

void BacktestController::setCapital(double c) {
        if (qFuzzyCompare(m_capital, c))
                return;
        m_capital = c;
        emit capitalChanged();
}

void BacktestController::setStrategy(const QString& s) {
        if (m_strategy == s)
                return;
        m_strategy = s;
        emit strategyChanged();
}

void BacktestController::setMaxPositionRatio(double r) {
        // 简单约束到 [0, 1]
        if (r < 0.0)
                r = 0.0;
        if (r > 1.0)
                r = 1.0;
        if (qFuzzyCompare(m_maxPositionRatio, r))
                return;
        m_maxPositionRatio = r;
        emit maxPositionRatioChanged();
}

void BacktestController::run() {
        if (m_running) {
                qWarning() << "[BacktestController] Backtest already running, ignore new request";
                return;
        }

        // 拍快照，避免回测过程中 UI 修改属性导致混乱
        const QString dataSource  = m_dataSource;
        const QString symbolCode  = m_symbol.isEmpty() ? QStringLiteral("000001.SZ") : m_symbol;
        const QString startDate   = m_startDate;
        const QString endDate     = m_endDate;
        const double  capital     = m_capital;
        const QString strategy    = m_strategy;
        const double  maxRatio    = m_maxPositionRatio;
        const double  commission  = m_commissionRate;
        const double  slippage    = m_slippageRate;
        const double  minVol      = m_minVolume;

        m_running = true;
        emit runningChanged();

        QPointer<BacktestController> self(this);

        QtConcurrent::run([self, dataSource, symbolCode, startDate, endDate, capital, strategy, maxRatio, commission, slippage, minVol]() {
                if (!self)
                        return;

                BacktestJobResult job = runBacktestJob(dataSource, symbolCode,
                                                       startDate, endDate,
                                                       capital, strategy,
                                                       maxRatio,
                                                       commission,
                                                       slippage,
                                                       minVol);

                QMetaObject::invokeMethod(self, [self, job]() mutable {
                        if (!self)
                                return;

                        if (!job.ok) {
                                self->m_running = false;
                                emit self->runningChanged();
                                return;
                        }

                        // auto* tradeModel  = GlobalModels::tradeModel();
                        // auto* equityModel = GlobalModels::equityModel();
                        if (!tradeModel || !equityModel) {
                                qWarning() << "[BacktestController] Global trade/equity model is null";
                                self->m_running = false;
                                emit self->runningChanged();
                                return;
                        }

                        tradeModel->clear();
                        equityModel->clear();

                        for (const auto &tr : job.result.trades()) {
                                const QString symbol = QString::fromStdString(tr.symbol);
                                const double  price  = (tr.exit_price > 0.0 ? tr.exit_price : tr.entry_price);
                                const double  volume = tr.quantity;
                                const QString side   = (tr.direction == "BUY" ? QStringLiteral("买入") : QStringLiteral("卖出"));
                                // 将每笔回测交易的收益一起传入模型，便于前端统计
                                tradeModel->addTrade(symbol, price, volume, side, tr.profit);
                        }

                        for (const auto &pt : job.result.equity_curve()) {
                                const auto sec = pt.timestamp.to_seconds();
                                QString timeStr = QString::number(static_cast<qlonglong>(sec));
                                equityModel->addPoint(timeStr, pt.equity);
                        }

                        self->m_running = false;
                        emit self->runningChanged();
                }, Qt::QueuedConnection);
        });
}

#include "moc_BacktestController.cpp"