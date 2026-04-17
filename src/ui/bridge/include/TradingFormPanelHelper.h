#pragma once

#include <algorithm>

#include <QObject>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "OrderRuntimeUtils.h"

class TradingFormPanelHelper : public QObject {
    Q_OBJECT

public:
    static TradingFormPanelHelper* instance()
    {
        QMutexLocker locker(&s_instanceMutex);
        if (!s_instance) {
            s_instance = new TradingFormPanelHelper();
        }
        return s_instance;
    }

    Q_INVOKABLE QVariantMap buildHeaderState(const QString& currentMode,
                                             const QString& currentSymbol,
                                             const QString& currentTabLabel,
                                             const QVariantMap& marketSnapshot) const
    {
        const double priceValue = currentMode == QStringLiteral("futures")
            ? mapNumber(marketSnapshot, QStringLiteral("futuresPrice"))
            : mapNumber(marketSnapshot, QStringLiteral("price"));
        const int digits = currentMode == QStringLiteral("futures")
            ? 0
            : (currentMode == QStringLiteral("options") ? 4 : 2);
        const QString priceText = formatDisplayPrice(priceValue, digits);
        const QString tabLabel = currentTabLabel.trimmed().isEmpty() ? currentMode.trimmed() : currentTabLabel.trimmed();
        const QString nameText = mapString(marketSnapshot, QStringLiteral("name"));
        const QString quoteTime = extractQuoteTime(marketSnapshot);

        QVariantMap result;
        result.insert(QStringLiteral("modePriceText"),
                      isEquityMode(currentMode) && priceText != QStringLiteral("--")
                          ? QStringLiteral("¥") + priceText
                          : priceText);
        result.insert(QStringLiteral("currentModeDisplayTitle"),
                      isEquityMode(currentMode)
                          ? QStringLiteral("%1  ·  %2").arg(tabLabel,
                                                             nameText.isEmpty() ? QStringLiteral("待识别标的") : nameText)
                          : QStringLiteral("%1  ·  %2").arg(tabLabel,
                                                             currentSymbol.trimmed().isEmpty()
                                                                 ? QStringLiteral("--")
                                                                 : currentSymbol.trimmed()));
        result.insert(QStringLiteral("quoteTimeText"), quoteTime);
        result.insert(QStringLiteral("openingMarketWindow"), isOpeningMarketWindowText(quoteTime));
        result.insert(QStringLiteral("referenceText"), QStringLiteral("市价参考 %1").arg(priceText));
        return result;
    }

    Q_INVOKABLE bool hasRealtimeEquityQuote(const QString& mode,
                                            const QVariantMap& marketSnapshot) const
    {
        return isEquityMode(mode) && hasEquityDisplayQuote(marketSnapshot) && marketSnapshot.value(QStringLiteral("live")).toBool();
    }

    Q_INVOKABLE double resolveEquityShortcutPrice(const QString& shortcut,
                                                  const QString& mode,
                                                  const QVariantMap& marketSnapshot,
                                                  const QVariantMap& depthSnapshot) const
    {
        if (!isEquityMode(mode) || !hasEquityDisplayQuote(marketSnapshot)) {
            return 0.0;
        }

        const double latestPrice = mapNumber(marketSnapshot, QStringLiteral("price"));
        const bool hasRealtime = hasRealtimeEquityQuote(mode, marketSnapshot);
        const double bid1Price = depthTopPrice(depthSnapshot, QStringLiteral("bids"));
        const double ask1Price = depthTopPrice(depthSnapshot, QStringLiteral("asks"));

        if (shortcut == QStringLiteral("opponent")) {
            if (!hasRealtime) {
                return 0.0;
            }
            return mode == QStringLiteral("margin_sell")
                ? (bid1Price > 0.0 ? bid1Price : latestPrice)
                : (ask1Price > 0.0 ? ask1Price : latestPrice);
        }
        if (shortcut == QStringLiteral("bid1")) {
            return hasRealtime ? (bid1Price > 0.0 ? bid1Price : latestPrice) : 0.0;
        }
        if (shortcut == QStringLiteral("ask1")) {
            return hasRealtime ? (ask1Price > 0.0 ? ask1Price : latestPrice) : 0.0;
        }
        if (shortcut == QStringLiteral("upper")) {
            const double upperLimit = mapNumber(marketSnapshot, QStringLiteral("upperLimit"));
            return upperLimit > 0.0 ? upperLimit : latestPrice;
        }
        if (shortcut == QStringLiteral("lower")) {
            const double lowerLimit = mapNumber(marketSnapshot, QStringLiteral("lowerLimit"));
            return lowerLimit > 0.0 ? lowerLimit : latestPrice;
        }
        return latestPrice;
    }

