#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class CacheDetailPreviewModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(QVariantList visibleRows READ visibleRows NOTIFY visibleRowsChanged)
    Q_PROPERTY(QVariantList visibleFields READ visibleFields NOTIFY visibleFieldsChanged)
    Q_PROPERTY(QVariantList dailyFields READ dailyFields NOTIFY visibleFieldsChanged)
    Q_PROPERTY(QVariantList financialFields READ financialFields NOTIFY visibleFieldsChanged)
    Q_PROPERTY(QVariantList availableDates READ availableDates NOTIFY availableDatesChanged)
    Q_PROPERTY(QString fieldGroup READ fieldGroup NOTIFY fieldGroupChanged)
    Q_PROPERTY(QString symbolFilter READ symbolFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString startDateFilter READ startDateFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString endDateFilter READ endDateFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString selectedDate READ selectedDate NOTIFY selectedDateChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY paginationChanged)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY currentPageChanged)
    Q_PROPERTY(int pageSize READ pageSize WRITE setPageSize NOTIFY pageSizeChanged)
    Q_PROPERTY(int totalPages READ totalPages NOTIFY paginationChanged)
    Q_PROPERTY(bool hasPreviousPage READ hasPreviousPage NOTIFY paginationChanged)
    Q_PROPERTY(bool hasNextPage READ hasNextPage NOTIFY paginationChanged)
    Q_PROPERTY(QString pageSummary READ pageSummary NOTIFY paginationChanged)

public:
    explicit CacheDetailPreviewModel(QObject* parent = nullptr);

    QVariantList visibleRows() const;
    QVariantList visibleFields() const;
    QVariantList dailyFields() const;
    QVariantList financialFields() const;
    QVariantList availableDates() const;

    QString fieldGroup() const { return m_fieldGroup; }
    QString symbolFilter() const { return m_symbolFilter; }
    QString startDateFilter() const { return m_startDateFilter; }
    QString endDateFilter() const { return m_endDateFilter; }
    QString selectedDate() const { return m_selectedDate; }

    int totalCount() const;
    int currentPage() const { return m_currentPage; }
    int pageSize() const { return m_pageSize; }
    int totalPages() const;
    bool hasPreviousPage() const;
    bool hasNextPage() const;
    QString pageSummary() const;

    void setSourceData(const QVariantList& rows);
    QVariantList filteredRows() const { return m_filteredRows; }

    Q_INVOKABLE void clearData();
    Q_INVOKABLE void clearFilters();
    Q_INVOKABLE void applyFilters(const QString& symbol,
                                  const QString& startDate,
                                  const QString& endDate,
                                  const QString& fieldGroup = QString());
    Q_INVOKABLE void selectDate(const QString& dateText);
    Q_INVOKABLE void clearSelectedDate();
    Q_INVOKABLE void nextPage();
    Q_INVOKABLE void previousPage();
    Q_INVOKABLE void firstPage();
    Q_INVOKABLE void lastPage();
    Q_INVOKABLE void setCurrentPage(int page);
    Q_INVOKABLE void setPageSize(int size);

signals:
    void visibleRowsChanged();
    void visibleFieldsChanged();
    void availableDatesChanged();
    void fieldGroupChanged();
    void filtersChanged();
    void selectedDateChanged();
    void currentPageChanged();
    void pageSizeChanged();
    void paginationChanged();
    void dataChanged();

private:
    void rebuildFieldSets();
    void rebuildFilteredRows();
    void resetFiltersInternal(bool keepFieldGroup = false);
    QString defaultFieldGroup() const;
    QStringList visibleFieldNames() const;

    QVariantList m_sourceRows;
    QVariantList m_filteredRows;
    QStringList m_dailyFieldNames;
    QStringList m_financialFieldNames;
    QStringList m_availableDateValues;
    QString m_fieldGroup;
    QString m_symbolFilter;
    QString m_startDateFilter;
    QString m_endDateFilter;
    QString m_selectedDate;
    int m_currentPage{1};
    int m_pageSize{20};
};
