// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfypoller.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>

NtfyPoller::NtfyPoller(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_nam(nam)
{
    qRegisterMetaType<NtfyEvent>();
}

NtfyPoller::~NtfyPoller()
{
    abort();
}

void NtfyPoller::setServer(const NtfyServerConfig &server)
{
    m_server = server;
}

void NtfyPoller::poll(const QStringList &topics, const QString &since)
{
    abort();
    m_reply = m_nam->get(m_server.request(m_server.subscribeUrl(topics, since, true)));
    connect(m_reply.data(), &QNetworkReply::finished, this, &NtfyPoller::onFinished);
}

void NtfyPoller::abort()
{
    if (!m_reply)
        return;

    QNetworkReply *reply = m_reply.data();
    m_reply.clear();
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void NtfyPoller::onFinished()
{
    QNetworkReply *reply = m_reply.data();
    if (!reply)
        return;
    m_reply.clear();
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // QNAM hands out never-opened replies while the network is down; reading those only warns.
    const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();
    if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
        emit finished(false, QString(), ntfyErrorString(reply, body));
        return;
    }

    QString lastId;
    for (const QByteArray &rawLine : body.split('\n')) {
        const QByteArray line = rawLine.trimmed();
        if (line.isEmpty())
            continue;

        const NtfyEvent event = NtfyEvent::fromJson(line);
        if (event.type != NtfyEvent::Message)
            continue;

        lastId = event.id;
        emit messageReceived(event);
    }

    emit finished(true, lastId, QString());
}
