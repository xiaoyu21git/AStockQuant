#pragma once
#include <chrono>
#include <cstddef>

namespace engine {

using Duration = foundation::Duration;

enum class DispatchMode {
    Immediate,
    Batch,
    TimeBased,
    Hybrid
};

struct DispatchPolicy {
    DispatchMode mode = DispatchMode::Hybrid;
    size_t batch_size = 100;
    Duration interval = Duration(10'000'000); // 10ms
};

} // namespace engine
