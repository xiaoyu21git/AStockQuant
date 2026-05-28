#include "StrategyBacktestRuntimeAccess.h"

#include "../../../application/backtest/include/StrategyBacktestRuntimeHost.h"
#include "../../../domain/backtest/include/HistoricalMarketDataCache.h"
#include "DatabaseConnectionManager.h"
#include "FactorService.h"
#include "foundation/config/ConfigManager.hpp"
#include "market/core/MarketData.h"

#include <QHash>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTime>
#include <QDebug>

#include <memory>
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include <cmath>

namespace {

using application::backtest::StrategyBacktestRuntimeHost;
using application::backtest::StrategyBacktestEntryService;
using domain::backtest::IFactorSnapshotProvider;
using domain::backtest::IndexedTradingDayRangeResolver;
using domain::backtest::TradingDayTimeRange;
using domain::backtest::strategy_engine::CandidateCount;
using domain::backtest::strategy_engine::FactorId;
using domain::backtest::strategy_engine::FactorIdList;
using domain::backtest::strategy_engine::FactorSnapshot;
using domain::backtest::strategy_engine::FactorSnapshotList;
using domain::backtest::strategy_engine::OverlayBindingScopeId;
using domain::backtest::strategy_engine::ScoreValue;
using domain::backtest::strategy_engine::SymbolId;
using domain::backtest::strategy_engine::SymbolIdList;
using domain::backtest::strategy_engine::TradingDayIndex;

constexpr std::uint32_t kStrategyBacktestMarketDataPeriod = 86400U;
constexpr std::uint32_t kStrategyBacktestWarmupDayCount = 1U;

struct LoadedTradingCalendar final {
    QVector<QDate> tradingDates;
    std::vector<TradingDayTimeRange> ranges;
};

QStringList candidateRepoRoots()
{
    QStringList candidates;
    candidates << QDir::currentPath();

    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        candidates << appDir;
        candidates << QDir(appDir).absoluteFilePath(QStringLiteral(".."));
        candidates << QDir(appDir).absoluteFilePath(QStringLiteral("../.."));
        candidates << QDir(appDir).absoluteFilePath(QStringLiteral("../../.."));
    }

    candidates.removeDuplicates();
    return candidates;
}

QString resolveRepoRoot()
{
    for (const QString& candidate : candidateRepoRoots()) {
        const QFileInfo scriptInfo(QDir(candidate).absoluteFilePath(QStringLiteral("tools/trading_day_utils.py")));
        if (scriptInfo.exists() && scriptInfo.isFile()) {
            return QDir(candidate).absolutePath();
        }
    }
    return {};
}

QString resolvePythonExecutable(const QString& repoRoot)
{
    if (!repoRoot.isEmpty()) {
        const QString windowsVenv = QDir(repoRoot).absoluteFilePath(QStringLiteral(".venv/Scripts/python.exe"));
        if (QFileInfo::exists(windowsVenv)) {
            return windowsVenv;
        }

        const QString unixVenv = QDir(repoRoot).absoluteFilePath(QStringLiteral(".venv/bin/python"));
        if (QFileInfo::exists(unixVenv)) {
            return unixVenv;
        }
    }

    return QStringLiteral("python");
}

