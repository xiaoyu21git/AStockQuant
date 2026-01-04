// domain/strategies/StrategyAction.h
#pragma once

namespace domain {

enum class StrategyAction {
    None,        // 不产生任何信号
    OpenLong,    // 开多
    CloseLong    // 平多
};

}
