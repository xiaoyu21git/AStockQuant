#include "TradingFormPanelHelper.h"

#include <QDateTime>
#include <cmath>
#include <algorithm>

namespace bridge {

TradingFormPanelHelper::TradingFormPanelHelper(QObject* parent)
    : QObject(parent) {}

// ═══════════════════════════════════════════════════════════════════
// 模式判断
// ═══════════════════════════════════════════════════════════════════
bool TradingFormPanelHelper::isEquityMode(const QString& mode) const {
    return mode == "stock" || mode == "margin_buy" || mode == "margin_sell";
}

bool TradingFormPanelHelper::isValidEquityCode(const QString& code) const {
    if (code.isEmpty()) return false;
    // 6位数字代码或交易所前缀+代码
    if (code.length() == 6) {
        bool ok = false;
        code.toLongLong(&ok);
        return ok;
    }
    return code.contains('.');
}

int TradingFormPanelHelper::priceDigitsForMode(const QString& mode) const {
    if (mode == "futures") return 1;
    if (mode == "options") return 4;
    return 2; // stock/margin
}

double TradingFormPanelHelper::priceStepForMode(const QString& mode) const {
    if (mode == "futures") return 1.0;
    if (mode == "options") return 0.0001;
    return 0.01;
}

double TradingFormPanelHelper::roundPriceByMode(const QString& mode, double price) const {
    double step = priceStepForMode(mode);
    return std::round(price / step) * step;
}

double TradingFormPanelHelper::referencePriceForMode(const QString& mode,
                                                       const QVariantMap& snapshot) const {
    if (snapshot.contains("preClose")) return snapshot.value("preClose").toDouble();
    if (snapshot.contains("lastPrice")) return snapshot.value("lastPrice").toDouble();
    return snapshot.value("price").toDouble();
}

QString TradingFormPanelHelper::formatDisplayPrice(double price, int digits) const {
    return QString::number(price, 'f', digits);
}

QString TradingFormPanelHelper::formatAmountCompact(double amount) const {
    if (std::abs(amount) >= 1e8) {
        return QString::number(amount / 1e8, 'f', 2) + QStringLiteral("亿");
    }
    if (std::abs(amount) >= 1e4) {
        return QString::number(amount / 1e4, 'f', 2) + QStringLiteral("万");
    }
    return QString::number(amount, 'f', 2);
}

QString TradingFormPanelHelper::extractQuoteTime(const QVariantMap& snapshot) const {
    return snapshot.value("updatedAt").toString();
}

// ═══════════════════════════════════════════════════════════════════
// 订单状态字符串规范化
// ═══════════════════════════════════════════════════════════════════
QString TradingFormPanelHelper::canonicalOrderStatus(const QString& rawStatus) const {
    const QString s = rawStatus.toLower().trimmed();
    if (s == "pending" || s == "new" || s == "accepted") return "pending";
    if (s == "submitted" || s == "sent") return "submitted";
    if (s == "partial" || s == "partially_filled" || s == "partialfilled") return "partially_filled";
    if (s == "filled" || s == "done" || s == "executed") return "filled";
    if (s == "cancelled" || s == "canceled") return "cancelled";
    if (s == "rejected" || s == "refused") return "rejected";
    if (s == "expired" || s == "timeout") return "expired";
    if (s == "paused") return "paused";
    if (s == "checkpoint") return "checkpoint";
    return s.isEmpty() ? QStringLiteral("unknown") : s;
}

bool TradingFormPanelHelper::canCancelOrder(const QString& status) const {
    const QString s = canonicalOrderStatus(status);
    return s == "pending" || s == "submitted" || s == "partially_filled" || s == "paused";
}

bool TradingFormPanelHelper::canApproveCheckpoint(const QString& status) const {
    return canonicalOrderStatus(status) == "checkpoint";
}

bool TradingFormPanelHelper::canRetryCheckpoint(const QString& status) const {
    const QString s = canonicalOrderStatus(status);
    return s == "rejected" || s == "expired";
}

bool TradingFormPanelHelper::canResumePause(const QString& status) const {
    return canonicalOrderStatus(status) == "paused";
}

bool TradingFormPanelHelper::canRetryPause(const QString& status) const {
    const QString s = canonicalOrderStatus(status);
    return s == "rejected" || s == "expired";
}

QString TradingFormPanelHelper::checkpointActionLabel(const QString& status) const {
    return canApproveCheckpoint(status) ? QStringLiteral("批准检查点")
                                        : QStringLiteral("重试检查点");
}

QString TradingFormPanelHelper::pauseActionLabel(const QString& status) const {
    return canResumePause(status) ? QStringLiteral("恢复执行")
                                   : QStringLiteral("重试执行");
}

// ═══════════════════════════════════════════════════════════════════
// Q_INVOKABLE 公共方法
// ═══════════════════════════════════════════════════════════════════

QVariantMap TradingFormPanelHelper::buildHeaderState(const QString& mode,
                                                       const QString& symbol,
                                                       const QString& tabLabel,
                                                       const QVariantMap& marketSnapshot) const {
    QVariantMap header;
    header["openingMarketWindow"] = false;

    // 参考价文本
    double preClose = marketSnapshot.value("preClose").toDouble();
    header["referenceText"] = preClose > 0.0
        ? QStringLiteral("昨收 ") + formatDisplayPrice(preClose, priceDigitsForMode(mode))
        : QStringLiteral("--");

    // 模式显示标题
    if (mode == "stock") header["currentModeDisplayTitle"] = QStringLiteral("股票");
    else if (mode == "margin_buy") header["currentModeDisplayTitle"] = QStringLiteral("融资买入");
    else if (mode == "margin_sell") header["currentModeDisplayTitle"] = QStringLiteral("融券卖出");
    else if (mode == "futures") header["currentModeDisplayTitle"] = QStringLiteral("期货");
    else if (mode == "options") header["currentModeDisplayTitle"] = QStringLiteral("期权");
    else header["currentModeDisplayTitle"] = tabLabel;

    // 当前价格文本
    double price = marketSnapshot.value("price").toDouble();
    header["modePriceText"] = price > 0.0
        ? formatDisplayPrice(price, priceDigitsForMode(mode))
        : QStringLiteral("--");

    header["quoteTime"] = extractQuoteTime(marketSnapshot);

    return header;
}

QStringList TradingFormPanelHelper::quickButtonsForMode(const QString& mode) const {
    QStringList buttons;
    if (mode == "stock" || mode == "margin_buy" || mode == "margin_sell") {
        buttons << "100" << "200" << "500" << "1000" << "2000" << "5000" << "10000";
    } else if (mode == "futures") {
        buttons << "1" << "2" << "5" << "10" << "20" << "50";
    } else if (mode == "options") {
        buttons << "1" << "5" << "10" << "20" << "50" << "100";
    }
    return buttons;
}

QVariantList TradingFormPanelHelper::equityQuickPriceButtons() const {
    QVariantList buttons;
    auto add = [&](const QString& code, const QString& label) {
        QVariantMap btn;
        btn["code"] = code;
        btn["label"] = label;
        buttons.append(btn);
    };
    add("opponent", QStringLiteral("对手价"));
    add("bid1", QStringLiteral("买一"));
    add("ask1", QStringLiteral("卖一"));
    add("latest", QStringLiteral("最新价"));
    add("upper", QStringLiteral("涨停"));
    add("lower", QStringLiteral("跌停"));
    return buttons;
}

QVariantMap TradingFormPanelHelper::buildEquityDisplay(
    const QString& eqMode, const QString& currentMode,
    const QString& code, const QString& shares, const QString& priceType,
    const QString& price, const QVariantMap& marketSnapshot,
    const QVariantMap& depthSnapshot, double availableCapital,
    const QVariantMap& posSummary, bool posError) const {

    Q_UNUSED(code)
    Q_UNUSED(depthSnapshot)
    QVariantMap display;

    // 标的摘要
    QString symbol = marketSnapshot.value("symbol").toString();
    QString name = marketSnapshot.value("name").toString();
    display["identitySummary"] = symbol.isEmpty()
        ? QStringLiteral("--") : symbol + " " + name;
    display["identityColor"] = posError ? "danger" : "neutral";

    // 价格摘要
    double priceVal = price.toDouble();
    int digits = priceDigitsForMode(currentMode);
    QString priceDisplay = priceVal > 0.0
        ? formatDisplayPrice(priceVal, digits)
        : formatDisplayPrice(marketSnapshot.value("price").toDouble(), digits);
    display["priceSummary"] = priceDisplay;

    // 金额摘要
    double sharesVal = shares.toDouble();
    double amount = sharesVal * (priceVal > 0.0 ? priceVal
                                                : marketSnapshot.value("price").toDouble());
    display["amountSummary"] = formatAmountCompact(amount);
    display["availableCapital"] = availableCapital;

    return display;
}

QVariantMap TradingFormPanelHelper::syncEquityReferenceState(
    const QString& mode, const QString& priceType, const QString& priceInput,
    double autoPrice, const QString& autoPriceType, const QVariantMap& marketSnapshot,
    const QVariantMap& depthSnapshot, bool openingMarketWindow) const {

    Q_UNUSED(depthSnapshot)
    QVariantMap state;
    state["priceType"] = priceType;
    state["priceInput"] = priceInput;
    state["autoPrice"] = autoPrice;
    state["autoPriceType"] = autoPriceType;

    if (openingMarketWindow && priceType != "limit") {
        state["priceType"] = QStringLiteral("limit");
        state["priceInput"] = formatDisplayPrice(
            referencePriceForMode(mode, marketSnapshot), priceDigitsForMode(mode));
        state["autoPrice"] = referencePriceForMode(mode, marketSnapshot);
        state["autoPriceType"] = QStringLiteral("latest");
    }

    return state;
}

QString TradingFormPanelHelper::formattedModePriceInput(const QString& targetMode,
                                                          double numericPrice) const {
    return formatDisplayPrice(numericPrice, priceDigitsForMode(targetMode));
}

double TradingFormPanelHelper::adjustedModePriceInput(const QString& targetMode,
                                                        const QString& priceInput,
                                                        const QVariantMap& marketSnapshot,
                                                        double stepDelta) const {
    double currentPrice = priceInput.toDouble();
    if (currentPrice <= 0.0) {
        currentPrice = marketSnapshot.value("price").toDouble();
    }
    if (currentPrice <= 0.0) return 0.0;

    double step = priceStepForMode(targetMode);
    double adjusted = currentPrice + stepDelta * step;
    return std::max(0.0, roundPriceByMode(targetMode, adjusted));
}

double TradingFormPanelHelper::resolveEquityShortcutPrice(
    const QString& shortcutCode, const QString& targetMode,
    const QVariantMap& marketSnapshot, const QVariantMap& depthSnapshot) const {

    Q_UNUSED(targetMode)
    if (shortcutCode == "latest" || shortcutCode == "last") {
        return marketSnapshot.value("price").toDouble();
    }
    if (shortcutCode == "opponent") {
        // 对手价：买用卖一，卖用买一
        return marketSnapshot.value("price").toDouble();
    }
    if (shortcutCode == "bid1") {
        QVariantList bids = depthSnapshot.value("bids").toList();
        return bids.isEmpty() ? 0.0 : bids.first().toMap().value("price").toDouble();
    }
    if (shortcutCode == "ask1") {
        QVariantList asks = depthSnapshot.value("asks").toList();
        return asks.isEmpty() ? 0.0 : asks.first().toMap().value("price").toDouble();
    }
    if (shortcutCode == "upper") {
        return marketSnapshot.value("upperLimit").toDouble();
    }
    if (shortcutCode == "lower") {
        return marketSnapshot.value("lowerLimit").toDouble();
    }
    return 0.0;
}

QString TradingFormPanelHelper::equityShortcutButtonText(
    const QString& code, const QString& label, const QString& mode,
    const QVariantMap& marketSnapshot, const QVariantMap& depthSnapshot) const {

    double price = resolveEquityShortcutPrice(code, mode, marketSnapshot, depthSnapshot);
    if (price > 0.0) {
        return label + "\n" + formatDisplayPrice(price, priceDigitsForMode(mode));
    }
    return label + "\n--";
}

QVariantMap TradingFormPanelHelper::buildOrderPresentation(
    const QVariantMap& orderData) const {

    QVariantMap pres;
    QString rawStatus = orderData.value("status").toString();
    QString status = canonicalOrderStatus(rawStatus);
    pres["normalizedStatus"] = status;

    // 标题金额
    double price = orderData.value("price").toDouble();
    double quantity = orderData.value("quantity").toDouble();
    double amount = price * quantity;
    pres["headlineAmount"] = formatAmountCompact(amount);

    // 价格摘要
    pres["priceSummary"] = formatDisplayPrice(price, 2) +
        QStringLiteral(" × ") + QString::number(static_cast<qint64>(quantity));

    // 成交摘要
    double filledQty = orderData.value("filledQuantity").toDouble();
    pres["filledSummary"] = filledQty > 0.0
        ? QStringLiteral("已成交 ") + QString::number(static_cast<qint64>(filledQty))
        : QStringLiteral("等待成交");

    // 辅助摘要
    pres["auxiliarySummary"] = orderData.value("message").toString();

    // 操作标志
    pres["canCancel"] = canCancelOrder(status);
    pres["canApproveManualCheckpoint"] = canApproveCheckpoint(status);
    pres["canRetryManualCheckpoint"] = canRetryCheckpoint(status);
    pres["checkpointActionLabel"] = checkpointActionLabel(status);
    pres["canResumeExecutionPause"] = canResumePause(status);
    pres["canRetryExecutionPause"] = canRetryPause(status);
    pres["executionPauseActionLabel"] = pauseActionLabel(status);

    return pres;
}

} // namespace bridge