LoadedTradingCalendar loadTradingCalendar(QString* error)
{
    const QString repoRoot = resolveRepoRoot();
    if (repoRoot.isEmpty()) {
        if (error) {
            *error = QStringLiteral("未找到项目根目录，无法加载策略回测交易日历");
        }
        return {};
    }

    static const QString script = QStringLiteral(
        "import json\n"
        "from tools.trading_day_utils import get_trade_calendar\n"
        "print(json.dumps([trade_date.isoformat() for trade_date in get_trade_calendar()], ensure_ascii=True))\n");

    QProcess process;
    process.setWorkingDirectory(repoRoot);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString existingPythonPath = environment.value(QStringLiteral("PYTHONPATH"));
    environment.insert(
        QStringLiteral("PYTHONPATH"),
        existingPythonPath.isEmpty()
            ? repoRoot
            : (repoRoot + QDir::listSeparator() + existingPythonPath));
    process.setProcessEnvironment(environment);

    process.start(resolvePythonExecutable(repoRoot), {QStringLiteral("-c"), script});
    if (!process.waitForStarted(2000)) {
        if (error) {
            *error = QStringLiteral("无法启动策略回测交易日历 Python 进程");
        }
        return {};
    }

    if (!process.waitForFinished(12000)) {
        process.kill();
        process.waitForFinished(1000);
        if (error) {
            *error = QStringLiteral("策略回测交易日历查询超时");
        }
        return {};
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            const QString standardError = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
            *error = standardError.isEmpty()
                ? QStringLiteral("策略回测交易日历进程退出失败，code=%1").arg(process.exitCode())
                : standardError;
        }
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(process.readAllStandardOutput(), &parseError);
    if (!document.isArray()) {
        if (error) {
            *error = QStringLiteral("策略回测交易日历返回了无效 JSON: %1").arg(parseError.errorString());
        }
        return {};
    }

    LoadedTradingCalendar calendar;
    calendar.ranges.reserve(static_cast<std::size_t>(document.array().size()));
    for (const QJsonValue& value : document.array()) {
        if (!value.isString()) {
            if (error) {
                *error = QStringLiteral("策略回测交易日历包含非字符串日期项");
            }
            return {};
        }

        const QDate tradingDate = QDate::fromString(value.toString(), Qt::ISODate);
        if (!tradingDate.isValid()) {
            if (error) {
                *error = QStringLiteral("策略回测交易日历包含无效日期: %1").arg(value.toString());
            }
            return {};
        }

        const QDateTime startOfDay(tradingDate, QTime(0, 0, 0), Qt::LocalTime);
        const QDateTime endOfDay(tradingDate, QTime(23, 59, 59), Qt::LocalTime);
        if (!startOfDay.isValid() || !endOfDay.isValid()) {
            if (error) {
                *error = QStringLiteral("策略回测交易日历无法转换交易日时间窗: %1").arg(tradingDate.toString(Qt::ISODate));
            }
            return {};
        }

        calendar.tradingDates.push_back(tradingDate);
        calendar.ranges.push_back(TradingDayTimeRange{static_cast<std::uint64_t>(startOfDay.toSecsSinceEpoch()),
                                                      static_cast<std::uint64_t>(endOfDay.toSecsSinceEpoch())});
    }

    return calendar;
}

