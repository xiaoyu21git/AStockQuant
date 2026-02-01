#pragma once

#include <QAbstractListModel>
#include <QVector>
#include <QString>

// 读取 data/live_actions.jsonl 中最近的实盘动作记录，供 UI 展示
class LiveActionLogModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum RoleNames {
        TimeRole = Qt::UserRole + 1,
        TypeRole,
        SymbolRole,
        SideRole,
        QuantityRole,
        PriceRole,
        StatusRole
    };
    Q_ENUM(RoleNames)

    explicit LiveActionLogModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh(); // 重新从文件加载最新记录

private:
    struct ActionRecord {
        QString time;
        QString type;
        QString symbol;
        QString side;
        double  quantity{0.0};
        double  price{0.0};
        QString status;
    };

    QVector<ActionRecord> m_records;
    QHash<int, QByteArray> m_roleNames;

    void loadFromFile();
};