    Q_INVOKABLE QString equityShortcutButtonText(const QString& shortcut,
                                                 const QString& label,
                                                 const QString& mode,
                                                 const QVariantMap& marketSnapshot,
                                                 const QVariantMap& depthSnapshot) const
    {
        return label + QStringLiteral(" ")
            + formatDisplayPrice(resolveEquityShortcutPrice(shortcut, mode, marketSnapshot, depthSnapshot), 2);
    }

    Q_INVOKABLE QVariantMap syncEquityReferenceState(const QString& mode,
                                                     const QString& currentPriceType,
                                                     const QString& currentPriceInput,
                                                     const QString& lastAutoPrice,
                                                     const QString& lastAutoPriceType,
                                                     const QVariantMap& marketSnapshot,
                                                     bool openingMarketWindow) const
    {
        QVariantMap result;
        if (!isEquityMode(mode)) {
            return result;
        }

        const QString preferredType = hasRealtimeEquityQuote(mode, marketSnapshot) && openingMarketWindow
            ? QStringLiteral("market")
            : QStringLiteral("limit");
        const QString normalizedCurrentType = currentPriceType.trimmed();
        const QString normalizedAutoType = lastAutoPriceType.trimmed();
        const QString normalizedCurrentInput = currentPriceInput.trimmed();
        const QString normalizedAutoPrice = lastAutoPrice.trimmed();
        const QString nextPriceType = (normalizedCurrentType.isEmpty() || normalizedCurrentType == normalizedAutoType)
            ? preferredType
            : normalizedCurrentType;

        QString nextPriceInput = currentPriceInput;
        QString nextAutoPrice = lastAutoPrice;
        const double latestPrice = mapNumber(marketSnapshot, QStringLiteral("price"));
        if (latestPrice > 0.0) {
            nextAutoPrice = formatDisplayPrice(latestPrice, 2);
            if (nextPriceType == QStringLiteral("market")
                    || normalizedCurrentInput.isEmpty()
                    || normalizedCurrentInput == normalizedAutoPrice) {
                nextPriceInput = nextAutoPrice;
            }
        }

        result.insert(QStringLiteral("priceType"), nextPriceType);
        result.insert(QStringLiteral("priceInput"), nextPriceInput);
        result.insert(QStringLiteral("autoPrice"), nextAutoPrice);
        result.insert(QStringLiteral("autoPriceType"), preferredType);
        return result;
    }

    Q_INVOKABLE QString formattedModePriceInput(const QString& mode,
                                                double priceValue) const
    {
        if (priceValue <= 0.0) {
            return {};
        }
        return QString::number(roundPriceByMode(priceValue, mode), 'f', priceDigitsForMode(mode));
    }

    Q_INVOKABLE QString adjustedModePriceInput(const QString& mode,
                                               const QString& currentPriceInput,
                                               const QVariantMap& marketSnapshot,
                                               double stepDelta) const
    {
        bool ok = false;
        const double currentValue = currentPriceInput.trimmed().toDouble(&ok);
        double basePrice = ok && currentValue > 0.0 ? currentValue : referencePriceForMode(mode, marketSnapshot);
        const double step = priceStepForMode(mode);
        if (basePrice <= 0.0 || step <= 0.0) {
            return {};
        }

        basePrice = std::max(step, basePrice + step * stepDelta);
        return formattedModePriceInput(mode, basePrice);
    }

