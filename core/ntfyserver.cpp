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
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
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

    if (status >= 300) {
        return serverError.isEmpty()
                ? QStringLiteral("HTTP %1").arg(status)
                : QStringLiteral("HTTP %1: %2").arg(status).arg(serverError);
    }
    return reply->errorString();
}
