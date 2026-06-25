#pragma once
// SymbolSearchModel — 标的搜索模型（同花顺式输入匹配）
// 从 symbol_info 表加载全市场标的信息，暴露给 QML 做输入联想

#include <QAbstractListModel>
#include <QString>
#include <QVector>

struct SymbolEntry {
    QString symbol;    // "000001.SZ"
    QString secName;   // "平安银行"
    QString exchange;  // "SZSE"
};

class SymbolSearchModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles { SymbolRole = Qt::UserRole + 1, NameRole, ExchangeRole };
    explicit SymbolSearchModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void init();
    Q_INVOKABLE void search(const QString& keyword);
    Q_INVOKABLE QVariantMap getRow(int index) const;

    /// @brief 通过 symbol 查询股票名称（供持仓等需要名称的地方调用）
    Q_INVOKABLE static QString nameForSymbol(const QString& symbol);

    /// @brief 返回 "名称 代码" 格式的显示字符串，如 "平安银行 000001.SZ"
    Q_INVOKABLE static QString displayName(const QString& symbol);

signals:
    void countChanged();

private:
    QVector<SymbolEntry> m_all;      // 全量数据
    QVector<int> m_filtered;         // 当前匹配的索引
};
