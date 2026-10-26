// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef FAKENTFYSERVER_H
#define FAKENTFYSERVER_H

#include <QHash>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

// Minimal HTTP server: records request lines and answers each connection with a scripted
// response, written in separate chunks to exercise line reassembly.
class FakeNtfyServer : public QTcpServer
{
    Q_OBJECT
public:
    struct Response {
        int status = 200;
        QByteArray headers; // extra header lines, each ending in \r\n
        QList<QByteArray> chunks;
        bool closeAfter = true;
    };

    QList<Response> responses;
    QList<QByteArray> requestLines;
    QList<QByteArray> authHeaders;

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }

protected:
    void incomingConnection(qintptr handle) override
    {
        QTcpSocket *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(handle);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            QByteArray &buffer = m_pending[socket];
            buffer += socket->readAll();
            if (!buffer.contains("\r\n\r\n"))
                return;

            const QList<QByteArray> lines = buffer.split('\n');
            requestLines.append(lines.first().trimmed());
            QByteArray auth;
            for (const QByteArray &line : lines) {
                if (line.toLower().startsWith("authorization:"))
                    auth = line.mid(line.indexOf(':') + 1).trimmed();
            }
            authHeaders.append(auth);
            m_pending.remove(socket);

            const Response response = responses.isEmpty() ? Response() : responses.takeFirst();
            socket->write("HTTP/1.1 " + QByteArray::number(response.status) + " X\r\n"
                          "Content-Type: application/x-ndjson\r\n"
                          + response.headers +
                          "Connection: close\r\n\r\n");
            for (int i = 0; i < response.chunks.size(); ++i) {
                const QByteArray chunk = response.chunks.at(i);
                QTimer::singleShot(20 * (i + 1), socket, [socket, chunk]() { socket->write(chunk); });
            }
            if (response.closeAfter) {
                QTimer::singleShot(20 * (response.chunks.size() + 1), socket,
                                   [socket]() { socket->disconnectFromHost(); });
            }
        });
    }

private:
    QHash<QTcpSocket *, QByteArray> m_pending;
};

#endif // FAKENTFYSERVER_H
