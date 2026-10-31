// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfydbusservice.h"

#include "ntfyactionrunner.h"
#include "ntfydeliverycontroller.h"
#include "ntfysubscriptionmanager.h"

#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace NtfyDBus {
const char *const ServiceName = "nl.readefries.harbourntfy";
const char *const ObjectPath = "/";
const char *const Interface = "nl.readefries.harbourntfy.Daemon";
const char *const LockedError = "nl.readefries.harbourntfy.Error.Locked";
}

namespace {

QJsonValue eventJson(const NtfyEvent &event)
{
    if (!event.isValid())
        return QJsonValue();
    return QJsonDocument::fromJson(event.toJson()).object();
}

QJsonObject messageJson(const NtfyStoredMessage &message)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), double(message.id));
    obj.insert(QStringLiteral("read"), message.read);
    obj.insert(QStringLiteral("event"), eventJson(message.event));
    return obj;
}

QString toString(const QJsonArray &array)
{
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

} // namespace

NtfyDBusService::NtfyDBusService(NtfySubscriptionManager *manager, QObject *parent)
    : QObject(parent)
    , m_manager(nullptr)
{
    if (manager)
        setManager(manager);
}

void NtfyDBusService::setManager(NtfySubscriptionManager *manager)
{
    Q_ASSERT(!m_manager && manager);
    m_manager = manager;
    connect(m_manager, &NtfySubscriptionManager::messageReceived, this,
            [this](const NtfySubscriptionRecord &subscription, const NtfyStoredMessage &message) {
        emit MessageReceived(subscription.id,
                             QString::fromUtf8(QJsonDocument(messageJson(message)).toJson(QJsonDocument::Compact)));
    });
    connect(m_manager, &NtfySubscriptionManager::subscriptionsChanged, this, &NtfyDBusService::SubscriptionsChanged);
    connect(m_manager, &NtfySubscriptionManager::serverStateChanged, this,
            [this](qint64 serverId, NtfyStream::State state) {
        emit ServerStateChanged(serverId, stateName(state));
    });
    connect(m_manager, &NtfySubscriptionManager::serverError, this, &NtfyDBusService::ServerError);
    emit LockedChanged(false);
    // Unknown until now, as far as the UI is concerned.
    emit SubscriptionsChanged();
    emit DeliveryChanged();
}

void NtfyDBusService::unlockFailed(const QString &error)
{
    emit UnlockFailed(error);
}

void NtfyDBusService::setDeliveryController(NtfyDeliveryController *delivery)
{
    if (m_delivery)
        m_delivery->disconnect(this);
    m_delivery = delivery;
    if (m_delivery)
        connect(m_delivery, &NtfyDeliveryController::deliveryChanged, this, &NtfyDBusService::DeliveryChanged);
}

void NtfyDBusService::setActionRunner(NtfyActionRunner *actions)
{
    m_actions = actions;
}

bool NtfyDBusService::registerOn(QDBusConnection connection, QString *error)
{
    if (!connection.isConnected()) {
        if (error)
            *error = QStringLiteral("no D-Bus connection: ") + connection.lastError().message();
        return false;
    }
    if (!connection.registerObject(QString::fromLatin1(NtfyDBus::ObjectPath), this,
                                   QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals)) {
        if (error)
            *error = QStringLiteral("could not register object: ") + connection.lastError().message();
        return false;
    }
    if (!connection.registerService(QString::fromLatin1(NtfyDBus::ServiceName))) {
        connection.unregisterObject(QString::fromLatin1(NtfyDBus::ObjectPath));
        if (error)
            *error = QStringLiteral("could not claim %1 (already running?)").arg(QLatin1String(NtfyDBus::ServiceName));
        return false;
    }
    return true;
}

QString NtfyDBusService::stateName(NtfyStream::State state)
{
    switch (state) {
    case NtfyStream::Connecting:
        return QStringLiteral("connecting");
    case NtfyStream::Connected:
        return QStringLiteral("connected");
    case NtfyStream::WaitingToReconnect:
        return QStringLiteral("waiting");
    case NtfyStream::Disconnected:
        break;
    }
    return QStringLiteral("disconnected");
}

