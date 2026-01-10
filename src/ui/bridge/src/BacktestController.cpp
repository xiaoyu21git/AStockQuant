#include "BacktestController.h"

#include "GlobalModels.h"

void BacktestController::run() {
        // 不暴露 Engine 给 QML
        auto* engine = GlobalModels::backtestEngine();
        engine->setTradeSink(GlobalModels::tradeModel());
        engine->run(GlobalModels::bars());
}
#include "moc_BacktestController.cpp"