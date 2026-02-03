#pragma once
#include <QObject>
#include <QString>

class GlobalState : public QObject {
    Q_OBJECT
public:
    static GlobalState& instance();

    Q_PROPERTY(bool usePreciseMatch READ usePreciseMatch WRITE setUsePreciseMatch NOTIFY usePreciseMatchChanged)
    Q_PROPERTY(QString token READ token WRITE setToken NOTIFY tokenChanged)
    Q_PROPERTY(QString accountId READ accountId WRITE setAccountId NOTIFY accountIdChanged)

    Q_PROPERTY(QString jqUsername READ jqUsername WRITE setJqUsername NOTIFY jqUsernameChanged)
    Q_PROPERTY(QString jqPassword READ jqPassword WRITE setJqPassword NOTIFY jqPasswordChanged)
    Q_PROPERTY(bool jqConnected READ jqConnected WRITE setJqConnected NOTIFY jqConnectedChanged)
    Q_PROPERTY(bool jqConnecting READ jqConnecting WRITE setJqConnecting NOTIFY jqConnectingChanged)

    bool usePreciseMatch() const;
    void setUsePreciseMatch(bool value);

    QString token() const;
    void setToken(const QString& value);

    QString accountId() const;
    void setAccountId(const QString& value);

    QString jqUsername() const;
    void setJqUsername(const QString& value);

    QString jqPassword() const;
    void setJqPassword(const QString& value);

    bool jqConnected() const;
    void setJqConnected(bool value);

    bool jqConnecting() const;
    void setJqConnecting(bool value);

signals:
    void usePreciseMatchChanged(bool value);
    void tokenChanged(const QString& value);
    void accountIdChanged(const QString& value);

    void jqUsernameChanged(const QString& value);
    void jqPasswordChanged(const QString& value);
    void jqConnectedChanged(bool value);
    void jqConnectingChanged(bool value);

private:
    explicit GlobalState(QObject* parent = nullptr);
    bool m_usePreciseMatch = false;
    QString m_token;
    QString m_accountId;

    QString m_jqUsername;
    QString m_jqPassword;
    bool m_jqConnected = false;
    bool m_jqConnecting = false;
};
