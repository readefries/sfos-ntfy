// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfysubscriptionmanager.h"

#include "ntfycredentials.h"
#include "ntfypoller.h"

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

    connect(this, &NtfySubscriptionManager::serverError, this, [this](qint64 serverId, const QString &error) {
        m_lastErrors.insert(serverId, error);
    });
    connect(this, &NtfySubscriptionManager::serverStateChanged, this, [this](qint64 serverId, NtfyStream::State state) {
        if (state == NtfyStream::Connected)
            m_lastErrors.remove(serverId);
    });
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
    syncAll();
}

void NtfySubscriptionManager::stop()
{
    const QList<qint64> serverIds = m_streams.keys();
    for (qint64 serverId : serverIds)
        dropConnection(serverId);
    m_started = false;
}

void NtfySubscriptionManager::setStreaming(bool streaming)
{
    if (m_streaming == streaming)
        return;
    m_streaming = streaming;
    if (m_started)
        syncAll();
}

void NtfySubscriptionManager::syncAll()
{
    for (const NtfyServerRecord &server : m_database->servers())
        syncServer(server.id);
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

    m_lastErrors.remove(serverId);
    // New credentials take effect right away, also for a stream that gave up after a 401.
    if (m_started) {
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

    m_lastErrors.remove(serverId);
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
    if (ok) {
        emit subscriptionsChanged();
        emit subscriptionRead(subscriptionId);
    }
    return ok;
}

NtfyStream::State NtfySubscriptionManager::serverState(qint64 serverId) const
{
    NtfyStream *stream = m_streams.value(serverId);
    return stream ? stream->state() : NtfyStream::Disconnected;
}

bool NtfySubscriptionManager::allConnected() const
{
    for (NtfyStream *stream : m_streams) {
        if (stream->state() != NtfyStream::Connected)
            return false;
    }
    return true;
}

void NtfySubscriptionManager::reconnectAll()
{
    for (NtfyStream *stream : m_streams)
        stream->reconnectNow();
}

int NtfySubscriptionManager::reconnectStale(qint64 maxSilenceMsecs)
{
    int reconnected = 0;
    for (NtfyStream *stream : m_streams) {
        const qint64 silence = stream->msecsSinceLastData();
        if (stream->state() != NtfyStream::Connected || silence < 0 || silence > maxSilenceMsecs) {
            stream->reconnectNow();
            ++reconnected;
        }
    }
    return reconnected;
}

bool NtfySubscriptionManager::pollAll()
{
    if (m_pendingPolls > 0)
        return false;

    for (const NtfyServerRecord &server : m_database->servers()) {
        QStringList topics;
        for (const NtfySubscriptionRecord &subscription : m_database->subscriptions(server.id))
            topics.append(subscription.topic);
        NtfyServerConfig config;
        if (topics.isEmpty() || !serverConfig(server, &config))
            continue;
        topics.sort();

        const qint64 serverId = server.id;
        NtfyPoller *poller = new NtfyPoller(m_nam, this);
        poller->setServer(config);
        connect(poller, &NtfyPoller::messageReceived, this, [this, serverId](const NtfyEvent &event) {
            handleMessage(serverId, event);
        });
        connect(poller, &NtfyPoller::finished, this,
                [this, serverId, poller](bool ok, const QString &, const QString &error) {
            if (ok)
                m_lastErrors.remove(serverId);
            else
                emit serverError(serverId, error);
            poller->deleteLater();
            if (--m_pendingPolls == 0)
                emit pollFinished();
        });

        ++m_pendingPolls;
        const qint64 since = m_database->resumeTime(serverId);
        poller->poll(topics, since > 0 ? QString::number(since) : QString());
    }

    if (m_pendingPolls == 0)
        emit pollFinished();
    return true;
}

bool NtfySubscriptionManager::serverConfig(const NtfyServerRecord &server, NtfyServerConfig *config)
{
    QByteArray stored;
    bool found = false;
    QString error;
    if (!m_credentials->load(ntfyCredentialKey(server.url), &stored, &found, &error)) {
        // Don't connect without the credentials the user configured.
        emit serverError(server.id, QStringLiteral("could not read credentials: ") + error);
        return false;
    }
    if (found)
        ntfyDecodeCredentials(stored, config);
    config->baseUrl = server.url;
    return true;
}

void NtfySubscriptionManager::syncServer(qint64 serverId)
{
    const NtfyServerRecord server = m_database->server(serverId);
    QStringList topics;
    for (const NtfySubscriptionRecord &subscription : m_database->subscriptions(serverId))
        topics.append(subscription.topic);
    topics.sort();

    if (!m_streaming || !server.isValid() || topics.isEmpty()) {
        dropConnection(serverId);
        return;
    }

    if (NtfyStream *stream = m_streams.value(serverId)) {
        if (stream->topics() != topics) {
            // Keeps resuming from the stream's last message id.
            stream->setTopics(topics);
            stream->reconnectNow();
        }
        return;
    }

    NtfyServerConfig config;
    if (!serverConfig(server, &config))
        return;

    NtfyStream *stream = new NtfyStream(m_nam, this);
    stream->setServer(config);
    stream->setTopics(topics);
    // Resume by time rather than message id: ids from expired messages are unknown to the server.
    // Messages from that same second come back again, but are already stored and get skipped.
    const qint64 since = m_database->resumeTime(serverId);
    if (since > 0)
        stream->setSince(QString::number(since));

    connect(stream, &NtfyStream::messageReceived, this, [this, serverId](const NtfyEvent &event) {
        handleMessage(serverId, event);
    });
    connect(stream, &NtfyStream::stateChanged, this, [this, serverId](NtfyStream::State state) {
        emit serverStateChanged(serverId, state);
    });
    connect(stream, &NtfyStream::errorOccurred, this, [this, serverId](const QString &error) {
        emit serverError(serverId, error);
    });

    m_streams.insert(serverId, stream);
    stream->start();
}

void NtfySubscriptionManager::dropConnection(qint64 serverId)
{
    NtfyStream *stream = m_streams.take(serverId);
    if (!stream)
        return;

    stream->disconnect(this);
    stream->stop();
    stream->deleteLater();
    emit serverStateChanged(serverId, NtfyStream::Disconnected);
}

void NtfySubscriptionManager::handleMessage(qint64 serverId, const NtfyEvent &event)
{
    NtfySubscriptionRecord subscription;
    for (const NtfySubscriptionRecord &candidate : m_database->subscriptions(serverId)) {
        if (candidate.topic == event.topic) {
            subscription = candidate;
            break;
        }
    }
    if (!subscription.isValid())
        return;

    const qint64 rowId = m_database->addMessage(subscription.id, event);
    if (rowId < 0) {
        emit serverError(serverId, m_database->lastError());
        return;
    }
    if (rowId == 0)
        return; // seen before

    NtfyStoredMessage message;
    message.id = rowId;
    message.subscriptionId = subscription.id;
    message.event = event;
    emit messageReceived(m_database->subscription(subscription.id), message);
}
