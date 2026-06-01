.pragma library

.import "./StrategyCreationUtils.js" as CreationUtils

function isObject(value) {
    return !!value && typeof value === "object" && !Array.isArray(value)
}

function toPlainJsValue(rawValue) {
    if (rawValue === undefined || rawValue === null) {
        return rawValue
    }

    if (typeof rawValue === "object") {
        try {
            return JSON.parse(JSON.stringify(rawValue))
        } catch (error) {
        }
    }

    return rawValue
}

function positiveDatasetId(value) {
    var number = Number(value)
    return isNaN(number) || number <= 0 ? -1 : Math.round(number)
}

function normalizeBacktestDateText(value, fallbackText) {
    var text = String(value || "").trim()
    return text ? text : fallbackText
}

function objectOrEmpty(value) {
    return isObject(value) ? value : ({})
}

function rootParameters(source) {
    var root = objectOrEmpty(source)
    return objectOrEmpty(root.parameters)
}

function resolveRuleProfile(source) {
    return objectOrEmpty(rootParameters(source).rule_profile)
}

function resolveExecutionPolicy(source) {
    return objectOrEmpty(rootParameters(source).execution_policy)
}

function resolveBacktestAssumptions(source) {
    return objectOrEmpty(rootParameters(source).backtest_assumptions)
}

function resolveUniverseContext(source) {
    var scopeContext = objectOrEmpty(rootParameters(source).strategy_scope_context)
    var universeType = String(scopeContext.universeType || "").trim()
    var universeId = String(scopeContext.universeId || "").trim()
    return {
        universeType: universeType,
        universeId: universeId,
        indexSymbol: universeType === "index" ? universeId : universeId
    }
}

function resolveStrategyTypeIndex(strategyDetail) {
    var strategy = toPlainJsValue(strategyDetail) || ({})
    var explicitTypeIndex = Number(strategy.strategyTypeIndex)
    if (isFinite(explicitTypeIndex) && explicitTypeIndex >= 0) {
        var normalizedTypeIndex = CreationUtils.normalizeStrategyTypeIndex(explicitTypeIndex)
        if (normalizedTypeIndex !== CreationUtils.StrategyTypeIndex.Invalid) {
            return normalizedTypeIndex
        }
    }

    return CreationUtils.StrategyTypeIndex.Invalid
}

function mergeParameterMaps(baseValue, overrideValue) {
    var baseMap = toPlainJsValue(baseValue)
    var overrideMap = toPlainJsValue(overrideValue)
    var baseIsObject = !!(baseMap && typeof baseMap === "object" && !Array.isArray(baseMap))
    var overrideIsObject = !!(overrideMap && typeof overrideMap === "object" && !Array.isArray(overrideMap))

    if (!baseIsObject) {
        return overrideIsObject ? overrideMap : (overrideMap !== undefined ? overrideMap : baseMap)
    }
    if (!overrideIsObject) {
        return baseMap
    }

    var merged = ({})
    for (var baseKey in baseMap) {
        merged[baseKey] = baseMap[baseKey]
    }
    for (var overrideKey in overrideMap) {
        var overrideItem = overrideMap[overrideKey]
        if (overrideItem && typeof overrideItem === "object" && !Array.isArray(overrideItem)
                && merged[overrideKey] && typeof merged[overrideKey] === "object" && !Array.isArray(merged[overrideKey])) {
            merged[overrideKey] = mergeParameterMaps(merged[overrideKey], overrideItem)
        } else {
            merged[overrideKey] = overrideItem
        }
    }
    return merged
}

function buildParameterSource(strategyDetail, latestBacktest) {
    var detailParameters = toPlainJsValue((strategyDetail && strategyDetail.parameters) || ({})) || ({})
    var latestRuntimeParameters = toPlainJsValue((latestBacktest && latestBacktest.runtimeParameters) || ({})) || ({})
    return mergeParameterMaps(detailParameters, latestRuntimeParameters)
}

