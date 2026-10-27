// SPDX-License-Identifier: AGPL-3.0-or-later
//
// harbour-ntfy: the UI. Runs sandboxed and holds no state of its own: everything lives in
// harbour-ntfy-daemon, which it talks to over D-Bus.

#include "ntfydaemonclient.h"
#include "ntfymessagemodel.h"
#include "ntfysubscriptionmodel.h"

#include <sailfishapp.h>

#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QScopedPointer>

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    // Must match OrganizationName and ApplicationName in harbour-ntfy.desktop.
    app->setOrganizationName(QStringLiteral("nl.readefries"));
    app->setApplicationName(QStringLiteral("harbourntfy"));
    app->setApplicationVersion(QStringLiteral(NTFY_VERSION));

    const char *uri = "harbour.ntfy";
    qmlRegisterUncreatableType<NtfyDaemonClient>(uri, 1, 0, "DaemonClient",
                                                 QStringLiteral("use the global \"daemon\""));
    qmlRegisterType<NtfySubscriptionModel>(uri, 1, 0, "SubscriptionModel");
    qmlRegisterType<NtfyMessageModel>(uri, 1, 0, "MessageModel");

    NtfyDaemonClient daemon;

    QScopedPointer<QQuickView> view(SailfishApp::createView());
    view->rootContext()->setContextProperty(QStringLiteral("daemon"), &daemon);
    view->setSource(SailfishApp::pathToMainQml());
    view->show();
    return app->exec();
}
