#include "StockNameResolver.h"
#include "../../../infrastructure/include/database/NativeMySQLConnectionPool.h"

#include <mutex>

QString StockNameResolver::name(const QString& symbol)
{
    if (symbol.isEmpty()) return {};

    static QHash<QString, QString> s_cache;
    static std::once_flag s_loaded;

    std::call_once(s_loaded, []() {
        auto& pool = astock::database::NativeMySQLConnectionPool::instance();
        if (!pool.isInitialized()) return;
        auto db = pool.getConnection();
        if (!db || !db->isOpen()) return;
        auto rs = db->executeQuery(
            "SELECT symbol, name FROM symbol_info WHERE asset_class='STOCK'");
        for (int i = 0; i < static_cast<int>(rs.rowCount()); ++i) {
            const auto& row = rs.getRow(i);
            s_cache[QString::fromStdString(row.getString("symbol"))] =
                QString::fromStdString(row.getString("name"));
        }
    });

    return s_cache.value(symbol);
}

QString StockNameResolver::displayName(const QString& symbol)
{
    const QString n = name(symbol);
    if (n.isEmpty()) return symbol;
    return n + QStringLiteral(" ") + symbol;
}
