// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef NTFYSUBSCRIPTIONMANAGER_H
#define NTFYSUBSCRIPTIONMANAGER_H

#include "ntfydatabase.h"
#include "ntfystream.h"

#include <QHash>
#include <QObject>

class NtfyCredentialStore;
class QNetworkAccessManager;

// Owns everything behind the UI: servers, subscriptions, message history, credentials and one
// NtfyStream per server carrying all of that server's topics.
//
// Credentials go in via addServer() and are only ever read back to authenticate requests.
class NtfySubscriptionManager : public QObject
{
    Q_OBJECT
public:
    NtfySubscriptionManager(NtfyDatabase *database, NtfyCredentialStore *credentials,
                            QNetworkAccessManager *nam, QObject *parent = nullptr);
    ~NtfySubscriptionManager() override;

    NtfyDatabase *database() const { return m_database; }

    // Connects every server that has subscriptions, resuming after the newest stored message.
    void start();
    void stop();
    bool isStarted() const { return m_started; }

    // Off means no permanent connections (battery saver): use pollAll() to fetch messages.
    void setStreaming(bool streaming);
    bool isStreaming() const { return m_streaming; }
    // Fetches what was missed from every server once, then emits pollFinished().
    // Returns false (and does nothing) while a previous poll is still running.
    bool pollAll();
    bool isPolling() const { return m_pendingPolls > 0; }

    // Adds a server or, if it already exists, replaces its credentials. Pass an empty token and
    // username for anonymous access. Returns the server id, or 0 with *error set.
    qint64 addServer(const QUrl &url, const QString &token, const QString &username,
                     const QString &password, QString *error = nullptr);
    bool removeServer(qint64 serverId, QString *error = nullptr);

    qint64 addSubscription(qint64 serverId, const QString &topic, const QString &displayName,
                           QString *error = nullptr);
    bool removeSubscription(qint64 subscriptionId, QString *error = nullptr);
    bool setMuted(qint64 subscriptionId, bool muted);
    bool markRead(qint64 subscriptionId);

    // Disconnected for servers without subscriptions.
    NtfyStream::State serverState(qint64 serverId) const;
    // The most recent error for a server, until it connects (or polls) successfully again.
    QString lastError(qint64 serverId) const { return m_lastErrors.value(serverId); }
    // Every stream is connected (trivially true without streams).
    bool allConnected() const;
    // Reconnects streams that aren't connected, or have been silent for longer than
    // maxSilenceMsecs. Returns how many it reconnected.
    int reconnectStale(qint64 maxSilenceMsecs);

    static bool isValidTopic(const QString &topic);

public slots:
    // Reconnect everything right away, e.g. when the network came back.
    void reconnectAll();

signals:
    // A message that wasn't stored before. Whether to notify is up to the receiver (see muted).
    void messageReceived(const NtfySubscriptionRecord &subscription, const NtfyStoredMessage &message);
    void serverStateChanged(qint64 serverId, NtfyStream::State state);
    void serverError(qint64 serverId, const QString &error);
    void subscriptionsChanged();
    // markRead(), so its notifications can go.
    void subscriptionRead(qint64 subscriptionId);
    // Likewise, also for each subscription of a removed server.
    void subscriptionRemoved(qint64 subscriptionId);
    void pollFinished();

private:
    void syncAll();
    void syncServer(qint64 serverId);
    bool serverConfig(const NtfyServerRecord &server, NtfyServerConfig *config);
    void dropConnection(qint64 serverId);
    void handleMessage(qint64 serverId, const NtfyEvent &event);

    NtfyDatabase *m_database;
    NtfyCredentialStore *m_credentials;
    QNetworkAccessManager *m_nam;
    QHash<qint64, NtfyStream *> m_streams;
    QHash<qint64, QString> m_lastErrors;
    int m_pendingPolls = 0;
    bool m_started = false;
    bool m_streaming = true;
};

#endif // NTFYSUBSCRIPTIONMANAGER_H