class BridgeBacktestFactorSnapshotProvider final : public IFactorSnapshotProvider,
                                                  public application::backtest::IOverlayFactorBindingSink {
public:
    explicit BridgeBacktestFactorSnapshotProvider(QVector<QDate> tradingDates)
        : tradingDates_(std::move(tradingDates))
    {
    }

    void bindOverlayFactors(const OverlayBindingScopeId overlayBindingScopeId,
                            const QVector<domain::strategy::FactorId>& configuredFactorIds,
                            const application::backtest::FactorIdList& resolvedFactorIds) override
    {
        const int bindCount = std::min(static_cast<int>(configuredFactorIds.size()),
                                       static_cast<int>(resolvedFactorIds.size()));
        QMutexLocker locker(&mutex_);
        for (int index = 0; index < bindCount; ++index) {
            const QString rawFactorId = configuredFactorIds.at(index).text().trimmed();
            const FactorId resolvedFactorId = resolvedFactorIds.values().at(static_cast<std::size_t>(index));
            if (!resolvedFactorId.isValid() || rawFactorId.isEmpty()) {
                continue;
            }

            factorBindings_.insert(buildBindingKey(overlayBindingScopeId, resolvedFactorId), rawFactorId);
        }
    }

    [[nodiscard]] FactorSnapshotList loadSnapshots(const OverlayBindingScopeId overlayBindingScopeId,
                                                   const TradingDayIndex tradingDay,
                                                   const SymbolIdList& symbols,
                                                   const FactorIdList& factorIds) const override
    {
        FactorSnapshotList snapshots;
        if (!tradingDay.isValid() || symbols.empty() || factorIds.empty()) {
            return snapshots;
        }

        const QString tradeDate = tradingDateText(tradingDay);
        if (tradeDate.isEmpty()) {
            return snapshots;
        }

        const QHash<qulonglong, QString> symbolCodes = resolveSymbolCodes(symbols);
        if (symbolCodes.size() != static_cast<int>(symbols.size())) {
            return {};
        }

        QStringList requestedSymbols;
        requestedSymbols.reserve(symbolCodes.size());
        for (const SymbolId symbolId : symbols.values()) {
            const QString symbolCode = symbolCodes.value(static_cast<qulonglong>(symbolId.value())).trimmed().toUpper();
            if (symbolCode.isEmpty()) {
                return {};
            }
            requestedSymbols.append(symbolCode);
        }

        FactorService* factorService = FactorService::instance();
        if (!factorService) {
            return {};
        }
        if (!factorService->isInitialized()) {
            factorService->initialize();
        }

        if (!populateFactorValueCache(overlayBindingScopeId,
                                      tradingDay,
                                      symbols,
                                      factorIds,
                                      tradeDate,
                                      requestedSymbols,
                                      factorService)) {
            return {};
        }

        for (const FactorId factorId : factorIds.values()) {
            const QVariantMap factorValues = cachedFactorValues(overlayBindingScopeId,
                                                                tradingDay,
                                                                factorId,
                                                                symbols);
            if (factorValues.value(QStringLiteral("status")).toString().trimmed() != QStringLiteral("success")) {
                return {};
            }

            const QVariantMap stockValues = factorValues.value(QStringLiteral("stockValues")).toMap();
            for (const SymbolId symbolId : symbols.values()) {
                const QString symbolCode = symbolCodes.value(static_cast<qulonglong>(symbolId.value())).trimmed().toUpper();
                if (symbolCode.isEmpty()) {
                    return {};
                }

                bool ok = false;
                const double value = stockValues.value(symbolCode).toDouble(&ok);
                if (!ok || !std::isfinite(value)) {
                    continue;
                }

                snapshots.add(FactorSnapshot{factorId,
                                             symbolId,
                                             ScoreValue(value),
                                             tradingDay});
            }
        }

        return snapshots;
    }

private:
    [[nodiscard]] bool populateFactorValueCache(const OverlayBindingScopeId overlayBindingScopeId,
                                                const TradingDayIndex tradingDay,
                                                const SymbolIdList& symbols,
                                                const FactorIdList& factorIds,
                                                const QString& tradeDate,
                                                const QStringList& requestedSymbols,
                                                FactorService* factorService) const
    {
        QVector<FactorId> missingFactorIds;
        QStringList missingRawFactorIds;
        missingFactorIds.reserve(factorIds.size());
        missingRawFactorIds.reserve(static_cast<int>(factorIds.size()));

        for (const FactorId factorId : factorIds.values()) {
            const qulonglong cacheKey = buildFactorSnapshotCacheKey(overlayBindingScopeId,
                                                                    tradingDay,
                                                                    factorId,
                                                                    symbols);
            {
                QMutexLocker locker(&mutex_);
                if (factorValueCache_.contains(cacheKey)) {
                    continue;
                }
            }

            const QString rawFactorId = rawFactorIdText(overlayBindingScopeId, factorId);
            if (rawFactorId.isEmpty()) {
                return false;
            }

            missingFactorIds.push_back(factorId);
            missingRawFactorIds.push_back(rawFactorId);
        }

        if (missingRawFactorIds.isEmpty()) {
            return true;
        }

        const QVariantMap batchFactorValues = factorService->getFactorValuesForSymbolsBatch(missingRawFactorIds,
                                                                                             tradeDate,
                                                                                             requestedSymbols);
        if (batchFactorValues.value(QStringLiteral("status")).toString().trimmed() != QStringLiteral("success")) {
            return false;
        }

        const QVariantMap factorValuesById = batchFactorValues.value(QStringLiteral("factorValues")).toMap();
        QMutexLocker locker(&mutex_);
        for (int index = 0; index < missingFactorIds.size(); ++index) {
            const QString rawFactorId = missingRawFactorIds.at(index);
            if (!factorValuesById.contains(rawFactorId)) {
                return false;
            }

            const QVariantMap factorValues = factorValuesById.value(rawFactorId).toMap();
            if (factorValues.value(QStringLiteral("status")).toString().trimmed() != QStringLiteral("success")) {
                return false;
            }

            factorValueCache_.insert(buildFactorSnapshotCacheKey(overlayBindingScopeId,
                                                                 tradingDay,
                                                                 missingFactorIds.at(index),
                                                                 symbols),
                                     factorValues);
        }

        return true;
    }

    [[nodiscard]] QVariantMap cachedFactorValues(const OverlayBindingScopeId overlayBindingScopeId,
                                                 const TradingDayIndex tradingDay,
                                                 const FactorId factorId,
                                                 const SymbolIdList& symbols) const
    {
        const qulonglong cacheKey = buildFactorSnapshotCacheKey(overlayBindingScopeId,
                                                                tradingDay,
                                                                factorId,
                                                                symbols);
        QMutexLocker locker(&mutex_);
        return factorValueCache_.value(cacheKey);
    }

    [[nodiscard]] static qulonglong buildBindingKey(const OverlayBindingScopeId overlayBindingScopeId,
                                                    const FactorId factorId)
    {
        return (static_cast<qulonglong>(overlayBindingScopeId.value()) << 32)
            ^ static_cast<qulonglong>(factorId.value());
    }

    [[nodiscard]] static qulonglong buildFactorSnapshotCacheKey(const OverlayBindingScopeId overlayBindingScopeId,
                                                                const TradingDayIndex tradingDay,
                                                                const FactorId factorId,
                                                                const SymbolIdList& symbols)
    {
        qulonglong hashValue = 1469598103934665603ULL;

        hashValue ^= static_cast<qulonglong>(overlayBindingScopeId.value());
        hashValue *= 1099511628211ULL;

        hashValue ^= static_cast<qulonglong>(static_cast<quint32>(tradingDay.value()));
        hashValue *= 1099511628211ULL;

        hashValue ^= static_cast<qulonglong>(factorId.value());
        hashValue *= 1099511628211ULL;

        for (const SymbolId symbolId : symbols.values()) {
            hashValue ^= static_cast<qulonglong>(symbolId.value());
            hashValue *= 1099511628211ULL;
        }

        return hashValue == 0ULL ? 1ULL : hashValue;
    }

    [[nodiscard]] QString tradingDateText(const TradingDayIndex tradingDay) const
    {
        const int index = tradingDay.value();
        if (index < 0 || index >= tradingDates_.size()) {
            return {};
        }
        return tradingDates_.at(index).toString(Qt::ISODate);
    }

    [[nodiscard]] QString rawFactorIdText(const OverlayBindingScopeId overlayBindingScopeId,
                                          const FactorId factorId) const
    {
        QMutexLocker locker(&mutex_);
        return factorBindings_.value(buildBindingKey(overlayBindingScopeId, factorId)).trimmed();
    }

    [[nodiscard]] QHash<qulonglong, QString> resolveSymbolCodes(const SymbolIdList& symbols) const
    {
        QHash<qulonglong, QString> resolved;
        QVector<qulonglong> missing;

        {
            QMutexLocker locker(&mutex_);
            for (const SymbolId symbolId : symbols.values()) {
                const qulonglong key = static_cast<qulonglong>(symbolId.value());
                const QString cached = symbolCodeCache_.value(key).trimmed().toUpper();
                if (cached.isEmpty()) {
                    missing.push_back(key);
                    continue;
                }
                resolved.insert(key, cached);
            }
        }

        if (!missing.isEmpty()) {
            auto database = astock::database::DatabaseConnectionManager::instance().getDatabase();
            if (!database) {
                return {};
            }

            QStringList placeholders;
            std::map<QString, QVariant> params;
            placeholders.reserve(missing.size());
            for (int index = 0; index < missing.size(); ++index) {
                const QString placeholder = QStringLiteral(":symbol_id_%1").arg(index);
                placeholders.push_back(placeholder);
                params[placeholder] = QVariant::fromValue<qulonglong>(missing.at(index));
            }

            const QString sql = QStringLiteral(
                "SELECT symbol_id, symbol FROM symbol_info WHERE symbol_id IN (%1)")
                .arg(placeholders.join(QStringLiteral(", ")));
            const auto queryResult = database->executeQuery(sql, params);

            QMutexLocker locker(&mutex_);
            for (size_t rowIndex = 0; rowIndex < queryResult.rowCount(); ++rowIndex) {
                const auto& row = queryResult.getRow(rowIndex);
                const qulonglong symbolId = row.getValueAs<qulonglong>(QStringLiteral("symbol_id"), 0ULL);
                const QString symbolCode = row.getString(QStringLiteral("symbol")).trimmed().toUpper();
                if (symbolId == 0ULL || symbolCode.isEmpty()) {
                    continue;
                }
                symbolCodeCache_.insert(symbolId, symbolCode);
                resolved.insert(symbolId, symbolCode);
            }
        }

        return resolved;
    }

    QVector<QDate> tradingDates_;
    mutable QMutex mutex_;
    QHash<qulonglong, QString> factorBindings_;
    mutable QHash<qulonglong, QString> symbolCodeCache_;
    mutable QHash<qulonglong, QVariantMap> factorValueCache_;
};

