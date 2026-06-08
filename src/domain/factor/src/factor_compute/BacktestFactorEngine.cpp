#include "factor_compute/BacktestFactorEngine.h"
#include "factor_compute/SignalCache.h"
#include "factor_compute/MarketDataViewHistoricalAdapter.h"
#include "FactorInstanceManager.h"
#include "BaseFactor.h"
#include "HistoricalView.h"

#include "../../ui/bridge/include/CachedMarketDataView.h"
#include "foundation/json/json_facade.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace factor::compute {

namespace {

/// @brief 从 JSON 行构建 CachedMarketDataView — 只加载指定的额外字段
/// @param root   JSON 数组
/// @param extraFields 因子需要的额外字段列表 (如 pb_ratio, pe_ratio, industry_code 等)
std::unique_ptr<CachedMarketDataView> buildMarketViewFromJson(
    const foundation::json::JsonFacade& root,
    const std::vector<std::string>& extraFields)
{
    if (!root.isArray() || root.size() == 0) {
        return nullptr;
    }

    auto view = std::make_unique<CachedMarketDataView>();
    std::unordered_map<std::string, InstrumentId> symToId;
    std::vector<InstrumentId> instruments;
    uint32_t nextId = 0;

    // 第一遍: 收集所有 symbol 和 trade_date
    std::unordered_set<std::string> symbolSet;
    std::unordered_map<std::string, int> dateIndex;
    std::vector<std::string> sortedDates;
    const size_t rowCount = root.size();

    for (size_t i = 0; i < rowCount; ++i) {
        const auto row = root.at(i);
        if (!row.isObject()) continue;

        std::string sym, date;
        if (row.has("symbol"))  sym  = row.get("symbol").asString();
        if (row.has("trade_date")) date = row.get("trade_date").asString();
        if (sym.empty() || date.empty()) continue;

        if (symbolSet.insert(sym).second) {
            InstrumentId instId{nextId};
            instruments.push_back(instId);
            symToId[sym] = instId;
            ++nextId;
        }
        if (dateIndex.find(date) == dateIndex.end()) {
            dateIndex[date] = static_cast<int>(sortedDates.size());
            sortedDates.push_back(date);
        }
    }

    int numDates = static_cast<int>(sortedDates.size());
    int numInsts = static_cast<int>(instruments.size());
    if (numDates <= 0 || numInsts <= 0) {
        return nullptr;
    }

    // 辅助: 构建列
    auto buildColumn = [&](float nanVal = 0.0f) -> CachedMarketDataView::ColumnData {
        CachedMarketDataView::ColumnData col;
        col.dateCount = numDates;
        col.instrumentCount = numInsts;
        col.values.assign(static_cast<size_t>(numDates) * numInsts, nanVal);
        for (const auto& d : sortedDates) {
            int dateInt = 0;
            try {
                // d 格式为 "2020-01-02"，转换为 20200102
                if (d.size() == 10 && d[4] == '-' && d[7] == '-') {
                    dateInt = std::stoi(d) * 10000 + std::stoi(d.substr(5, 2)) * 100 + std::stoi(d.substr(8, 2));
                } else {
                    dateInt = std::stoi(d);
                }
            } catch (...) {
                dateInt = 0;
            }
            col.dates.push_back(DateKey{dateInt});
        }
        col.instruments = instruments;
        return col;
    };

    // 字符串 → 整数 哈希映射（用于 industry_code 等分类字段）
    std::unordered_map<std::string, int> stringToIntMap;
    int nextStringId = 1;

    auto fillColumn = [&](CachedMarketDataView::ColumnData& col,
                          const std::string& field) {
        for (size_t i = 0; i < rowCount; ++i) {
            const auto row = root.at(i);
            if (!row.isObject()) continue;
            if (!row.has(field)) continue;
            const auto fieldValue = row.get(field);

            double val = 0.0;
            if (fieldValue.isNumber()) {
                val = fieldValue.asDouble();
            } else if (fieldValue.isString()) {
                const std::string strVal = fieldValue.asString();
                auto it = stringToIntMap.find(strVal);
                if (it == stringToIntMap.end()) {
                    it = stringToIntMap.emplace(strVal, nextStringId++).first;
                }
                val = static_cast<double>(it->second);
            } else {
                continue;  // null 等类型跳过
            }

            std::string sym, date;
            if (row.has("symbol"))  sym  = row.get("symbol").asString();
            if (row.has("trade_date")) date = row.get("trade_date").asString();
            auto sit = symToId.find(sym);
            auto dit = dateIndex.find(date);
            if (sit == symToId.end() || dit == dateIndex.end()) continue;

            int sIdx = static_cast<int>(sit->second.value);
            int dIdx = dit->second;
            col.values[static_cast<size_t>(dIdx) * numInsts + sIdx]
                = static_cast<float>(val);
        }
    };

    auto colOpen  = buildColumn();
    auto colHigh  = buildColumn();
    auto colLow   = buildColumn();
    auto colClose = buildColumn();
    auto colVol   = buildColumn();

    fillColumn(colOpen,  "open");
    fillColumn(colHigh,  "high");
    fillColumn(colLow,   "low");
    fillColumn(colClose, "close");
    fillColumn(colVol,   "volume");

    view->loadFromColumnData(
        std::move(colOpen), std::move(colHigh), std::move(colLow),
        std::move(colClose), std::move(colVol));

    // 只加载因子需要的额外字段
    for (const auto& field : extraFields) {
        auto col = buildColumn(std::numeric_limits<float>::quiet_NaN());
        fillColumn(col, field);
        view->loadAdditionalField(field, std::move(col));
    }

    return view;
}

} // anonymous namespace

