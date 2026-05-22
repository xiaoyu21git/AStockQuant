.pragma library

var GENERIC_EQUITY = 0
var CN_A_SHARE = 1
var HK_EQUITY = 2
var US_EQUITY = 3

function normalizeValue(value) {
    var numeric = Number(value)
    if (!isFinite(numeric)) {
        return GENERIC_EQUITY
    }

    numeric = Math.floor(numeric)
    if (numeric === CN_A_SHARE || numeric === HK_EQUITY || numeric === US_EQUITY) {
        return numeric
    }

    return GENERIC_EQUITY
}

function options() {
    return [
        { value: GENERIC_EQUITY, label: "通用股票" },
        { value: CN_A_SHARE, label: "A股" },
        { value: HK_EQUITY, label: "港股" },
        { value: US_EQUITY, label: "美股" }
    ]
}

function label(value) {
    var normalized = normalizeValue(value)
    var values = options()
    for (var index = 0; index < values.length; index++) {
        if (values[index].value === normalized) {
            return values[index].label
        }
    }
    return values[0].label
}

function indexForValue(value) {
    var normalized = normalizeValue(value)
    var values = options()
    for (var index = 0; index < values.length; index++) {
        if (values[index].value === normalized) {
            return index
        }
    }
    return 0
}

function valueForIndex(index) {
    var values = options()
    if (index >= 0 && index < values.length) {
        return values[index].value
    }
    return GENERIC_EQUITY
}