function buildDynamicParamConfigs(strategyTypeIndex) {
    var configs = CreationUtils.buildParamConfigs(strategyTypeIndex)
    return Array.isArray(configs) ? configs : []
}

function isBacktestDatasetSelectable(dataset) {
    if (!dataset) {
        return false
    }

    var datasetId = positiveDatasetId(dataset.id !== undefined ? dataset.id : dataset.value)
    if (datasetId <= 0) {
        return false
    }

    var isBacktestReady = dataset.isBacktestReady === true
    var tags = Array.isArray(dataset.tags) ? dataset.tags : []
    if (!isBacktestReady && tags.indexOf("factor_backtest_ready") < 0) {
        return false
    }

    var availableFields = Array.isArray(dataset.availableFields) ? dataset.availableFields : []
    return availableFields.length > 0
}

function buildCacheDatasetOptions(datasetList) {
    var options = [{ label: "请选择清洗数据", value: -1, raw: null }]
    var list = Array.isArray(datasetList) ? datasetList : []
    var selectable = []

    for (var index = 0; index < list.length; ++index) {
        var dataset = toPlainJsValue(list[index]) || null
        if (isBacktestDatasetSelectable(dataset)) {
            selectable.push(dataset)
        }
    }

    selectable.sort(function(left, right) {
        return positiveDatasetId(right && right.id) - positiveDatasetId(left && left.id)
    })

    for (var selectableIndex = 0; selectableIndex < selectable.length; ++selectableIndex) {
        var current = selectable[selectableIndex]
        var datasetId = positiveDatasetId(current.id)
        var displayName = String(current.displayName || current.name || "").trim()
        var startDate = String(current.startDate || "").trim()
        var endDate = String(current.endDate || "").trim()
        var label = displayName ? displayName : ("清洗数据 #" + datasetId)
        if (startDate && endDate) {
            label += " (" + startDate + " ~ " + endDate + ")"
        }

        options.push({
            label: label,
            value: datasetId,
            raw: current
        })
    }

    return options
}

function buildDynamicParamGroups(configs) {
    var resolvedConfigs = Array.isArray(configs) ? configs : []
    var commonParamIds = {
        positionSize: true,
        stopLoss: true,
        takeProfit: true,
        maxDrawdownLimit: true,
        rebalanceDays: true
    }
    var commonParams = []
    var strategySpecificParams = []

    for (var index = 0; index < resolvedConfigs.length; ++index) {
        var config = resolvedConfigs[index]
        if (!config || !config.id) {
            continue
        }

        if (commonParamIds[config.id]) {
            commonParams.push(config.id)
        } else {
            strategySpecificParams.push(config.id)
        }
    }

    var groups = []
    if (commonParams.length > 0) {
        groups.push({
            id: "commonBacktestParams",
            name: "通用回测参数",
            description: "只保留仓位、风控和调仓频率等核心回测参数。",
            minColumnWidth: 320,
            maxColumns: 2,
            params: commonParams
        })
    }
    if (strategySpecificParams.length > 0) {
        groups.push({
            id: "strategySpecificParams",
            name: "当前策略参数",
            description: "仅展示当前策略类型真正参与运行的参数。",
            minColumnWidth: 320,
            maxColumns: 2,
            params: strategySpecificParams
        })
    }
    return groups
}

function selectedCacheDatasetInfo(selectedDatasetId, selectedDatasetInfo, cacheDatasetOptions) {
    if (selectedDatasetId > 0 && selectedDatasetInfo) {
        var info = toPlainJsValue(selectedDatasetInfo) || ({})
        if (positiveDatasetId(info.id) === selectedDatasetId) {
            return info
        }
    }

    var options = Array.isArray(cacheDatasetOptions) ? cacheDatasetOptions : []
    for (var index = 0; index < options.length; ++index) {
        var option = options[index]
        if (positiveDatasetId(option && option.value) === selectedDatasetId) {
            return toPlainJsValue(option.raw) || ({})
        }
    }

    return ({})
}

