// PreviewDataModel.h - 预览窗口专用的数据模型
#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QVector>
#include <QHash>
#include <QVariant>
#include <QString>

/**
 * @brief 数据预览模型 - QAbstractListModel子类
 * 
 * 专为预览窗口设计，管理预览数据的显示
 * QML通过绑定此模型自动更新UI
 */       
class PreviewDataModel : public QAbstractListModel {
    Q_OBJECT
    
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int maxDisplayCount READ maxDisplayCount WRITE setMaxDisplayCount NOTIFY maxDisplayCountChanged)
    
public:
    // 角色定义 - 与QML中的model.roleName对应
    enum Roles {
        DateRole = Qt::UserRole + 1,    // 日期
        CodeRole,                       // 股票代码
        NameRole,                       // 股票名称
        TimeRangeRole,                  // 时间范围
        RecordCountRole,                // 区间记录数
        OpenRole,                       // 开盘价
        CloseRole,                      // 收盘价
        HighRole,                       // 最高价
        LowRole,                        // 最低价
        ChangeRole,                     // 涨跌幅
        VolumeRole                      // 成交量
    };
    Q_ENUM(Roles)
    
    explicit PreviewDataModel(QObject* parent = nullptr);
    ~PreviewDataModel();
    
    // QAbstractListModel接口
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    
    // 数据操作方法 - Q_INVOKABLE允许QML调用
    Q_INVOKABLE void updateData(const QVector<QVariantMap>& data);
    Q_INVOKABLE void clearData();
    Q_INVOKABLE void appendData(const QVariantMap& item);
    
    // 批量添加数据
    Q_INVOKABLE void addDataBatch(const QVector<QVariantMap>& data);
    
    // 数据访问器
    Q_INVOKABLE int count() const { return rowCount(); }
    Q_INVOKABLE QVariantMap getRow(int index) const;
    
    // 属性访问器
    int maxDisplayCount() const { return m_maxDisplayCount; }
    Q_INVOKABLE void setMaxDisplayCount(int count);
    
signals:
    void countChanged();
    void maxDisplayCountChanged();
    void dataUpdated();
    
private:
    // 内部数据结构
    struct PreviewItem {
        QString date;      // 日期
        QString code;      // 股票代码
        QString name;      // 股票名称
        QString timeRange; // 时间范围
        int recordCount;   // 汇总记录数
        double open;       // 开盘价
        double close;      // 收盘价
        double high;       // 最高价
        double low;        // 最低价
        double change;     // 涨跌幅
        double volume;     // 成交量
        
        PreviewItem() : recordCount(0), open(0.0), close(0.0), high(0.0), low(0.0), change(0.0), volume(0.0) {}
        PreviewItem(const QVariantMap& map);
    };
    
    QVector<PreviewItem> m_data;
    QHash<int, QByteArray> m_roleNames;
    int m_maxDisplayCount{10000};  // 默认显示最多10000条，提高显示限制
};