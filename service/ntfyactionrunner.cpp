// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfyactionrunner.h"

#include <QBuffer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {
const int DefaultTimeout = 15 * 1000;
} // namespace

NtfyActionRunner::NtfyActionRunner(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_nam(nam)
    , m_timeout(DefaultTimeout)
{
}

bool NtfyActionRunner::canRun(const NtfyAction &action, QString *error)
{
    if (action.action != QLatin1String("http")) {
        if (error)
            *error = QStringLiteral("only http actions are run by the daemon");
        return false;
    }
    const QUrl url(action.url);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || url.host().isEmpty()
            || (scheme != QLatin1String("https") && scheme != QLatin1String("http"))) {
        if (error)
            *error = QStringLiteral("not an http(s) URL");
        return false;
    }
    return true;
}

void NtfyActionRunner::run(const NtfyAction &action, const Callback &done)
{
    QString error;
    if (!canRun(action, &error)) {
        done(false, 0, error);
        return;
    }

    QNetworkRequest request(QUrl(action.url));
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, false);
    request.setRawHeader("User-Agent", "harbour-ntfy/" NTFY_VERSION " (Sailfish OS)");
    for (auto it = action.headers.constBegin(); it != action.headers.constEnd(); ++it)
        request.setRawHeader(it.key().toUtf8(), it.value().toString().toUtf8());

    // ntfy's default for http actions is POST.
    const QByteArray method = action.method.isEmpty() ? QByteArray("POST") : action.method.toUpper().toUtf8();
    // The QByteArray overload of sendCustomRequest() only exists since Qt 5.8.
    QBuffer *body = new QBuffer;
    body->setData(action.body.toUtf8());
    body->open(QIODevice::ReadOnly);
    QNetworkReply *reply = m_nam->sendCustomRequest(request, method, body);
    body->setParent(reply);

    QTimer *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeout->start(m_timeout);

    connect(reply, &QNetworkReply::finished, this, [reply, done]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 200 && status < 300) {
            done(true, status, QString());
        } else if (status > 0) {
            done(false, status, QStringLiteral("HTTP %1").arg(status));
        } else {
            done(false, 0, reply->error() == QNetworkReply::OperationCanceledError
                                   ? QStringLiteral("timed out")
                                   : reply->errorString());
        }
    });
}
