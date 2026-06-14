#include "TradingFormPanelHelper.h"

TradingFormPanelHelper* TradingFormPanelHelper::m_instance = nullptr;

TradingFormPanelHelper* TradingFormPanelHelper::instance() {
    if (!m_instance) { m_instance = new TradingFormPanelHelper(); }
    return m_instance;
}

TradingFormPanelHelper::TradingFormPanelHelper(QObject* parent) : QObject(parent) {}