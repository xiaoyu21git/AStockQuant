// StrategyDataAdapter.js
// 策略数据映射适配器，将策略数据转换为统一的卡片数据格式

/**
 * 策略数据适配器
 * 提供策略数据到统一卡片数据格式的转换功能
 */

function normalizeStrategyStatus(status) {
    return status ? status.toString().trim().toUpperCase() : "";
}

function strategyStatusFromIndex(statusIndex) {
    switch (Number(statusIndex)) {
    case 0:
        return "DRAFT";
    case 1:
        return "ACTIVE";
    case 2:
        return "INACTIVE";
    case 3:
        return "TESTING";
    case 4:
        return "ARCHIVED";
    case 5:
        return "RUNNING";
    case 6:
        return "PAUSED";
    case 7:
        return "STOPPED";
    default:
        return "";
    }
}

function resolveStrategyBusinessStatus(strategy) {
    if (!strategy) {
        return "";
    }

    return strategyStatusFromIndex(strategy.statusIndex)
}

function resolveStrategyIdentifier(strategy) {
    if (!strategy) {
        return "";
    }

    return strategy.strategyId || "";
}

function resolveBoundStrategyIdentifier(tradingConfiguration) {
    var identifiers = resolveBoundStrategyIdentifiers(tradingConfiguration)
    return identifiers.length > 0 ? identifiers[0] : ""
}

function resolveBoundStrategyIdentifiers(tradingConfiguration) {
    if (!tradingConfiguration) {
        return [];
    }

    var identifiers = []
    var appendIdentifier = function(value) {
        var identifier = String(value || "").trim()
        if (!identifier || identifiers.indexOf(identifier) !== -1) {
            return
        }
        identifiers.push(identifier)
    }

    var bindings = tradingConfiguration.boundStrategies || []
    if (Array.isArray(bindings)) {
        for (var index = 0; index < bindings.length; ++index) {
            var entry = bindings[index]
            if (!entry) {
                continue
            }
            if (typeof entry === "string") {
                appendIdentifier(entry)
                continue
            }
            appendIdentifier(entry.strategyId)
        }
    }

    appendIdentifier(tradingConfiguration.boundStrategyId || tradingConfiguration.strategyId)
    return identifiers;
}

function isStrategyBoundToTradingConfiguration(strategy, tradingConfiguration) {
    var strategyId = resolveStrategyIdentifier(strategy)
    if (!strategyId) {
        return false
    }

    return resolveBoundStrategyIdentifiers(tradingConfiguration).indexOf(strategyId) !== -1
}

function isChinaTradingSessionOpen(nowDate) {
    var now = nowDate || new Date();
    var dayOfWeek = now.getDay();
    if (dayOfWeek === 0 || dayOfWeek === 6) {
        return false;
    }

    var totalMinutes = now.getHours() * 60 + now.getMinutes();
    var morningOpen = 9 * 60 + 15;
    var morningClose = 11 * 60 + 30;
    var afternoonOpen = 13 * 60;
    var afternoonClose = 15 * 60;
    return (totalMinutes >= morningOpen && totalMinutes < morningClose)
        || (totalMinutes >= afternoonOpen && totalMinutes < afternoonClose);
}

function isTradingConfigurationEnabled(tradingConfiguration) {
    return !!(tradingConfiguration && tradingConfiguration.enabled) && !tradingConfiguration.readOnly;
}

function hasMarketCalendarSnapshot(marketCalendarSnapshot) {
    return !!(marketCalendarSnapshot && (marketCalendarSnapshot.source || marketCalendarSnapshot.sessionPhase || marketCalendarSnapshot.calendarDate));
}

function isMarketCalendarSessionOpen(marketCalendarSnapshot) {
    return !!(hasMarketCalendarSnapshot(marketCalendarSnapshot) && marketCalendarSnapshot.sessionOpen);
}

function resolveRuntimeSnapshotStatus(runtimeSnapshot) {
    var runtimeState = normalizeStrategyStatus(runtimeSnapshot && runtimeSnapshot.state);
    if (!runtimeState) {
        return "";
    }

    if (runtimeState === "RUNNING") {
        return "RUNNING";
    }
    if (runtimeState === "STARTING" || runtimeState === "INITIALIZED" || runtimeState === "CREATED") {
        return "STARTING";
    }
    if (runtimeState === "STOPPING") {
        return "STOPPING";
    }
    if (runtimeState === "ERROR" || (runtimeSnapshot && runtimeSnapshot.hasError)) {
        return "ERROR";
    }
    if (runtimeState === "STOPPED") {
        return "STOPPED";
    }

    return "";
}

function resolveStrategyRuntimeStatus(strategy, tradingConfiguration, runtimeSnapshot, marketCalendarSnapshot, nowDate) {
    var businessStatus = resolveStrategyBusinessStatus(strategy);
    if (!businessStatus) {
        return "STOPPED";
    }

    if (businessStatus === "INACTIVE") {
        return "STOPPED";
    }
    if (businessStatus === "ARCHIVED") {
        return "DEPRECATED";
    }
    if (businessStatus === "PAUSED" || businessStatus === "STOPPED"
        || businessStatus === "DEPRECATED" || businessStatus === "PENDING") {
        return businessStatus;
    }
    // RUNNING 必须有运行时快照证实，否则降级为 ACTIVE
    if (businessStatus === "RUNNING") {
        var rt = resolveRuntimeSnapshotStatus(runtimeSnapshot);
        return rt === "RUNNING" ? "RUNNING" : "ACTIVE";
    }

    var runtimeStatus = resolveRuntimeSnapshotStatus(runtimeSnapshot);
    if (runtimeStatus) {
        return runtimeStatus;
    }

    if (businessStatus === "ACTIVE" || businessStatus === "TESTING") {
        if (!isStrategyBoundToTradingConfiguration(strategy, tradingConfiguration)) {
            return "STOPPED";
        }

        if (!isTradingConfigurationEnabled(tradingConfiguration)) {
            return "STOPPED";
        }

        // 状态由运行时快照决定，不根据时间推断
        return businessStatus;
    }

    return businessStatus;
}

function isRunningDisplayStatus(status) {
    return normalizeStrategyStatus(status) === "RUNNING";
}