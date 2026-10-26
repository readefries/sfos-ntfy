// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef NTFYMESSAGE_H
#define NTFYMESSAGE_H

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVariantMap>

struct NtfyAttachment
{
    QString name;
    QString type;
    QString url;
    qint64 size = 0;
    qint64 expires = 0;

    bool isValid() const { return !url.isEmpty(); }
};

// See https://docs.ntfy.sh/publish/#action-buttons
struct NtfyAction
{
    QString action; // "view", "http" or "broadcast"
    QString label;
    QString url;
    QString method;
    QString body;
    bool clear = false;
    QVariantMap headers;
    QVariantMap extras;
};

// One line of the ntfy JSON stream (https://docs.ntfy.sh/subscribe/api/#json-message-format).
struct NtfyEvent
{
    enum Type {
        Invalid,     // not parseable at all
        Open,
        Keepalive,
        Message,
        PollRequest,
        Unknown      // valid JSON with an event type we don't handle (yet)
    };

    Type type = Invalid;
    QString event; // raw event name as sent by the server
    QString id;
    qint64 time = 0;
    qint64 expires = 0;
    QString topic;

    // Only set for Type::Message
    QString title;
    QString message;
    int priority = 3; // 1 (min) .. 5 (max), 3 is default
    QStringList tags;
    QString click;
    QString icon;
    QString contentType;
    QList<NtfyAction> actions;
    NtfyAttachment attachment;

    bool isValid() const { return type != Invalid; }

    static NtfyEvent fromJson(const QByteArray &line, QString *error = nullptr);
};

Q_DECLARE_METATYPE(NtfyEvent)

#endif // NTFYMESSAGE_H
