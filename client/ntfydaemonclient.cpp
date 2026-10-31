// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfydaemonclient.h"

#include "ntfyserver.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonDocument>
#include <QUrl>

namespace {
// Keep in sync with service/ntfydbusservice.cpp.
const QString ServiceName = QStringLiteral("nl.readefries.harbourntfy");
const QString ObjectPath = QStringLiteral("/");
const QString Interface = QStringLiteral("nl.readefries.harbourntfy.Daemon");
const QString LockedError = QStringLiteral("nl.readefries.harbourntfy.Error.Locked");

QJsonArray parseArray(const QVariant &value)
{
    return QJsonDocument::fromJson(value.toString().toUtf8()).array();
}
} // namespace

NtfyDaemonClient::NtfyDaemonClient(QObject *parent)
    : NtfyDaemonClient(QDBusConnection::sessionBus(), parent)
{
}

NtfyDaemonClient::NtfyDaemonClient(const QDBusConnection &connection, QObject *parent)
    : QObject(parent)
    , m_connection(connection)
{
    connectSignals();

    // Not via callThen(): an older daemon without Locked() is simply unlocked.
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call(QStringLiteral("Locked")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusMessage reply = watcher->reply();
        if (reply.type() == QDBusMessage::ReplyMessage)
            setLocked(reply.arguments().value(0).toBool());
    });
}

void NtfyDaemonClient::connectSignals()
{
    m_connection.connect(ServiceName, ObjectPath, Interface, QStringLiteral("SubscriptionsChanged"),
                         this, SIGNAL(subscriptionsChanged()));
    m_connection.connect(ServiceName, ObjectPath, Interface, QStringLiteral("DeliveryChanged"),
                         this, SIGNAL(deliveryChanged()));
    m_connection.connect(ServiceName, ObjectPath, Interface, QStringLiteral("ServerError"),
                         this, SIGNAL(serverError(qlonglong,QString)));
    m_connection.connect(ServiceName, ObjectPath, Interface, QStringLiteral("ServerStateChanged"),
                         this, SIGNAL(serverStateChanged(qlonglong,QString)));
    m_connection.connect(ServiceName, ObjectPath, Interface, QStringLiteral("MessageReceived"),
                         this, SLOT(onMessageReceived(qlonglong,QString)));
    m_connection.connect(ServiceName, ObjectPath, Interface, QStringLiteral("LockedChanged"),
                         this, SLOT(onLockedChanged(bool)));
    m_connection.connect(ServiceName, ObjectPath, Interface, QStringLiteral("UnlockFailed"),
                         this, SLOT(onUnlockFailed(QString)));
}

void NtfyDaemonClient::setLocked(bool locked)
{
    if (m_locked == locked)
        return;
    m_locked = locked;
    emit lockedChanged();
}

void NtfyDaemonClient::setUnlocking(bool unlocking)
{
    if (m_unlocking == unlocking)
        return;
    m_unlocking = unlocking;
    emit unlockingChanged();
}

void NtfyDaemonClient::onLockedChanged(bool locked)
{
    setUnlocking(false);
    setLocked(locked);
}

void NtfyDaemonClient::onUnlockFailed(const QString &error)
{
    setUnlocking(false);
    emit unlockFailed(error);
}

void NtfyDaemonClient::unlock()
{
    if (m_unlocking)
        return;
    setUnlocking(true);
    // Replies at once; the outcome comes as a signal.
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call(QStringLiteral("Unlock")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusMessage reply = watcher->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            setUnlocking(false);
            emit unlockFailed(reply.errorMessage());
        }
    });
}

void NtfyDaemonClient::onMessageReceived(qlonglong subscriptionId, const QString &message)
{
    emit messageReceived(subscriptionId, QJsonDocument::fromJson(message.toUtf8()).object());
}

QDBusPendingCall NtfyDaemonClient::call(const QString &method, const QVariantList &arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(ServiceName, ObjectPath, Interface, method);
    message.setArguments(arguments);
    return m_connection.asyncCall(message);
}

void NtfyDaemonClient::callThen(const QString &method, const QVariantList &arguments,
                                const std::function<void(const QVariant &)> &onReply)
{
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call(method, arguments), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, onReply](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusMessage reply = watcher->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            // Not a failure worth a notice: the UI shows the locked state instead.
            if (reply.errorName() == LockedError)
                setLocked(true);
            else
                emit failed(reply.errorMessage());
            return;
        }
        if (onReply)
            onReply(reply.arguments().value(0));
    });
}

