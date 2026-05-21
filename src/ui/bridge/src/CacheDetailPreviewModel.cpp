#include "CacheDetailPreviewModel.h"

#include "DataFetchFieldContractUtils.h"

#include <QDate>
#include <QDateTime>
#include <QMetaType>
#include <QSet>

#include <algorithm>

namespace {

QVariantList toVariantList(const QStringList& values)
{
    QVariantList result;
    result.reserve(values.size());
    for (const QString& value : values) {
        result.append(value);
    }
    return result;
}

QString normalizeFieldName(const QString& value)
{
    return value.trimmed().toLower();
}

bool hasMeaningfulValue(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return false;
    }

    if (value.metaType().id() == QMetaType::QString) {
        return !value.toString().trimmed().isEmpty();
    }

    return true;
}

QDate parseDateValue(const QVariant& value)
{
    if (!value.isValid() || value.isNull()) {
        return QDate();
    }

    if (value.canConvert<QDate>()) {
        const QDate date = value.toDate();
        if (date.isValid()) {
            return date;
        }
    }

    if (value.canConvert<QDateTime>()) {
        const QDateTime dateTime = value.toDateTime();
        if (dateTime.isValid()) {
            return dateTime.date();
        }
    }

    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return QDate();
    }

    const QDate isoDate = QDate::fromString(text, Qt::ISODate);
    if (isoDate.isValid()) {
        return isoDate;
    }

    const QDate compactDate = QDate::fromString(text, QStringLiteral("yyyyMMdd"));
    if (compactDate.isValid()) {
        return compactDate;
    }

    const QDate slashDate = QDate::fromString(text, QStringLiteral("yyyy/MM/dd"));
    if (slashDate.isValid()) {
        return slashDate;
    }

    const QDate dashedDate = QDate::fromString(text, QStringLiteral("yyyy-MM-dd"));
    if (dashedDate.isValid()) {
        return dashedDate;
    }

    if (text.size() >= 10) {
        const QDate prefixDate = QDate::fromString(text.left(10), QStringLiteral("yyyy-MM-dd"));
        if (prefixDate.isValid()) {
            return prefixDate;
        }
    }

    return QDate();
}

QDate resolveRowDate(const QVariantMap& row)
{
    static const QStringList dateKeys = {
        QStringLiteral("trade_date"),
        QStringLiteral("report_date"),
        QStringLiteral("disclosure_date"),
        QStringLiteral("publish_date"),
        QStringLiteral("ann_date"),
        QStringLiteral("announcement_date"),
        QStringLiteral("date"),
        QStringLiteral("bar_time"),
        QStringLiteral("time_stamp")
    };

    for (const QString& key : dateKeys) {
        auto it = row.constFind(key);
        if (it == row.constEnd()) {
            continue;
        }

        const QDate resolved = parseDateValue(it.value());
        if (resolved.isValid()) {
            return resolved;
        }
    }

    return QDate();
}

QString resolveRowSymbol(const QVariantMap& row)
{
    static const QStringList symbolKeys = {
        QStringLiteral("symbol"),
        QStringLiteral("code"),
        QStringLiteral("stock_code")
    };

    for (const QString& key : symbolKeys) {
        const QString value = row.value(key).toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }

    return {};
}

bool rowHasAnyObservedField(const QVariantMap& row, const QSet<QString>& fields)
{
    for (const QString& field : fields) {
        auto it = row.constFind(field);
        if (it != row.constEnd() && hasMeaningfulValue(it.value())) {
            return true;
        }
    }
    return false;
}

QString groupLabel(const QString& fieldGroup)
{
    const QString normalized = fieldGroup.trimmed().toLower();
    if (normalized == QStringLiteral("financial")) {
        return QStringLiteral("财务字段");
    }
    if (normalized == QStringLiteral("all")) {
        return QStringLiteral("全部字段");
    }
    return QStringLiteral("日线字段");
}

}

