#pragma once
// ═════════════════════════════════════════════════════════════════════════
// TradingFormPanelHelper — 交易表单 UI 辅助计算器
// 纯展示逻辑：价格格式化、快捷按钮生成、订单呈现计算
// 零领域依赖，所有数据由 QML 传入
// ═════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace bridge {

class TradingFormPanelHelper : public QObject {
    Q_OBJECT
public:
    explicit TradingFormPanelHelper(QObject* parent = nullptr);

    /// @brief 构建头部状态信息
    Q_INVOKABLE QVariantMap buildHeaderState(const QString& mode,
                                              const QString& symbol,
                                              const QString& tabLabel,
                                              const QVariantMap& marketSnapshot) const;

    /// @brief 获取当前模式下的快捷数量按钮列表
    Q_INVOKABLE QStringList quickButtonsForMode(const QString& mode) const;

    /// @brief 获取股票快捷价格按钮列表 [{code, label}]
    Q_INVOKABLE QVariantList equityQuickPriceButtons() const;

    /// @brief 构建股票委托展示信息
    Q_INVOKABLE QVariantMap buildEquityDisplay(const QString& eqMode,
                                                const QString& currentMode,
                                                const QString& code,
                                                const QString& shares,
                                                const QString& priceType,
                                                const QString& price,
                                                const QVariantMap& marketSnapshot,
                                                const QVariantMap& depthSnapshot,
                                                double availableCapital,
                                                const QVariantMap& posSummary,
                                                bool posError) const;

    /// @brief 同步股票参考状态（价格类型和对齐）
    Q_INVOKABLE QVariantMap syncEquityReferenceState(const QString& mode,
                                                      const QString& priceType,
                                                      const QString& priceInput,
                                                      double autoPrice,
                                                      const QString& autoPriceType,
                                                      const QVariantMap& marketSnapshot,
                                                      const QVariantMap& depthSnapshot,
                                                      bool openingMarketWindow) const;

    /// @brief 格式化模式下的价格输入
    Q_INVOKABLE QString formattedModePriceInput(const QString& targetMode,
                                                 double numericPrice) const;

    /// @brief 调整模式下的价格输入（步进）
    Q_INVOKABLE double adjustedModePriceInput(const QString& targetMode,
                                               const QString& priceInput,
                                               const QVariantMap& marketSnapshot,
                                               double stepDelta) const;

    /// @brief 解析股票快捷价格
    Q_INVOKABLE double resolveEquityShortcutPrice(const QString& shortcutCode,
                                                   const QString& targetMode,
                                                   const QVariantMap& marketSnapshot,
                                                   const QVariantMap& depthSnapshot) const;

    /// @brief 快捷按钮显示文本
    Q_INVOKABLE QString equityShortcutButtonText(const QString& code,
                                                  const QString& label,
                                                  const QString& mode,
                                                  const QVariantMap& marketSnapshot,
                                                  const QVariantMap& depthSnapshot) const;

    /// @brief 构建订单呈现信息
    Q_INVOKABLE QVariantMap buildOrderPresentation(const QVariantMap& orderData) const;

private:
    // ── 内部辅助方法 ──

    bool isEquityMode(const QString& mode) const;
    bool isValidEquityCode(const QString& code) const;

    int priceDigitsForMode(const QString& mode) const;
    double priceStepForMode(const QString& mode) const;
    double roundPriceByMode(const QString& mode, double price) const;
    double referencePriceForMode(const QString& mode, const QVariantMap& snapshot) const;

    QString formatDisplayPrice(double price, int digits) const;
    QString formatAmountCompact(double amount) const;
    QString extractQuoteTime(const QVariantMap& snapshot) const;

    QString canonicalOrderStatus(const QString& rawStatus) const;
    bool canCancelOrder(const QString& status) const;
    bool canApproveCheckpoint(const QString& status) const;
    bool canRetryCheckpoint(const QString& status) const;
    bool canResumePause(const QString& status) const;
    bool canRetryPause(const QString& status) const;
    QString checkpointActionLabel(const QString& status) const;
    QString pauseActionLabel(const QString& status) const;
};

} // namespace bridge
