// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfyserver.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

QUrl NtfyServerConfig::subscribeUrl(const QStringList &topics, const QString &since, bool poll) const
{
    QUrl url = baseUrl;

    // Keep any sub-path the server lives under (e.g. behind a reverse proxy).
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    path += QLatin1Char('/') + topics.join(QLatin1Char(',')) + QStringLiteral("/json");
    url.setPath(path);

    QUrlQuery query;
    if (!since.isEmpty())
        query.addQueryItem(QStringLiteral("since"), since);
    if (poll)
        query.addQueryItem(QStringLiteral("poll"), QStringLiteral("1"));
    url.setQuery(query);

    return url;
}

QByteArray NtfyServerConfig::authorizationHeader() const
{
    if (!token.isEmpty())
        return "Bearer " + token.toUtf8();
    if (!username.isEmpty())
        return "Basic " + (username + QLatin1Char(':') + password).toUtf8().toBase64();
    return QByteArray();
}

QNetworkRequest NtfyServerConfig::request(const QUrl &url) const
{
    QNetworkRequest request(url);
    // Never follow redirects: Qt re-sends our Authorization header to wherever the server points,
    // including other hosts. ntfy servers don't redirect, so a redirect means a wrong server URL.
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, false);
    request.setRawHeader("User-Agent", "harbour-ntfy/" NTFY_VERSION " (Sailfish OS)");

    const QByteArray auth = authorizationHeader();
    if (!auth.isEmpty())
        request.setRawHeader("Authorization", auth);

    return request;
}

QString ntfyErrorString(QNetworkReply *reply, const QByteArray &body)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString serverError = QJsonDocument::fromJson(body).object()
            .value(QLatin1String("error")).toString();

    if (status >= 300 && status < 400) {
        const QUrl target = ntfyRedirectTarget(reply);
        return QStringLiteral("HTTP %1: server redirects to %2; redirects are not followed, "
                              "update the server URL instead")
                .arg(status)
                .arg(target.isEmpty() ? QStringLiteral("an unknown location")
                                      : target.toString(QUrl::RemoveUserInfo | QUrl::RemoveQuery));
    }
    if (status >= 300) {
        return serverError.isEmpty()
                ? QStringLiteral("HTTP %1").arg(status)
                : QStringLiteral("HTTP %1: %2").arg(status).arg(serverError);
    }
    return reply->errorString();
}

QUrl ntfyRedirectTarget(QNetworkReply *reply)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status < 300 || status >= 400)
        return QUrl();

    const QByteArray location = reply->rawHeader("Location");
    if (location.isEmpty())
        return QUrl();
    return reply->url().resolved(QUrl::fromEncoded(location));
}
