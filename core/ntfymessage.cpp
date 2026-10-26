// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfymessage.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

qint64 toInt64(const QJsonValue &value)
{
    return static_cast<qint64>(value.toDouble());
}

NtfyEvent::Type eventType(const QString &event)
{
    if (event == QLatin1String("message"))
        return NtfyEvent::Message;
    if (event == QLatin1String("keepalive"))
        return NtfyEvent::Keepalive;
    if (event == QLatin1String("open"))
        return NtfyEvent::Open;
    if (event == QLatin1String("poll_request"))
        return NtfyEvent::PollRequest;
    return NtfyEvent::Unknown;
}

NtfyAction parseAction(const QJsonObject &obj)
{
    NtfyAction action;
    action.action = obj.value(QLatin1String("action")).toString();
    action.label = obj.value(QLatin1String("label")).toString();
    action.url = obj.value(QLatin1String("url")).toString();
    action.method = obj.value(QLatin1String("method")).toString();
    action.body = obj.value(QLatin1String("body")).toString();
    action.clear = obj.value(QLatin1String("clear")).toBool();
    action.headers = obj.value(QLatin1String("headers")).toObject().toVariantMap();
    action.extras = obj.value(QLatin1String("extras")).toObject().toVariantMap();
    return action;
}

} // namespace

NtfyEvent NtfyEvent::fromJson(const QByteArray &line, QString *error)
{
    NtfyEvent result;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = parseError.error != QJsonParseError::NoError
                    ? parseError.errorString()
                    : QStringLiteral("not a JSON object");
        return result;
    }

    const QJsonObject obj = doc.object();
    result.event = obj.value(QLatin1String("event")).toString();
    if (result.event.isEmpty()) {
        if (error)
            *error = QStringLiteral("missing \"event\" field");
        return result;
    }

    result.type = eventType(result.event);
    result.id = obj.value(QLatin1String("id")).toString();
    result.time = toInt64(obj.value(QLatin1String("time")));
    result.expires = toInt64(obj.value(QLatin1String("expires")));
    result.topic = obj.value(QLatin1String("topic")).toString();

    if (result.type != Message)
        return result;

    result.title = obj.value(QLatin1String("title")).toString();
    result.message = obj.value(QLatin1String("message")).toString();
    result.click = obj.value(QLatin1String("click")).toString();
    result.icon = obj.value(QLatin1String("icon")).toString();
    result.contentType = obj.value(QLatin1String("content_type")).toString();

    const int priority = obj.value(QLatin1String("priority")).toInt(3);
    result.priority = (priority < 1 || priority > 5) ? 3 : priority;

    for (const QJsonValue &tag : obj.value(QLatin1String("tags")).toArray())
        result.tags.append(tag.toString());

    for (const QJsonValue &action : obj.value(QLatin1String("actions")).toArray())
        result.actions.append(parseAction(action.toObject()));

    const QJsonObject attachment = obj.value(QLatin1String("attachment")).toObject();
    if (!attachment.isEmpty()) {
        result.attachment.name = attachment.value(QLatin1String("name")).toString();
        result.attachment.type = attachment.value(QLatin1String("type")).toString();
        result.attachment.url = attachment.value(QLatin1String("url")).toString();
        result.attachment.size = toInt64(attachment.value(QLatin1String("size")));
        result.attachment.expires = toInt64(attachment.value(QLatin1String("expires")));
    }

    return result;
}