std::string buildDatabaseProviderConfig()
{
    std::string host = "127.0.0.1";
    int port = 3306;
    std::string database = "astock_quant";
    std::string user = "root";
    std::string password = "123456a";

    try {
        auto& configManager = foundation::config::ConfigManager::instance();
        host = configManager.get_app_config_string("mysql.host", host);
        port = configManager.get_app_config_int("mysql.port", port);
        database = configManager.get_app_config_string("mysql.database", database);
        user = configManager.get_app_config_string("mysql.user", user);
        password = configManager.get_app_config_string("mysql.password", password);
    } catch (const std::exception& error) {
        qWarning() << "StrategyBacktestRuntimeAccess: load mysql config failed," << error.what();
    }

    std::ostringstream stream;
    stream << "host=" << host
           << ";port=" << port
           << ";database=" << database
           << ";user=" << user
           << ";password=" << password
           << ";charset=utf8mb4";
    return stream.str();
}

struct RuntimeStorage final {
    QMutex mutex;
    std::shared_ptr<astock::market::IDataProvider> dataProvider;
    std::unique_ptr<IndexedTradingDayRangeResolver> tradingDayRangeResolver;
    std::unique_ptr<BridgeBacktestFactorSnapshotProvider> factorSnapshotProvider;
    std::unique_ptr<StrategyBacktestRuntimeHost> runtimeHost;
    StrategyBacktestEntryService* testingEntryService{nullptr};
    bool initializeAttempted{false};
};