BacktestDataService::BacktestDataService() = default;
BacktestDataService::~BacktestDataService() = default;

void BacktestDataService::storeRawJson(const std::string& jsonContent)
{
    m_rawJson = jsonContent;
    m_jsonStored = true;
    m_ownedView.reset();
    m_marketView = nullptr;
}

void BacktestDataService::buildViewForFields(const std::vector<std::string>& extraFields)
{
    if (!m_jsonStored || m_rawJson.empty()) {
        return;
    }

    auto root = foundation::json::JsonFacade::parse(m_rawJson);
    if (!root.isArray() || root.size() == 0) {
        return;
    }

    m_ownedView = buildMarketViewFromJson(root, extraFields);
    if (m_ownedView) {
        m_marketView = m_ownedView.get();
    }
}

void BacktestDataService::setMarketView(IMarketDataView* view) {
    m_marketView = view;
}

MarketMatrixBatch BacktestDataService::loadBatch(std::size_t batchIndex) {
    MarketMatrixBatch batch;
    batch.batchIndex = batchIndex;
    batch.marketView = m_marketView;
    return batch;
}

BacktestFactorEngine::BacktestFactorEngine(uint64_t maxMemoryBytes)
    : m_signalCache(std::make_unique<SignalCache>(maxMemoryBytes)) {
}

BacktestFactorEngine::~BacktestFactorEngine() = default;

void BacktestFactorEngine::setInstanceManager(factor::FactorInstanceManager* mgr) {
    m_instanceManager = mgr;
}

void BacktestFactorEngine::setDataService(BacktestDataService* dataSvc) {
    m_dataSvc = dataSvc;
}

FactorMatrix BacktestFactorEngine::compute(const MarketMatrixBatch& marketData,
                                            const FactorCacheKey& cacheKey) {
    FactorMatrix result;
    result.batchIndex = marketData.batchIndex;

    if (!m_instanceManager) {
        fprintf(stderr, "[BacktestFactorEngine] m_instanceManager is null, aborting compute\n");
        fflush(stderr);
        return result;
    }

    auto factor = m_instanceManager->createInstance(cacheKey.factorName);
    if (!factor) {
        fprintf(stderr, "[BacktestFactorEngine] createInstance returned null for: %s\n", cacheKey.factorName.c_str());
        fflush(stderr);
        return result;
    }

    // 获取因子需要的字段 → 按需构建 MarketView
    if (m_dataSvc) {
        auto fieldReqs = factor->getDataRequirements();
        std::vector<std::string> neededFields = fieldReqs.requiredFields;
        for (const auto& f : fieldReqs.optionalFields) {
            neededFields.push_back(f);
        }
        m_dataSvc->buildViewForFields(neededFields);
    }

    auto boundary = factor->getBoundaryRules();
    unsigned int numThreads = std::max(1u,
        std::thread::hardware_concurrency() > 2
            ? std::thread::hardware_concurrency() - 2 : 1u);
    (void)boundary;
    (void)numThreads;

    // 从 DataSvc 获取数据视图 (已由 buildViewForFields 构建)
    const IMarketDataView* view = marketData.marketView;
    if (!view && m_dataSvc) {
        MarketMatrixBatch batch = m_dataSvc->loadBatch(0);
        view = batch.marketView;
    }

    if (!view) {
        fprintf(stderr, "[BacktestFactorEngine] view is null, no data to compute\n");
        fflush(stderr);
        return result;
    }

    CachedMarketDataViewHistoricalAdapter adapter(*view);
    auto symbols = adapter.getAvailableSymbols("");

    int dateCount = 0, valueCount = 0;
    for (const auto& date : view->dates()) {
        std::string dateStr = std::to_string(date.value);
        factor::CalculationContext ctx(dateStr, symbols,
            std::make_shared<CachedMarketDataViewHistoricalAdapter>(*view));
        auto cr = factor->calculate(ctx);
        std::map<std::string, double> dateValues;
        for (const auto& [sym, val] : cr.values) {
            if (std::isfinite(val)) {
                dateValues[sym] = val;
                ++valueCount;
            }
        }
        if (!dateValues.empty()) {
            result.factorValues[dateStr] = std::move(dateValues);
            ++dateCount;
        }
    }
    fprintf(stderr, "[BacktestFactorEngine] compute done: dates=%zu symbols=%zu validDates=%d values=%d\n",
            view->dates().size(), symbols.size(), dateCount, valueCount);
    fflush(stderr);

    return result;
}

BacktestReporter::BacktestReporter() = default;
BacktestReporter::~BacktestReporter() = default;

BacktestReporterOutput BacktestReporter::analyze(const BacktestReporterInput& input) {
    BacktestReporterOutput output;

    for (const auto& [date, symbolMap] : input.factorValuesByDate) {
        output.totalSignalCount += static_cast<uint32_t>(symbolMap.size());
        for (const auto& [symbol, value] : symbolMap) {
            if (std::isfinite(value) && std::abs(value) > 1e-9) {
                output.presentSignalCount++;
            }
        }
    }

    return output;
}

} // namespace factor::compute