#include "DatabaseStockDataProvider.h"
#include <foundation/log/logging.hpp>
#include "../../ui/bridge/include/DatabaseConnectionManager.h"
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <mutex>
#include <set>
#include <QCoreApplication>
#include <QThread>
#include <QMetaObject>
#include <QTimer>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDate>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include "../../ui/bridge/include/DataServiceCache.h"

#include "../../infrastructure/include/database/QtMySQLDatabase.h"

#include <limits>

namespace domain::backtest {

namespace {

QString normalizeDataSourceMode(const std::string& rawMode) {
    const QString mode = QString::fromStdString(rawMode).trimmed().toLower();
    if (mode == "cleaned" || mode == "cleaned_table" || mode == "cleaned_daily_bar") {
        return "cleaned";
    }
    if (mode == "cache" || mode == "dataset") {
        return "cache";
    }
    return "raw";
}

QString normalizeTradeDate(const QVariantMap& row) {
    QString tradeDate = row.value("trade_date").toString().trimmed();
    if (tradeDate.isEmpty()) {
        tradeDate = row.value("date").toString().trimmed();
    }
    if (tradeDate.contains('T')) {
        tradeDate = tradeDate.left(tradeDate.indexOf('T'));
    }
    return tradeDate;
}

QString resolveIndexSnapshotDate(std::shared_ptr<astock::database::QtMySQLDatabase> database,
                                 const QString& indexSymbol,
                                 const QString& requestedSnapshotDate) {
    if (!database || indexSymbol.trimmed().isEmpty()) {
        return requestedSnapshotDate;
    }

    try {
        std::map<QString, QVariant> params;
        params[":index_symbol"] = indexSymbol;
        params[":snapshot_date"] = requestedSnapshotDate;

        const QString sql =
            "SELECT COALESCE(MAX(CASE WHEN start_date <= :snapshot_date THEN start_date END), MIN(start_date)) AS resolved_date "
            "FROM index_constituents WHERE index_symbol = :index_symbol";

        const auto result = database->executeQuery(sql, params);
        if (result.getRows().empty()) {
            return requestedSnapshotDate;
        }

        const QString resolvedDate = result.getRows().front().getString("resolved_date").trimmed();
        return resolvedDate.isEmpty() ? requestedSnapshotDate : resolvedDate;
    } catch (const std::exception& e) {
        INTERNAL_WARN_STREAM << "Failed to resolve index snapshot date for " << indexSymbol.toStdString()
                             << ": " << e.what();
        return requestedSnapshotDate;
    }
}

bool inDateRange(const QString& tradeDate, const QString& startDate, const QString& endDate) {
    if (tradeDate.isEmpty()) {
        return false;
    }
    const QDate current = QDate::fromString(tradeDate, "yyyy-MM-dd");
    if (!current.isValid()) {
        return false;
    }

    const QDate start = QDate::fromString(startDate, "yyyy-MM-dd");
    const QDate end = QDate::fromString(endDate, "yyyy-MM-dd");
    if (start.isValid() && current < start) {
        return false;
    }
    if (end.isValid() && current > end) {
        return false;
    }
    return true;
}

bool isBacktestReadyDataset(const DataServiceCache::DataSetInfo& info) {
    return info.id > 0 && info.schemaVersion >= 2 && info.isBacktestReady;
}

} // namespace

DatabaseStockDataProvider::DatabaseStockDataProvider(
    std::shared_ptr<ui::bridge::DataFetchController> dataFetchController)
    : dataFetchController_(dataFetchController) {
    
    // 允许dataFetchController为nullptr，因为我们可以使用DataServiceCache
    if (dataFetchController_) {
        INTERNAL_INFO_STREAM << "DatabaseStockDataProvider initialized with DataFetchController";
    } else {
        INTERNAL_WARN_STREAM << "DatabaseStockDataProvider initialized without DataFetchController, will use DataServiceCache only";
    }
    
    // 初始化DataServiceCache
    DataServiceCache::getInstance().initializeCache();
    database_ = astock::database::DatabaseConnectionManager::instance().getDatabase();
}

DatabaseStockDataProvider::~DatabaseStockDataProvider() = default;

std::vector<domain::model::Bar> DatabaseStockDataProvider::getStockBars(
    const std::string& symbol,
    const std::string& startDate,
    const std::string& endDate) {
    // 首先检查本地缓存
    auto cachedData = getFromCache(buildCacheKey(symbol, startDate, endDate), startDate, endDate);
    if (!cachedData.empty()) {
        INTERNAL_DEBUG_STREAM << "Returning cached data for symbol " << symbol << " from " << startDate << " to " << endDate;
        return cachedData;
    }
    
    std::vector<domain::model::Bar> bars;
    
    try {
        bars = loadBarsFromActiveSource(QString::fromStdString(symbol),
                                        QString::fromStdString(startDate),
                                        QString::fromStdString(endDate));

        if (bars.empty()) {
            INTERNAL_WARN_STREAM << "No data found for symbol " << symbol << " from " << startDate << " to " << endDate
                                 << " sourceMode=" << dataSourceMode_;
            return bars;
        }
        
        INTERNAL_INFO_STREAM << "Successfully loaded " << bars.size() << " bars for symbol " << symbol << " from " << startDate << " to " << endDate;
        
        // 将数据添加到本地缓存
        addToCache(buildCacheKey(symbol, startDate, endDate), bars);
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to get stock bars for symbol " << symbol << " from " << startDate << " to " << endDate << ": " << e.what();
        throw;
    }
    
    return bars;
}

void DatabaseStockDataProvider::setDataSourceContext(const std::string& dataSourceMode,
                                                    int datasetId) {
    dataSourceMode_ = normalizeDataSourceMode(dataSourceMode).toStdString();
    selectedDatasetId_ = datasetId;
}

std::map<std::string, std::vector<domain::model::Bar>> DatabaseStockDataProvider::getMultipleStockBars(
    const std::vector<std::string>& symbols,
    const std::string& startDate,
    const std::string& endDate) {
    
    std::map<std::string, std::vector<domain::model::Bar>> result;
    
    try {
        // 并行获取多只股票的数据
        for (const auto& symbol : symbols) {
            try {
                auto bars = getStockBars(symbol, startDate, endDate);
                result[symbol] = bars;
            } catch (const std::exception& e) {
                INTERNAL_ERROR_STREAM << "Failed to get bars for symbol " << symbol << ": " << e.what();
                // 继续处理其他股票
            }
        }
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to get multiple stock bars: " << e.what();
        throw;
    }
    
    return result;
}

std::vector<std::string> DatabaseStockDataProvider::getAvailableSymbols() {
    if (normalizeDataSourceMode(dataSourceMode_) == "cache") {
        return loadSymbolsFromCacheDataset();
    }
    return loadSymbolsFromTable(resolveTableName());
}

std::vector<std::string> DatabaseStockDataProvider::getAvailableDates(
    const std::string& symbol) {
    
    std::vector<std::string> dates;
    
    try {
        // 这里应该从数据库中获取指定股票可用的日期
        // 由于时间关系，返回空列表
        
        INTERNAL_WARN_STREAM << "getAvailableDates not fully implemented for symbol " << symbol;
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to get available dates for symbol " << symbol << ": " << e.what();
        throw;
    }
    
    return dates;
}

std::vector<std::string> DatabaseStockDataProvider::getIndexConstituentSymbols(
    const QString& indexSymbol,
    const QString& snapshotDate) const {
    std::vector<std::string> symbols;
    QElapsedTimer timer;
    timer.start();
    auto database = database_ ? database_ : astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database || indexSymbol.trimmed().isEmpty()) {
        return symbols;
    }

