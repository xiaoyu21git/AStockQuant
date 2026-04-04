#include "TradingCommandQueue.h"

namespace thirdparty {

bool TradingCommandQueue::enqueue(const TradingCommand& command)
{
    std::lock_guard<std::mutex> lock(mutex_);
    commands_.push_back(command);
    return true;
}

bool TradingCommandQueue::try_dequeue(TradingCommand* command)
{
    if (!command) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (commands_.empty()) {
        return false;
    }

    *command = commands_.front();
    commands_.pop_front();
    return true;
}

std::vector<TradingCommand> TradingCommandQueue::dequeue_all(size_t max_count)
{
    std::lock_guard<std::mutex> lock(mutex_);

    const size_t dequeue_count = max_count == 0
        ? commands_.size()
        : (commands_.size() < max_count ? commands_.size() : max_count);

    std::vector<TradingCommand> result;
    result.reserve(dequeue_count);

    for (size_t index = 0; index < dequeue_count; ++index) {
        result.push_back(commands_.front());
        commands_.pop_front();
    }

    return result;
}

size_t TradingCommandQueue::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return commands_.size();
}

bool TradingCommandQueue::empty() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return commands_.empty();
}

void TradingCommandQueue::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    commands_.clear();
}

} // namespace thirdparty
