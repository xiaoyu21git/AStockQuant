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
    Q_PROPERTY(int totalCount READ totalCount NOTIFY totalCountChanged)
    Q_PROPERTY(QString currentCategory READ currentCategory WRITE setCurrentCategory NOTIFY currentCategoryChanged)
    Q_PROPERTY(int klineCount READ klineCount NOTIFY categoryCountsChanged)
    Q_PROPERTY(int financialCount READ financialCount NOTIFY categoryCountsChanged)
    Q_PROPERTY(int otherCount READ otherCount NOTIFY categoryCountsChanged)
    Q_PROPERTY(int currentPage READ currentPage WRITE setCurrentPage NOTIFY currentPageChanged)
    Q_PROPERTY(int pageSize READ pageSize WRITE setPageSize NOTIFY pageSizeChanged)
    Q_PROPERTY(int totalPages READ totalPages NOTIFY paginationChanged)
    Q_PROPERTY(bool hasPreviousPage READ hasPreviousPage NOTIFY paginationChanged)
    Q_PROPERTY(bool hasNextPage READ hasNextPage NOTIFY paginationChanged)
    Q_PROPERTY(QString pageSummary READ pageSummary NOTIFY paginationChanged)
    Q_PROPERTY(int maxDisplayCount READ maxDisplayCount WRITE setMaxDisplayCount NOTIFY maxDisplayCountChanged)
    
public:
    // 角色定义 - 与QML中的model.roleName对应
    enum Roles {
        DateRole = Qt::UserRole + 1,    // 日期
        CodeRole,                       // 股票代码
        NameRole,                       // 股票名称
        TimeRangeRole,                  // 时间范围
        RecordCountRole,                // 区间记录数
        SourceRole,                     // 来源标签
        DataTypeRole,                   // 数据类型
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
    Q_INVOKABLE void nextPage();
    Q_INVOKABLE void previousPage();
    Q_INVOKABLE void firstPage();
    Q_INVOKABLE void lastPage();
    
    // 数据访问器
    Q_INVOKABLE int count() const { return rowCount(); }
    Q_INVOKABLE QVariantMap getRow(int index) const;
    
    // 属性访问器
    int totalCount() const;
    QString currentCategory() const { return m_currentCategory; }
    void setCurrentCategory(const QString& category);
    Q_INVOKABLE int categoryCount(const QString& category) const;
    int klineCount() const;
    int financialCount() const;
    int otherCount() const;
    int currentPage() const { return m_currentPage; }
    void setCurrentPage(int page);
    int pageSize() const { return m_pageSize; }
    void setPageSize(int size);
    int totalPages() const;
    bool hasPreviousPage() const { return m_currentPage > 1 && totalPages() > 1; }
    bool hasNextPage() const { return m_currentPage < totalPages(); }
    QString pageSummary() const;
    int maxDisplayCount() const { return m_maxDisplayCount; }
    Q_INVOKABLE void setMaxDisplayCount(int count);

    // 内部数据结构
    struct PreviewItem {
        QString date;      // 日期
        QString code;      // 股票代码
        QString name;      // 股票名称
        QString timeRange; // 时间范围
        QString source;    // 来源
        QString dataType;  // 实际数据类型
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
    
signals:
    void countChanged();
    void totalCountChanged();
    void currentCategoryChanged();
    void categoryCountsChanged();
    void currentPageChanged();
    void pageSizeChanged();
    void paginationChanged();
    void maxDisplayCountChanged();
    void dataUpdated();
    
private:
    int countForCategory(const QString& category) const;
    int actualIndexForVisibleRow(int visibleRow) const;
    static QString previewItemCategoryKey(const PreviewItem& item);
    static bool previewItemMatchesCategory(const PreviewItem& item, const QString& category);
    
    QVector<PreviewItem> m_allData;
    QHash<int, QByteArray> m_roleNames;
        QString m_currentCategory{"kline"};
    int m_currentPage{1};
    int m_pageSize{20};
    int m_maxDisplayCount{10000};  // 默认显示最多10000条，提高显示限制
};