function resolveWindowSelection(selectedStartDate,
                                selectedEndDate,
                                selectedDataSourceMode,
                                selectedDatasetId,
                                selectedDatasetInfo,
                                cacheDatasetOptions) {
    var startDate = normalizeBacktestDateText(selectedStartDate, "")
    var endDate = normalizeBacktestDateText(selectedEndDate, "")

    if (selectedDataSourceMode === 1) {
        var datasetInfo = selectedCacheDatasetInfo(
            selectedDatasetId,
            selectedDatasetInfo,
            cacheDatasetOptions)
        var datasetStartDate = normalizeBacktestDateText(datasetInfo.startDate, "")
        var datasetEndDate = normalizeBacktestDateText(datasetInfo.endDate, "")
        if (datasetStartDate) {
            startDate = datasetStartDate
        }
        if (datasetEndDate) {
            endDate = datasetEndDate
        }
    }

    return {
        startDate: startDate,
        endDate: endDate
    }
}

function buildRuntimeParameters(strategyTypeIndex, sourceParameters) {
    var sourceParams = toPlainJsValue(sourceParameters) || ({})
    var normalizedStrategyTypeIndex = CreationUtils.normalizeStrategyTypeIndex(strategyTypeIndex)
    var mappedValues = ({})
    var persistedRuleProfile = resolveRuleProfile(sourceParams)
    var persistedExecutionPolicy = resolveExecutionPolicy(sourceParams)
    var persistedBacktestAssumptions = resolveBacktestAssumptions(sourceParams)

    function assignIfPresent(targetKey, sourceKeys, transform) {
        for (var index = 0; index < sourceKeys.length; ++index) {
            var key = sourceKeys[index]
            var resolvedValue = sourceParams[key]
            if (resolvedValue === undefined || resolvedValue === null || resolvedValue === "") {
                resolvedValue = persistedRuleProfile[key]
            }
            if (resolvedValue === undefined || resolvedValue === null || resolvedValue === "") {
                resolvedValue = persistedExecutionPolicy[key]
            }
            if (resolvedValue === undefined || resolvedValue === null || resolvedValue === "") {
                resolvedValue = persistedBacktestAssumptions[key]
            }
            if (resolvedValue === undefined || resolvedValue === null || resolvedValue === "") {
                if (key === "stopLoss") {
                    resolvedValue = persistedRuleProfile.stopLossPercent
                } else if (key === "takeProfit") {
                    resolvedValue = persistedRuleProfile.takeProfitPercent
                } else if (key === "rebalanceDays") {
                    resolvedValue = persistedRuleProfile.rebalanceDays
                }
            }
            if (resolvedValue === undefined || resolvedValue === null || resolvedValue === "") {
                continue
            }
            mappedValues[targetKey] = transform ? transform(resolvedValue) : resolvedValue
            return
        }
    }

    function ratioToPercent(value) {
        var numeric = Number(value)
        if (!isFinite(numeric)) {
            return value
        }
        return numeric <= 1 ? numeric * 100 : numeric
    }

    assignIfPresent("positionSize", ["positionSize"], ratioToPercent)
    assignIfPresent("stopLoss", ["stopLoss"], ratioToPercent)
    assignIfPresent("takeProfit", ["takeProfit"], ratioToPercent)
    assignIfPresent("maxDrawdownLimit", ["maxDrawdownLimit"], Number)
    assignIfPresent("rebalanceDays", ["rebalanceDays"], Number)

    if (normalizedStrategyTypeIndex === CreationUtils.StrategyTypeIndex.TrendFollowing) {
        assignIfPresent("fastPeriod", ["fastPeriod"], Number)
        assignIfPresent("slowPeriod", ["slowPeriod"], Number)
    } else if (normalizedStrategyTypeIndex === CreationUtils.StrategyTypeIndex.TrendBreakout) {
        assignIfPresent("longTrendPeriod", ["longTrendPeriod"], Number)
        assignIfPresent("breakoutLookbackPeriod", ["breakoutLookbackPeriod"], Number)
        assignIfPresent("breakoutThreshold", ["breakoutThreshold"], ratioToPercent)
        assignIfPresent("adxPeriod", ["adxPeriod"], Number)
        assignIfPresent("adxThreshold", ["adxThreshold"], Number)
        assignIfPresent("exitMaPeriod", ["exitMaPeriod"], Number)
        assignIfPresent("atrPeriod", ["atrPeriod"], Number)
        assignIfPresent("atrMultiplier", ["atrMultiplier"], Number)
    } else if (normalizedStrategyTypeIndex === CreationUtils.StrategyTypeIndex.MeanReversion) {
        assignIfPresent("bollPeriod", ["bollPeriod"], Number)
        assignIfPresent("bollStd", ["bollStd"], Number)
        assignIfPresent("reversionThreshold", ["reversionThreshold"], Number)
    } else if (normalizedStrategyTypeIndex === CreationUtils.StrategyTypeIndex.Momentum) {
        assignIfPresent("momentumPeriod", ["momentumPeriod"], Number)
        assignIfPresent("topN", ["topN"], Number)
    } else if (normalizedStrategyTypeIndex === CreationUtils.StrategyTypeIndex.Arbitrage) {
        assignIfPresent("spreadThreshold", ["spreadThreshold"], Number)
        assignIfPresent("entryZScore", ["entryZScore"], Number)
        assignIfPresent("exitZScore", ["exitZScore"], Number)
    } else if (normalizedStrategyTypeIndex === CreationUtils.StrategyTypeIndex.MachineLearning) {
        assignIfPresent("featureWindow", ["featureWindow"], Number)
        assignIfPresent("predictionDays", ["predictionDays"], Number)
        assignIfPresent("trainingDays", ["trainingDays"], Number)
        assignIfPresent("confidenceThreshold", ["confidenceThreshold"], ratioToPercent)
    } else if (normalizedStrategyTypeIndex === CreationUtils.StrategyTypeIndex.MultiFactor) {
        assignIfPresent("factorTypes", ["factorTypes"])
    } else if (normalizedStrategyTypeIndex === CreationUtils.StrategyTypeIndex.HighFrequency) {
        assignIfPresent("timeframe", ["timeframe"])
    } else if (normalizedStrategyTypeIndex === CreationUtils.StrategyTypeIndex.EventDriven) {
        assignIfPresent("eventTypes", ["eventTypes"])
    } else if (normalizedStrategyTypeIndex === CreationUtils.StrategyTypeIndex.Custom) {
        assignIfPresent("customCode", ["customCode"])
    }

    return mappedValues
}

