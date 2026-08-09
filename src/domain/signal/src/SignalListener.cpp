// SignalListener.cpp — SignalListener out-of-line 定义
// 析构函数/构造函数放在 .cpp 中, 避免头文件暴露 ISignalPersistencePort 完整定义
#include "ISignalListener.h"
#include "ISignalPersistencePort.h"

namespace domain::sigout {

SignalListener::SignalListener(std::unique_ptr<ISignalFormatter> formatter,
                               std::unique_ptr<ISignalPusher> pusher,
                               std::shared_ptr<ISignalPersistencePort> persistence)
    : m_formatter(std::move(formatter))
    , m_pusher(std::move(pusher))
    , m_persistence(std::move(persistence))
{
}

SignalListener::~SignalListener()
{
    if (m_persistence) {
        m_persistence->flush();
    }
}

void SignalListener::onSignals(const std::vector<SignalOutput>& signalList)
{
    if (signalList.empty()) return;

    // 1. 格式化
    std::string batch;
    batch.reserve(signalList.size() * 128);

    for (const auto& s : signalList) {
        batch += m_formatter->format(s);
        batch += '\n';
    }

    // 2. 推送
    std::string pushError;
    if (!batch.empty()) {
        bool ok = m_pusher->push(batch);
        if (!ok) {
            pushError = "push failed";
            INTERNAL_WARN_STREAM << "[SignalListener] Push failed via "
                                 << m_pusher->targetName()
                                 << " batch_bytes=" << batch.size();
        }
    }

    // 3. 异步持久化 (无论推送成功与否, 信号本身都应记录)
    if (m_persistence) {
        m_persistence->saveAsync(signalList, pushError);
    }
}

} // namespace domain::sigout
