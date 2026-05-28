#include "../include/BacktestApplicationService.h"

namespace application::backtest {

BacktestApplicationService::BacktestApplicationService(const BacktestEngineGateway& engineGateway,
                                                       IAsyncBacktestScheduler& asyncScheduler)
    : engineGateway_(engineGateway)
    , asyncScheduler_(asyncScheduler)
{
}

BacktestResultDto BacktestApplicationService::runInline(const BacktestRequest& request) const
{
    return engineGateway_.execute(request);
}

AsyncBacktestHandle BacktestApplicationService::run(const BacktestRequest& request)
{
    return asyncScheduler_.submit(request);
}

AsyncBacktestHandleList BacktestApplicationService::runBatch(const BacktestBatchRequest& batchRequest)
{
    AsyncBacktestHandleList handles;
    handles.reserve(batchRequest.requests.size());

    for (const BacktestRequest& request : batchRequest.requests) {
        handles.add(asyncScheduler_.submit(request));
    }

    return handles;
}

BacktestProgressSnapshot BacktestApplicationService::progress(const AsyncBacktestHandle& handle) const
{
    return asyncScheduler_.progress(handle);
}

std::optional<BacktestResultDto> BacktestApplicationService::tryCollect(const AsyncBacktestHandle& handle)
{
    return asyncScheduler_.tryCollect(handle);
}

void BacktestApplicationService::cancel(const CancellationRequest& request)
{
    asyncScheduler_.requestCancel(request);
}

} // namespace application::backtest