void NtfyDaemonClient::subscribe(const QString &serverUrl, const QString &topic, const QString &displayName,
                                 const QString &token, const QString &username, const QString &password)
{
    auto addSubscription = [this, topic, displayName](qlonglong serverId) {
        callThen(QStringLiteral("AddSubscription"), QVariantList() << serverId << topic << displayName,
                 [this](const QVariant &subscriptionId) {
            emit subscribed(subscriptionId.toLongLong());
        });
    };
    auto addServer = [this, serverUrl, token, username, password, addSubscription]() {
        callThen(QStringLiteral("AddServer"), QVariantList() << serverUrl << token << username << password,
                 [addSubscription](const QVariant &serverId) { addSubscription(serverId.toLongLong()); });
    };

    if (!token.isEmpty() || !username.isEmpty()) {
        addServer();
        return;
    }

    // AddServer() without credentials would remove the stored ones, so reuse a known server as is.
    const QUrl wanted = ntfyNormalizeServerUrl(QUrl(serverUrl));
    callThen(QStringLiteral("Servers"), QVariantList(), [wanted, addServer, addSubscription](const QVariant &json) {
        for (const QJsonValue &server : parseArray(json)) {
            const QJsonObject obj = server.toObject();
            if (QUrl(obj.value(QLatin1String("url")).toString()) == wanted) {
                addSubscription(qlonglong(obj.value(QLatin1String("id")).toDouble()));
                return;
            }
        }
        addServer();
    });
}

void NtfyDaemonClient::unsubscribe(qlonglong subscriptionId)
{
    callThen(QStringLiteral("RemoveSubscription"), QVariantList() << subscriptionId, nullptr);
}

void NtfyDaemonClient::setMuted(qlonglong subscriptionId, bool muted)
{
    callThen(QStringLiteral("SetMuted"), QVariantList() << subscriptionId << muted, nullptr);
}

void NtfyDaemonClient::markRead(qlonglong subscriptionId)
{
    callThen(QStringLiteral("MarkRead"), QVariantList() << subscriptionId, nullptr);
}

void NtfyDaemonClient::removeServer(qlonglong serverId)
{
    callThen(QStringLiteral("RemoveServer"), QVariantList() << serverId, nullptr);
}

void NtfyDaemonClient::updateServer(const QString &serverUrl, const QString &token, const QString &username,
                                    const QString &password)
{
    callThen(QStringLiteral("AddServer"), QVariantList() << serverUrl << token << username << password,
             [this](const QVariant &serverId) { emit serverUpdated(serverId.toLongLong()); });
}

void NtfyDaemonClient::setDelivery(const QString &mode, int intervalSeconds)
{
    callThen(QStringLiteral("SetDelivery"), QVariantList() << mode << intervalSeconds, nullptr);
}

void NtfyDaemonClient::runAction(qlonglong messageId, int actionIndex)
{
    // Not via callThen(): a failing action is reported as its own result, not as a client error.
    QDBusPendingCallWatcher *watcher =
            new QDBusPendingCallWatcher(call(QStringLiteral("RunAction"), QVariantList() << messageId << actionIndex), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, messageId, actionIndex](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusMessage reply = watcher->reply();
        if (reply.type() == QDBusMessage::ErrorMessage)
            emit actionFinished(messageId, actionIndex, false, reply.errorMessage());
        else
            emit actionFinished(messageId, actionIndex, true,
                                QStringLiteral("HTTP %1").arg(reply.arguments().value(0).toInt()));
    });
}

void NtfyDaemonClient::fetchSubscriptions()
{
    callThen(QStringLiteral("Subscriptions"), QVariantList(), [this](const QVariant &json) {
        emit subscriptionsLoaded(parseArray(json));
    });
}

void NtfyDaemonClient::fetchServers()
{
    callThen(QStringLiteral("Servers"), QVariantList(), [this](const QVariant &json) {
        emit serversLoaded(parseArray(json).toVariantList());
    });
}

void NtfyDaemonClient::fetchMessages(qlonglong subscriptionId, int limit, qlonglong beforeId)
{
    callThen(QStringLiteral("Messages"), QVariantList() << subscriptionId << limit << beforeId,
             [this, subscriptionId, beforeId](const QVariant &json) {
        emit messagesLoaded(subscriptionId, beforeId, parseArray(json));
    });
}

void NtfyDaemonClient::fetchDelivery()
{
    callThen(QStringLiteral("Delivery"), QVariantList(), [this](const QVariant &json) {
        emit deliveryLoaded(QJsonDocument::fromJson(json.toString().toUtf8()).object().toVariantMap());
    });
}

void NtfyDaemonClient::fetchDeliveryStats()
{
    callThen(QStringLiteral("DeliveryStats"), QVariantList(), [this](const QVariant &json) {
        emit deliveryStatsLoaded(QJsonDocument::fromJson(json.toString().toUtf8()).object().toVariantMap());
    });
}
