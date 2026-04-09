.pragma library

.import QtQuick.LocalStorage 2.0 as LocalStorage

var DATABASE_NAME = "AStockQuantEngine_CustomStockPools"
var DATABASE_VERSION = "1.0"
var DATABASE_DESCRIPTION = "Custom stock pools"
var DATABASE_SIZE = 1024 * 1024

function database() {
    var db = LocalStorage.LocalStorage.openDatabaseSync(
        DATABASE_NAME,
        DATABASE_VERSION,
        DATABASE_DESCRIPTION,
        DATABASE_SIZE
    )

    db.transaction(function(tx) {
        tx.executeSql(
            "CREATE TABLE IF NOT EXISTS custom_stock_pools ("
            + "pool_id TEXT PRIMARY KEY, "
            + "pool_name TEXT NOT NULL, "
            + "symbols_json TEXT NOT NULL, "
            + "notes TEXT, "
            + "created_at TEXT NOT NULL, "
            + "updated_at TEXT NOT NULL)"
        )
    })

    return db
}

function cloneValue(value) {
    if (value === undefined) {
        return undefined
    }

    return JSON.parse(JSON.stringify(value))
}

function nowText() {
    return new Date().toISOString()
}

function generatePoolId() {
    return "custom_pool_" + Date.now().toString(36) + "_" + Math.floor(Math.random() * 1679616).toString(36)
}

function normalizeSymbolList(source) {
    var rawValues = []
    if (Array.isArray(source)) {
        rawValues = source
    } else if (source !== undefined && source !== null) {
        rawValues = String(source).split(/[,;\s，；]+/)
    }

    var normalized = []
    var seen = {}
    for (var index = 0; index < rawValues.length; ++index) {
        var token = String(rawValues[index] || "").trim().toUpperCase()
        if (!token || seen[token]) {
            continue
        }

        seen[token] = true
        normalized.push(token)
    }

    return normalized
}

function parseSymbols(jsonText) {
    if (!jsonText) {
        return []
    }

    try {
        return normalizeSymbolList(JSON.parse(jsonText))
    } catch (error) {
        return normalizeSymbolList(jsonText)
    }
}

function normalizePoolRecord(pool) {
    var source = pool || ({})
    var normalizedSymbols = normalizeSymbolList(source.symbols || source.symbol_pool || source.symbolPool || [])
    return {
        id: String(source.id || source.poolId || source.pool_id || generatePoolId()).trim(),
        name: String(source.name || source.poolName || source.pool_name || "").trim(),
        symbols: normalizedSymbols,
        notes: String(source.notes || "").trim(),
        createdAt: String(source.createdAt || source.created_at || nowText()).trim(),
        updatedAt: String(source.updatedAt || source.updated_at || nowText()).trim()
    }
}

function listPools() {
    var pools = []
    database().readTransaction(function(tx) {
        var rs = tx.executeSql(
            "SELECT pool_id, pool_name, symbols_json, notes, created_at, updated_at "
            + "FROM custom_stock_pools ORDER BY updated_at DESC, pool_name ASC"
        )

        for (var index = 0; index < rs.rows.length; ++index) {
            var row = rs.rows.item(index)
            pools.push({
                id: row.pool_id,
                name: row.pool_name,
                symbols: parseSymbols(row.symbols_json),
                notes: row.notes || "",
                createdAt: row.created_at,
                updatedAt: row.updated_at
            })
        }
    })

    return pools
}

function getPoolById(poolId) {
    var normalizedPoolId = String(poolId || "").trim()
    if (!normalizedPoolId) {
        return ({})
    }

    var pool = ({})
    database().readTransaction(function(tx) {
        var rs = tx.executeSql(
            "SELECT pool_id, pool_name, symbols_json, notes, created_at, updated_at "
            + "FROM custom_stock_pools WHERE pool_id = ?",
            [normalizedPoolId]
        )

        if (rs.rows.length > 0) {
            var row = rs.rows.item(0)
            pool = {
                id: row.pool_id,
                name: row.pool_name,
                symbols: parseSymbols(row.symbols_json),
                notes: row.notes || "",
                createdAt: row.created_at,
                updatedAt: row.updated_at
            }
        }
    })

    return pool
}