CacheDetailPreviewModel::CacheDetailPreviewModel(QObject* parent)
    : QObject(parent)
{
}

QVariantList CacheDetailPreviewModel::visibleRows() const
{
    QVariantList rows;
    const int pageStart = (m_currentPage - 1) * m_pageSize;
    if (pageStart < 0 || pageStart >= m_filteredRows.size()) {
        return rows;
    }

    const int pageEnd = qMin(pageStart + m_pageSize, m_filteredRows.size());
    rows.reserve(pageEnd - pageStart);
    for (int index = pageStart; index < pageEnd; ++index) {
        rows.append(m_filteredRows.at(index));
    }
    return rows;
}

QVariantList CacheDetailPreviewModel::visibleFields() const
{
    return toVariantList(visibleFieldNames());
}

QVariantList CacheDetailPreviewModel::dailyFields() const
{
    return toVariantList(m_dailyFieldNames);
}

QVariantList CacheDetailPreviewModel::financialFields() const
{
    return toVariantList(m_financialFieldNames);
}

QVariantList CacheDetailPreviewModel::availableDates() const
{
    return toVariantList(m_availableDateValues);
}

int CacheDetailPreviewModel::totalCount() const
{
    return m_filteredRows.size();
}

int CacheDetailPreviewModel::totalPages() const
{
    if (m_pageSize <= 0 || m_filteredRows.isEmpty()) {
        return 1;
    }
    return qMax(1, (m_filteredRows.size() + m_pageSize - 1) / m_pageSize);
}

bool CacheDetailPreviewModel::hasPreviousPage() const
{
    return m_currentPage > 1 && totalPages() > 1;
}

bool CacheDetailPreviewModel::hasNextPage() const
{
    return m_currentPage < totalPages();
}

QString CacheDetailPreviewModel::pageSummary() const
{
    if (m_filteredRows.isEmpty()) {
        return QStringLiteral("第 0 / 0 页，共 0 条");
    }

    return QStringLiteral("%1，第 %2 / %3 页，共 %4 条")
        .arg(groupLabel(m_fieldGroup))
        .arg(m_currentPage)
        .arg(totalPages())
        .arg(m_filteredRows.size());
}

void CacheDetailPreviewModel::setSourceData(const QVariantList& rows)
{
    m_sourceRows.clear();
    m_sourceRows.reserve(rows.size());

    for (const QVariant& item : rows) {
        if (!item.canConvert<QVariantMap>()) {
            continue;
        }

        const QVariantMap rawRow = item.toMap();
        QVariantMap normalizedRow;
        for (auto it = rawRow.constBegin(); it != rawRow.constEnd(); ++it) {
            const QString normalizedField = normalizeFieldName(it.key());
            if (normalizedField.isEmpty()) {
                continue;
            }
            normalizedRow.insert(normalizedField, it.value());
        }

        if (!normalizedRow.isEmpty()) {
            m_sourceRows.append(normalizedRow);
        }
    }

    rebuildFieldSets();
    resetFiltersInternal(false);
    emit dataChanged();
}

void CacheDetailPreviewModel::clearData()
{
    m_sourceRows.clear();
    m_filteredRows.clear();
    m_dailyFieldNames.clear();
    m_financialFieldNames.clear();
    m_availableDateValues.clear();
    m_fieldGroup.clear();
    m_symbolFilter.clear();
    m_startDateFilter.clear();
    m_endDateFilter.clear();
    m_selectedDate.clear();
    m_currentPage = 1;

    emit visibleRowsChanged();
    emit visibleFieldsChanged();
    emit availableDatesChanged();
    emit fieldGroupChanged();
    emit filtersChanged();
    emit selectedDateChanged();
    emit currentPageChanged();
    emit paginationChanged();
    emit dataChanged();
}

void CacheDetailPreviewModel::clearFilters()
{
    resetFiltersInternal(true);
}

