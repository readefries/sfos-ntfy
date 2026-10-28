// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef NTFYUISERVICE_H
#define NTFYUISERVICE_H

#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QStringList>

class QQuickView;

// nl.readefries.harbourntfy.ui on the session bus: what notification taps call (see the daemon's
// NtfyNotifier setup). When D-Bus activation starts us for that, we run with -prestart and only
// show the window once asked to.
class NtfyUiService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "nl.readefries.harbourntfy.ui")
public:
    explicit NtfyUiService(QQuickView *view, QObject *parent = nullptr);

    // Fails if another copy of the app already owns the name (or the sandbox doesn't allow it).
    bool registerOnSessionBus();
    // Whether another copy of the app is running and serving the name.
    static bool otherInstanceRunning();
    // Asks that copy to show its window.
    static void activateOtherInstance();

public slots:
    Q_SCRIPTABLE void openSubscription(qlonglong subscriptionId);
    Q_SCRIPTABLE void activate();

signals:
    void subscriptionRequested(qlonglong subscriptionId);
    void activationRequested();

private:
    void showWindow();

    QQuickView *m_view;
};

// MeeGo's launch interface: the launcher starts X-Maemo-Service apps through D-Bus activation and
// then calls launch() on /org/maemo/m.
class NtfyMeegoLaunchAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.nokia.MApplicationIf")
public:
    explicit NtfyMeegoLaunchAdaptor(NtfyUiService *service);

public slots:
    void launch();
    void launch(const QStringList &parameters);

private:
    NtfyUiService *m_service;
};

#endif // NTFYUISERVICE_H
