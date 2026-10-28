// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef NTFYDATABASE_H
#define NTFYDATABASE_H

#include "ntfymessage.h"

#include <QList>
#include <QString>
#include <QUrl>

struct NtfyServerRecord
{
    qint64 id = 0;
    QUrl url; // normalized, see ntfyNormalizeServerUrl()

    bool isValid() const { return id > 0; }
};

struct NtfySubscriptionRecord
{
    qint64 id = 0;
    qint64 serverId = 0;
    QString topic;
    QString displayName;
    bool muted = false;
    int unreadCount = 0;
    NtfyEvent lastMessage; // Invalid if there are no messages yet

    bool isValid() const { return id > 0; }
};

struct NtfyStoredMessage
{
    qint64 id = 0; // local row id, increases with arrival order
    qint64 subscriptionId = 0;
    bool read = false;
    NtfyEvent event;

    bool isValid() const { return id > 0; }
};

Q_DECLARE_METATYPE(NtfySubscriptionRecord)
Q_DECLARE_METATYPE(NtfyStoredMessage)

// SQLite storage for servers, subscriptions and message history. Never holds credentials;
// those live in an NtfyCredentialStore.
//
// Topic names are effectively passwords on public servers, so the database file is only
// readable by its owner.
class NtfyDatabase
{
public:
    NtfyDatabase();
    ~NtfyDatabase();

    // Opens (creating if needed) and migrates the database. ":memory:" gives a throwaway one.
    // With a key (32 random bytes) the whole file is encrypted with SQLCipher; opening it with
    // the wrong key fails.
    bool open(const QString &path, const QByteArray &key = QByteArray());
    void close();
    bool isOpen() const;
    QString lastError() const { return m_error; }

    // Returns the id of the (possibly already existing) server, or 0 on error.
    qint64 addServer(const QUrl &url);
    bool removeServer(qint64 serverId); // also removes its subscriptions and messages
    QList<NtfyServerRecord> servers() const;
    NtfyServerRecord server(qint64 serverId) const;

    // Returns the id of the new subscription, or 0 on error (including duplicates).
    qint64 addSubscription(qint64 serverId, const QString &topic, const QString &displayName);
    bool removeSubscription(qint64 subscriptionId); // also removes its messages
    bool setMuted(qint64 subscriptionId, bool muted);
    // All subscriptions, or only those of one server.
    QList<NtfySubscriptionRecord> subscriptions(qint64 serverId = 0) const;
    NtfySubscriptionRecord subscription(qint64 subscriptionId) const;

    // Returns the new row id, 0 if this message was already stored, or -1 on error.
    qint64 addMessage(qint64 subscriptionId, const NtfyEvent &message);
    // Newest first. Pass the id of the oldest message you have as beforeId to page further back.
    QList<NtfyStoredMessage> messages(qint64 subscriptionId, int limit, qint64 beforeId = 0) const;
    // One stored message by its row id; invalid if there's no such message.
    NtfyStoredMessage message(qint64 messageId) const;
    bool markRead(qint64 subscriptionId);
    // ntfy timestamp of the newest stored message of a server, or 0. Used to resume with since=.
    qint64 newestMessageTime(qint64 serverId) const;
    // Where to resume a server from with since=: its newest stored message, or when its first
    // subscription was made if there are no messages yet (so nothing older shows up).
    qint64 resumeTime(qint64 serverId) const;
    // Keeps the newest keepPerSubscription messages of every subscription. Returns how many went.
    int pruneMessages(int keepPerSubscription);

    // Whether the SQLCipher driver (qt5-plugin-sqldriver-sqlcipher) is installed.
    static bool isEncryptionAvailable();
    // Whether path is an unencrypted SQLite database (from before databases were encrypted).
    static bool isPlaintextFile(const QString &path);
    // Rewrites the unencrypted database at path as one encrypted with key, in place.
    static bool encryptFile(const QString &path, const QByteArray &key, QString *error = nullptr);

    QString setting(const QString &key, const QString &defaultValue = QString()) const;
    bool setSetting(const QString &key, const QString &value);

private:
    bool migrate();
    bool exec(const QString &sql);
    bool fail(const QString &context, const QString &error) const;

    QString m_connectionName;
    mutable QString m_error;
};

#endif // NTFYDATABASE_H
