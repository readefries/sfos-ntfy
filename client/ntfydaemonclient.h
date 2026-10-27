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
public:
    explicit NtfyDaemonClient(QObject *parent = nullptr);
    NtfyDaemonClient(const QDBusConnection &connection, QObject *parent = nullptr);

    // Subscribes, adding the server if needed. Credentials, if given, replace the server's stored
    // ones; without them an existing server keeps its credentials. Emits subscribed() or failed().
    Q_INVOKABLE void subscribe(const QString &serverUrl, const QString &topic, const QString &displayName,
                               const QString &token, const QString &username, const QString &password);
    Q_INVOKABLE void unsubscribe(qlonglong subscriptionId);
    Q_INVOKABLE void setMuted(qlonglong subscriptionId, bool muted);
    Q_INVOKABLE void markRead(qlonglong subscriptionId);
    Q_INVOKABLE void removeServer(qlonglong serverId);
    Q_INVOKABLE void setDelivery(const QString &mode, int intervalSeconds);

    // Results arrive through the *Loaded signals.
    Q_INVOKABLE void fetchSubscriptions();
    Q_INVOKABLE void fetchServers();
    Q_INVOKABLE void fetchMessages(qlonglong subscriptionId, int limit, qlonglong beforeId);
    Q_INVOKABLE void fetchDelivery();

signals:
    void subscriptionsLoaded(const QJsonArray &subscriptions);
    void serversLoaded(const QJsonArray &servers);
    void messagesLoaded(qlonglong subscriptionId, qlonglong beforeId, const QJsonArray &messages);
    void deliveryLoaded(const QJsonObject &delivery);

    void subscribed(qlonglong subscriptionId);
    void failed(const QString &error);

    // Forwarded from the daemon.
    void subscriptionsChanged();
    void messageReceived(qlonglong subscriptionId, const QJsonObject &message);
    void deliveryChanged();
    void serverError(qlonglong serverId, const QString &error);

private slots:
    void onMessageReceived(qlonglong subscriptionId, const QString &message);

private:
    QDBusPendingCall call(const QString &method, const QVariantList &arguments = QVariantList());
    // Calls method and hands the first reply argument to onReply, or emits failed().
    void callThen(const QString &method, const QVariantList &arguments,
                  const std::function<void(const QVariant &)> &onReply);
    void connectSignals();

    QDBusConnection m_connection;
};

#endif // NTFYDAEMONCLIENT_H
