#include "domain/factor/include/MomentumFactor.h"
#include "domain/factor/include/FactorDataProvider.h"
#include "infrastructure/include/database/QtMySQLDatabase.h"

#include <QDate>
#include <QDebug>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace factor {

namespace {

std::map<QString, QVariant> makePositionalParams(std::initializer_list<QVariant> values)
{
    std::map<QString, QVariant> params;
    for (const QVariant& value : values) {
        params.emplace(QString(), value);
    }
    return params;
}

std::vector<std::string> extractSymbolsFromRangeResult(const astock::database::QueryResult& queryResult)
{
    std::vector<std::string> symbols;
    symbols.reserve(queryResult.rowCount());
    for (size_t i = 0; i < queryResult.rowCount(); ++i) {
        symbols.push_back(queryResult.getRow(i).getString("symbol").toStdString());
    }
    return symbols;
}

QString earliestMomentumSeriesDate(const QDate& anchorDate, int window, int skipRecent)
{
    const int lookbackDays = std::max(365, (window + skipRecent + 10) * 2);
    return anchorDate.addDays(-lookbackDays).toString("yyyy-MM-dd");
}

}

MomentumFactor::MomentumFactor() {
    factorType_ = "动量因子";
}

void MomentumFactor::initializeFromDatabase(const std::string& instanceId) {
    BaseFactor::initializeFromDatabase(instanceId);
    
    // 动量因子特定的初始化
    if (db_) {
        auto result = db_->executeQuery(
            "SELECT CAST(full_config AS CHAR) AS full_config FROM factor_instance WHERE instance_id = ?",
            makePositionalParams({QString::fromStdString(instanceId)})
        );
        
        if (!result.isEmpty()) {
            auto config = foundation::json::JsonFacade::parse(result.getRow(0).getString("full_config").toStdString());
            loadConfig(config);
        }
    }
}

CalculationResult MomentumFactor::calculate(const CalculationContext& context) {
    CalculationResult result;
    result.calculationId = foundation::utils::Uuid::generate_v4();
    result.date = context.date;

    if (context.dataProvider) {
        result.dataStatus.availability = DataAvailability::AVAILABLE;
        result.dataStatus.coverage = 1.0;
        result.dataStatus.message = "使用缓存数据集";
    } else {
        auto dataStatus = checkDataAvailability(context.date);
        result.dataStatus = dataStatus;
    }
    
    if (!result.dataStatus.isValid()) {
        result.metadata.set("error", json_helper::toJsonValue("数据不可用: " + result.dataStatus.message));
        return result;
    }
    
    try {
        // 根据类型计算动量
        std::unordered_map<std::string, double> momentumValues;
        
        if (params_.type == "simple") {
            momentumValues = calculateSimpleMomentum(context);
        } else if (params_.type == "rank") {
            momentumValues = calculateRankMomentum(context);
        } else if (params_.type == "normalized") {
            momentumValues = calculateNormalizedMomentum(context);
        } else {
            momentumValues = calculateSimpleMomentum(context);
        }
        
        // 应用边界规则
        result.values = applyBoundaryRules(momentumValues, context);
        
        // 处理异常值
        result.values = handleOutliers(result.values);
        
        // 设置元数据
        result.metadata.set("params", params_.toJson());
        result.metadata.set("calculation_type", json_helper::toJsonValue(params_.type));
        result.metadata.set("window", json_helper::toJsonValue(params_.window));
        result.metadata.set("symbol_count", json_helper::toJsonValue(static_cast<int>(result.values.size())));
        result.metadata.set("skip_recent", json_helper::toJsonValue(params_.skipRecent));

        if (result.values.empty()) {
            const QString emptyReason = QString("动量因子需要至少 %1 个交易日样本（窗口 %2，跳过最近 %3 个交易日），当前区间内未找到满足条件的股票")
                .arg(params_.window + params_.skipRecent + 1)
                .arg(params_.window)
                .arg(params_.skipRecent);
            result.metadata.set("empty_reason", json_helper::toJsonValue(emptyReason.toStdString()));
        }
        
    } catch (const std::exception& e) {
        result.dataStatus.availability = DataAvailability::UNAVAILABLE;
        result.dataStatus.message = "计算失败: " + std::string(e.what());
        result.metadata.set("error", json_helper::toJsonValue(e.what()));
    }
    
    return result;
}