function savePool(pool) {
    var record = normalizePoolRecord(pool)
    if (!record.name) {
        throw new Error("股票池名称不能为空")
    }
    if (record.symbols.length === 0) {
        throw new Error("股票池至少需要一个标的")
    }

    var existing = getPoolById(record.id)
    if (existing && existing.id) {
        record.createdAt = existing.createdAt || record.createdAt
    }
    record.updatedAt = nowText()

    database().transaction(function(tx) {
        tx.executeSql(
            "INSERT OR REPLACE INTO custom_stock_pools (pool_id, pool_name, symbols_json, notes, created_at, updated_at) "
            + "VALUES (?, ?, ?, ?, ?, ?)",
            [
                record.id,
                record.name,
                JSON.stringify(record.symbols),
                record.notes,
                record.createdAt,
                record.updatedAt
            ]
        )
    })

    return record
}

function deletePool(poolId) {
    var normalizedPoolId = String(poolId || "").trim()
    if (!normalizedPoolId) {
        return false
    }

    var deleted = false
    database().transaction(function(tx) {
        var rs = tx.executeSql("DELETE FROM custom_stock_pools WHERE pool_id = ?", [normalizedPoolId])
        deleted = rs.rowsAffected > 0
    })
    return deleted
}

function extractLinkedStockPool(entity) {
    var source = entity || ({})
    var parameters = source.parameters || ({})
    var poolId = String(
        parameters.linked_stock_pool_id
        || parameters.linkedStockPoolId
        || source.linked_stock_pool_id
        || source.linkedStockPoolId
        || ""
    ).trim()
    var poolName = String(
        parameters.linked_stock_pool_name
        || parameters.linkedStockPoolName
        || source.linked_stock_pool_name
        || source.linkedStockPoolName
        || ""
    ).trim()
    var symbols = normalizeSymbolList(
        parameters.linked_stock_pool_symbols
        || parameters.linkedStockPoolSymbols
        || source.linked_stock_pool_symbols
        || source.linkedStockPoolSymbols
        || []
    )

    return {
        poolId: poolId,
        poolName: poolName,
        symbols: symbols,
        hasBinding: !!poolId
    }
}

function applyLinkedStockPool(entity, pool) {
    var updated = cloneValue(entity || ({})) || ({})
    var parameters = cloneValue(updated.parameters || ({})) || ({})
    var binding = normalizePoolRecord({
        id: pool && (pool.id || pool.poolId || pool.pool_id),
        name: pool && (pool.name || pool.poolName || pool.pool_name),
        symbols: pool && (pool.symbols || pool.symbol_pool || pool.symbolPool || [])
    })

    parameters.linked_stock_pool_id = binding.id
    parameters.linked_stock_pool_name = binding.name
    parameters.linked_stock_pool_symbols = binding.symbols
    updated.parameters = parameters
    return updated
}

function clearLinkedStockPool(entity) {
    var updated = cloneValue(entity || ({})) || ({})
    var parameters = cloneValue(updated.parameters || ({})) || ({})

    delete parameters.linked_stock_pool_id
    delete parameters.linked_stock_pool_name
    delete parameters.linked_stock_pool_symbols
    delete parameters.linkedStockPoolId
    delete parameters.linkedStockPoolName
    delete parameters.linkedStockPoolSymbols

    updated.parameters = parameters
    return updated
}

var CustomStockPoolStore = {
    cloneValue: cloneValue,
    normalizeSymbolList: normalizeSymbolList,
    listPools: listPools,
    getPoolById: getPoolById,
    savePool: savePool,
    deletePool: deletePool,
    extractLinkedStockPool: extractLinkedStockPool,
    applyLinkedStockPool: applyLinkedStockPool,
    clearLinkedStockPool: clearLinkedStockPool
}