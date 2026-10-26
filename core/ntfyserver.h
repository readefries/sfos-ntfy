// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef NTFYSERVER_H
#define NTFYSERVER_H

#include <QString>
#include <QStringList>
#include <QUrl>

class QNetworkReply;
class QNetworkRequest;

// Connection settings for one ntfy server, shared by the stream and the poller.
struct NtfyServerConfig
{
    QUrl baseUrl = QUrl(QStringLiteral("https://ntfy.sh"));

    // Either an access token (tk_...) or username/password. The token wins if both are set.
    QString token;
    QString username;
    QString password;

    // GET <base>/<topic1,topic2>/json[?since=..][&poll=1]
    QUrl subscribeUrl(const QStringList &topics, const QString &since = QString(),
                      bool poll = false) const;

    QNetworkRequest request(const QUrl &url) const;
    QByteArray authorizationHeader() const;
};

// Canonical form of a server base URL: no user info, query, fragment or trailing slash, lower
// case host. "https://NTFY.sh/" and "https://ntfy.sh" are the same server.
QUrl ntfyNormalizeServerUrl(const QUrl &url);

// Human readable error for a failed request, using ntfy's JSON error body when present.
QString ntfyErrorString(QNetworkReply *reply, const QByteArray &body);

// Where a 3xx reply points to (resolved against the request URL), or an empty URL.
QUrl ntfyRedirectTarget(QNetworkReply *reply);

#endif // NTFYSERVER_H