    QString effectiveSnapshotDate = snapshotDate.trimmed();
    if (effectiveSnapshotDate.isEmpty()) {
        effectiveSnapshotDate = QDate::currentDate().toString("yyyy-MM-dd");
    }
    effectiveSnapshotDate = resolveIndexSnapshotDate(database, indexSymbol.trimmed(), effectiveSnapshotDate);

    std::map<QString, QVariant> params;
    params[":index_symbol"] = indexSymbol.trimmed();
    params[":snapshot_date"] = effectiveSnapshotDate;

    const QString sql =
        "SELECT DISTINCT constituent_symbol AS symbol "
        "FROM index_constituents "
        "WHERE index_symbol = :index_symbol "
        "  AND start_date <= :snapshot_date "
        "  AND (end_date IS NULL OR end_date >= :snapshot_date) "
        "ORDER BY constituent_symbol ASC";

    try {
        const auto result = database->executeQuery(sql, params);
        symbols.reserve(result.rowCount());
        for (size_t rowIndex = 0; rowIndex < result.rowCount(); ++rowIndex) {
            const QString symbol = result.getRow(rowIndex).getString("symbol").trimmed();
            if (!symbol.isEmpty()) {
                symbols.push_back(symbol.toStdString());
            }
        }
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to load index constituents for " << indexSymbol.toStdString()
                              << ": " << e.what();
    }

