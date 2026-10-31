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
#include <QSocketNotifier>
#include <QDBusConnection>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkConfigurationManager>
#include <QScopedPointer>
#include <QStandardPaths>

#include <signal.h>
#include <unistd.h>

// Info level on by default: the system-wide logging rules may hide plain qInfo().
Q_LOGGING_CATEGORY(lcDaemon, "harbour-ntfy.daemon", QtInfoMsg)

namespace {

int signalPipe[2];

void onTerminationSignal(int)
{
    const char byte = 1;
    // Only async-signal-safe work here; the event loop picks it up.
    if (::write(signalPipe[1], &byte, 1) < 0) {
    }
}

// systemd stops us with SIGTERM: quit through the event loop, so the database gets closed (and its
// write-ahead log checkpointed) instead of the process just disappearing.
void quitOnTermination(QCoreApplication *app)
{
    if (::pipe(signalPipe) != 0)
        return;
    QSocketNotifier *notifier = new QSocketNotifier(signalPipe[0], QSocketNotifier::Read, app);
    QObject::connect(notifier, &QSocketNotifier::activated, app, []() {
        char byte;
        if (::read(signalPipe[0], &byte, 1) > 0)
            QCoreApplication::quit();
    });
    struct sigaction action = {};
    action.sa_handler = onTerminationSignal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);
}

// Everything that needs the (open) database.
struct Services
{
    Services(NtfyDatabase *database, NtfyCredentialStore *credentials, QNetworkAccessManager *nam)
        : manager(database, credentials, nam)
        , delivery(&manager, &scheduler)
        , actions(nam)
    {
    }

    NtfySubscriptionManager manager;
    NtfyBackgroundActivityScheduler scheduler;
    NtfyDeliveryController delivery;
    NtfyActionRunner actions;
    // Don't sit out the reconnect backoff when the network comes back.
    QNetworkConfigurationManager network;
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("harbour-ntfy-daemon"));
    QCoreApplication::setApplicationVersion(QStringLiteral(NTFY_VERSION));

    if (argc > 1) {
        qCCritical(lcDaemon, "harbour-ntfy-daemon takes no arguments");
        return 2;
    }
    quitOnTermination(&app);

    NtfySecretsCredentialStore credentials(QStringLiteral("harbourntfydaemon"));
    QNetworkAccessManager nam;
    NtfyDBusService service;

    NtfyNotifier notifier;
    notifier.setAppName(QStringLiteral("ntfy"));
    notifier.setAppIcon(QStringLiteral("/usr/share/icons/hicolor/86x86/apps/harbour-ntfy.png"));
    // Taps open the UI at the subscription (D-Bus activation starts it if needed, see
    // app/nl.readefries.harbourntfy.ui.service); the UI marks it read.
    notifier.setActivationCall(QStringLiteral("nl.readefries.harbourntfy.ui"), QStringLiteral("/"),
                               QStringLiteral("nl.readefries.harbourntfy.ui"), QStringLiteral("openSubscription"));

    const QString databasePath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/harbour-ntfy/ntfy.db");
    QScopedPointer<NtfyDatabase> database;
    QScopedPointer<Services> services;

    // Opens the database with key and gets everything going.
    auto start = [&](const QByteArray &key, bool newKey, QString *error) {
        if (newKey)
            qCInfo(lcDaemon, "created a new database key");
        QScopedPointer<NtfyDatabase> opened(new NtfyDatabase);
        QString notice;
        if (!ntfyOpenEncryptedDatabase(opened.data(), databasePath, key, &notice, error)) {
            *error = QStringLiteral("could not open database: ") + *error;
            return false;
        }
        if (!notice.isEmpty())
            qCWarning(lcDaemon, "%s", qPrintable(notice));
        database.swap(opened);
        services.reset(new Services(database.data(), &credentials, &nam));

        NtfySubscriptionManager *manager = &services->manager;
        NtfyDeliveryController *delivery = &services->delivery;
        QObject::connect(manager, &NtfySubscriptionManager::messageReceived, &notifier,
                         [&notifier](const NtfySubscriptionRecord &subscription, const NtfyStoredMessage &message) {
            if (!subscription.muted)
                notifier.notify(message.event, subscription.id);
        });
        QObject::connect(manager, &NtfySubscriptionManager::subscriptionRead, &notifier, &NtfyNotifier::closeTagged);

        // Log server ids only: URLs and topic names are private.
        QObject::connect(manager, &NtfySubscriptionManager::serverStateChanged, [](qint64 serverId, NtfyStream::State state) {
            qCInfo(lcDaemon, "server %lld: %s", serverId, qPrintable(NtfyDBusService::stateName(state)));
        });
        QObject::connect(manager, &NtfySubscriptionManager::serverError, [](qint64 serverId, const QString &error) {
            qCWarning(lcDaemon, "server %lld: %s", serverId, qPrintable(error));
        });
        QObject::connect(&services->scheduler, &NtfyWakeupScheduler::wokeUp, [delivery]() {
            qCInfo(lcDaemon, "wake-up (%s, every %d s)", qPrintable(NtfyDeliveryController::modeName(delivery->mode())),
                   delivery->interval());
        });
        QObject::connect(delivery, &NtfyDeliveryController::deliveryChanged, [delivery]() {
            qCInfo(lcDaemon, "delivery: %s, every %d s", qPrintable(NtfyDeliveryController::modeName(delivery->mode())),
                   delivery->interval());
        });
        QObject::connect(&services->network, &QNetworkConfigurationManager::onlineStateChanged, manager,
                         [manager](bool online) {
            if (online)
                manager->reconnectAll();
        });

        service.setDeliveryController(delivery);
        service.setActionRunner(&services->actions);
        service.setManager(manager);
        delivery->start();
        qCInfo(lcDaemon, "started (%s, every %d s)", qPrintable(NtfyDeliveryController::modeName(delivery->mode())),
               delivery->interval());
        return true;
    };

    // Everything in the database (topics, messages) is encrypted with a key kept in Secrets, in a
    // collection that is locked after a reboot. Only the system's device lock prompt unlocks it,
    // and a background process mustn't pop that up by itself: stay locked until the UI asks
    // (Unlock()). Any other reason not to get the key ends up there too, so the user sees it.
    QByteArray key;
    bool newKey = false;
    QString error;
    if (ntfyDatabaseKey(&credentials, &key, &newKey, &error)) {
        if (!start(key, newKey, &error)) {
            qCCritical(lcDaemon, "%s", qPrintable(error));
            return 1;
        }
    } else {
        qCWarning(lcDaemon, "locked until the app unlocks it: %s", qPrintable(error));
    }

    QObject::connect(&service, &NtfyDBusService::unlockRequested, [&]() {
        // Shows the prompt, and blocks until it's answered.
        QByteArray key;
        bool newKey = false;
        QString error;
        credentials.setInteractive(true);
        const bool unlocked = ntfyDatabaseKey(&credentials, &key, &newKey, &error);
        credentials.setInteractive(false);
        if (!unlocked || !start(key, newKey, &error)) {
            qCWarning(lcDaemon, "could not unlock: %s", qPrintable(error));
            service.unlockFailed(error);
        }
    });

    if (!service.registerOn(QDBusConnection::sessionBus(), &error)) {
        qCCritical(lcDaemon, "%s", qPrintable(error));
        return 1;
    }

    const int result = app.exec();
    qCInfo(lcDaemon, "stopping");
    return result;
}
