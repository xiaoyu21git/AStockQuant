.pragma library

// ── 内联自 PureUtils (pragma library 无法跨文件 import) ──
function hasMetricValue(value) {
    return value !== undefined && value !== null
}

function hasNumericMetricValue(value) {
    if (!hasMetricValue(value)) return false
    return isFinite(Number(value))
}

function runtimePercentToText(rate) {
    var numeric = Number(rate)
    if (!isFinite(numeric)) {
        numeric = 0
    }
    return (numeric * 100).toFixed(2)
}

function formatTextMetric(value, fallback) {
    return hasMetricValue(value) && String(value).length > 0 ? String(value) : fallback
}

function tradingPreviewCountText(value) {
    if (!hasNumericMetricValue(value)) {
        return "0"
    }
    return String(Math.max(0, Math.round(Number(value))))
}

function formatAssetMetric(value) {
    if (!hasNumericMetricValue(value)) {
        return "0.00"
    }

    var numericValue = Number(value)
    var absoluteValue = Math.abs(numericValue)
    if (absoluteValue >= 100000000) {
        return (numericValue / 100000000).toFixed(2) + "亿"
    }
    if (absoluteValue >= 10000) {
        return (numericValue / 10000).toFixed(2) + "万"
    }
    return numericValue.toFixed(2)
}

function formatOptionalAssetMetric(value) {
    return hasNumericMetricValue(value) ? formatAssetMetric(value) : "N/A"
}

function normalizeRuntimeDisplayValue(value, fallbackValue) {
    var fallback = fallbackValue === undefined ? "--" : fallbackValue
    if (value === undefined || value === null) {
        return fallback
    }

    var text = String(value).trim()
    return text.length > 0 ? text : fallback
}

function formatRuntimeBooleanValue(value, trueText, falseText, fallbackText) {
    if (value === true) {
        return trueText
    }
    if (value === false) {
        return falseText
    }
    return fallbackText === undefined ? "--" : fallbackText
}

function truncateDisplayText(value, maxLength) {
    var text = normalizeRuntimeDisplayValue(value, "")
    if (!text) {
        return ""
    }

    var limit = maxLength === undefined ? 32 : maxLength
    if (text.length <= limit) {
        return text
    }
    return text.substring(0, Math.max(0, limit - 1)) + "..."
}
