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
        bool silent = false; // accept the request but never answer
    };

    QList<Response> responses;
    QList<QByteArray> requestLines;
    QList<QByteArray> authHeaders;
    QList<QByteArray> requests; // complete raw requests, headers and body

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }

protected:
    void incomingConnection(qintptr handle) override
    {
        QTcpSocket *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(handle);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            QByteArray &buffer = m_pending[socket];
            buffer += socket->readAll();
            const int headerEnd = buffer.indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;

            const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
            QByteArray auth;
            int contentLength = 0;
            for (const QByteArray &line : lines) {
                const QByteArray lower = line.toLower();
                if (lower.startsWith("authorization:"))
                    auth = line.mid(line.indexOf(':') + 1).trimmed();
                if (lower.startsWith("content-length:"))
                    contentLength = line.mid(line.indexOf(':') + 1).trimmed().toInt();
            }
            if (buffer.size() < headerEnd + 4 + contentLength)
                return; // wait for the body

            requestLines.append(lines.first().trimmed());
            authHeaders.append(auth);
            requests.append(buffer);
            m_pending.remove(socket);

            const Response response = responses.isEmpty() ? Response() : responses.takeFirst();
            if (response.silent)
                return;
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
