// 纯C++自由函数 — 所有策略回测业务逻辑
// 不依赖Qt，桥接层只负责传入参数并获取结果

#include "../include/BacktestRequest.h"
#include "../include/SignalToPnLAdapter.h"
#include "../include/MatrixPnLEngine.h"
#include "../../strategy/include/IStrategyService.h"
#include "../../factor/include/factor_compute/BacktestFactorEngine.h"
#include "../../factor/include/FactorInstanceManager.h"
#include "../../../ui/bridge/include/StrategyBridge.h"
#include "../../../ui/bridge/include/DataServiceCache.h"
#include "../../../ui/bridge/include/FactorService.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>

#include <stdexcept>
#include <vector>
#include <cstdint>
#include <string>

namespace domain::backtest {

void RunStrategyBacktest(const std::string& strategyId, const BacktestRequest& request)
{
    // 1. 加载数据集
    int dsId = request.dataSourceSpec.datasetId.value;
    if (dsId <= 0) throw std::runtime_error("Dataset not specified");
    QVariantList data = DataServiceCache::getInstance().getDataSetById(dsId);
    if (data.isEmpty()) throw std::runtime_error("Empty dataset");

    factor::compute::BacktestDataService dataSvc;
    dataSvc.storeRawJson(QJsonDocument(QJsonArray::fromVariantList(data))
        .toJson(QJsonDocument::Compact).toStdString());

    // 2. 初始化 FactorService
    auto* fs = FactorService::instance();
    if (!fs->isInitialized()) { qDebug() << "[RunStrategyBacktest] FactorService init…"; fs->initialize(); }
    factor::FactorInstanceManager* im = fs->isInitialized() ? fs->instanceManager() : nullptr;

    // 3. 获取策略引擎实例
    QString qId = QString::fromStdString(strategyId);
    auto* engine = StrategyBridge::instance() ? StrategyBridge::instance()->backtestEngineProvider(qId) : nullptr;
    if (!engine) throw std::runtime_error("StrategyEngine not available");

    // 4. 行情视图
    auto mb = dataSvc.loadBatch(0);
    if (!mb.marketView) throw std::runtime_error("market view failed");
    auto* view = const_cast<factor::compute::IMarketDataView*>(mb.marketView);

    // 5. 启动策略引擎
    engine->start();

    // 6. 驱动策略引擎遍历全部日期
    auto om = view->open(), cm = view->close(), vm = view->volume();
    int T = om.rowCount, N = om.columnCount;
    for (int t = 0; t < T; ++t) {
        std::vector<domain::strategy::MarketDataPoint> batch;
        batch.reserve(N);
        for (int n = 0; n < N; ++n) {
            domain::strategy::MarketDataPoint pt(
                request.window.startDate + t,
                domain::strategy::InstrumentId(static_cast<std::uint32_t>(n + 1)),
                om.data[(size_t)t * om.rowStride + n],
                cm.data[(size_t)t * cm.rowStride + n],
                static_cast<std::uint64_t>(vm.data[(size_t)t * vm.rowStride + n]));
            batch.push_back(pt);
        }
        engine->stepBatch(batch);
    }

    // 7. 构建占位 SignalSet 用于 PnL
    factor::compute::SignalSet empty;
    empty.dates.push_back(factor::compute::DateKey{request.window.startDate});
    empty.instruments.push_back(factor::compute::InstrumentId{1U});
    empty.signalIds.push_back(factor::compute::SignalId{1U});
    empty.values.push_back(0.0f); empty.mask.push_back(0);
    empty.index.timeStride = 1; empty.index.instrumentStride = 1;
    empty.index.factorStride = 1; empty.progress = {1U, 1U};

    SignalToPnLAdapter adapter;
    MatrixPnLEngine pnl;
    auto ps = adapter.buildPnLSpec(empty, *view, request);
    if (ps.isValid()) { pnl.computePnL(ps); }
}

}