    INTERNAL_INFO_STREAM << "Index constituents resolved index=" << indexSymbol.trimmed().toStdString()
                         << " snapshotDate=" << effectiveSnapshotDate.toStdString()
                         << " count=" << symbols.size()
                         << " elapsedMs=" << timer.elapsed();

    return symbols;
}

std::map<std::string, std::vector<double>> DatabaseStockDataProvider::getStockTimeSeries(
    const std::string& symbol,
    const std::string& field,
    const std::string& startDate,
    const std::string& endDate) {
    
    std::map<std::string, std::vector<double>> result;
    
    try {
        auto bars = getStockBars(symbol, startDate, endDate);
        
        std::vector<double> values;
        values.reserve(bars.size());
        
        for (const auto& bar : bars) {
            if (field == "open") {
                values.push_back(bar.open);
            } else if (field == "high") {
                values.push_back(bar.high);
            } else if (field == "low") {
                values.push_back(bar.low);
            } else if (field == "close") {
                values.push_back(bar.close);
            } else if (field == "volume") {
                values.push_back(static_cast<double>(bar.volume));
            } else {
                throw std::invalid_argument("Unknown field: " + field);
            }
        }
        
        result[field] = values;
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to get stock time series for symbol " << symbol << " field " << field << ": " << e.what();
        throw;
    }
    
    return result;
}

std::map<std::string, std::map<std::string, std::vector<double>>> DatabaseStockDataProvider::getMultipleStockTimeSeries(
    const std::vector<std::string>& symbols,
    const std::vector<std::string>& fields,
    const std::string& startDate,
    const std::string& endDate) {
    
    std::map<std::string, std::map<std::string, std::vector<double>>> result;
    
    try {
        for (const auto& symbol : symbols) {
            std::map<std::string, std::vector<double>> symbolData;
            
            for (const auto& field : fields) {
                auto timeSeries = getStockTimeSeries(symbol, field, startDate, endDate);
                if (!timeSeries.empty()) {
                    symbolData[field] = timeSeries[field];
                }
            }
            
            if (!symbolData.empty()) {
                result[symbol] = symbolData;
            }
        }
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to get multiple stock time series: " << e.what();
        throw;
    }
    
    return result;
}

bool DatabaseStockDataProvider::validateStockData(
    const std::string& symbol,
    const std::string& startDate,
    const std::string& endDate) {
    
    try {
        auto bars = getStockBars(symbol, startDate, endDate);
        return !bars.empty();
        
    } catch (const std::exception&) {
        return false;
    }
}

