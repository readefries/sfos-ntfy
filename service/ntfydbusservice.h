// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef NTFYDBUSSERVICE_H
#define NTFYDBUSSERVICE_H

#include "ntfystream.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QObject>

class NtfySubscriptionManager;
struct NtfySubscriptionRecord;
struct NtfyStoredMessage;

namespace NtfyDBus {
extern const char *const ServiceName;
extern const char *const ObjectPath;
extern const char *const Interface;
}

// The daemon's session bus API, used by the UI. Lists and messages are passed as JSON strings.
//
// Credentials can be set through AddServer() but are never returned. Note that any unsandboxed
// process of the same user can talk to (and listen on) the session bus, just like it can read
// the database file; Sailjail keeps sandboxed apps off it.
class NtfyDBusService : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "nl.readefries.harbourntfy.Daemon")
public:
    explicit NtfyDBusService(NtfySubscriptionManager *manager, QObject *parent = nullptr);

    // Exports this object and claims the service name. Fails if another daemon already runs.
    bool registerOn(QDBusConnection connection, QString *error = nullptr);

    static QString stateName(NtfyStream::State state);

public slots:
    Q_SCRIPTABLE qlonglong AddServer(const QString &url, const QString &token, const QString &username,
                                     const QString &password);
    Q_SCRIPTABLE void RemoveServer(qlonglong serverId);
    // [{"id", "url", "state"}]
    Q_SCRIPTABLE QString Servers();

    Q_SCRIPTABLE qlonglong AddSubscription(qlonglong serverId, const QString &topic, const QString &displayName);
    Q_SCRIPTABLE void RemoveSubscription(qlonglong subscriptionId);
    Q_SCRIPTABLE void SetMuted(qlonglong subscriptionId, bool muted);
    Q_SCRIPTABLE void MarkRead(qlonglong subscriptionId);
    // [{"id", "serverId", "serverUrl", "topic", "displayName", "muted", "unread", "lastMessage"}]
    Q_SCRIPTABLE QString Subscriptions();
    // Newest first: [{"id", "read", "event"}], "event" in the ntfy JSON format.
    Q_SCRIPTABLE QString Messages(qlonglong subscriptionId, int limit, qlonglong beforeId);

    // Called by lipstick when the user taps one of our notifications.
    Q_SCRIPTABLE void NotificationActivated(qlonglong subscriptionId);
    Q_SCRIPTABLE void ReconnectAll();

signals:
    Q_SCRIPTABLE void MessageReceived(qlonglong subscriptionId, const QString &message);
    Q_SCRIPTABLE void SubscriptionsChanged();
    Q_SCRIPTABLE void ServerStateChanged(qlonglong serverId, const QString &state);
    Q_SCRIPTABLE void ServerError(qlonglong serverId, const QString &error);

    // Local only, not exported.
    void notificationActivated(qlonglong subscriptionId);

private:
    void replyError(const QString &error);

    NtfySubscriptionManager *m_manager;
};

#endif // NTFYDBUSSERVICE_H
