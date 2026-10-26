// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfynotificationcontent.h"

#include <QHash>
#include <QStringList>

namespace {

// The most common short codes from https://docs.ntfy.sh/emojis/. ntfy supports the full
// GitHub emoji list; unknown tags simply end up in the "Tags:" line.
const QHash<QString, QString> &emojiTable()
{
    static const QHash<QString, QString> table = [] {
        const char *const entries[][2] = {
            {"+1", "👍"}, {"thumbsup", "👍"}, {"-1", "👎"}, {"thumbsdown", "👎"},
            {"warning", "⚠️"}, {"rotating_light", "🚨"}, {"skull", "💀"}, {"fire", "🔥"},
            {"no_entry", "⛔"}, {"no_entry_sign", "🚫"}, {"x", "❌"}, {"heavy_check_mark", "✔️"},
            {"white_check_mark", "✅"}, {"tada", "🎉"}, {"partying_face", "🥳"},
            {"loudspeaker", "📢"}, {"bell", "🔔"}, {"triangular_flag_on_post", "🚩"},
            {"computer", "💻"}, {"desktop_computer", "🖥️"}, {"floppy_disk", "💾"}, {"cd", "💿"},
            {"rocket", "🚀"}, {"zap", "⚡"}, {"bug", "🐛"}, {"lock", "🔒"}, {"key", "🔑"},
            {"package", "📦"}, {"email", "📧"}, {"calendar", "📆"}, {"hourglass", "⌛"},
            {"stopwatch", "⏱️"}, {"information_source", "ℹ️"}, {"question", "❓"},
            {"exclamation", "❗"}, {"heart", "❤️"}, {"house", "🏠"}, {"sunny", "☀️"},
            {"cloud", "☁️"}, {"umbrella", "☔"}, {"snowflake", "❄️"}, {"battery", "🔋"},
            {"electric_plug", "🔌"}, {"chart_with_upwards_trend", "📈"},
            {"chart_with_downwards_trend", "📉"}, {"money_with_wings", "💸"}, {"robot", "🤖"},
        };
        QHash<QString, QString> result;
        for (const auto &entry : entries)
            result.insert(QString::fromUtf8(entry[0]), QString::fromUtf8(entry[1]));
        return result;
    }();
    return table;
}

} // namespace

QString ntfyEmojiForTag(const QString &tag)
{
    return emojiTable().value(tag);
}

NtfyNotificationContent NtfyNotificationContent::fromMessage(const NtfyEvent &message)
{
    NtfyNotificationContent content;

    QStringList emojis;
    QStringList otherTags;
    for (const QString &tag : message.tags) {
        const QString emoji = ntfyEmojiForTag(tag);
        if (emoji.isEmpty())
            otherTags.append(tag);
        else
            emojis.append(emoji);
    }

    content.summary = message.title.isEmpty() ? message.topic : message.title;
    if (!emojis.isEmpty())
        content.summary = emojis.join(QLatin1Char(' ')) + QLatin1Char(' ') + content.summary;

    content.body = message.message;
    if (content.body.isEmpty() && message.attachment.isValid())
        content.body = message.attachment.name;
    if (!otherTags.isEmpty()) {
        if (!content.body.isEmpty())
            content.body += QLatin1Char('\n');
        content.body += QStringLiteral("Tags: ") + otherTags.join(QStringLiteral(", "));
    }

    // ntfy: 1 = min, 2 = low, 3 = default, 4 = high, 5 = max/urgent.
    // Min and low land quietly in the Events view, like the silent channels on Android.
    if (message.priority <= 2)
        content.urgency = Low;
    else if (message.priority == 5)
        content.urgency = Critical;
    else
        content.urgency = Normal;
    content.popup = message.priority >= 3;

    if (message.time > 0)
        content.timestamp = QDateTime::fromMSecsSinceEpoch(message.time * 1000);

    return content;
}