std::map<std::string, double> DatabaseStockDataProvider::getStockStatistics(
    const std::string& symbol,
    const std::string& field,
    const std::string& startDate,
    const std::string& endDate) {
    
    std::map<std::string, double> stats;
    
    try {
        auto timeSeries = getStockTimeSeries(symbol, field, startDate, endDate);
        auto& values = timeSeries[field];
        
        if (values.empty()) {
            return stats;
        }
        
        // 计算统计量
        double sum = std::accumulate(values.begin(), values.end(), 0.0);
        double mean = sum / values.size();
        
        double sumSq = 0.0;
        double minValue = std::numeric_limits<double>::infinity();
        double maxValue = -std::numeric_limits<double>::infinity();
        
        for (double value : values) {
            double diff = value - mean;
            sumSq += diff * diff;
            if (value < minValue) minValue = value;
            if (value > maxValue) maxValue = value;
        }
        
        double variance = sumSq / values.size();
        double stdDev = std::sqrt((std::max)(variance, 0.0));
        
        // 计算分位数
        std::vector<double> sortedValues = values;
        std::sort(sortedValues.begin(), sortedValues.end());
        
        double median = sortedValues[sortedValues.size() / 2];
        double q1 = sortedValues[sortedValues.size() / 4];
        double q3 = sortedValues[3 * sortedValues.size() / 4];
        
        // 填充统计结果
        stats["count"] = static_cast<double>(values.size());
        stats["mean"] = mean;
        stats["std"] = stdDev;
        stats["min"] = minValue;
        stats["max"] = maxValue;
        stats["median"] = median;
        stats["q1"] = q1;
        stats["q3"] = q3;
        stats["skewness"] = calculateSkewness(values, mean, stdDev);
        stats["kurtosis"] = calculateKurtosis(values, mean, stdDev);
        
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to calculate stock statistics: " << e.what();
        throw;
    }
    
    return stats;
}

void DatabaseStockDataProvider::preloadStockData(
    const std::string& symbol,
    const std::string& startDate,
    const std::string& endDate) {
    
    try {
        // 预加载数据到缓存
        auto bars = getStockBars(symbol, startDate, endDate);
        INTERNAL_INFO_STREAM << "Preloaded stock " << symbol << " data from " << startDate << " to " << endDate << ", " << bars.size() << " bars";
        
    } catch (const std::exception& e) {
        INTERNAL_WARN_STREAM << "Failed to preload stock data: " << e.what();
    }
}

void DatabaseStockDataProvider::clearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    dataCache_.clear();
    INTERNAL_INFO_STREAM << "Cleared stock data cache";
}

std::vector<domain::model::Bar> DatabaseStockDataProvider::loadBarsFromActiveSource(
    const QString& symbol,
    const QString& startDate,
    const QString& endDate) {
    const QString mode = normalizeDataSourceMode(dataSourceMode_);
    if (mode == "cache") {
        return loadBarsFromCacheDataset(symbol, startDate, endDate);
    }
    return loadBarsFromTable(resolveTableName(), symbol, startDate, endDate);
}

std::vector<domain::model::Bar> DatabaseStockDataProvider::loadBarsFromCacheDataset(
    const QString& symbol,
    const QString& startDate,
    const QString& endDate) {
    std::vector<domain::model::Bar> bars;
    const int datasetId = resolveDatasetId();
    if (datasetId <= 0) {
        return bars;
    }

    const QVariantList rows = DataServiceCache::getInstance().getDataSetById(datasetId);
    if (rows.isEmpty()) {
        return bars;
    }

    QVariantList filteredRows;
    for (const QVariant& rowVariant : rows) {
        const QVariantMap row = rowVariant.toMap();
        const QString rowSymbol = row.value("symbol").toString().trimmed();
        const QString tradeDate = normalizeTradeDate(row);
        if (rowSymbol != symbol || !inDateRange(tradeDate, startDate, endDate)) {
            continue;
        }

        QVariantMap normalizedRow = row;
        normalizedRow["date"] = tradeDate;
        filteredRows.append(normalizedRow);
    }

    bars = convertToBars(filteredRows);
    std::sort(bars.begin(), bars.end(), [](const auto& left, const auto& right) {
        return left.time < right.time;
    });
    return bars;
}

