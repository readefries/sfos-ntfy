// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef NTFYSTREAM_H
#define NTFYSTREAM_H

#include "ntfymessage.h"
#include "ntfyserver.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;

// Long-lived subscription on <server>/<topics>/json.
//
// Reconnects with exponential backoff and resumes from the last seen message id,
// so no messages are lost across network changes or suspend.
class NtfyStream : public QObject
{
    Q_OBJECT
public:
    enum State {
        Disconnected,
        Connecting,
        Connected,
        WaitingToReconnect
    };
    Q_ENUM(State)

    explicit NtfyStream(QNetworkAccessManager *nam, QObject *parent = nullptr);
    ~NtfyStream() override;

    void setServer(const NtfyServerConfig &server);
    NtfyServerConfig server() const { return m_server; }

    void setTopics(const QStringList &topics);
    QStringList topics() const { return m_topics; }

    // Where to start on the first connect: a message id, unix timestamp, duration ("10m")
    // or "all". Subsequent reconnects always resume from lastMessageId().
    void setSince(const QString &since);
    QString lastMessageId() const { return m_lastId; }

    // ntfy sends a keepalive every 45s by default; no data for this long means a dead connection.
    void setKeepaliveTimeout(int msecs);
    void setMaxReconnectDelay(int msecs);

    State state() const { return m_state; }
    // Milliseconds since anything (message or keepalive) arrived on the current connection,
    // or -1 if nothing did yet. A connected stream that's been silent for longer than the
    // server's keepalive interval is probably dead, e.g. after the phone slept.
    qint64 msecsSinceLastData() const;

public slots:
    void start();
    void stop();
    // Drop the current connection (if any) and connect again right away, e.g. on network change.
    void reconnectNow();

signals:
    void stateChanged(NtfyStream::State state);
    void eventReceived(const NtfyEvent &event);
    void messageReceived(const NtfyEvent &message);
    void errorOccurred(const QString &error);
    // The server rejected our credentials or topic access; we won't retry on our own.
    void accessDenied(int httpStatus);
    // The server answered with a redirect, which we never follow; we won't retry on our own.
    void redirectRefused(const QUrl &target);

private:
    void connectStream();
    void onReadyRead();
    void onFinished();
    void onKeepaliveTimeout();
    void handleLine(const QByteArray &line);
    void scheduleReconnect();
    void teardownReply();
    void setState(State state);

    QNetworkAccessManager *m_nam;
    QPointer<QNetworkReply> m_reply;
    NtfyServerConfig m_server;
    QStringList m_topics;
    QString m_initialSince;
    QString m_lastId;
    QByteArray m_buffer;

    // Wall clock on purpose: the monotonic clock (QElapsedTimer) stops while the phone is suspended.
    qint64 m_lastDataMsecs = 0;
    QTimer m_keepaliveTimer;
    QTimer m_reconnectTimer;
    int m_reconnectDelay;
    int m_maxReconnectDelay;
    State m_state = Disconnected;
};

#endif // NTFYSTREAM_H
