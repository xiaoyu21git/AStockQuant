#include "MarketDataFacade.h"
#include "factor_compute/IMarketDataStream.h"

#include <mutex>
#include <string>

namespace app::facade {

void MarketDataFacade::setDataStream(std::shared_ptr<factor::compute::IMarketDataStream> stream) {
    std::lock_guard<std::mutex> lock(mutex_);
    stream_ = std::move(stream);
}

void MarketDataFacade::startRealtime(const std::vector<std::string>& symbols) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!stream_) return;
    // symbols 由上层（Bridge/Adapter）转换为 InstrumentId 后订阅
    stream_->start();
}

void MarketDataFacade::stopRealtime() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_) stream_->stop();
}

void MarketDataFacade::setDataCallback(DataCallback cb) {
    // 预留回调入口
}

} // namespace app::facade