std::vector<domain::model::Bar> DatabaseStockDataProvider::loadBarsFromTable(
    const QString& tableName,
    const QString& symbol,
    const QString& startDate,
    const QString& endDate) {
    std::vector<domain::model::Bar> bars;
    QElapsedTimer timer;
    timer.start();
    if (!database_) {
        database_ = astock::database::DatabaseConnectionManager::instance().getDatabase();
    }
    if (!database_) {
        throw std::runtime_error("Database connection is not available");
    }

    const QString sql = QString(
        "SELECT symbol, trade_date, open, high, low, close, volume "
        "FROM %1 WHERE symbol = :symbol "
        "AND trade_date >= :start_date AND trade_date <= :end_date "
        "AND close IS NOT NULL ORDER BY trade_date ASC")
        .arg(tableName);

    std::map<QString, QVariant> params;
    params[":symbol"] = symbol;
    params[":start_date"] = startDate;
    params[":end_date"] = endDate;

    const auto result = database_->executeQuery(sql, params);
    bars.reserve(result.rowCount());
    for (size_t rowIndex = 0; rowIndex < result.rowCount(); ++rowIndex) {
        const auto& row = result.getRow(rowIndex);
        QVariantMap barMap;
        barMap["symbol"] = row.getString("symbol");
        barMap["date"] = row.getString("trade_date");
        barMap["open"] = row.getDouble("open");
        barMap["high"] = row.getDouble("high");
        barMap["low"] = row.getDouble("low");
        barMap["close"] = row.getDouble("close");
        barMap["volume"] = row.getDouble("volume", 0.0);
        bars.push_back(convertToBar(barMap));
    }

    const qint64 elapsedMs = timer.elapsed();
    if (elapsedMs >= 200 || bars.empty()) {
        INTERNAL_INFO_STREAM << "Loaded bars table=" << tableName.toStdString()
                             << " symbol=" << symbol.toStdString()
                             << " startDate=" << startDate.toStdString()
                             << " endDate=" << endDate.toStdString()
                             << " rows=" << bars.size()
                             << " elapsedMs=" << elapsedMs;
    }

    return bars;
}

std::vector<std::string> DatabaseStockDataProvider::loadSymbolsFromCacheDataset() const {
    std::vector<std::string> symbols;
    const int datasetId = resolveDatasetId();
    if (datasetId <= 0) {
        return symbols;
    }

    const auto info = DataServiceCache::getInstance().getDataSetInfo(datasetId);
    if (!info.stockCodes.isEmpty()) {
        symbols.reserve(static_cast<size_t>(info.stockCodes.size()));
        for (const QString& code : info.stockCodes) {
            if (!code.trimmed().isEmpty()) {
                symbols.push_back(code.trimmed().toStdString());
            }
        }
        return symbols;
    }

    const QVariantList rows = DataServiceCache::getInstance().getDataSetById(datasetId);
    std::set<QString> uniqueSymbols;
    for (const QVariant& rowVariant : rows) {
        const QString code = rowVariant.toMap().value("symbol").toString().trimmed();
        if (!code.isEmpty()) {
            uniqueSymbols.insert(code);
        }
    }

    symbols.reserve(uniqueSymbols.size());
    for (const QString& code : uniqueSymbols) {
        symbols.push_back(code.toStdString());
    }
    return symbols;
}

std::vector<std::string> DatabaseStockDataProvider::loadSymbolsFromTable(const QString& tableName) const {
    std::vector<std::string> symbols;
    auto database = database_ ? database_ : astock::database::DatabaseConnectionManager::instance().getDatabase();
    if (!database) {
        return symbols;
    }

    const QString sql = QString("SELECT DISTINCT symbol FROM %1 ORDER BY symbol ASC").arg(tableName);
    const auto result = database->executeQuery(sql);
    symbols.reserve(result.rowCount());
    for (size_t rowIndex = 0; rowIndex < result.rowCount(); ++rowIndex) {
        const QString symbol = result.getRow(rowIndex).getString("symbol").trimmed();
        if (!symbol.isEmpty()) {
            symbols.push_back(symbol.toStdString());
        }
    }
    return symbols;
}

int DatabaseStockDataProvider::resolveDatasetId() const {
    if (selectedDatasetId_ > 0) {
        return selectedDatasetId_;
    }

    const auto dataSets = DataServiceCache::getInstance().getAllDataSetInfos();
    int latestDatasetId = -1;
    QDateTime latestCreated;
    for (const auto& info : dataSets) {
        if (!isBacktestReadyDataset(info)) {
            continue;
        }
        if (latestDatasetId < 0 || info.createdTime > latestCreated) {
            latestDatasetId = info.id;
            latestCreated = info.createdTime;
        }
    }
    return latestDatasetId;
}

QString DatabaseStockDataProvider::resolveTableName() const {
    return normalizeDataSourceMode(dataSourceMode_) == "cleaned" ? "cleaned_daily_bar" : "daily_bar";
}

