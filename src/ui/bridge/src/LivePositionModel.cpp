#include "LivePositionModel.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

LivePositionModel::LivePositionModel(QObject* parent)
    : QAbstractListModel(parent)
{
    m_roleNames[SymbolRole]      = "symbol";
    m_roleNames[QuantityRole]    = "quantity";
    m_roleNames[PriceRole]       = "price";
    m_roleNames[MarketValueRole] = "marketValue";
    m_roleNames[DirectionRole]   = "direction";
}

int LivePositionModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_records.size();
}

QVariant LivePositionModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    const int row = index.row();
    if (row < 0 || row >= m_records.size())
        return {};

    const auto& rec = m_records[row];
    switch (role) {
    case SymbolRole:      return rec.symbol;
    case QuantityRole:    return rec.quantity;
    case PriceRole:       return rec.price;
    case MarketValueRole: return rec.marketValue;
    case DirectionRole:   return rec.direction;
    default:
        return {};
    }
}

QHash<int, QByteArray> LivePositionModel::roleNames() const
{
    return m_roleNames;
}

void LivePositionModel::refresh()
{
    beginResetModel();
    loadFromFile();
    endResetModel();
}

void LivePositionModel::loadFromFile()
{
    m_records.clear();

    const QString baseDir = QCoreApplication::applicationDirPath() + "/../..";
    const QString path = baseDir + "/data/live_account_snapshot.json";

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    const QByteArray bytes = file.readAll();
    file.close();

    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject())
        return;

    const QJsonObject obj = doc.object();
    const QJsonArray positions = obj.value("positions").toArray();

    QVector<PositionRecord> all;
    all.reserve(positions.size());

    for (const QJsonValue& v : positions) {
        if (!v.isObject())
            continue;
        const QJsonObject p = v.toObject();

        PositionRecord rec;
        rec.symbol      = p.value("symbol").toString();
        rec.quantity    = p.value("quantity").toDouble();
        rec.price       = p.value("price").toDouble();
        rec.marketValue = p.value("market_value").toDouble();
        rec.direction   = p.value("direction").toString();

        if (rec.symbol.isEmpty() || rec.quantity <= 0.0 || rec.price <= 0.0)
            continue;

        all.push_back(rec);
    }

    m_records = all;
}

#include "moc_LivePositionModel.cpp"