void NtfyDBusService::replyError(const QString &error)
{
    if (calledFromDBus())
        sendErrorReply(QDBusError::InvalidArgs, error);
}

bool NtfyDBusService::unlocked()
{
    if (m_manager)
        return true;
    if (calledFromDBus())
        sendErrorReply(QString::fromLatin1(NtfyDBus::LockedError),
                       QStringLiteral("locked until the device lock code is entered"));
    return false;
}

bool NtfyDBusService::Locked()
{
    return !m_manager;
}

void NtfyDBusService::Unlock()
{
    if (m_manager)
        return;
    QTimer::singleShot(0, this, [this]() {
        if (!m_manager)
            emit unlockRequested();
    });
}

qlonglong NtfyDBusService::AddServer(const QString &url, const QString &token, const QString &username,
                                     const QString &password)
{
    if (!unlocked())
        return 0;
    QString error;
    const qint64 id = m_manager->addServer(QUrl(url), token, username, password, &error);
    if (!id)
        replyError(error);
    return id;
}

void NtfyDBusService::RemoveServer(qlonglong serverId)
{
    if (!unlocked())
        return;
    QString error;
    if (!m_manager->removeServer(serverId, &error))
        replyError(error);
}

QString NtfyDBusService::Servers()
{
    if (!unlocked())
        return QString();
    QJsonArray array;
    for (const NtfyServerRecord &server : m_manager->database()->servers()) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), double(server.id));
        obj.insert(QStringLiteral("url"), server.url.toString());
        obj.insert(QStringLiteral("state"), stateName(m_manager->serverState(server.id)));
        obj.insert(QStringLiteral("error"), m_manager->lastError(server.id));
        array.append(obj);
    }
    return toString(array);
}

qlonglong NtfyDBusService::AddSubscription(qlonglong serverId, const QString &topic, const QString &displayName)
{
    if (!unlocked())
        return 0;
    QString error;
    const qint64 id = m_manager->addSubscription(serverId, topic, displayName, &error);
    if (!id)
        replyError(error);
    return id;
}

void NtfyDBusService::RemoveSubscription(qlonglong subscriptionId)
{
    if (!unlocked())
        return;
    QString error;
    if (!m_manager->removeSubscription(subscriptionId, &error))
        replyError(error);
}

void NtfyDBusService::SetMuted(qlonglong subscriptionId, bool muted)
{
    if (!unlocked())
        return;
    if (!m_manager->setMuted(subscriptionId, muted))
        replyError(QStringLiteral("no such subscription"));
}

void NtfyDBusService::MarkRead(qlonglong subscriptionId)
{
    if (!unlocked())
        return;
    if (!m_manager->markRead(subscriptionId))
        replyError(QStringLiteral("could not mark as read"));
}

QString NtfyDBusService::Subscriptions()
{
    if (!unlocked())
        return QString();

    QHash<qint64, QString> serverUrls;
    for (const NtfyServerRecord &server : m_manager->database()->servers())
        serverUrls.insert(server.id, server.url.toString());

    QJsonArray array;
    for (const NtfySubscriptionRecord &subscription : m_manager->database()->subscriptions()) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), double(subscription.id));
        obj.insert(QStringLiteral("serverId"), double(subscription.serverId));
        obj.insert(QStringLiteral("serverUrl"), serverUrls.value(subscription.serverId));
        obj.insert(QStringLiteral("topic"), subscription.topic);
        obj.insert(QStringLiteral("displayName"), subscription.displayName);
        obj.insert(QStringLiteral("muted"), subscription.muted);
        obj.insert(QStringLiteral("unread"), subscription.unreadCount);
        obj.insert(QStringLiteral("lastMessage"), eventJson(subscription.lastMessage));
        array.append(obj);
    }
    return toString(array);
}

