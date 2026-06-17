#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class StrategyListModel : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        StatusRole,
        StatusTextRole,
        UpdatedAtRole,
        RunningRole,
        DisplayStatusRole   // 综合 DB 状态 + 引擎实际运行状态
    };
    Q_ENUM(Roles)

    explicit StrategyListModel(QObject* parent = nullptr);
    ~StrategyListModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void replaceAll(const QVariantList& items);
    Q_INVOKABLE void upsertOne(const QVariantMap& item);
    Q_INVOKABLE bool removeById(const QString& strategyId);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QVariantMap getRow(int index) const;

    /// @brief 直接更新单行 displayStatus，不查 DB
    void updateDisplayStatus(const QString& strategyId, const QString& status);

signals:
    void countChanged();

private:
    struct StrategyRow final {
        QString strategyId;
        QString name;
        int status{0};
        QString statusText;
        QString updatedAt;
        bool running{false};
        QString displayStatus;
    };

    static StrategyRow fromVariantMap(const QVariantMap& map);
    [[nodiscard]] QVariantMap toVariantMap(const StrategyRow& row) const;
    [[nodiscard]] int findRow(const QString& strategyId) const;

    QVector<StrategyRow> m_rows;
};
