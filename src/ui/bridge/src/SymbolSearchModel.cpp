#include "SymbolSearchModel.h"
#include "StockNameResolver.h"
#include "foundation/log/logging.hpp"
#include "../../../infrastructure/include/database/NativePgConnectionPool.h"

#include <algorithm>

SymbolSearchModel::SymbolSearchModel(QObject* parent)
    : QAbstractListModel(parent) {}

int SymbolSearchModel::rowCount(const QModelIndex&) const {
    return static_cast<int>(m_filtered.size());
}

QVariant SymbolSearchModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_filtered.size()) return {};
    const auto& e = m_all.at(m_filtered.at(index.row()));
    switch (role) {
    case SymbolRole:  return e.symbol;
    case NameRole:    return e.secName;
    case ExchangeRole:return e.exchange;
    }
    return {};
}

QHash<int, QByteArray> SymbolSearchModel::roleNames() const {
    return {{SymbolRole, "symbol"}, {NameRole, "secName"}, {ExchangeRole, "exchange"}};
}

void SymbolSearchModel::init() {
    auto& pool = astock::database::NativePgConnectionPool::instance();
    if (!pool.isInitialized()) {
        INTERNAL_WARN_STREAM << "[SymbolSearch] DB pool not initialized";
        return;
    }

    auto db = pool.getConnection();
    if (!db || !db->isOpen()) {
        INTERNAL_WARN_STREAM << "[SymbolSearch] DB connection failed";
        return;
    }

    auto result = db->executeQuery(
        "SELECT symbol, name, exchange FROM ref.symbol_info WHERE asset_class='STOCK' ORDER BY symbol");
    int rows = static_cast<int>(result.rowCount());
    INTERNAL_DEBUG_STREAM << "[SymbolSearch] query returned" << rows << "rows";
    if (rows == 0) {
        INTERNAL_WARN_STREAM << "[SymbolSearch] symbol_info table empty or missing";
        return;
    }

    beginResetModel();
    m_all.clear();
    m_all.reserve(rows);
    for (int i = 0; i < rows; ++i) {
        const auto& row = result.getRow(i);
        SymbolEntry e;
        e.symbol   = QString::fromStdString(row.getString("symbol"));
        e.secName  = QString::fromStdString(row.getString("name"));
        e.exchange = QString::fromStdString(row.getString("exchange"));
        m_all.push_back(e);
    }
    m_filtered.clear();
    for (int i = 0; i < m_all.size(); ++i) m_filtered.push_back(i);
    endResetModel();
    INTERNAL_DEBUG_STREAM << "[SymbolSearch] loaded" << m_all.size() << "symbols, first="
             << (m_all.isEmpty() ? "none" : (m_all[0].symbol + " " + m_all[0].secName).toStdString());
    emit countChanged();
}

QVariantMap SymbolSearchModel::getRow(int index) const {
    QVariantMap map;
    if (index < 0 || index >= m_filtered.size()) return map;
    const auto& e = m_all.at(m_filtered.at(index));
    map["symbol"]   = e.symbol;
    map["secName"]  = e.secName;
    map["exchange"] = e.exchange;
    return map;
}

QString SymbolSearchModel::nameForSymbol(const QString& symbol)
{
    return StockNameResolver::name(symbol);
}

QString SymbolSearchModel::displayName(const QString& symbol)
{
    return StockNameResolver::displayName(symbol);
}

void SymbolSearchModel::search(const QString& keyword) {
    beginResetModel();
    if (keyword.isEmpty()) {
        m_filtered.clear();
        for (int i = 0; i < m_all.size(); ++i) m_filtered.push_back(i);
        endResetModel();
        emit countChanged();
        return;
    }

    const QString kw = keyword.trimmed().toUpper();
    m_filtered.clear();

    // 1. 代码前缀精确匹配（6位数字优先）
    bool isNumeric = true;
    for (auto& c : kw) { if (!c.isDigit()) { isNumeric = false; break; } }
    if (isNumeric) {
        for (int i = 0; i < m_all.size(); ++i) {
            // 去掉后缀后的纯代码前缀匹配
            QString code = m_all[i].symbol.left(m_all[i].symbol.indexOf('.'));
            if (code.startsWith(kw))
                m_filtered.push_back(i);
        }
    } else {
        // 2. 名称包含 + 拼音首字母匹配
        for (int i = 0; i < m_all.size(); ++i) {
            if (m_all[i].secName.contains(kw, Qt::CaseInsensitive))
                m_filtered.push_back(i);
        }
        // 3. 如果名称匹配太少，代码包含也加上
        if (m_filtered.size() < 5) {
            for (int i = 0; i < m_all.size(); ++i) {
                if (m_all[i].symbol.contains(kw, Qt::CaseInsensitive)) {
                    if (std::find(m_filtered.begin(), m_filtered.end(), i) == m_filtered.end())
                        m_filtered.push_back(i);
                }
            }
        }
    }

    // 最多返回 8 条
    if (m_filtered.size() > 8) m_filtered.resize(8);
    endResetModel();
    emit countChanged();
}
