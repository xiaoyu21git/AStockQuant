.pragma library

function normalizedBenchmarkText(value) {
    if (value === undefined || value === null) {
        return ""
    }
    return String(value).trim().toUpperCase()
}

function normalizePreflightFailures(value) {
    var normalized = []
    if (!value || !value.length) {
        return normalized
    }

    for (var i = 0; i < value.length; i++) {
        var item = value[i]
        if (!item) {
            continue
        }

        normalized.push({
            factorId: item.factorId !== undefined && item.factorId !== null ? String(item.factorId) : "",
            instanceId: item.instanceId !== undefined && item.instanceId !== null ? String(item.instanceId) : "",
            reason: item.reason !== undefined && item.reason !== null ? String(item.reason) : "",
            category: item.category !== undefined && item.category !== null ? String(item.category) : "",
            runFailureReason: item.runFailureReason !== undefined && item.runFailureReason !== null ? String(item.runFailureReason) : "",
            runErrorCode: item.runErrorCode !== undefined && item.runErrorCode !== null ? String(item.runErrorCode) : ""
        })
    }

    return normalized
}

function normalizeStringList(value) {
    var normalized = []
    var seen = {}
    if (value === undefined || value === null) {
        return normalized
    }

    var values = []
    if (Array.isArray(value)) {
        values = value
    } else if (typeof value === "string") {
        values = value.split(/[,;\s，；]+/)
    } else if (value.length !== undefined) {
        for (var i = 0; i < value.length; i++) {
            values.push(value[i])
        }
    } else {
        values = [value]
    }

    for (var j = 0; j < values.length; j++) {
        var item = values[j]
        if (item === undefined || item === null) {
            continue
        }

        var normalizedItem = String(item).trim()
        if (!normalizedItem || seen[normalizedItem]) {
            continue
        }

        seen[normalizedItem] = true
        normalized.push(normalizedItem)
    }

    return normalized
}

function normalizedWinRate(value) {
    if (!hasMetricValue(value)) {
        return 0
    }

    var numeric = Number(value)
    if (!isFinite(numeric)) {
        return 0
    }

    // 兼容历史百分比口径（0~100）
    if (Math.abs(numeric) > 1) {
        numeric = numeric / 100
    }

    if (numeric < 0) {
        return 0
    }
    if (numeric > 1) {
        return 1
    }
    return numeric
}

function normalizedListValue(value) {
    if (!value) {
        return []
    }

    if (Array.isArray(value)) {
        return value
    }

    if (typeof value.length === "number") {
        var normalized = []
        for (var index = 0; index < value.length; index++) {
            normalized.push(value[index])
        }
        return normalized
    }

    return []
}

function normalizedSupportMapFactorIds(factorIds) {
    if (!factorIds || factorIds.length === 0) {
        return []
    }

    var seen = ({})
    var normalized = []
    for (var index = 0; index < factorIds.length; index++) {
        var factorId = String(factorIds[index] === undefined || factorIds[index] === null ? "" : factorIds[index]).trim()
        if (!factorId || seen[factorId] === true) {
            continue
        }
        seen[factorId] = true
        normalized.push(factorId)
    }

    normalized.sort()
    return normalized
}

function stringifyLogValue(value) {
    try {
        return JSON.stringify(value)
    } catch (error) {
        return String(value)
    }
}

function normalizePercentFromRuntime(value) {
    var numericValue = Number(value)
    if (isNaN(numericValue)) {
        return 0
    }
    return Math.abs(numericValue) <= 1 ? numericValue * 100 : numericValue
}

function normalizeTradeDirection(direction) {
    var normalized = String(direction || "long").toLowerCase()
    return normalized === "short" ? "short" : "long"
}

function normalizePercentValue(value, fallback) {
    var numericValue = Number(value)
    if (isNaN(numericValue)) {
        return fallback
    }
    return Math.abs(numericValue) <= 1 ? numericValue * 100 : numericValue
}

function normalizeSignedPercentValue(value, fallback) {
    var numericValue = Number(value)
    if (isNaN(numericValue)) {
        return fallback
    }
    return Math.abs(numericValue) <= 1 ? numericValue * 100 : numericValue
}

function normalizePositiveIntOrDefault(value, fallback) {
    var numericValue = Number(value)
    if (isNaN(numericValue) || numericValue <= 0) {
        return fallback
    }
    return Math.floor(numericValue)
}

function parseAllocationList(rawValue) {
    if (!rawValue) {
        return []
    }
    if (rawValue instanceof Array) {
        return rawValue
    }
    if (typeof rawValue === "string") {
        try {
            var parsed = JSON.parse(rawValue)
            return parsed instanceof Array ? parsed : []
        } catch (error) {
            console.warn("RiskConfigurationPage: failed to parse allocations", error)
        }
    }
    return []
}

function normalizeAllocationName(item, index) {
    if (!item) {
        return "配置项 " + (index + 1)
    }
    return item.display_name
        || item.factor_id
        || ("配置项 " + (index + 1))
}

function normalizeAllocationWeight(item) {
    if (!item) {
        return 0
    }
    return normalizePercentFromRuntime(
        item.weight !== undefined ? item.weight
            : (item.ratio !== undefined ? item.ratio
                : (item.allocation !== undefined ? item.allocation : item.value))
    )
}

function normalizeSymbolValue(symbol) {
    return String(symbol || "").trim().toUpperCase()
}
