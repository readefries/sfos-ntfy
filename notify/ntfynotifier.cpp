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

void NtfyNotifier::notify(const NtfyEvent &message)
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
    QVariantMap defaultAction;
    defaultAction.insert(QStringLiteral("name"), QStringLiteral("default"));
    notification->setRemoteAction(defaultAction);

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
