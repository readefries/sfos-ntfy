// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef NTFYNOTIFIER_H
#define NTFYNOTIFIER_H

#include "ntfymessage.h"

#include <QList>
#include <QObject>
#include <QPointer>

class Notification;

// Posts ntfy messages as Sailfish OS notifications (via lipstick / nemo-qml-plugin-notifications).
class NtfyNotifier : public QObject
{
    Q_OBJECT
public:
    explicit NtfyNotifier(QObject *parent = nullptr);

    void setAppName(const QString &appName);
    void setAppIcon(const QString &appIcon);
    // When set, tapping a notification makes lipstick call this D-Bus method with the tag passed
    // to notify() as its only (int64) argument. That works even if this process has exited since,
    // given D-Bus activation. Otherwise only clicked() reports taps.
    void setActivationCall(const QString &service, const QString &path, const QString &interface,
                           const QString &method);

public slots:
    void notify(const NtfyEvent &message, qint64 tag = 0);

signals:
    // Only delivered while this process is alive; lipstick keeps the notification regardless.
    void clicked(const NtfyEvent &message);

private:
    QString m_appName;
    QString m_appIcon;
    QString m_service;
    QString m_path;
    QString m_interface;
    QString m_method;
    // Keep recent notification objects around so clicks on them still reach us.
    QList<QPointer<Notification>> m_recent;
};

#endif // NTFYNOTIFIER_H
