// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef NTFYNOTIFICATIONCONTENT_H
#define NTFYNOTIFICATIONCONTENT_H

#include "ntfymessage.h"

#include <QDateTime>
#include <QString>

// What a notification for an ntfy message should look like, independent of the platform API,
// following the ntfy Android app where it makes sense.
struct NtfyNotificationContent
{
    // Same values as Nemo's Notification::Urgency.
    enum Urgency {
        Low = 0,
        Normal = 1,
        Critical = 2
    };

    QString summary;
    QString body;
    Urgency urgency = Normal;
    // Show a banner popup, not just an entry in the Events view.
    bool popup = true;
    QDateTime timestamp;

    static NtfyNotificationContent fromMessage(const NtfyEvent &message);
};

// Emoji for an ntfy tag short code ("warning" -> ⚠️), or an empty string if the tag isn't one we know.
// ntfy prefixes the title with these; other tags are listed below the message.
QString ntfyEmojiForTag(const QString &tag);

#endif // NTFYNOTIFICATIONCONTENT_H