void CacheDetailPreviewModel::applyFilters(const QString& symbol,
                                           const QString& startDate,
                                           const QString& endDate,
                                           const QString& fieldGroup)
{
    const QString normalizedSymbol = symbol.trimmed();
    const QString normalizedStartDate = startDate.trimmed();
    const QString normalizedEndDate = endDate.trimmed();
    const QString requestedGroup = fieldGroup.trimmed().toLower();

    bool fieldGroupChangedFlag = false;
    if (!requestedGroup.isEmpty()) {
        const QString normalizedGroup = requestedGroup == QStringLiteral("financial")
            ? QStringLiteral("financial")
            : (requestedGroup == QStringLiteral("all") ? QStringLiteral("all") : QStringLiteral("daily"));
        if (m_fieldGroup != normalizedGroup) {
            m_fieldGroup = normalizedGroup;
            fieldGroupChangedFlag = true;
        }
    } else if (m_fieldGroup.isEmpty()) {
        m_fieldGroup = defaultFieldGroup();
        fieldGroupChangedFlag = true;
    }

    const bool filterChanged = m_symbolFilter != normalizedSymbol
        || m_startDateFilter != normalizedStartDate
        || m_endDateFilter != normalizedEndDate;

    m_symbolFilter = normalizedSymbol;
    m_startDateFilter = normalizedStartDate;
    m_endDateFilter = normalizedEndDate;
    m_currentPage = 1;

    rebuildFilteredRows();

    if (fieldGroupChangedFlag) {
        emit fieldGroupChanged();
        emit visibleFieldsChanged();
    }
    if (filterChanged) {
        emit filtersChanged();
    }
    emit visibleRowsChanged();
    emit currentPageChanged();
    emit paginationChanged();
    emit dataChanged();
}

void CacheDetailPreviewModel::selectDate(const QString& dateText)
{
    const QDate parsedDate = parseDateValue(dateText);
    const QString normalizedDate = parsedDate.isValid()
        ? parsedDate.toString(QStringLiteral("yyyy-MM-dd"))
        : QString();

    if (m_selectedDate == normalizedDate) {
        return;
    }

    m_selectedDate = normalizedDate;
    m_currentPage = 1;
    rebuildFilteredRows();

    emit selectedDateChanged();
    emit visibleRowsChanged();
    emit currentPageChanged();
    emit paginationChanged();
    emit dataChanged();
}

void CacheDetailPreviewModel::clearSelectedDate()
{
    if (m_selectedDate.isEmpty()) {
        return;
    }

    m_selectedDate.clear();
    m_currentPage = 1;
    rebuildFilteredRows();

    emit selectedDateChanged();
    emit visibleRowsChanged();
    emit currentPageChanged();
    emit paginationChanged();
    emit dataChanged();
}

void CacheDetailPreviewModel::nextPage()
{
    setCurrentPage(m_currentPage + 1);
}

void CacheDetailPreviewModel::previousPage()
{
    setCurrentPage(m_currentPage - 1);
}

void CacheDetailPreviewModel::firstPage()
{
    setCurrentPage(1);
}

void CacheDetailPreviewModel::lastPage()
{
    setCurrentPage(totalPages());
}

void CacheDetailPreviewModel::setCurrentPage(int page)
{
    const int nextPage = qBound(1, page, totalPages());
    if (nextPage == m_currentPage) {
        return;
    }

    m_currentPage = nextPage;
    emit visibleRowsChanged();
    emit currentPageChanged();
    emit paginationChanged();
}

void CacheDetailPreviewModel::setPageSize(int size)
{
    if (size <= 0 || size == m_pageSize) {
        return;
    }

    m_pageSize = size;
    m_currentPage = qBound(1, m_currentPage, totalPages());
    emit visibleRowsChanged();
    emit pageSizeChanged();
    emit paginationChanged();
}

