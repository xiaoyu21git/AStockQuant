#include "EquityCurveModel.h"

#include <QDebug>

EquityCurveModel::EquityCurveModel(QObject* parent)
    : QAbstractListModel(parent)
{
    m_roleNames[TimeRole]   = "time";
    m_roleNames[EquityRole] = "equity";
}

int EquityCurveModel::rowCount(const QModelIndex& parent) const {
    Q_UNUSED(parent)
    return m_points.size();
}

QVariant EquityCurveModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_points.size())
        return QVariant();

    const auto& p = m_points.at(index.row());

    switch (role) {
    case TimeRole:
        return p.time;
    case EquityRole:
        return p.equity;
    case Qt::DisplayRole:
        return QString("%1: %2").arg(p.time).arg(p.equity, 0, 'f', 2);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> EquityCurveModel::roleNames() const {
    return m_roleNames;
}

void EquityCurveModel::addPoint(const QString& time, double equity) {
    beginInsertRows(QModelIndex(), m_points.size(), m_points.size());
    Point p;
    p.time   = time;
    p.equity = equity;
    m_points.append(p);
    endInsertRows();

    // 更新概要信息：首尾权益和收益率
    if (m_points.size() == 1) {
        m_firstEquity = equity;
    }
    m_lastEquity = equity;

    emit countChanged();
    emit summaryChanged();
}

void EquityCurveModel::clear() {
    if (m_points.isEmpty())
        return;

    beginRemoveRows(QModelIndex(), 0, m_points.size() - 1);
    m_points.clear();
    endRemoveRows();

    m_firstEquity = 0.0;
    m_lastEquity  = 0.0;

    emit countChanged();
    emit summaryChanged();
}

//#include "moc_EquityCurveModel.cpp"
