#pragma once

#include <QObject>
#include <QMutex>
#include <QMutexLocker>
#include <QMetaObject>

#include "MarketDataService.h"
#include "PositionAccountService.h"
#include "StrategyService.h"
#include "TradeExecutionService.h"
#include "TradingConnectionConfigService.h"
#include "TradingMarketCalendarService.h"
#include "TradingRuntimeStatusService.h"

class UiLifecycleCoordinator : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool tradingPageActivated READ tradingPageActivated NOTIFY tradingPageActivatedChanged)
    Q_PROPERTY(bool strategyLibraryPageActivated READ strategyLibraryPageActivated NOTIFY strategyLibraryPageActivatedChanged)

public:
    static UiLifecycleCoordinator* instance();

    UiLifecycleCoordinator(const UiLifecycleCoordinator&) = delete;
    UiLifecycleCoordinator& operator=(const UiLifecycleCoordinator&) = delete;

    Q_INVOKABLE void activateTradingPage();
    Q_INVOKABLE void activateStrategyLibraryPage();

    bool tradingPageActivated() const;
    bool strategyLibraryPageActivated() const;

signals:
    void tradingPageActivatedChanged();
    void strategyLibraryPageActivatedChanged();

private:
    explicit UiLifecycleCoordinator(QObject* parent = nullptr);

    void initializeTradingRuntimeStage();
    void initializeTradingHoldingsStage();

    inline static UiLifecycleCoordinator* m_instance = nullptr;
    inline static QMutex m_instanceMutex;

    mutable QMutex m_mutex;
    bool m_tradingPageActivated;
    bool m_tradingRuntimeStageScheduled;
    bool m_tradingRuntimeStageInitialized;
    bool m_tradingHoldingsStageScheduled;
    bool m_tradingHoldingsStageInitialized;
    bool m_strategyLibraryPageActivated;
};

inline UiLifecycleCoordinator* UiLifecycleCoordinator::instance()
{
    QMutexLocker locker(&m_instanceMutex);
    if (!m_instance) {
        m_instance = new UiLifecycleCoordinator();
    }
    return m_instance;
}

inline UiLifecycleCoordinator::UiLifecycleCoordinator(QObject* parent)
    : QObject(parent)
    , m_tradingPageActivated(false)
    , m_tradingRuntimeStageScheduled(false)
    , m_tradingRuntimeStageInitialized(false)
    , m_tradingHoldingsStageScheduled(false)
    , m_tradingHoldingsStageInitialized(false)
    , m_strategyLibraryPageActivated(false)
{
}

inline void UiLifecycleCoordinator::activateTradingPage()
{
    bool tradingActivatedChanged = false;
    bool shouldScheduleRuntimeStage = false;
    bool shouldScheduleHoldingsStage = false;

    {
        QMutexLocker locker(&m_mutex);
        if (!m_tradingPageActivated) {
            m_tradingPageActivated = true;
            tradingActivatedChanged = true;
        }
        if (!m_tradingRuntimeStageScheduled && !m_tradingRuntimeStageInitialized) {
            m_tradingRuntimeStageScheduled = true;
            shouldScheduleRuntimeStage = true;
        }
        if (!m_tradingHoldingsStageScheduled && !m_tradingHoldingsStageInitialized) {
            m_tradingHoldingsStageScheduled = true;
            shouldScheduleHoldingsStage = true;
        }
    }

    if (MarketDataService* marketDataService = MarketDataService::instance()) {
        marketDataService->initializeAsync();
    }
    if (StrategyService* strategyService = StrategyService::instance()) {
        strategyService->initializeAsync();
    }

    if (tradingActivatedChanged) {
        emit tradingPageActivatedChanged();
    }

    if (shouldScheduleRuntimeStage) {
        QMetaObject::invokeMethod(this, [this]() {
            initializeTradingRuntimeStage();
        }, Qt::QueuedConnection);
    }

    if (shouldScheduleHoldingsStage) {
        QMetaObject::invokeMethod(this, [this]() {
            initializeTradingHoldingsStage();
        }, Qt::QueuedConnection);
    }
}

inline void UiLifecycleCoordinator::activateStrategyLibraryPage()
{
    bool activatedChanged = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_strategyLibraryPageActivated) {
            return;
        }
        m_strategyLibraryPageActivated = true;
        activatedChanged = true;
    }

    if (TradingConnectionConfigService* configService = TradingConnectionConfigService::instance()) {
        configService->initializeAsync();
    }
    if (TradingMarketCalendarService* marketCalendarService = TradingMarketCalendarService::instance()) {
        marketCalendarService->initializeAsync();
    }
    if (TradingRuntimeStatusService* runtimeStatusService = TradingRuntimeStatusService::instance()) {
        runtimeStatusService->initializeAsync();
    }

    if (activatedChanged) {
        emit strategyLibraryPageActivatedChanged();
    }
}

inline bool UiLifecycleCoordinator::tradingPageActivated() const
{
    QMutexLocker locker(&m_mutex);
    return m_tradingPageActivated;
}

inline bool UiLifecycleCoordinator::strategyLibraryPageActivated() const
{
    QMutexLocker locker(&m_mutex);
    return m_strategyLibraryPageActivated;
}

inline void UiLifecycleCoordinator::initializeTradingRuntimeStage()
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_tradingRuntimeStageInitialized) {
            return;
        }
        m_tradingRuntimeStageInitialized = true;
    }

    if (TradingConnectionConfigService* configService = TradingConnectionConfigService::instance()) {
        configService->initializeAsync();
    }
    if (TradingMarketCalendarService* marketCalendarService = TradingMarketCalendarService::instance()) {
        marketCalendarService->initializeAsync();
    }
    if (TradeExecutionService* tradeExecutionService = TradeExecutionService::instance()) {
        tradeExecutionService->initializeAsync();
    }
    if (TradingRuntimeStatusService* runtimeStatusService = TradingRuntimeStatusService::instance()) {
        runtimeStatusService->initializeAsync();
    }
}

inline void UiLifecycleCoordinator::initializeTradingHoldingsStage()
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_tradingHoldingsStageInitialized) {
            return;
        }
        m_tradingHoldingsStageInitialized = true;
    }

    if (PositionAccountService* positionAccountService = PositionAccountService::instance()) {
        positionAccountService->initialize();
        QMetaObject::invokeMethod(this, [positionAccountService]() {
            positionAccountService->requestInitialSnapshot();
        }, Qt::QueuedConnection);
    }
}