DataRequirements MomentumFactor::getDataRequirements() const {
    DataRequirements req;
    req.requiredFields = {"close"};
    req.optionalFields = {"adj_factor"};
    
    if (params_.useVolume) {
        req.optionalFields.push_back("volume");
    }
    
    return req;
}

BoundaryRules MomentumFactor::getBoundaryRules() const {
    BoundaryRules rules;
    rules.minDataPoints = params_.window + 1;  // 需要足够的数据点计算动量
    rules.handleNewStock = "exclude_if_lt_60d";
    rules.handleSuspended = "forward_fill";
    rules.handleDelisted = "keep_until_delist";
    rules.handleOutliers = "winsorize_3sigma";
    return rules;
}

std::shared_ptr<MomentumFactor> MomentumFactor::create(
    const std::string& instanceId,
    std::shared_ptr<astock::database::QtMySQLDatabase> db,
    std::shared_ptr<DataAvailabilityChecker> dataChecker) {
    
    auto factor = std::make_shared<MomentumFactor>();
    factor->db_ = db;
    factor->dataChecker_ = dataChecker;
    factor->initializeFromDatabase(instanceId);
    return factor;
}

// ============ 私有方法实现 ============

double MomentumFactor::calculateSymbolMomentum(const std::string& symbol,
                                               const CalculationContext& context) {
    auto prices = getPriceData(symbol, context);
    const double currentClose = prices.first;
    const double previousClose = prices.second;

    if (currentClose <= 0.0 || previousClose <= 0.0) {
        throw std::runtime_error("价格数据无效");
    }

    return (currentClose - previousClose) / previousClose;
}

std::unordered_map<std::string, double> MomentumFactor::calculateSimpleMomentum(
    const CalculationContext& context) {
    
    std::unordered_map<std::string, double> momentumValues;

    std::vector<std::string> symbols = context.symbols;
    if (symbols.empty() && context.dataProvider) {
        symbols = context.dataProvider->getAvailableSymbols(context.date);
    } else if (symbols.empty() && db_) {
        auto queryResult = db_->executeQuery(
            "SELECT DISTINCT symbol FROM daily_bar WHERE trade_date = :trade_date ORDER BY symbol",
            {{":trade_date", QString::fromStdString(context.date)}}
        );
        symbols = extractSymbolsFromRangeResult(queryResult);
    }

    for (const auto& symbol : symbols) {
        try {
            momentumValues[symbol] = calculateSymbolMomentum(symbol, context);
        } catch (const std::exception&) {
            continue;
        }
    }
    
    return momentumValues;
}

std::unordered_map<std::string, double> MomentumFactor::calculateRankMomentum(
    const CalculationContext& context) {
    
    auto simpleMomentum = calculateSimpleMomentum(context);
    
    // 转换为排名（0-1）
    std::vector<double> values;
    for (const auto& [symbol, value] : simpleMomentum) {
        values.push_back(value);
    }
    
    // 排序
    std::sort(values.begin(), values.end());
    
    std::unordered_map<std::string, double> rankValues;
    if (values.empty()) {
        return rankValues;
    }

    for (const auto& [symbol, value] : simpleMomentum) {
        // 计算百分位排名
        auto it = std::lower_bound(values.begin(), values.end(), value);
        double rank = static_cast<double>(std::distance(values.begin(), it)) / values.size();
        rankValues[symbol] = rank;
    }
    
    return rankValues;
}

