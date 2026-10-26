// SPDX-License-Identifier: AGPL-3.0-or-later
//
// harbour-ntfy-daemon: keeps the ntfy subscriptions connected, stores messages, posts
// notifications and serves the UI over D-Bus. Started by systemd (see harbour-ntfy-daemon.service)
// or by D-Bus activation.
//
// Takes no arguments, ever: Sailfish Secrets ties our credentials to our command line.

#include "ntfydatabase.h"
#include "ntfydbusservice.h"
#include "ntfynotifier.h"
#include "ntfysecretscredentialstore.h"
#include "ntfysubscriptionmanager.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QNetworkAccessManager>
#include <QNetworkConfigurationManager>
#include <QStandardPaths>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("harbour-ntfy-daemon"));
    QCoreApplication::setApplicationVersion(QStringLiteral(NTFY_VERSION));

    if (argc > 1) {
        qCritical("harbour-ntfy-daemon takes no arguments");
        return 2;
    }

    const QString databasePath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/harbour-ntfy/ntfy.db");
    NtfyDatabase database;
    if (!database.open(databasePath)) {
        qCritical("could not open database: %s", qPrintable(database.lastError()));
        return 1;
    }

    NtfySecretsCredentialStore credentials(QStringLiteral("harbourntfydaemon"));
    QNetworkAccessManager nam;
    NtfySubscriptionManager manager(&database, &credentials, &nam);
    NtfyDBusService service(&manager);

    NtfyNotifier notifier;
    notifier.setActivationCall(QString::fromLatin1(NtfyDBus::ServiceName), QString::fromLatin1(NtfyDBus::ObjectPath),
                               QString::fromLatin1(NtfyDBus::Interface), QStringLiteral("NotificationActivated"));
    QObject::connect(&manager, &NtfySubscriptionManager::messageReceived, &notifier,
                     [&notifier](const NtfySubscriptionRecord &subscription, const NtfyStoredMessage &message) {
        if (!subscription.muted)
            notifier.notify(message.event, subscription.id);
    });

    // Log server ids only: URLs and topic names are private.
    QObject::connect(&manager, &NtfySubscriptionManager::serverStateChanged, [](qint64 serverId, NtfyStream::State state) {
        qInfo("server %lld: %s", serverId, qPrintable(NtfyDBusService::stateName(state)));
    });
    QObject::connect(&manager, &NtfySubscriptionManager::serverError, [](qint64 serverId, const QString &error) {
        qWarning("server %lld: %s", serverId, qPrintable(error));
    });

    QString error;
    if (!service.registerOn(QDBusConnection::sessionBus(), &error)) {
        qCritical("%s", qPrintable(error));
        return 1;
    }

    // Don't sit out the reconnect backoff when the network comes back.
    QNetworkConfigurationManager network;
    QObject::connect(&network, &QNetworkConfigurationManager::onlineStateChanged, &manager, [&manager](bool online) {
        if (online)
            manager.reconnectAll();
    });

    manager.start();
    return app.exec();
}
