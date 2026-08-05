// CrosshairManager.cpp
#include "CrosshairManager.h"

namespace bridge {

CrosshairManager::CrosshairManager(QObject* parent) : QObject(parent) {}

CrosshairManager& CrosshairManager::instance() {
    static CrosshairManager s_instance;
    return s_instance;
}

void CrosshairManager::setSelectedIndex(int idx) {
    if (m_index != idx) {
        m_index = idx;
        if (idx >= 0 && !m_visible) {
            m_visible = true;
            emit visibleChanged();
        }
        emit selectedIndexChanged();
    }
}

void CrosshairManager::setVisible(bool v) {
    if (m_visible != v) {
        m_visible = v;
        emit visibleChanged();
    }
}

void CrosshairManager::hide() {
    setVisible(false);
}

} // namespace bridge