QString NtfyDBusService::Messages(qlonglong subscriptionId, int limit, qlonglong beforeId)
{
    if (!unlocked())
        return QString();
    QJsonArray array;
    for (const NtfyStoredMessage &message : m_manager->database()->messages(subscriptionId, qBound(1, limit, 500), beforeId))
        array.append(messageJson(message));
    return toString(array);
}

QString NtfyDBusService::Delivery()
{
    if (!unlocked())
        return QString();
    if (!m_delivery) {
        replyError(QStringLiteral("delivery settings are not available"));
        return QString();
    }

    QJsonObject options;
    QJsonObject defaults;
    for (NtfyDeliveryController::Mode mode : {NtfyDeliveryController::Instant, NtfyDeliveryController::Balanced,
                                              NtfyDeliveryController::BatterySaver}) {
        QJsonArray seconds;
        for (int interval : NtfyDeliveryController::intervals(mode))
            seconds.append(interval);
        options.insert(NtfyDeliveryController::modeName(mode), seconds);
        defaults.insert(NtfyDeliveryController::modeName(mode), NtfyDeliveryController::defaultInterval(mode));
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("mode"), NtfyDeliveryController::modeName(m_delivery->mode()));
    obj.insert(QStringLiteral("interval"), m_delivery->interval());
    obj.insert(QStringLiteral("options"), options);
    obj.insert(QStringLiteral("defaults"), defaults);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void NtfyDBusService::SetDelivery(const QString &mode, int intervalSeconds)
{
    if (!unlocked())
        return;
    NtfyDeliveryController::Mode parsed;
    if (!m_delivery) {
        replyError(QStringLiteral("delivery settings are not available"));
        return;
    }
    if (!NtfyDeliveryController::modeFromName(mode, &parsed)) {
        replyError(QStringLiteral("mode must be instant, balanced or battery-saver"));
        return;
    }
    QString error;
    if (!m_delivery->setDelivery(parsed, intervalSeconds, &error))
        replyError(error);
}

QString NtfyDBusService::DeliveryStats()
{
    if (!unlocked())
        return QString();
    if (!m_delivery) {
        replyError(QStringLiteral("delivery settings are not available"));
        return QString();
    }

    const NtfyDeliveryController::Stats stats = m_delivery->stats();
    QJsonObject obj;
    obj.insert(QStringLiteral("wakeups"), stats.wakeups);
    obj.insert(QStringLiteral("lastWakeup"), double(stats.lastWakeup));
    obj.insert(QStringLiteral("messages"), stats.messages);
    obj.insert(QStringLiteral("averageDelay"), stats.messages ? double(stats.totalDelay) / stats.messages : 0.0);
    obj.insert(QStringLiteral("maxDelay"), double(stats.maxDelay));
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

int NtfyDBusService::RunAction(qlonglong messageId, int actionIndex)
{
    if (!unlocked())
        return 0;
    const NtfyStoredMessage message = m_manager->database()->message(messageId);
    if (!message.isValid() || actionIndex < 0 || actionIndex >= message.event.actions.size()) {
        replyError(QStringLiteral("no such action"));
        return 0;
    }
    const NtfyAction action = message.event.actions.at(actionIndex);
    QString error;
    if (!m_actions || !NtfyActionRunner::canRun(action, &error)) {
        replyError(error.isEmpty() ? QStringLiteral("actions are not available") : error);
        return 0;
    }
    if (!calledFromDBus())
        return 0;

    // Answer when the request is done, without blocking the daemon meanwhile.
    setDelayedReply(true);
    const QDBusMessage request = QDBusContext::message();
    QDBusConnection bus = connection();
    m_actions->run(action, [request, bus](bool ok, int status, const QString &error) mutable {
        bus.send(ok ? request.createReply(status) : request.createErrorReply(QDBusError::Failed, error));
    });
    return 0;
}

void NtfyDBusService::NotificationActivated(qlonglong subscriptionId)
{
    if (!unlocked())
        return;
    m_manager->markRead(subscriptionId);
    emit notificationActivated(subscriptionId);
}

void NtfyDBusService::ReconnectAll()
{
    if (!unlocked())
        return;
    m_manager->reconnectAll();
}
