#pragma once

#include <QObject>
#include <QMutex>
#include <QVariantMap>

class PortfolioAnalysisService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)

public:
    static PortfolioAnalysisService* instance();

    PortfolioAnalysisService(const PortfolioAnalysisService&) = delete;
    PortfolioAnalysisService& operator=(const PortfolioAnalysisService&) = delete;

    Q_INVOKABLE void initialize();
    Q_INVOKABLE QVariantMap analyzePortfolioState(const QVariantMap& strategy,
                                                  const QVariantMap& latestBacktest = QVariantMap());
    Q_INVOKABLE QVariantMap buildPortfolioExecutionPlan(const QVariantMap& strategy,
                                                        const QVariantMap& latestBacktest = QVariantMap());
    Q_INVOKABLE QVariantMap optimizePortfolioAllocations(const QVariantMap& strategy,
                                                         const QVariantMap& options = QVariantMap());
    Q_INVOKABLE QVariantMap adjustPortfolioExposure(const QVariantMap& strategy,
                                                    const QString& focusType,
                                                    const QString& focusKey,
                                                    const QVariantMap& options = QVariantMap());
    Q_INVOKABLE QVariantMap checkPortfolioRisk(const QVariantMap& strategy,
                                               const QVariantMap& latestBacktest = QVariantMap());

    bool isInitialized() const;

signals:
    void initializedChanged();

private:
    explicit PortfolioAnalysisService(QObject* parent = nullptr);

    static PortfolioAnalysisService* m_instance;
    static QMutex m_instanceMutex;

    mutable QMutex m_mutex;
    bool m_initialized;
};