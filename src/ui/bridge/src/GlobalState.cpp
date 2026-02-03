#include "GlobalState.h"

GlobalState& GlobalState::instance() {
    static GlobalState inst;
    return inst;
}

GlobalState::GlobalState(QObject* parent) : QObject(parent) {}

bool GlobalState::usePreciseMatch() const {
    return m_usePreciseMatch;
}

void GlobalState::setUsePreciseMatch(bool value) {
    if (m_usePreciseMatch != value) {
        m_usePreciseMatch = value;
        emit usePreciseMatchChanged(value);
    }
}

QString GlobalState::token() const {
    return m_token;
}

void GlobalState::setToken(const QString& value) {
    if (m_token != value) {
        m_token = value;
        emit tokenChanged(value);
    }
}

QString GlobalState::accountId() const {
    return m_accountId;
}

void GlobalState::setAccountId(const QString& value) {
    if (m_accountId != value) {
        m_accountId = value;
        emit accountIdChanged(value);
    }
}

QString GlobalState::jqUsername() const {
    return m_jqUsername;
}

void GlobalState::setJqUsername(const QString& value) {
    if (m_jqUsername != value) {
        m_jqUsername = value;
        emit jqUsernameChanged(value);
    }
}

QString GlobalState::jqPassword() const {
    return m_jqPassword;
}

void GlobalState::setJqPassword(const QString& value) {
    if (m_jqPassword != value) {
        m_jqPassword = value;
        emit jqPasswordChanged(value);
    }
}

bool GlobalState::jqConnected() const {
    return m_jqConnected;
}

void GlobalState::setJqConnected(bool value) {
    if (m_jqConnected != value) {
        m_jqConnected = value;
        emit jqConnectedChanged(value);
    }
}

bool GlobalState::jqConnecting() const {
    return m_jqConnecting;
}

void GlobalState::setJqConnecting(bool value) {
    if (m_jqConnecting != value) {
        m_jqConnecting = value;
        emit jqConnectingChanged(value);
    }
}