void CacheDetailPreviewModel::rebuildFieldSets()
{
    QSet<QString> observedFields;
    QStringList observedOrder;

    const auto appendObservedField = [&](const QString& field) {
        const QString normalized = normalizeFieldName(field);
        if (normalized.isEmpty() || factor::bridge::isCleaningInternalField(normalized) || observedFields.contains(normalized)) {
            return;
        }
        observedFields.insert(normalized);
        observedOrder.append(normalized);
    };

    for (const QVariant& rowVariant : m_sourceRows) {
        const QVariantMap row = rowVariant.toMap();
        for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
            appendObservedField(it.key());
        }
    }

    const QSet<QString> financialFieldSet = factor::bridge::financialFields().toQStringSet();
    const auto buildOrderedFieldList = [&](const QStringList& preferredOrder,
                                           const std::function<bool(const QString&)>& extraPredicate) {
        QStringList result;
        QSet<QString> appended;

        const auto appendField = [&](const QString& field) {
            const QString normalized = normalizeFieldName(field);
            if (normalized.isEmpty() || !observedFields.contains(normalized) || appended.contains(normalized)) {
                return;
            }
            appended.insert(normalized);
            result.append(normalized);
        };

        for (const QString& field : preferredOrder) {
            appendField(field);
        }
        for (const QString& field : observedOrder) {
            if (!appended.contains(field) && extraPredicate(field)) {
                appendField(field);
            }
        }

        return result;
    };

    QStringList dailyPreferredOrder = {
        QStringLiteral("symbol"),
        QStringLiteral("code"),
        QStringLiteral("stock_code"),
        QStringLiteral("name"),
        QStringLiteral("trade_date"),
        QStringLiteral("date"),
        QStringLiteral("data_source")
    };
    dailyPreferredOrder.append(factor::bridge::marketBarFields().orderedValues());
    dailyPreferredOrder.append(factor::bridge::contextualMetadataFields().orderedValues());
    dailyPreferredOrder.append(factor::bridge::symbolInfoFields().orderedValues());

    QStringList financialPreferredOrder = {
        QStringLiteral("symbol"),
        QStringLiteral("code"),
        QStringLiteral("stock_code"),
        QStringLiteral("name"),
        QStringLiteral("disclosure_date"),
        QStringLiteral("report_date"),
        QStringLiteral("trade_date"),
        QStringLiteral("report_type"),
        QStringLiteral("data_source")
    };
    financialPreferredOrder.append(factor::bridge::financialFields().orderedValues());

    m_dailyFieldNames = buildOrderedFieldList(dailyPreferredOrder,
        [&](const QString& field) {
            if (financialFieldSet.contains(field)) {
                return false;
            }
            return true;
        });

    m_financialFieldNames = buildOrderedFieldList(financialPreferredOrder,
        [&](const QString& field) {
            return financialFieldSet.contains(field)
                || field == QStringLiteral("report_type")
                || field.contains(QStringLiteral("report"))
                || field.contains(QStringLiteral("disclosure"))
                || field.contains(QStringLiteral("dividend"));
        });
}

