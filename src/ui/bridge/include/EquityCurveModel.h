#pragma once

#include <QAbstractListModel>
#include <QVector>
#include <QString>

// 简单资金曲线模型：时间 + 权益
class EquityCurveModel : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(double firstEquity READ firstEquity NOTIFY summaryChanged)
    Q_PROPERTY(double lastEquity READ lastEquity NOTIFY summaryChanged)
    Q_PROPERTY(double returnPct READ returnPct NOTIFY summaryChanged)

public:
    enum Roles {
        TimeRole = Qt::UserRole + 1,
        EquityRole
    };

    explicit EquityCurveModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void addPoint(const QString& time, double equity);
    Q_INVOKABLE void clear();

signals:
    void countChanged();
    void summaryChanged();

private:
    struct Point {
        QString time;
        double  equity{0.0};
    };

    QVector<Point>          m_points;
    QHash<int, QByteArray> m_roleNames;

    double m_firstEquity{0.0};
    double m_lastEquity{0.0};

public:
    double firstEquity() const { return m_firstEquity; }
    double lastEquity() const { return m_lastEquity; }
    double returnPct() const {
        return m_firstEquity > 0.0 ? (m_lastEquity - m_firstEquity) / m_firstEquity : 0.0;
    }
};
