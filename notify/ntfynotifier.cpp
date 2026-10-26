// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfynotifier.h"

#include "ntfynotificationcontent.h"

#include <notification.h>

namespace {
const int MaxRecentNotifications = 20;

static_assert(int(NtfyNotificationContent::Low) == int(Notification::Low)
              && int(NtfyNotificationContent::Normal) == int(Notification::Normal)
              && int(NtfyNotificationContent::Critical) == int(Notification::Critical),
              "urgency values must match Nemo's Notification::Urgency");
} // namespace

NtfyNotifier::NtfyNotifier(QObject *parent)
    : QObject(parent)
    , m_appName(QStringLiteral("ntfy"))
{
}

void NtfyNotifier::setAppName(const QString &appName)
{
    m_appName = appName;
}

void NtfyNotifier::setAppIcon(const QString &appIcon)
{
    m_appIcon = appIcon;
}

void NtfyNotifier::setActivationCall(const QString &service, const QString &path, const QString &interface,
                                     const QString &method)
{
    m_service = service;
    m_path = path;
    m_interface = interface;
    m_method = method;
}

void NtfyNotifier::notify(const NtfyEvent &message, qint64 tag)
{
    const NtfyNotificationContent content = NtfyNotificationContent::fromMessage(message);

    Notification *notification = new Notification(this);
    notification->setAppName(m_appName);
    if (!m_appIcon.isEmpty())
        notification->setAppIcon(m_appIcon);
    notification->setSummary(content.summary);
    notification->setBody(content.body);
    if (content.popup) {
        notification->setPreviewSummary(content.summary);
        notification->setPreviewBody(content.body);
    }
    notification->setUrgency(static_cast<Notification::Urgency>(content.urgency));
    if (content.timestamp.isValid())
        notification->setTimestamp(content.timestamp);

    // Lipstick only reports activation for notifications that have a "default" action. Without a
    // D-Bus callback it just signals us (if we're still running) and dismisses the notification.
    if (m_method.isEmpty()) {
        QVariantMap defaultAction;
        defaultAction.insert(QStringLiteral("name"), QStringLiteral("default"));
        notification->setRemoteAction(defaultAction);
    } else {
        notification->setRemoteAction(Notification::remoteAction(
                QStringLiteral("default"), QString(), m_service, m_path, m_interface, m_method,
                QVariantList() << QVariant::fromValue<qlonglong>(tag)));
    }

    connect(notification, &Notification::clicked, this, [this, message]() {
        emit clicked(message);
    });

    notification->publish();

    // Deleting the object doesn't remove the notification from the Events view.
    m_recent.append(notification);
    while (m_recent.size() > MaxRecentNotifications) {
        QPointer<Notification> oldest = m_recent.takeFirst();
        if (oldest)
            oldest->deleteLater();
    }
}
