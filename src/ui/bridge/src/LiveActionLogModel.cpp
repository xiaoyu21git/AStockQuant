#include "LiveActionLogModel.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace {
// 最多保留到内存中的记录条数，用于 UI 展示
constexpr int kMaxRecords = 200;
}

LiveActionLogModel::LiveActionLogModel(QObject* parent)
    : QAbstractListModel(parent)
{
    m_roleNames[TimeRole]     = "time";
    m_roleNames[TypeRole]     = "type";
    m_roleNames[SymbolRole]   = "symbol";
    m_roleNames[SideRole]     = "side";
    m_roleNames[QuantityRole] = "quantity";
    m_roleNames[PriceRole]    = "price";
    m_roleNames[StatusRole]   = "status";
}

int LiveActionLogModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_records.size();
}

QVariant LiveActionLogModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    const int row = index.row();
    if (row < 0 || row >= m_records.size())
        return {};

    const auto& rec = m_records[row];
    switch (role) {
    case TimeRole:     return rec.time;
    case TypeRole:     return rec.type;
    case SymbolRole:   return rec.symbol;
    case SideRole:     return rec.side;
    case QuantityRole: return rec.quantity;
    case PriceRole:    return rec.price;
    case StatusRole:   return rec.status;
    default:
        return {};
    }
}

QHash<int, QByteArray> LiveActionLogModel::roleNames() const
{
    return m_roleNames;
}

void LiveActionLogModel::refresh()
{
    beginResetModel();
    loadFromFile();
    endResetModel();
}

void LiveActionLogModel::loadFromFile()
{
    m_records.clear();

    const QString baseDir = QCoreApplication::applicationDirPath() + "/../..";
    const QString path = baseDir + "/data/live_actions.jsonl";

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    const QByteArray content = file.readAll();
    file.close();

    const QList<QByteArray> lines = content.split('\n');

    QVector<ActionRecord> all;
    for (const QByteArray& rawLine : lines) {
        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.isEmpty())
            continue;

        const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
        if (!doc.isObject())
            continue;

        const QJsonObject obj = doc.object();

        ActionRecord rec;
        rec.time   = obj.value("ts").toString();
        rec.type   = obj.value("type").toString();
        rec.symbol = obj.value("symbol").toString();
        rec.side   = obj.value("side").toString();
        rec.quantity = obj.value("quantity").toDouble();
        rec.price    = obj.value("price").toDouble();
        rec.status   = obj.value("status").toString();

        all.push_back(rec);
    }

    // 只保留最新的 kMaxRecords 条
    if (all.size() > kMaxRecords) {
        const int start = all.size() - kMaxRecords;
        m_records = all.mid(start, kMaxRecords);
    } else {
        m_records = all;
    }
}

#include "moc_LiveActionLogModel.cpp"