std::string DatabaseStockDataProvider::buildCacheKey(const std::string& symbol,
                                                     const std::string& startDate,
                                                     const std::string& endDate) const {
    std::ostringstream stream;
    stream << dataSourceMode_ << ":" << selectedDatasetId_ << ":" << symbol << ":" << startDate << ":" << endDate;
    return stream.str();
}

domain::model::Bar DatabaseStockDataProvider::convertToBar(const QVariantMap& data) {
    domain::model::Bar bar;
    
    // 从QVariantMap中提取数据
    if (data.contains("symbol")) {
        bar.symbol = data["symbol"].toString().toStdString();
    }
    
    if (data.contains("time")) {
        bar.time = data["time"].toLongLong();
    } else if (data.contains("date")) {
        // 如果只有日期字符串，转换为时间戳
        QString dateStr = data["date"].toString();
        QDateTime dateTime = QDateTime::fromString(dateStr, "yyyy-MM-dd");
        if (!dateTime.isValid()) {
            dateTime = QDateTime::fromString(dateStr, "yyyy/MM/dd");
        }
        if (dateTime.isValid()) {
            bar.time = dateTime.toMSecsSinceEpoch();
        }
    }
    
    if (data.contains("open")) {
        bar.open = data["open"].toDouble();
    }
    
    if (data.contains("high")) {
        bar.high = data["high"].toDouble();
    }
    
    if (data.contains("low")) {
        bar.low = data["low"].toDouble();
    }
    
    if (data.contains("close")) {
        bar.close = data["close"].toDouble();
    }
    
    if (data.contains("volume")) {
        bar.volume = data["volume"].toDouble();
    }
    
    return bar;
}

std::vector<domain::model::Bar> DatabaseStockDataProvider::convertToBars(const QVariantList& data) {
    std::vector<domain::model::Bar> bars;
    bars.reserve(data.size());
    
    for (const auto& item : data) {
        bars.push_back(convertToBar(item.toMap()));
    }
    
    return bars;
}

void DatabaseStockDataProvider::addToCache(const std::string& symbol, const std::vector<domain::model::Bar>& bars) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    dataCache_[symbol] = bars;
}

std::vector<domain::model::Bar> DatabaseStockDataProvider::getFromCache(
    const std::string& symbol,
    const std::string& startDate,
    const std::string& endDate) {
    
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    auto it = dataCache_.find(symbol);
    if (it == dataCache_.end()) {
        return {};
    }
    
    // 这里应该根据日期范围过滤缓存数据
    // 由于时间关系，返回所有缓存数据
    return it->second;
}

void DatabaseStockDataProvider::clearCacheForSymbol(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    dataCache_.erase(symbol);
}

double DatabaseStockDataProvider::calculateMean(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    return sum / values.size();
}

double DatabaseStockDataProvider::calculateStdDev(const std::vector<double>& values, double mean) {
    if (values.size() < 2) return 0.0;
    
    double sumSq = 0.0;
    for (double value : values) {
        double diff = value - mean;
        sumSq += diff * diff;
    }
    
    double variance = sumSq / values.size();
    return std::sqrt((std::max)(variance, 0.0));
}

double DatabaseStockDataProvider::calculateSkewness(const std::vector<double>& values, double mean, double stdDev) {
    if (values.size() < 3 || stdDev < 1e-10) {
        return 0.0;
    }
    
    double sumCubed = 0.0;
    for (double value : values) {
        double diff = value - mean;
        sumCubed += diff * diff * diff;
    }
    
    double n = static_cast<double>(values.size());
    return (sumCubed / n) / (stdDev * stdDev * stdDev);
}

double DatabaseStockDataProvider::calculateKurtosis(const std::vector<double>& values, double mean, double stdDev) {
    if (values.size() < 4 || stdDev < 1e-10) {
        return 0.0;
    }
    
    double sumFourth = 0.0;
    for (double value : values) {
        double diff = value - mean;
        sumFourth += diff * diff * diff * diff;
    }
    
    double n = static_cast<double>(values.size());
    return (sumFourth / n) / (stdDev * stdDev * stdDev * stdDev) - 3.0;
}

} // namespace domain::backtest
