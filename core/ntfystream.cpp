// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfystream.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace {
const int InitialReconnectDelay = 1000;
const int DefaultMaxReconnectDelay = 5 * 60 * 1000;
const int DefaultKeepaliveTimeout = 120 * 1000;
// A single ntfy message is limited to a few KB; anything this large without a newline is bogus.
const int MaxLineLength = 1024 * 1024;

bool isSuccess(int status)
{
    return status >= 200 && status < 300;
}
} // namespace

NtfyStream::NtfyStream(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_nam(nam)
    , m_reconnectDelay(InitialReconnectDelay)
    , m_maxReconnectDelay(DefaultMaxReconnectDelay)
{
    qRegisterMetaType<NtfyEvent>();

    m_keepaliveTimer.setSingleShot(true);
    m_keepaliveTimer.setInterval(DefaultKeepaliveTimeout);
    connect(&m_keepaliveTimer, &QTimer::timeout, this, &NtfyStream::onKeepaliveTimeout);

    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &NtfyStream::connectStream);
}

NtfyStream::~NtfyStream()
{
    teardownReply();
}

void NtfyStream::setServer(const NtfyServerConfig &server)
{
    m_server = server;
}

void NtfyStream::setTopics(const QStringList &topics)
{
    m_topics = topics;
}

void NtfyStream::setSince(const QString &since)
{
    m_initialSince = since;
}

void NtfyStream::setKeepaliveTimeout(int msecs)
{
    m_keepaliveTimer.setInterval(msecs);
}

void NtfyStream::setMaxReconnectDelay(int msecs)
{
    m_maxReconnectDelay = msecs;
}

void NtfyStream::start()
{
    if (m_state == Connecting || m_state == Connected)
        return;

    m_reconnectDelay = InitialReconnectDelay;
    connectStream();
}

void NtfyStream::stop()
{
    m_reconnectTimer.stop();
    m_keepaliveTimer.stop();
    teardownReply();
    setState(Disconnected);
}

void NtfyStream::reconnectNow()
{
    m_reconnectTimer.stop();
    m_keepaliveTimer.stop();
    teardownReply();
    m_reconnectDelay = InitialReconnectDelay;
    connectStream();
}

void NtfyStream::connectStream()
{
    if (m_topics.isEmpty()) {
        emit errorOccurred(QStringLiteral("no topics to subscribe to"));
        setState(Disconnected);
        return;
    }

    teardownReply();
    m_buffer.clear();

    const QString since = m_lastId.isEmpty() ? m_initialSince : m_lastId;
    m_reply = m_nam->get(m_server.request(m_server.subscribeUrl(m_topics, since)));
    connect(m_reply.data(), &QNetworkReply::readyRead, this, &NtfyStream::onReadyRead);
    connect(m_reply.data(), &QNetworkReply::finished, this, &NtfyStream::onFinished);

    m_keepaliveTimer.start();
    setState(Connecting);
}

void NtfyStream::onReadyRead()
{
    // Leave error bodies in the reply; onFinished() turns them into an error message.
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!isSuccess(status))
        return;

    m_keepaliveTimer.start();
    m_buffer += m_reply->readAll();

    int newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(newline).trimmed();
        m_buffer.remove(0, newline + 1);
        if (!line.isEmpty())
            handleLine(line);
        // handleLine() emits signals; a receiver may have stopped or restarted us.
        if (!m_reply)
            return;
    }

    if (m_buffer.size() > MaxLineLength) {
        emit errorOccurred(QStringLiteral("stream line too long, reconnecting"));
        teardownReply();
        scheduleReconnect();
    }
}

void NtfyStream::onFinished()
{
    QNetworkReply *reply = m_reply.data();
    if (!reply)
        return;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = (isSuccess(status) || !reply->isOpen()) ? QByteArray() : reply->readAll();
    const bool failed = reply->error() != QNetworkReply::NoError || !isSuccess(status);
    const QString error = failed ? ntfyErrorString(reply, body) : QString();
    teardownReply();
    m_keepaliveTimer.stop();

    if (status == 401 || status == 403) {
        emit errorOccurred(error);
        emit accessDenied(status);
        setState(Disconnected);
        return;
    }

    emit errorOccurred(failed ? error : QStringLiteral("server closed the stream"));
    scheduleReconnect();
}

void NtfyStream::onKeepaliveTimeout()
{
    emit errorOccurred(QStringLiteral("no keepalive from server, reconnecting"));
    teardownReply();
    scheduleReconnect();
}

void NtfyStream::handleLine(const QByteArray &line)
{
    QString error;
    const NtfyEvent event = NtfyEvent::fromJson(line, &error);
    if (!event.isValid()) {
        emit errorOccurred(QStringLiteral("ignoring malformed line: %1").arg(error));
        return;
    }

    switch (event.type) {
    case NtfyEvent::Open:
        m_reconnectDelay = InitialReconnectDelay;
        setState(Connected);
        break;
    case NtfyEvent::Message:
        m_lastId = event.id;
        break;
    default:
        break;
    }

    emit eventReceived(event);
    if (event.type == NtfyEvent::Message)
        emit messageReceived(event);
}

void NtfyStream::scheduleReconnect()
{
    setState(WaitingToReconnect);
    m_reconnectTimer.start(m_reconnectDelay);
    m_reconnectDelay = qMin(m_reconnectDelay * 2, m_maxReconnectDelay);
}

void NtfyStream::teardownReply()
{
    if (!m_reply)
        return;

    QNetworkReply *reply = m_reply.data();
    m_reply.clear();
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void NtfyStream::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}