std::unordered_map<std::string, double> MomentumFactor::calculateNormalizedMomentum(
    const CalculationContext& context) {
    
    auto simpleMomentum = calculateSimpleMomentum(context);
    if (simpleMomentum.empty()) {
        return {};
    }
    
    // 计算均值和标准差
    std::vector<double> values;
    for (const auto& [symbol, value] : simpleMomentum) {
        values.push_back(value);
    }
    
    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    double mean = sum / values.size();
    
    double sq_sum = std::inner_product(values.begin(), values.end(), 
                                      values.begin(), 0.0);
    double stdev = std::sqrt(sq_sum / values.size() - mean * mean);
    
    // 标准化
    std::unordered_map<std::string, double> normalizedValues;
    for (const auto& [symbol, value] : simpleMomentum) {
        if (stdev > 0) {
            normalizedValues[symbol] = (value - mean) / stdev;
        } else {
            normalizedValues[symbol] = 0.0;
        }
    }
    
    return normalizedValues;
}

std::pair<double, double> MomentumFactor::getPriceData(const std::string& symbol,
                                                       const CalculationContext& context) {
    const QDate currentDate = QDate::fromString(QString::fromStdString(context.date), "yyyy-MM-dd");
    if (!currentDate.isValid()) {
        throw std::runtime_error("非法计算日期");
    }

    const int requiredPoints = params_.window + params_.skipRecent + 1;

    if (context.dataProvider) {
        const auto series = context.dataProvider->getSeries(
            symbol,
            earliestMomentumSeriesDate(currentDate, params_.window, params_.skipRecent).toStdString(),
            currentDate.toString("yyyy-MM-dd").toStdString(),
            "close"
        );

        if (static_cast<int>(series.size()) < requiredPoints) {
            qDebug() << "MomentumFactor: 缓存序列长度不足"
                     << "symbol=" << QString::fromStdString(symbol)
                     << "date=" << QString::fromStdString(context.date)
                     << "requiredPoints=" << requiredPoints
                     << "actualPoints=" << static_cast<int>(series.size());
            throw std::runtime_error("缓存集中缺少足够的历史交易日数据");
        }

        const size_t anchorIndex = series.size() - static_cast<size_t>(params_.skipRecent) - 1;
        const size_t previousIndex = anchorIndex - static_cast<size_t>(params_.window);
        const double currentClose = series[anchorIndex].value;
        const double previousClose = series[previousIndex].value;
        return {currentClose, previousClose};
    }

    if (!db_) {
        throw std::runtime_error("数据库连接未初始化");
    }

    auto queryResult = db_->executeQuery(
        QString("SELECT trade_date, close FROM daily_bar "
                "WHERE symbol = :symbol AND trade_date <= :current_date "
                "ORDER BY trade_date DESC LIMIT %1")
            .arg(requiredPoints),
        {
            {":symbol", QString::fromStdString(symbol)},
            {":current_date", currentDate.toString("yyyy-MM-dd")}
        }
    );

    if (static_cast<int>(queryResult.rowCount()) < requiredPoints) {
        throw std::runtime_error("缺少足够的历史交易日价格数据");
    }

    const double currentClose = queryResult.getRow(static_cast<size_t>(params_.skipRecent)).getDouble("close");
    const double previousClose = queryResult.getRow(static_cast<size_t>(params_.skipRecent + params_.window)).getDouble("close");
    return {currentClose, previousClose};
}

void MomentumFactor::loadConfig(const foundation::json::JsonFacade& config) {
    // 调用基类加载
    BaseFactor::loadConfig(config);
    
    // 加载动量因子特定配置
    if (config.has("calculation")) {
        auto calcConfig = config.get("calculation");
        params_.fromJson(calcConfig);

        int resolvedWindow = params_.window;
        if (calcConfig.has("lookback_window")) {
            resolvedWindow = calcConfig.get("lookback_window").asInt();
        } else if (calcConfig.has("lookbackWindow")) {
            resolvedWindow = calcConfig.get("lookbackWindow").asInt();
        }

        if (resolvedWindow > 0) {
            params_.window = resolvedWindow;
        }
    }

    const int requiredMinDataPoints = std::max(1, params_.window + 1);
    if (boundaryRules_.minDataPoints < requiredMinDataPoints) {
        boundaryRules_.minDataPoints = requiredMinDataPoints;
    }
    
    // 设置数据需求
    dataRequirements_.requiredFields = {"close"};
    dataRequirements_.optionalFields.clear();
    if (params_.useVolume) {
        dataRequirements_.optionalFields.push_back("volume");
    }
}

} // namespace factor