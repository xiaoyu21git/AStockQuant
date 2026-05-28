#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QVariantMap>
#include <QVariantList>
#include <QDebug>
#include "ui/bridge/include/FactorService.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    auto* factorService = FactorService::instance();
    if (!factorService) {
        qCritical().noquote() << "FactorService instance unavailable";
        return 1;
    }
    factorService->initialize();
    const QVariantList factors = factorService->getAllFactors();
    QJsonArray output;
    for (const QVariant& factorValue : factors) {
        const QVariantMap factor = factorValue.toMap();
        const QString factorId = factor.value("factorId").toString().trimmed();
        if (factorId.isEmpty()) {
            continue;
        }
        const QVariantMap detail = factorService->getFactorById(factorId);
        const QVariantList poolValues = detail.value("backtestSymbolPool").toList();
        if (poolValues.isEmpty()) {
            continue;
        }
        QJsonArray pool;
        for (const QVariant& item : poolValues) {
            const QString symbol = item.toString().trimmed();
            if (!symbol.isEmpty()) {
                pool.append(symbol);
            }
        }
        if (pool.isEmpty()) {
            continue;
        }
        QJsonObject obj;
        obj.insert("factorId", factorId);
        obj.insert("factorName", detail.value("displayName").toString().trimmed().isEmpty()
            ? detail.value("factorName").toString()
            : detail.value("displayName").toString());
        obj.insert("stockPoolCount", static_cast<int>(pool.size()));
        obj.insert("stockPool", pool);
        obj.insert("actualStartDate", detail.value("actualStartDate").toString());
        obj.insert("effectiveEndDate", detail.value("effectiveEndDate").toString());
        output.append(obj);
    }
    qInfo().noquote() << QString::fromUtf8(QJsonDocument(output).toJson(QJsonDocument::Indented));
    return 0;
}