RuntimeStorage& runtimeStorage()
{
    static RuntimeStorage storage;
    return storage;
}

} // namespace

namespace bridge {

void StrategyBacktestRuntimeAccess::initialize()
{
    RuntimeStorage& storage = runtimeStorage();
    QMutexLocker locker(&storage.mutex);
    if (storage.testingEntryService || storage.runtimeHost || storage.initializeAttempted) {
        return;
    }

    storage.initializeAttempted = true;

    QString calendarError;
    LoadedTradingCalendar tradingCalendar = loadTradingCalendar(&calendarError);
    if (tradingCalendar.ranges.empty() || tradingCalendar.tradingDates.empty()) {
        qWarning() << "StrategyBacktestRuntimeAccess: unable to initialize runtime host:" << calendarError;
        return;
    }

    std::shared_ptr<astock::market::IDataProvider> dataProvider =
        astock::market::DataProviderFactory::create_provider(
            astock::market::DataProviderFactory::ProviderType::DATABASE,
            buildDatabaseProviderConfig());
    if (!dataProvider) {
        qWarning() << "StrategyBacktestRuntimeAccess: database provider factory returned null";
        return;
    }

    if (!dataProvider->connect()) {
        qWarning() << "StrategyBacktestRuntimeAccess: database provider connect failed";
        return;
    }

    try {
        auto factorSnapshotProvider = std::make_unique<BridgeBacktestFactorSnapshotProvider>(
            tradingCalendar.tradingDates);
        auto tradingDayRangeResolver =
            std::make_unique<IndexedTradingDayRangeResolver>(TradingDayIndex(0), std::move(tradingCalendar.ranges));
        auto runtimeHost = std::make_unique<StrategyBacktestRuntimeHost>(
            *dataProvider,
            *tradingDayRangeResolver,
            kStrategyBacktestMarketDataPeriod,
            CandidateCount(kStrategyBacktestWarmupDayCount),
            factorSnapshotProvider.get(),
            factorSnapshotProvider.get());

        storage.dataProvider = std::move(dataProvider);
        storage.tradingDayRangeResolver = std::move(tradingDayRangeResolver);
        storage.factorSnapshotProvider = std::move(factorSnapshotProvider);
        storage.runtimeHost = std::move(runtimeHost);
    } catch (const std::exception& error) {
        qWarning() << "StrategyBacktestRuntimeAccess: runtime host initialization failed:" << error.what();
    }
}

application::backtest::StrategyBacktestEntryService* StrategyBacktestRuntimeAccess::entryService()
{
    RuntimeStorage& storage = runtimeStorage();
    QMutexLocker locker(&storage.mutex);
    if (storage.testingEntryService) {
        return storage.testingEntryService;
    }
    return storage.runtimeHost ? &storage.runtimeHost->entryService() : nullptr;
}

void StrategyBacktestRuntimeAccess::resetForTesting()
{
    RuntimeStorage& storage = runtimeStorage();
    QMutexLocker locker(&storage.mutex);
    storage.testingEntryService = nullptr;
    storage.runtimeHost.reset();
    storage.factorSnapshotProvider.reset();
    storage.tradingDayRangeResolver.reset();
    storage.dataProvider.reset();
    storage.initializeAttempted = false;
}

void StrategyBacktestRuntimeAccess::installEntryServiceForTesting(
    application::backtest::StrategyBacktestEntryService* entryService)
{
    RuntimeStorage& storage = runtimeStorage();
    QMutexLocker locker(&storage.mutex);
    storage.testingEntryService = entryService;
}

} // namespace bridge