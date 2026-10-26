// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfycredentials.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>

bool NtfyMemoryCredentialStore::store(const QString &key, const QByteArray &secret, QString *)
{
    m_secrets.insert(key, secret);
    return true;
}

bool NtfyMemoryCredentialStore::load(const QString &key, QByteArray *secret, bool *found, QString *)
{
    *found = m_secrets.contains(key);
    *secret = m_secrets.value(key);
    return true;
}

bool NtfyMemoryCredentialStore::remove(const QString &key, QString *)
{
    m_secrets.remove(key);
    return true;
}

QString ntfyCredentialKey(const QUrl &serverUrl)
{
    // Normalize so that "https://NTFY.sh/" and "https://ntfy.sh" share credentials.
    QUrl url = serverUrl.adjusted(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment
                                  | QUrl::NormalizePathSegments);
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    url.setPath(path);
    url.setHost(url.host().toLower());

    const QByteArray hash = QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha256);
    return QStringLiteral("server") + QString::fromLatin1(hash.toHex());
}

QByteArray ntfyEncodeCredentials(const NtfyServerConfig &server)
{
    QJsonObject obj;
    if (!server.token.isEmpty()) {
        obj.insert(QStringLiteral("token"), server.token);
    } else if (!server.username.isEmpty()) {
        obj.insert(QStringLiteral("username"), server.username);
        obj.insert(QStringLiteral("password"), server.password);
    }
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

bool ntfyDecodeCredentials(const QByteArray &data, NtfyServerConfig *server)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonObject obj = doc.object();
    server->token = obj.value(QLatin1String("token")).toString();
    server->username = obj.value(QLatin1String("username")).toString();
    server->password = obj.value(QLatin1String("password")).toString();
    return true;
}