    Q_INVOKABLE QVariantList quickButtonsForMode(const QString& mode) const
    {
        QVariantList buttons;
        if (mode == QStringLiteral("stock")
                || mode == QStringLiteral("margin_buy")
                || mode == QStringLiteral("margin_sell")) {
            buttons << QStringLiteral("100")
                    << QStringLiteral("500")
                    << QStringLiteral("1000")
                    << QStringLiteral("2000")
                    << QStringLiteral("5000");
            return buttons;
        }
        if (mode == QStringLiteral("futures")) {
            buttons << QStringLiteral("1")
                    << QStringLiteral("5")
                    << QStringLiteral("10")
                    << QStringLiteral("20");
            return buttons;
        }
        buttons << QStringLiteral("1/1")
                << QStringLiteral("1/2")
                << QStringLiteral("1/3")
                << QStringLiteral("1/4");
        return buttons;
    }

    Q_INVOKABLE QVariantList equityQuickPriceButtons() const
    {
        return QVariantList{
            QVariantMap{{QStringLiteral("code"), QStringLiteral("opponent")}, {QStringLiteral("label"), QStringLiteral("对手")}},
            QVariantMap{{QStringLiteral("code"), QStringLiteral("bid1")}, {QStringLiteral("label"), QStringLiteral("买一")}},
            QVariantMap{{QStringLiteral("code"), QStringLiteral("ask1")}, {QStringLiteral("label"), QStringLiteral("卖一")}},
            QVariantMap{{QStringLiteral("code"), QStringLiteral("latest")}, {QStringLiteral("label"), QStringLiteral("最新")}},
            QVariantMap{{QStringLiteral("code"), QStringLiteral("upper")}, {QStringLiteral("label"), QStringLiteral("涨停")}},
            QVariantMap{{QStringLiteral("code"), QStringLiteral("lower")}, {QStringLiteral("label"), QStringLiteral("跌停")}}
        };
    }

    Q_INVOKABLE QVariantMap buildEquityDisplay(const QString& mode,
                                               const QString& currentMode,
                                               const QString& currentCode,
                                               const QString& sharesText,
                                               const QString& priceType,
                                               const QString& priceInput,
                                               const QVariantMap& marketSnapshot,
                                               const QVariantMap& depthSnapshot,
                                               double availableCapital,
                                               const QString& positionAvailabilitySummary,
                                               bool positionAvailabilityError) const
    {
        const QString symbolText = equitySymbolText(currentCode, marketSnapshot);
        const bool realtime = hasRealtimeEquityQuote(mode, marketSnapshot);
        const bool hasDisplayQuote = hasEquityDisplayQuote(marketSnapshot);
        const QString nameText = mapString(marketSnapshot, QStringLiteral("name"));
        const QString quoteTime = extractQuoteTime(marketSnapshot);

        QVariantMap result;
        if (!isValidEquityCodeInput(currentCode)) {
            result.insert(QStringLiteral("identitySummary"), QStringLiteral("请输入有效6位股票代码"));
        } else if (!hasDisplayQuote) {
            result.insert(QStringLiteral("identitySummary"),
                          symbolText.isEmpty()
                              ? QStringLiteral("输入股票代码后等待实时行情")
                              : QStringLiteral("%1  未收到实时行情").arg(symbolText));
        } else {
            QStringList parts;
            if (!nameText.isEmpty()) {
                parts.push_back(nameText);
            }
            if (!symbolText.isEmpty()) {
                parts.push_back(symbolText);
            }
            if (!quoteTime.isEmpty()) {
                parts.push_back(quoteTime);
            }
            if (!realtime) {
                parts.push_back(QStringLiteral("缓存快照"));
            }
            if (mode == currentMode && !positionAvailabilitySummary.trimmed().isEmpty()) {
                parts.push_back(positionAvailabilitySummary.trimmed());
            }
            result.insert(QStringLiteral("identitySummary"),
                          parts.isEmpty() ? QStringLiteral("实时行情") : parts.join(QStringLiteral("  ")));
        }

        result.insert(QStringLiteral("identityColor"),
                      mode == currentMode && positionAvailabilityError
                          ? QStringLiteral("#fbbf24")
                          : QStringLiteral("#7ea1c5"));

        const double latestPrice = resolveEquityShortcutPrice(QStringLiteral("latest"), mode, marketSnapshot, depthSnapshot);
        const double bid1Price = resolveEquityShortcutPrice(QStringLiteral("bid1"), mode, marketSnapshot, depthSnapshot);
        const double ask1Price = resolveEquityShortcutPrice(QStringLiteral("ask1"), mode, marketSnapshot, depthSnapshot);
        result.insert(QStringLiteral("priceSummary"),
                      QStringLiteral("最新 %1 / 买一 %2 / 卖一 %3")
                          .arg(formatDisplayPrice(latestPrice, 2),
                               formatDisplayPrice(bid1Price, 2),
                               formatDisplayPrice(ask1Price, 2)));

        const double manualPrice = priceInput.trimmed().toDouble();
        const double resolvedPrice = priceType == QStringLiteral("limit") && manualPrice > 0.0
            ? manualPrice
            : latestPrice;
        const double sharesValue = sharesText.trimmed().toDouble();
        const double amountValue = sharesValue > 0.0 && resolvedPrice > 0.0 ? sharesValue * resolvedPrice : 0.0;
        const double ratioValue = amountValue > 0.0 && availableCapital > 0.0 ? (amountValue / availableCapital * 100.0) : 0.0;
        result.insert(QStringLiteral("amountSummary"),
                      QStringLiteral("金额 %1 / 仓位 %2 / 可用 %3")
                          .arg(formatAmountShort(amountValue),
                               ratioValue > 0.0 ? QString::number(ratioValue, 'f', 2) + QStringLiteral("%") : QStringLiteral("--"),
                               formatAmountShort(availableCapital)));
        return result;
    }