function resolveDataSourceModeValue(rawValue) {
    var numeric = Number(rawValue)
    if (isFinite(numeric)) {
        var rounded = Math.round(numeric)
        if (rounded === 0) {
            return 0
        }
        if (rounded === 1 || rounded === 2) {
            return 1
        }
    }

    var normalized = String(rawValue || "").trim().toLowerCase()
    if (normalized === "raw") {
        return 0
    }
    if (normalized === "cleaned" || normalized === "cachedataset" || normalized === "cache_dataset" || normalized === "cache") {
        return 1
    }
    return 0
}

var StrategyStructureAdapter = {
    resolveRuleProfile: resolveRuleProfile,
    resolveExecutionPolicy: resolveExecutionPolicy,
    resolveBacktestAssumptions: resolveBacktestAssumptions,
    resolveUniverseContext: resolveUniverseContext,
    resolveStrategyTypeIndex: resolveStrategyTypeIndex,
    mergeParameterMaps: mergeParameterMaps,
    buildParameterSource: buildParameterSource,
    buildDynamicParamConfigs: buildDynamicParamConfigs,
    isBacktestDatasetSelectable: isBacktestDatasetSelectable,
    buildCacheDatasetOptions: buildCacheDatasetOptions,
    buildDynamicParamGroups: buildDynamicParamGroups,
    selectedCacheDatasetInfo: selectedCacheDatasetInfo,
    resolveWindowSelection: resolveWindowSelection,
    buildRuntimeParameters: buildRuntimeParameters,
    resolveDataSourceModeValue: resolveDataSourceModeValue
}
