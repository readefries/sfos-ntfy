// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef NTFYUISERVICE_H
#define NTFYUISERVICE_H

#include <QObject>

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

    bool registerOnSessionBus();

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

#endif // NTFYUISERVICE_H