    Q_INVOKABLE QVariantMap buildOrderPresentation(const QVariantMap& order) const
    {
        const QString action = mapString(order, QStringLiteral("action"));
        const QString type = mapString(order, QStringLiteral("type"));
        const QString normalizedStatus = canonicalOrderStatus(order);
        const double cashAmount = mapNumber(order, QStringLiteral("cashAmount"));
        qint64 quantity = mapInteger(order, QStringLiteral("qty"));
        qint64 filledQuantity = mapInteger(order, QStringLiteral("filledQty"));
        if (quantity < 0) {
            quantity = 0;
        }
        if (filledQuantity < 0) {
            filledQuantity = 0;
        }
        if (quantity > 0 && filledQuantity > quantity) {
            filledQuantity = quantity;
        }
        if (normalizedStatus == QStringLiteral("FILLED") && filledQuantity <= 0 && quantity > 0) {
            filledQuantity = quantity;
        }

        const bool cashRepay = action == QStringLiteral("现金还款") && cashAmount > 0.0;
        const QString unit = orderUnit(type, action);
        const bool approveManual = canApproveManualCheckpoint(order, normalizedStatus);
        const bool retryManual = canRetryManualCheckpoint(order, approveManual);
        const bool resumeExecution = canResumeExecutionPause(order, normalizedStatus);
        const bool retryExecution = canRetryExecutionPause(order, resumeExecution);

        QVariantMap result;
        result.insert(QStringLiteral("normalizedStatus"), normalizedStatus);
        result.insert(QStringLiteral("headlineAmount"),
                      cashRepay
                          ? formatAmountShort(cashAmount)
                          : QStringLiteral("%1%2").arg(quantity).arg(unit));
        result.insert(QStringLiteral("filledSummary"),
                      buildFilledSummary(normalizedStatus, quantity, filledQuantity, cashAmount, action, unit));
        result.insert(QStringLiteral("priceSummary"), buildPriceSummary(type, cashAmount, action, order));
        result.insert(QStringLiteral("auxiliarySummary"), buildAuxiliarySummary(order, normalizedStatus));
        result.insert(QStringLiteral("canCancel"), canCancelOrder(order, normalizedStatus, quantity, filledQuantity));
        result.insert(QStringLiteral("canApproveManualCheckpoint"), approveManual);
        result.insert(QStringLiteral("canRetryManualCheckpoint"), retryManual);
        result.insert(QStringLiteral("checkpointActionLabel"),
                      retryManual ? QStringLiteral("确认并重试") : QStringLiteral("人工确认"));
        result.insert(QStringLiteral("canResumeExecutionPause"), resumeExecution);
        result.insert(QStringLiteral("canRetryExecutionPause"), retryExecution);
        result.insert(QStringLiteral("executionPauseActionLabel"),
                      retryExecution ? QStringLiteral("恢复并重试") : QStringLiteral("恢复执行"));
        return result;
    }

private:
    explicit TradingFormPanelHelper(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    static inline TradingFormPanelHelper* s_instance = nullptr;
    static inline QMutex s_instanceMutex;

    static QString mapString(const QVariantMap& map, const QString& key)
    {
        return map.value(key).toString().trimmed();
    }

    static double mapNumber(const QVariantMap& map, const QString& key)
    {
        bool ok = false;
        const double value = map.value(key).toDouble(&ok);
        return ok ? value : 0.0;
    }

    static qint64 mapInteger(const QVariantMap& map, const QString& key)
    {
        bool ok = false;
        const qint64 value = map.value(key).toLongLong(&ok);
        return ok ? value : 0;
    }

    static bool isEquityMode(const QString& mode)
    {
        return mode == QStringLiteral("stock")
            || mode == QStringLiteral("margin_buy")
            || mode == QStringLiteral("margin_sell");
    }

    static bool isValidEquityCodeInput(const QString& code)
    {
        const QString text = code.trimmed().toUpper();
        if (text.size() == 6 && std::all_of(text.begin(), text.end(), [](QChar c) { return c.isDigit(); })) {
            return true;
        }
        static const QRegularExpression dotSuffix(QStringLiteral("^(\\d{6})\\.(SH|SZ|BJ)$"));
        static const QRegularExpression exchangePrefix(QStringLiteral("^(SHSE|SZSE|BSE)\\.\\d{6}$"));
        return dotSuffix.match(text).hasMatch() || exchangePrefix.match(text).hasMatch();
    }

    static QString formatDisplayPrice(double value, int digits)
    {
        if (value <= 0.0) {
            return QStringLiteral("--");
        }
        return QString::number(value, 'f', digits);
    }

    static QString formatAmountShort(double value)
    {
        if (value <= 0.0) {
            return QStringLiteral("--");
        }
        if (value >= 100000000.0) {
            return QString::number(value / 100000000.0, 'f', 2) + QStringLiteral("亿");
        }
        if (value >= 10000.0) {
            return QString::number(value / 10000.0, 'f', 2) + QStringLiteral("万");
        }
        return QString::number(value, 'f', 2);
    }

    static QString extractQuoteTime(const QVariantMap& marketSnapshot)
    {
        QString updatedText = mapString(marketSnapshot, QStringLiteral("updatedAt"));
        if (updatedText.isEmpty()) {
            return {};
        }
        if (updatedText.contains(QLatin1Char(' ')) || updatedText.contains(QLatin1Char(':'))) {
            if (updatedText.size() >= 8) {
                return updatedText.right(8);
            }
        }
        return {};
    }

    static bool isOpeningMarketWindowText(const QString& quoteTime)
    {
        return quoteTime.size() == 8
            && ((quoteTime >= QStringLiteral("09:30:00") && quoteTime <= QStringLiteral("09:35:00"))
                || (quoteTime >= QStringLiteral("13:00:00") && quoteTime <= QStringLiteral("13:05:00")));
    }

    static bool hasEquityDisplayQuote(const QVariantMap& marketSnapshot)
    {
        return mapNumber(marketSnapshot, QStringLiteral("price")) > 0.0;
    }

    static int priceDigitsForMode(const QString& mode)
    {
        if (mode == QStringLiteral("futures")) {
            return 0;
        }
        if (mode == QStringLiteral("options")) {
            return 4;
        }
        return 2;
    }

    static double roundPriceByMode(double value, const QString& mode)
    {
        return QString::number(value, 'f', priceDigitsForMode(mode)).toDouble();
    }

    static double priceStepForMode(const QString& mode)
    {
        if (mode == QStringLiteral("futures")) {
            return 1.0;
        }
        if (mode == QStringLiteral("options")) {
            return 0.0001;
        }
        return 0.01;
    }

    static double referencePriceForMode(const QString& mode, const QVariantMap& marketSnapshot)
    {
        if (mode == QStringLiteral("futures")) {
            return mapNumber(marketSnapshot, QStringLiteral("futuresPrice"));
        }
        return mapNumber(marketSnapshot, QStringLiteral("price"));
    }

    static QString equitySymbolText(const QString& currentCode, const QVariantMap& marketSnapshot)
    {
        const QString liveSymbol = mapString(marketSnapshot, QStringLiteral("symbol"));
        return liveSymbol.isEmpty() ? currentCode.trimmed().toUpper() : liveSymbol;
    }

    static double depthTopPrice(const QVariantMap& depthSnapshot, const QString& side)
    {
        const QVariantList rows = depthSnapshot.value(side).toList();
        if (rows.isEmpty()) {
            return 0.0;
        }
        const QVariantMap topRow = rows.first().toMap();
        return mapNumber(topRow, QStringLiteral("price"));
    }

    static QString orderUnit(const QString& type, const QString& action)
    {
        if (action == QStringLiteral("现金还款")) {
            return QStringLiteral("元");
        }
        return (type == QStringLiteral("futures") || type == QStringLiteral("options"))
            ? QStringLiteral("手")
            : QStringLiteral("股");
    }

    static QString canonicalOrderStatus(const QVariantMap& order)
    {
        const QString rawStatus = mapString(order, QStringLiteral("rawStatus"));
        const QString fallbackStatus = mapString(order, QStringLiteral("status"));
        const QString sourceStatus = rawStatus.isEmpty() ? fallbackStatus : rawStatus;
        return order_runtime::normalizeOrderStatus(sourceStatus, order_runtime::EmptyStatusPolicy::KeepEmpty);
    }

    static QString buildFilledSummary(const QString& normalizedStatus,
                                      qint64 quantity,
                                      qint64 filledQuantity,
                                      double cashAmount,
                                      const QString& action,
                                      const QString& unit)
    {
        if (action == QStringLiteral("现金还款") && cashAmount > 0.0) {
            return normalizedStatus == QStringLiteral("FILLED")
                ? QStringLiteral("已还款 %1").arg(formatAmountShort(cashAmount))
                : QString{};
        }
        if (normalizedStatus != QStringLiteral("PARTIAL_FILLED")
                && normalizedStatus != QStringLiteral("FILLED")
                && normalizedStatus != QStringLiteral("CANCELLED")
                && normalizedStatus != QStringLiteral("REJECTED")) {
            return {};
        }
        if (filledQuantity <= 0 && normalizedStatus != QStringLiteral("FILLED")) {
            return {};
        }
        if (filledQuantity <= 0 && quantity <= 0) {
            return {};
        }
        const QString prefix = normalizedStatus == QStringLiteral("FILLED")
            ? QStringLiteral("全部成交 ")
            : QStringLiteral("成交 ");
        if (quantity > 0) {
            return QStringLiteral("%1%2/%3%4").arg(prefix).arg(filledQuantity).arg(quantity).arg(unit);
        }
        return QStringLiteral("%1%2%3").arg(prefix).arg(filledQuantity).arg(unit);
    }

    static QString buildPriceSummary(const QString& type,
                                     double cashAmount,
                                     const QString& action,
                                     const QVariantMap& order)
    {
        if (action == QStringLiteral("现金还款") && cashAmount > 0.0) {
            return QStringLiteral("还款额 %1").arg(formatAmountShort(cashAmount));
        }
        const int digits = type == QStringLiteral("options") ? 4 : (type == QStringLiteral("futures") ? 0 : 2);
        const QString priceText = formatDisplayPrice(mapNumber(order, QStringLiteral("price")), digits);
        return QStringLiteral("委托价 %1").arg(priceText);
    }

    static QString orderIdentifierSummary(const QVariantMap& order)
    {
        const QString clientOrderId = mapString(order, QStringLiteral("clientOrderId"));
        const QString brokerOrderId = mapString(order, QStringLiteral("brokerOrderId"));
        if (clientOrderId.isEmpty() && brokerOrderId.isEmpty()) {
            return {};
        }
        if (!brokerOrderId.isEmpty() && brokerOrderId != clientOrderId) {
            return QStringLiteral("委托 %1  ·  柜台 %2").arg(clientOrderId, brokerOrderId);
        }
        return QStringLiteral("委托 %1").arg(clientOrderId.isEmpty() ? brokerOrderId : clientOrderId);
    }

    static QString buildAuxiliarySummary(const QVariantMap& order, const QString& normalizedStatus)
    {
        const QString message = mapString(order, QStringLiteral("message"));
        if (normalizedStatus == QStringLiteral("REJECTED")) {
            QStringList parts;
            const QString ruleId = mapString(order, QStringLiteral("ruleId"));
            const QString reasonCode = mapString(order, QStringLiteral("reasonCode"));
            const QString batchId = mapString(order, QStringLiteral("requiredBatchId")).isEmpty()
                ? mapString(order, QStringLiteral("batchId"))
                : mapString(order, QStringLiteral("requiredBatchId"));
            const QString blockingBatchId = mapString(order, QStringLiteral("blockingBatchId"));
            if (!ruleId.isEmpty()) {
                parts.push_back(QStringLiteral("规则 ") + ruleId);
            }
            if (!reasonCode.isEmpty()) {
                parts.push_back(QStringLiteral("原因码 ") + reasonCode);
            }
            if (!batchId.isEmpty()) {
                parts.push_back(QStringLiteral("批次 ") + batchId);
            }
            if (!blockingBatchId.isEmpty()) {
                parts.push_back(QStringLiteral("阻断批次 ") + blockingBatchId);
            }
            if (!message.isEmpty()) {
                parts.push_back(message);
            }
            if (!parts.isEmpty()) {
                return parts.join(QStringLiteral(" · "));
            }
        }
        return orderIdentifierSummary(order);
    }

    static bool canCancelOrder(const QVariantMap& order,
                               const QString& normalizedStatus,
                               qint64 quantity,
                               qint64 filledQuantity)
    {
        if (quantity > 0 && filledQuantity >= quantity) {
            return false;
        }
        if (mapString(order, QStringLiteral("source")) == QStringLiteral("simulation")) {
            return normalizedStatus != QStringLiteral("CANCELLED")
                && normalizedStatus != QStringLiteral("REJECTED")
                && normalizedStatus != QStringLiteral("FILLED")
                && normalizedStatus != QStringLiteral("PENDING_CANCEL");
        }
        return normalizedStatus == QStringLiteral("SUBMITTED")
            || normalizedStatus == QStringLiteral("PENDING")
            || normalizedStatus == QStringLiteral("PARTIAL_FILLED");
    }

    static bool hasExecutableRetryPayload(const QVariantMap& order)
    {
        const QString symbol = mapString(order, QStringLiteral("symbol"));
        const QString side = mapString(order, QStringLiteral("side")).toUpper();
        const qint64 quantity = mapInteger(order, QStringLiteral("qty"));
        const double cashAmount = mapNumber(order, QStringLiteral("cashAmount"));
        return !symbol.isEmpty()
            && !side.isEmpty()
            && (quantity > 0 || cashAmount > 0.0);
    }

    static bool canApproveManualCheckpoint(const QVariantMap& order, const QString& normalizedStatus)
    {
        if (normalizedStatus != QStringLiteral("REJECTED")) {
            return false;
        }
        if (mapString(order, QStringLiteral("ruleId")) != QStringLiteral("ManualCheckpointRule")) {
            return false;
        }
        const QString executionScopeId = mapString(order, QStringLiteral("executionScopeId"));
        const QString batchId = mapString(order, QStringLiteral("batchId")).isEmpty()
            ? mapString(order, QStringLiteral("requiredBatchId"))
            : mapString(order, QStringLiteral("batchId"));
        return !executionScopeId.isEmpty() && !batchId.isEmpty();
    }

    static bool canRetryManualCheckpoint(const QVariantMap& order, bool canApprove)
    {
        return canApprove && hasExecutableRetryPayload(order);
    }

    static bool canResumeExecutionPause(const QVariantMap& order, const QString& normalizedStatus)
    {
        return normalizedStatus == QStringLiteral("REJECTED")
            && mapString(order, QStringLiteral("ruleId")) == QStringLiteral("RetryOrPauseRule")
            && !mapString(order, QStringLiteral("executionScopeId")).isEmpty();
    }

    static bool canRetryExecutionPause(const QVariantMap& order, bool canResume)
    {
        return canResume && hasExecutableRetryPayload(order);
    }
};