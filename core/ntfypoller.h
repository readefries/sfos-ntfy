// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef NTFYPOLLER_H
#define NTFYPOLLER_H

#include "ntfymessage.h"
#include "ntfyserver.h"

#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

// One-shot fetch of cached messages via <server>/<topics>/json?poll=1&since=...
// Used to catch up after a wake-up when no stream was running.
class NtfyPoller : public QObject
{
    Q_OBJECT
public:
    explicit NtfyPoller(QNetworkAccessManager *nam, QObject *parent = nullptr);
    ~NtfyPoller() override;

    void setServer(const NtfyServerConfig &server);
    NtfyServerConfig server() const { return m_server; }

    bool isRunning() const { return !m_reply.isNull(); }

public slots:
    // Emits messageReceived() for every cached message, then finished().
    void poll(const QStringList &topics, const QString &since);
    void abort();

signals:
    void messageReceived(const NtfyEvent &message);
    // lastId is the id of the newest message seen, or empty if there were none.
    void finished(bool ok, const QString &lastId, const QString &error);

private:
    void onFinished();

    QNetworkAccessManager *m_nam;
    QPointer<QNetworkReply> m_reply;
    NtfyServerConfig m_server;
};

#endif // NTFYPOLLER_H
