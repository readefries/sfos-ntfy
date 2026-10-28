// SPDX-License-Identifier: AGPL-3.0-or-later
//
// harbour-ntfy-daemon: keeps the ntfy subscriptions connected, stores messages, posts
// notifications and serves the UI over D-Bus. Started by systemd (see harbour-ntfy-daemon.service)
// or by D-Bus activation.
//
// Takes no arguments, ever: Sailfish Secrets ties our credentials to our command line.

#include "ntfyactionrunner.h"
#include "ntfybackgroundactivityscheduler.h"
#include "ntfydatabase.h"
#include "ntfydatabasekey.h"
#include "ntfydbusservice.h"
#include "ntfydeliverycontroller.h"
#include "ntfynotifier.h"
#include "ntfysecretscredentialstore.h"
#include "ntfysubscriptionmanager.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkConfigurationManager>
#include <QStandardPaths>

// Info level on by default: the system-wide logging rules may hide plain qInfo().
Q_LOGGING_CATEGORY(lcDaemon, "harbour-ntfy.daemon", QtInfoMsg)

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("harbour-ntfy-daemon"));
    QCoreApplication::setApplicationVersion(QStringLiteral(NTFY_VERSION));

    if (argc > 1) {
        qCCritical(lcDaemon, "harbour-ntfy-daemon takes no arguments");
        return 2;
    }

    NtfySecretsCredentialStore credentials(QStringLiteral("harbourntfydaemon"));

    // Everything in the database (topics, messages) is encrypted with a key kept in Secrets.
    // Until the first unlock after boot Secrets can't hand it out: exit, and systemd retries.
    QByteArray key;
    bool newKey = false;
    QString error;
    if (!ntfyDatabaseKey(&credentials, &key, &newKey, &error)) {
        qCCritical(lcDaemon, "%s (is the device still locked?)", qPrintable(error));
        return 1;
    }
    if (newKey)
        qCInfo(lcDaemon, "created a new database key");

    const QString databasePath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/harbour-ntfy/ntfy.db");
    NtfyDatabase database;
    QString notice;
    if (!ntfyOpenEncryptedDatabase(&database, databasePath, key, &notice, &error)) {
        qCCritical(lcDaemon, "could not open database: %s", qPrintable(error));
        return 1;
    }
    if (!notice.isEmpty())
        qCWarning(lcDaemon, "%s", qPrintable(notice));
    QNetworkAccessManager nam;
    NtfySubscriptionManager manager(&database, &credentials, &nam);
    NtfyBackgroundActivityScheduler scheduler;
    NtfyDeliveryController delivery(&manager, &scheduler);
    NtfyDBusService service(&manager);
    service.setDeliveryController(&delivery);
    NtfyActionRunner actions(&nam);
    service.setActionRunner(&actions);

    NtfyNotifier notifier;
    notifier.setAppName(QStringLiteral("ntfy"));
    notifier.setAppIcon(QStringLiteral("/usr/share/icons/hicolor/86x86/apps/harbour-ntfy.png"));
    // Taps open the UI at the subscription (D-Bus activation starts it if needed, see
    // app/nl.readefries.harbourntfy.ui.service); the UI marks it read.
    notifier.setActivationCall(QStringLiteral("nl.readefries.harbourntfy.ui"), QStringLiteral("/"),
                               QStringLiteral("nl.readefries.harbourntfy.ui"), QStringLiteral("openSubscription"));
    QObject::connect(&manager, &NtfySubscriptionManager::messageReceived, &notifier,
                     [&notifier](const NtfySubscriptionRecord &subscription, const NtfyStoredMessage &message) {
        if (!subscription.muted)
            notifier.notify(message.event, subscription.id);
    });

    // Log server ids only: URLs and topic names are private.
    QObject::connect(&manager, &NtfySubscriptionManager::serverStateChanged, [](qint64 serverId, NtfyStream::State state) {
        qCInfo(lcDaemon, "server %lld: %s", serverId, qPrintable(NtfyDBusService::stateName(state)));
    });
    QObject::connect(&manager, &NtfySubscriptionManager::serverError, [](qint64 serverId, const QString &error) {
        qCWarning(lcDaemon, "server %lld: %s", serverId, qPrintable(error));
    });
    QObject::connect(&scheduler, &NtfyWakeupScheduler::wokeUp, [&delivery]() {
        qCInfo(lcDaemon, "wake-up (%s, every %d s)", qPrintable(NtfyDeliveryController::modeName(delivery.mode())),
               delivery.interval());
    });
    QObject::connect(&delivery, &NtfyDeliveryController::deliveryChanged, [&delivery]() {
        qCInfo(lcDaemon, "delivery: %s, every %d s", qPrintable(NtfyDeliveryController::modeName(delivery.mode())),
               delivery.interval());
    });

    if (!service.registerOn(QDBusConnection::sessionBus(), &error)) {
        qCCritical(lcDaemon, "%s", qPrintable(error));
        return 1;
    }

    // Don't sit out the reconnect backoff when the network comes back.
    QNetworkConfigurationManager network;
    QObject::connect(&network, &QNetworkConfigurationManager::onlineStateChanged, &manager, [&manager](bool online) {
        if (online)
            manager.reconnectAll();
    });

    delivery.start();
    qCInfo(lcDaemon, "started (%s, every %d s)", qPrintable(NtfyDeliveryController::modeName(delivery.mode())),
           delivery.interval());
    return app.exec();
}
