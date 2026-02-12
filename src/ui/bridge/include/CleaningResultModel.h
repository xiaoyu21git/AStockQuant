// CleaningResultModel.h
#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QVector>
#include <QHash>
#include <QVariant>

/**
 * @brief 数据清洗结果模型 - QAbstractListModel子类
 * 
 * 负责管理数据清洗结果的显示，QML通过绑定此模型自动更新UI
 * 遵循规则：QML只负责渲染，不直接操作数据
 */
class CleaningResultModel : public QAbstractListModel {
    Q_OBJECT
    
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int maxDisplayCount READ maxDisplayCount WRITE setMaxDisplayCount NOTIFY maxDisplayCountChanged)
    
public:
    // 角色定义 - 与QML中的model.roleName对应
    enum Roles {
        DateRole = Qt::UserRole + 1,    // 日期
        CodeRole,                       // 股票代码
        NameRole,                       // 股票名称
        CloseRole,                      // 收盘价
        ChangeRole,                     // 涨跌幅
        VolumeRole                      // 成交量
    };
    Q_ENUM(Roles)
    
    explicit CleaningResultModel(QObject* parent = nullptr);
    ~CleaningResultModel();
    
    // QAbstractListModel接口
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    
    // 数据操作方法（C++内部调用，不是Q_INVOKABLE）
    void updateResults(const QVector<QVariantMap>& results);
    void clearResults();
    void appendResult(const QVariantMap& result);
    
    // 属性访问器
    int maxDisplayCount() const { return m_maxDisplayCount; }
    void setMaxDisplayCount(int count);
    
signals:
    void countChanged();
    void maxDisplayCountChanged();
    void resultsUpdated();
    
private:
    // 内部数据结构
    struct CleaningResult {
        QString date;      // 日期
        QString code;      // 股票代码
        QString name;      // 股票名称
        double close;      // 收盘价
        double change;     // 涨跌幅
        double volume;     // 成交量
        
        CleaningResult() : close(0.0), change(0.0), volume(0.0) {}
        CleaningResult(const QVariantMap& map);
    };
    
    QVector<CleaningResult> m_results;
    QHash<int, QByteArray> m_roleNames;
    int m_maxDisplayCount{5};  // 默认显示前5条
};