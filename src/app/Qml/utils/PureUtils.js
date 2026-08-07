.pragma library

function shallowCopyMap(source) {
    var target = {}
    if (!source) {
        return target
    }

    for (var key in source) {
        if (Object.prototype.hasOwnProperty.call(source, key)) {
            target[key] = source[key]
        }
    }

    return target
}

function hasMetricValue(value) {
    return value !== undefined && value !== null
}

function hasNumericMetricValue(value) {
    if (!hasMetricValue(value)) {
        return false
    }

    var numericValue = Number(value)
    return isFinite(numericValue)
}

function positiveDatasetId(value) {
    var numeric = Number(value)
    if (!isFinite(numeric) || numeric <= 0) {
        return 0
    }
    return Math.floor(numeric)
}

function numberOrDefault(value, fallback) {
    var numericValue = Number(value)
    return isNaN(numericValue) ? fallback : numericValue
}

function parseTimestamp(value) {
    if (!value) {
        return 0
    }
    var normalizedValue = String(value).replace(" ", "T")
    var timestamp = Date.parse(normalizedValue)
    return isNaN(timestamp) ? 0 : timestamp
}

function firstDefinedValue(source, keys) {
    if (!source) {
        return undefined
    }

    for (var index = 0; index < keys.length; ++index) {
        var key = keys[index]
        if (source[key] !== undefined && source[key] !== null && source[key] !== "") {
            return source[key]
        }
    }

    return undefined
}

function cloneObject(source) {
    var target = {}
    if (!source) {
        return target
    }
    for (var key in source) {
        if (Object.prototype.hasOwnProperty.call(source, key)) {
            target[key] = source[key]
        }
    }
    return target
}

function configurationHasValues(values) {
    for (var key in values) {
        if (Object.prototype.hasOwnProperty.call(values, key)) {
            return true
        }
    }
    return false
}

function clampWidth(minWidth, preferredWidth, maxWidth) {
    return Math.round(Math.max(minWidth, Math.min(maxWidth, preferredWidth)))
}

function cloneValue(value) {
    return JSON.parse(JSON.stringify(value))
}

function isPlainObject(value) {
    return !!value && typeof value === "object" && !Array.isArray(value)
}

function countStageRules(stageData) {
    var n = 0
    var gs = Array.isArray(stageData && stageData.groups) ? stageData.groups : []
    for (var i = 0; i < gs.length; i++)
        n += Array.isArray(gs[i].rules) ? gs[i].rules.length : 0
    return n
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
