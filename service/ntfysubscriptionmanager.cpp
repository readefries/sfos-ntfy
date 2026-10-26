// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfysubscriptionmanager.h"

#include "ntfycredentials.h"

#include <QRegularExpression>

namespace {
// Per subscription; older messages are dropped on start.
const int MaxStoredMessages = 1000;
} // namespace

NtfySubscriptionManager::NtfySubscriptionManager(NtfyDatabase *database, NtfyCredentialStore *credentials,
                                                 QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_credentials(credentials)
    , m_nam(nam)
{
    qRegisterMetaType<NtfySubscriptionRecord>();
    qRegisterMetaType<NtfyStoredMessage>();
}

NtfySubscriptionManager::~NtfySubscriptionManager()
{
    stop();
}

bool NtfySubscriptionManager::isValidTopic(const QString &topic)
{
    // Same rule as the ntfy server.
    static const QRegularExpression pattern(QStringLiteral("^[-_A-Za-z0-9]{1,64}$"));
    return pattern.match(topic).hasMatch();
}

void NtfySubscriptionManager::start()
{
    if (m_started)
        return;
    m_started = true;

    m_database->pruneMessages(MaxStoredMessages);
    for (const NtfyServerRecord &server : m_database->servers())
        syncServer(server.id);
}

void NtfySubscriptionManager::stop()
{
    const QList<qint64> serverIds = m_connections.keys();
    for (qint64 serverId : serverIds)
        dropConnection(serverId);
    m_started = false;
}

qint64 NtfySubscriptionManager::addServer(const QUrl &url, const QString &token, const QString &username,
                                          const QString &password, QString *error)
{
    const QUrl normalized = ntfyNormalizeServerUrl(url);
    const QString scheme = normalized.scheme();
    if (!normalized.isValid() || normalized.host().isEmpty()
            || (scheme != QLatin1String("https") && scheme != QLatin1String("http"))) {
        if (error)
            *error = QStringLiteral("not a valid http(s) server URL");
        return 0;
    }

    NtfyServerConfig credentials;
    credentials.baseUrl = normalized;
    credentials.token = token;
    credentials.username = token.isEmpty() ? username : QString();
    credentials.password = token.isEmpty() ? password : QString();

    // Store the credentials first, so a server never exists with half its configuration.
    const QString key = ntfyCredentialKey(normalized);
    QString storeError;
    const bool anonymous = credentials.token.isEmpty() && credentials.username.isEmpty();
    const bool stored = anonymous ? m_credentials->remove(key, &storeError)
                                  : m_credentials->store(key, ntfyEncodeCredentials(credentials), &storeError);
    if (!stored) {
        if (error)
            *error = QStringLiteral("could not store credentials: ") + storeError;
        return 0;
    }

    const qint64 serverId = m_database->addServer(normalized);
    if (!serverId) {
        if (error)
            *error = m_database->lastError();
        return 0;
    }

    // New credentials for a connected server take effect right away.
    if (m_started && m_connections.contains(serverId)) {
        dropConnection(serverId);
        syncServer(serverId);
    }
    return serverId;
}

bool NtfySubscriptionManager::removeServer(qint64 serverId, QString *error)
{
    const NtfyServerRecord server = m_database->server(serverId);
    if (!server.isValid()) {
        if (error)
            *error = QStringLiteral("no such server");
        return false;
    }

    dropConnection(serverId);

    QString removeError;
    if (!m_credentials->remove(ntfyCredentialKey(server.url), &removeError)) {
        // Keep the server, so the user can retry instead of leaving orphaned credentials behind.
        if (error)
            *error = QStringLiteral("could not remove credentials: ") + removeError;
        if (m_started)
            syncServer(serverId);
        return false;
    }

    if (!m_database->removeServer(serverId)) {
        if (error)
            *error = m_database->lastError();
        return false;
    }
    emit subscriptionsChanged();
    return true;
}

qint64 NtfySubscriptionManager::addSubscription(qint64 serverId, const QString &topic,
                                                const QString &displayName, QString *error)
{
    if (!isValidTopic(topic)) {
        if (error)
            *error = QStringLiteral("topics are 1-64 characters: letters, digits, - and _");
        return 0;
    }
    if (!m_database->server(serverId).isValid()) {
        if (error)
            *error = QStringLiteral("no such server");
        return 0;
    }

    const qint64 subscriptionId = m_database->addSubscription(serverId, topic, displayName);
    if (!subscriptionId) {
        if (error)
            *error = m_database->lastError();
        return 0;
    }

    if (m_started)
        syncServer(serverId);
    emit subscriptionsChanged();
    return subscriptionId;
}

bool NtfySubscriptionManager::removeSubscription(qint64 subscriptionId, QString *error)
{
    const NtfySubscriptionRecord subscription = m_database->subscription(subscriptionId);
    if (!subscription.isValid()) {
        if (error)
            *error = QStringLiteral("no such subscription");
        return false;
    }
    if (!m_database->removeSubscription(subscriptionId)) {
        if (error)
            *error = m_database->lastError();
        return false;
    }

    if (m_started)
        syncServer(subscription.serverId);
    emit subscriptionsChanged();
    return true;
}

bool NtfySubscriptionManager::setMuted(qint64 subscriptionId, bool muted)
{
    const bool ok = m_database->setMuted(subscriptionId, muted);
    if (ok)
        emit subscriptionsChanged();
    return ok;
}

bool NtfySubscriptionManager::markRead(qint64 subscriptionId)
{
    const bool ok = m_database->markRead(subscriptionId);
    if (ok)
        emit subscriptionsChanged();
    return ok;
}

NtfyStream::State NtfySubscriptionManager::serverState(qint64 serverId) const
{
    const auto it = m_connections.constFind(serverId);
    return it == m_connections.constEnd() ? NtfyStream::Disconnected : it->stream->state();
}

void NtfySubscriptionManager::reconnectAll()
{
    for (const Connection &connection : m_connections)
        connection.stream->reconnectNow();
}

void NtfySubscriptionManager::syncServer(qint64 serverId)
{
    const NtfyServerRecord server = m_database->server(serverId);
    const QList<NtfySubscriptionRecord> subscriptions = m_database->subscriptions(serverId);
    if (!server.isValid() || subscriptions.isEmpty()) {
        dropConnection(serverId);
        return;
    }

    QHash<QString, qint64> byTopic;
    for (const NtfySubscriptionRecord &subscription : subscriptions)
        byTopic.insert(subscription.topic, subscription.id);
    QStringList topics = byTopic.keys();
    topics.sort();

    auto it = m_connections.find(serverId);
    if (it != m_connections.end()) {
        it->subscriptionByTopic = byTopic;
        if (it->stream->topics() != topics) {
            // Keeps resuming from the stream's last message id.
            it->stream->setTopics(topics);
            it->stream->reconnectNow();
        }
        return;
    }

    NtfyServerConfig config;
    QByteArray stored;
    bool found = false;
    QString error;
    if (!m_credentials->load(ntfyCredentialKey(server.url), &stored, &found, &error)) {
        // Don't connect without the credentials the user configured.
        emit serverError(serverId, QStringLiteral("could not read credentials: ") + error);
        return;
    }
    if (found)
        ntfyDecodeCredentials(stored, &config);
    config.baseUrl = server.url;

    Connection connection;
    connection.subscriptionByTopic = byTopic;
    connection.stream = new NtfyStream(m_nam, this);
    connection.stream->setServer(config);
    connection.stream->setTopics(topics);
    // Resume by time rather than message id: ids from expired messages are unknown to the server.
    // Messages from that same second come back again, but are already stored and get skipped.
    const qint64 newest = m_database->newestMessageTime(serverId);
    if (newest > 0)
        connection.stream->setSince(QString::number(newest));

    connect(connection.stream, &NtfyStream::messageReceived, this, [this, serverId](const NtfyEvent &event) {
        handleMessage(serverId, event);
    });
    connect(connection.stream, &NtfyStream::stateChanged, this, [this, serverId](NtfyStream::State state) {
        emit serverStateChanged(serverId, state);
    });
    connect(connection.stream, &NtfyStream::errorOccurred, this, [this, serverId](const QString &error) {
        emit serverError(serverId, error);
    });

    m_connections.insert(serverId, connection);
    connection.stream->start();
}

void NtfySubscriptionManager::dropConnection(qint64 serverId)
{
    const auto it = m_connections.find(serverId);
    if (it == m_connections.end())
        return;

    NtfyStream *stream = it->stream;
    m_connections.erase(it);
    stream->disconnect(this);
    stream->stop();
    stream->deleteLater();
    emit serverStateChanged(serverId, NtfyStream::Disconnected);
}

void NtfySubscriptionManager::handleMessage(qint64 serverId, const NtfyEvent &event)
{
    const auto it = m_connections.constFind(serverId);
    if (it == m_connections.constEnd())
        return;
    const qint64 subscriptionId = it->subscriptionByTopic.value(event.topic);
    if (!subscriptionId)
        return;

    const qint64 rowId = m_database->addMessage(subscriptionId, event);
    if (rowId < 0) {
        emit serverError(serverId, m_database->lastError());
        return;
    }
    if (rowId == 0)
        return; // seen before

    NtfyStoredMessage message;
    message.id = rowId;
    message.subscriptionId = subscriptionId;
    message.event = event;
    emit messageReceived(m_database->subscription(subscriptionId), message);
}