void CacheDetailPreviewModel::rebuildFilteredRows()
{
    m_filteredRows.clear();
    QStringList availableDates;
    QSet<QString> seenDates;

    const QDate startDate = parseDateValue(m_startDateFilter);
    const QDate endDate = parseDateValue(m_endDateFilter);
    const QDate selectedDate = parseDateValue(m_selectedDate);
    const QString symbolNeedle = m_symbolFilter.trimmed().toUpper();
    const QSet<QString> dailyPredicateFields = [] {
        QSet<QString> fields = factor::bridge::marketBarFields().toQStringSet();
        fields.insert(QStringLiteral("adj_factor"));
        fields.insert(QStringLiteral("amount"));
        fields.insert(QStringLiteral("turnover_amount"));
        return fields;
    }();
    const QSet<QString> financialPredicateFields = [] {
        QSet<QString> fields = factor::bridge::financialFields().toQStringSet();
        fields.insert(QStringLiteral("report_type"));
        return fields;
    }();

    for (const QVariant& rowVariant : m_sourceRows) {
        const QVariantMap row = rowVariant.toMap();
        if (row.isEmpty()) {
            continue;
        }

        const QDate rowDate = resolveRowDate(row);

        if (!symbolNeedle.isEmpty()) {
            const QString rowSymbol = resolveRowSymbol(row).toUpper();
            if (rowSymbol.isEmpty() || !rowSymbol.contains(symbolNeedle)) {
                continue;
            }
        }

        if (startDate.isValid() || endDate.isValid()) {
            if (!rowDate.isValid()) {
                continue;
            }
            if (startDate.isValid() && rowDate < startDate) {
                continue;
            }
            if (endDate.isValid() && rowDate > endDate) {
                continue;
            }
        }

        if (rowDate.isValid()) {
            const QString normalizedDate = rowDate.toString(QStringLiteral("yyyy-MM-dd"));
            if (!seenDates.contains(normalizedDate)) {
                seenDates.insert(normalizedDate);
                availableDates.append(normalizedDate);
            }
        }

        if (m_fieldGroup == QStringLiteral("financial") && !rowHasAnyObservedField(row, financialPredicateFields)) {
            continue;
        }
        if (m_fieldGroup == QStringLiteral("daily") && !rowHasAnyObservedField(row, dailyPredicateFields)) {
            continue;
        }

        if (selectedDate.isValid() && (!rowDate.isValid() || rowDate != selectedDate)) {
            continue;
        }

        m_filteredRows.append(row);
    }

    std::sort(availableDates.begin(), availableDates.end(), std::greater<QString>());
    const QStringList previousAvailableDates = m_availableDateValues;
    m_availableDateValues = availableDates;
    if (previousAvailableDates != m_availableDateValues) {
        emit availableDatesChanged();
    }

    if (!m_selectedDate.isEmpty() && !m_availableDateValues.contains(m_selectedDate)) {
        m_selectedDate.clear();
        emit selectedDateChanged();
        rebuildFilteredRows();
        return;
    }

    m_currentPage = qBound(1, m_currentPage, totalPages());
}

void CacheDetailPreviewModel::resetFiltersInternal(bool keepFieldGroup)
{
    const QString previousFieldGroup = m_fieldGroup;
    const bool hadSelectedDate = !m_selectedDate.isEmpty();
    m_symbolFilter.clear();
    m_startDateFilter.clear();
    m_endDateFilter.clear();
    m_selectedDate.clear();
    m_currentPage = 1;
    if (!keepFieldGroup || m_fieldGroup.isEmpty()) {
        m_fieldGroup = defaultFieldGroup();
    }

    rebuildFilteredRows();

    if (previousFieldGroup != m_fieldGroup) {
        emit fieldGroupChanged();
        emit visibleFieldsChanged();
    }
    emit filtersChanged();
    if (hadSelectedDate) {
        emit selectedDateChanged();
    }
    emit visibleRowsChanged();
    emit currentPageChanged();
    emit paginationChanged();
}

QString CacheDetailPreviewModel::defaultFieldGroup() const
{
    if (!m_dailyFieldNames.isEmpty()) {
        return QStringLiteral("daily");
    }
    if (!m_financialFieldNames.isEmpty()) {
        return QStringLiteral("financial");
    }
    return QStringLiteral("all");
}

QStringList CacheDetailPreviewModel::visibleFieldNames() const
{
    if (m_fieldGroup == QStringLiteral("financial")) {
        return m_financialFieldNames;
    }

    if (m_fieldGroup == QStringLiteral("all")) {
        QStringList result = m_dailyFieldNames;
        QSet<QString> seen(result.begin(), result.end());
        for (const QString& field : m_financialFieldNames) {
            if (!seen.contains(field)) {
                seen.insert(field);
                result.append(field);
            }
        }
        return result;
    }

    return m_dailyFieldNames;
}