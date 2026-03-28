#include "DatabaseFactorDataProvider.h"
#include "../../ui/bridge/include/FactorService.h"
#include "../../ui/bridge/include/DataServiceCache.h"

#include <foundation/log/logging.hpp>

#include <QDate>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <stdexcept>
#include <utility>

namespace domain::backtest {

DatabaseFactorDataProvider::DatabaseFactorDataProvider(
    std::shared_ptr<FactorService> factorService)
    : factorService_(std::move(factorService)) {
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

        const QString cacheKey = QString("factor_values_range_%1_%2_%3")
            .arg(qFactorId, qStartDate, qEndDate);

        const QVariantList cachedData = DataServiceCache::getInstance().getData(cacheKey);
        if (!cachedData.isEmpty() && cachedData.first().canConvert<QVariantMap>()) {
            const QVariantMap cachedResult = cachedData.first().toMap();
            if (cachedResult.value("status").toString() == "success" && cachedResult.contains("data")) {
                const QVariantMap dataMap = cachedResult.value("data").toMap();
                for (auto it = dataMap.begin(); it != dataMap.end(); ++it) {
                    const QVariantMap stockValuesMap = it.value().toMap();
                    std::map<std::string, double> stockValues;
                    for (auto stockIt = stockValuesMap.begin(); stockIt != stockValuesMap.end(); ++stockIt) {
                        stockValues[stockIt.key().toStdString()] = stockIt.value().toDouble();
                    }
                    if (!stockValues.empty()) {
                        result[it.key().toStdString()] = std::move(stockValues);
                    }
                }

                INTERNAL_INFO_STREAM << "Retrieved factor values range from cache for factor " << factorId
                                     << " from " << startDate << " to " << endDate
                                     << ", got " << result.size() << " days with data";
                return result;
            }
        }

        QDate currentDate = start;
        bool sawMessageWithoutValues = false;
        std::string emptyResultMessage;

        while (currentDate <= end) {
            const QString dateStr = currentDate.toString("yyyy-MM-dd");
            const QVariantMap dayResult = factorService_->getFactorValues(qFactorId, dateStr);
            const QString status = dayResult.value("status").toString();

            if (status == "error") {
                const QString error = dayResult.value("error").toString();
                throw std::runtime_error(
                    QString("Failed to calculate factor %1 on %2: %3")
                        .arg(qFactorId, dateStr, error)
                        .toStdString());
            }

            const QVariantMap stockValuesMap = dayResult.value("stockValues").toMap();
            if (stockValuesMap.isEmpty()) {
                const QString message = dayResult.value("message").toString();
                if (!message.isEmpty()) {
                    sawMessageWithoutValues = true;
                    if (emptyResultMessage.empty()) {
                        emptyResultMessage = message.toStdString();
                    }
                }
                currentDate = currentDate.addDays(1);
                continue;
            }

            std::map<std::string, double> stockValues;
            for (auto it = stockValuesMap.begin(); it != stockValuesMap.end(); ++it) {
                if (it.value().canConvert<double>()) {
                    stockValues[it.key().toStdString()] = it.value().toDouble();
                }
            }

            if (!stockValues.empty()) {
                result[dateStr.toStdString()] = std::move(stockValues);
            }

            currentDate = currentDate.addDays(1);
        }

        if (result.empty() && sawMessageWithoutValues) {
            throw std::runtime_error(
                QString("Factor %1 does not provide executable backtest values: %2")
                    .arg(qFactorId,
                         QString::fromStdString(
                             emptyResultMessage.empty()
                                 ? std::string("factor has no executable calculation logic")
                                 : emptyResultMessage))
                    .toStdString());
        }

        if (result.empty()) {
            throw std::runtime_error(
                QString("No factor values generated for factor %1 between %2 and %3")
                    .arg(qFactorId, qStartDate, qEndDate)
                    .toStdString());
        }

        QVariantMap dataMap;
        for (const auto& dateData : result) {
            QVariantMap stockValuesMap;
            for (const auto& stockValue : dateData.second) {
                stockValuesMap[QString::fromStdString(stockValue.first)] = stockValue.second;
            }
            dataMap[QString::fromStdString(dateData.first)] = stockValuesMap;
        }

        QVariantMap cacheResult;
        cacheResult["status"] = "success";
        cacheResult["factorId"] = qFactorId;
        cacheResult["startDate"] = qStartDate;
        cacheResult["endDate"] = qEndDate;
        cacheResult["data"] = dataMap;

        QVariantList cacheList;
        cacheList.append(cacheResult);
        DataServiceCache::getInstance().storeData(cacheKey, cacheList);

        INTERNAL_INFO_STREAM << "Calculated factor values range for factor " << factorId
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