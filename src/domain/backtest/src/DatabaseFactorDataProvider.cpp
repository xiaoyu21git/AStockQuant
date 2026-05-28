#include "DatabaseFactorDataProvider.h"
#include "../../ui/bridge/include/FactorService.h"

#include <foundation/log/logging.hpp>

#include <QDate>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <stdexcept>
#include <utility>

namespace domain::backtest {

DatabaseFactorDataProvider::DatabaseFactorDataProvider(
    std::shared_ptr<FactorService> factorService,
    std::function<void()> rangeLoadStartedCallback)
    : factorService_(std::move(factorService))
    , rangeLoadStartedCallback_(std::move(rangeLoadStartedCallback)) {
    if (factorService_) {
        INTERNAL_INFO_STREAM << "DatabaseFactorDataProvider initialized with FactorService";
    } else {
        INTERNAL_WARN_STREAM << "DatabaseFactorDataProvider initialized without FactorService";
    }
}

DatabaseFactorDataProvider::~DatabaseFactorDataProvider() = default;

std::map<std::string, std::map<std::string, double>> DatabaseFactorDataProvider::getFactorValuesRange(
    const std::string& factorId,
    const std::string& startDate,
    const std::string& endDate)
{
    std::map<std::string, std::map<std::string, double>> result;

    const QString qFactorId = QString::fromStdString(factorId);
    const QString qStartDate = QString::fromStdString(startDate);
    const QString qEndDate = QString::fromStdString(endDate);

    try {
        const QDate start = QDate::fromString(qStartDate, "yyyy-MM-dd");
        const QDate end = QDate::fromString(qEndDate, "yyyy-MM-dd");

        if (!start.isValid() || !end.isValid()) {
            throw std::invalid_argument("Invalid date format. Use yyyy-MM-dd");
        }
        if (start > end) {
            throw std::invalid_argument("Start date must be before end date");
        }
        if (!factorService_) {
            throw std::runtime_error("FactorService is not available");
        }

        factorService_->resolveDomainInstanceId(qFactorId);

        if (rangeLoadStartedCallback_) {
            rangeLoadStartedCallback_();
        }

        QStringList requestedDates;
        for (QDate currentDate = start; currentDate <= end; currentDate = currentDate.addDays(1)) {
            requestedDates.append(currentDate.toString("yyyy-MM-dd"));
        }

        INTERNAL_INFO_STREAM << "Requesting factor batch values for factor " << factorId
                             << " from " << startDate << " to " << endDate
                             << ", requestedDates=" << requestedDates.size();

        const QVariantMap batchResult = factorService_->getFactorValuesBatch(qFactorId, requestedDates);
        const QString status = batchResult.value("status").toString();
        if (status == "error") {
            throw std::runtime_error(
                QString("Failed to calculate factor %1 between %2 and %3: %4")
                    .arg(qFactorId,
                         qStartDate,
                         qEndDate,
                         batchResult.value("error").toString())
                    .toStdString());
        }

        const QVariantMap batchData = batchResult.value("data").toMap();
        const QString batchMessage = batchResult.value("message").toString();
        for (auto dayIt = batchData.begin(); dayIt != batchData.end(); ++dayIt) {
            const QVariantMap stockValuesMap = dayIt.value().toMap();
            std::map<std::string, double> stockValues;
            for (auto stockIt = stockValuesMap.begin(); stockIt != stockValuesMap.end(); ++stockIt) {
                if (stockIt.value().canConvert<double>()) {
                    stockValues[stockIt.key().toStdString()] = stockIt.value().toDouble();
                }
            }

            if (!stockValues.empty()) {
                result[dayIt.key().toStdString()] = std::move(stockValues);
            }
        }

        if (result.empty() && !batchMessage.isEmpty()) {
            throw std::runtime_error(
                QString("Factor %1 does not provide executable backtest values: %2")
                    .arg(qFactorId, batchMessage)
                    .toStdString());
        }

        if (result.empty()) {
            throw std::runtime_error(
                QString("No factor values generated for factor %1 between %2 and %3")
                    .arg(qFactorId, qStartDate, qEndDate)
                    .toStdString());
        }

        INTERNAL_INFO_STREAM << "Loaded factor values range for factor " << factorId
                             << " from " << startDate << " to " << endDate
                             << ", got " << result.size() << " days with data";
    } catch (const std::exception& e) {
        INTERNAL_ERROR_STREAM << "Failed to get factor values range for factor " << factorId
                              << " from " << startDate << " to " << endDate
                              << ": " << e.what();
        throw;
    }

    return result;
}

} // namespace domain::backtest