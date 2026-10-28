// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef NTFYACTIONRUNNER_H
#define NTFYACTIONRUNNER_H

#include "ntfymessage.h"

#include <QObject>

#include <functional>

class QNetworkAccessManager;

// Performs ntfy "http" action buttons (https://docs.ntfy.sh/publish/#send-http-request): a
// request to whatever URL the publisher chose. The UI asks the user first; this never sends our
// ntfy credentials along and never follows redirects.
class NtfyActionRunner : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(bool ok, int httpStatus, const QString &error)>;

    explicit NtfyActionRunner(QNetworkAccessManager *nam, QObject *parent = nullptr);

    // Whether run() would try this action at all: an "http" action with an http(s) URL.
    static bool canRun(const NtfyAction &action, QString *error = nullptr);

    // Calls done exactly once. A 2xx answer counts as success, even if the rest of the response
    // never arrives: the server did accept the action.
    void run(const NtfyAction &action, const Callback &done);

    void setTimeout(int msecs) { m_timeout = msecs; }

private:
    QNetworkAccessManager *m_nam;
    int m_timeout;
};

#endif // NTFYACTIONRUNNER_H
