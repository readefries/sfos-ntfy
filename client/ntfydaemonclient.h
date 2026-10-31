// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef NTFYDAEMONCLIENT_H
#define NTFYDAEMONCLIENT_H

#include <QDBusConnection>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

#include <functional>

class QDBusPendingCall;

// The UI's handle on harbour-ntfy-daemon (see service/ntfydbusservice.h). All calls are
// asynchronous; results come back as signals.
class NtfyDaemonClient : public QObject
{
    Q_OBJECT
    // After a reboot the daemon can't read its data until the device lock code has been entered
    // (see unlock()).
    Q_PROPERTY(bool locked READ isLocked NOTIFY lockedChanged)
    // Between unlock() and the outcome, i.e. while the system's prompt is up.
    Q_PROPERTY(bool unlocking READ isUnlocking NOTIFY unlockingChanged)
public:
    explicit NtfyDaemonClient(QObject *parent = nullptr);
    NtfyDaemonClient(const QDBusConnection &connection, QObject *parent = nullptr);

    bool isLocked() const { return m_locked; }
    bool isUnlocking() const { return m_unlocking; }
    // Has the daemon bring up the device lock prompt. Ends in lockedChanged() or unlockFailed().
    Q_INVOKABLE void unlock();

    // Subscribes, adding the server if needed. Credentials, if given, replace the server's stored
    // ones; without them an existing server keeps its credentials. Emits subscribed() or failed().
    Q_INVOKABLE void subscribe(const QString &serverUrl, const QString &topic, const QString &displayName,
                               const QString &token, const QString &username, const QString &password);
    Q_INVOKABLE void unsubscribe(qlonglong subscriptionId);
    Q_INVOKABLE void setMuted(qlonglong subscriptionId, bool muted);
    Q_INVOKABLE void markRead(qlonglong subscriptionId);
    Q_INVOKABLE void removeServer(qlonglong serverId);
    // Replaces a server's login; all empty removes it. Emits serverUpdated() or failed().
    Q_INVOKABLE void updateServer(const QString &serverUrl, const QString &token, const QString &username,
                                  const QString &password);
    Q_INVOKABLE void setDelivery(const QString &mode, int intervalSeconds);
    // Has the daemon perform an "http" action button. Emits actionFinished().
    Q_INVOKABLE void runAction(qlonglong messageId, int actionIndex);

    // Results arrive through the *Loaded signals.
    Q_INVOKABLE void fetchSubscriptions();
    Q_INVOKABLE void fetchServers();
    Q_INVOKABLE void fetchMessages(qlonglong subscriptionId, int limit, qlonglong beforeId);
    Q_INVOKABLE void fetchDelivery();
    Q_INVOKABLE void fetchDeliveryStats();

signals:
    void subscriptionsLoaded(const QJsonArray &subscriptions);
    void messagesLoaded(qlonglong subscriptionId, qlonglong beforeId, const QJsonArray &messages);
    // For QML, hence plain variants.
    void serversLoaded(const QVariantList &servers);
    void deliveryLoaded(const QVariantMap &delivery);
    void deliveryStatsLoaded(const QVariantMap &stats);

    void subscribed(qlonglong subscriptionId);
    void serverUpdated(qlonglong serverId);
    void actionFinished(qlonglong messageId, int actionIndex, bool ok, const QString &result);
    void failed(const QString &error);
    void lockedChanged();
    void unlockingChanged();
    void unlockFailed(const QString &error);

    // Forwarded from the daemon.
    void subscriptionsChanged();
    void messageReceived(qlonglong subscriptionId, const QJsonObject &message);
    void deliveryChanged();
    void serverError(qlonglong serverId, const QString &error);
    void serverStateChanged(qlonglong serverId, const QString &state);

private slots:
    void onMessageReceived(qlonglong subscriptionId, const QString &message);
    void onLockedChanged(bool locked);
    void onUnlockFailed(const QString &error);

private:
    QDBusPendingCall call(const QString &method, const QVariantList &arguments = QVariantList());
    // Calls method and hands the first reply argument to onReply, or emits failed().
    void callThen(const QString &method, const QVariantList &arguments,
                  const std::function<void(const QVariant &)> &onReply);
    void connectSignals();
    void setLocked(bool locked);
    void setUnlocking(bool unlocking);

    QDBusConnection m_connection;
    bool m_locked = false;
    bool m_unlocking = false;
};

#endif // NTFYDAEMONCLIENT_H
