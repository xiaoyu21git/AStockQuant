#pragma once

namespace application::backtest {
class StrategyBacktestEntryService;
}

namespace bridge {

class StrategyBacktestRuntimeAccess final {
public:
    static void initialize();

    [[nodiscard]] static application::backtest::StrategyBacktestEntryService* entryService();

    static void resetForTesting();
    static void installEntryServiceForTesting(
        application::backtest::StrategyBacktestEntryService* entryService);

private:
    StrategyBacktestRuntimeAccess() = delete;
};

} // namespace bridge