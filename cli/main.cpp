// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Command line spike for the ntfy core: subscribe to topics and print what arrives.
//
//   harbour-ntfy-cli mytopic
//   harbour-ntfy-cli --server https://ntfy.example.com --since 1h alerts,backups
//   NTFY_TOKEN=tk_... harbour-ntfy-cli --poll --since all alerts
//   harbour-ntfy-cli --notify mytopic          (Sailfish OS: also post system notifications)

#include "ntfypoller.h"
#include "ntfystream.h"
#ifdef NTFY_HAVE_NOTIFY
#include "ntfynotifier.h"
#endif

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QMetaEnum>
#include <QNetworkAccessManager>
#include <QTextStream>

namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream &err()
{
    static QTextStream stream(stderr);
    return stream;
}

void printMessage(const NtfyEvent &msg)
{
    const QDateTime time = QDateTime::fromMSecsSinceEpoch(msg.time * 1000);
    out() << time.toString(Qt::ISODate) << "  [" << msg.topic << "] p" << msg.priority;
    if (!msg.tags.isEmpty())
        out() << " {" << msg.tags.join(QLatin1Char(',')) << "}";
    if (!msg.title.isEmpty())
        out() << "  " << msg.title << ":";
    out() << "  " << msg.message;
    if (msg.attachment.isValid())
        out() << "  <" << msg.attachment.url << ">";
    out() << "  (id " << msg.id << ")" << endl;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("harbour-ntfy-cli"));
    QCoreApplication::setApplicationVersion(QStringLiteral(NTFY_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Subscribe to ntfy topics and print messages."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("topics"),
                                 QStringLiteral("Topic(s) to subscribe to, comma or space separated."));

    QCommandLineOption serverOption(QStringList() << QStringLiteral("s") << QStringLiteral("server"),
                                    QStringLiteral("Server base URL (default https://ntfy.sh)."),
                                    QStringLiteral("url"), QStringLiteral("https://ntfy.sh"));
    QCommandLineOption tokenOption(QStringList() << QStringLiteral("t") << QStringLiteral("token"),
                                   QStringLiteral("Access token (or set NTFY_TOKEN)."),
                                   QStringLiteral("token"));
    QCommandLineOption userOption(QStringList() << QStringLiteral("u") << QStringLiteral("user"),
                                  QStringLiteral("Basic auth credentials as user:password."),
                                  QStringLiteral("user:password"));
    QCommandLineOption sinceOption(QStringLiteral("since"),
                                   QStringLiteral("Start from a message id, unix time, duration (10m) or 'all'."),
                                   QStringLiteral("since"));
    QCommandLineOption pollOption(QStringLiteral("poll"),
                                  QStringLiteral("Fetch cached messages once and exit instead of streaming."));
    QCommandLineOption verboseOption(QStringLiteral("verbose"),
                                     QStringLiteral("Print connection state changes and keepalives."));
    parser.addOptions(QList<QCommandLineOption>() << serverOption << tokenOption << userOption
                      << sinceOption << pollOption << verboseOption);
#ifdef NTFY_HAVE_NOTIFY
    QCommandLineOption notifyOption(QStringLiteral("notify"),
                                    QStringLiteral("Also post each message as a system notification."));
    parser.addOption(notifyOption);
#endif
    parser.process(app);

    QStringList topics;
    for (const QString &arg : parser.positionalArguments())
        topics += arg.split(QLatin1Char(','), QString::SkipEmptyParts);
    if (topics.isEmpty()) {
        err() << "error: no topics given" << endl;
        parser.showHelp(1);
    }

    NtfyServerConfig server;
    server.baseUrl = QUrl::fromUserInput(parser.value(serverOption));
    server.token = parser.isSet(tokenOption) ? parser.value(tokenOption)
                                             : QString::fromLocal8Bit(qgetenv("NTFY_TOKEN"));
    if (parser.isSet(userOption)) {
        const QString credentials = parser.value(userOption);
        const int colon = credentials.indexOf(QLatin1Char(':'));
        server.username = credentials.left(colon);
        server.password = colon >= 0 ? credentials.mid(colon + 1) : QString();
    }

    const bool verbose = parser.isSet(verboseOption);
    QNetworkAccessManager nam;

#ifdef NTFY_HAVE_NOTIFY
    NtfyNotifier *notifier = nullptr;
    if (parser.isSet(notifyOption)) {
        notifier = new NtfyNotifier(&app);
        QObject::connect(notifier, &NtfyNotifier::clicked, [](const NtfyEvent &msg) {
            err() << "notification clicked: " << msg.id << endl;
        });
    }
#endif

    if (parser.isSet(pollOption)) {
        NtfyPoller *poller = new NtfyPoller(&nam, &app);
        poller->setServer(server);
        QObject::connect(poller, &NtfyPoller::messageReceived, &printMessage);
#ifdef NTFY_HAVE_NOTIFY
        if (notifier)
            QObject::connect(poller, &NtfyPoller::messageReceived, notifier, &NtfyNotifier::notify);
#endif
        QObject::connect(poller, &NtfyPoller::finished,
                         [](bool ok, const QString &lastId, const QString &error) {
            if (ok)
                err() << "poll done, last id: " << (lastId.isEmpty() ? QStringLiteral("-") : lastId) << endl;
            else
                err() << "poll failed: " << error << endl;
            QCoreApplication::exit(ok ? 0 : 1);
        });
        poller->poll(topics, parser.value(sinceOption));
        return app.exec();
    }

    NtfyStream *stream = new NtfyStream(&nam, &app);
    stream->setServer(server);
    stream->setTopics(topics);
    stream->setSince(parser.value(sinceOption));

    QObject::connect(stream, &NtfyStream::messageReceived, &printMessage);
#ifdef NTFY_HAVE_NOTIFY
    if (notifier)
        QObject::connect(stream, &NtfyStream::messageReceived, notifier, &NtfyNotifier::notify);
#endif
    QObject::connect(stream, &NtfyStream::errorOccurred, [](const QString &error) {
        err() << "warning: " << error << endl;
    });
    QObject::connect(stream, &NtfyStream::accessDenied, [](int status) {
        err() << "access denied (HTTP " << status << "), check your credentials" << endl;
        QCoreApplication::exit(2);
    });
    if (verbose) {
        QObject::connect(stream, &NtfyStream::stateChanged, [](NtfyStream::State state) {
            err() << "state: " << QMetaEnum::fromType<NtfyStream::State>().valueToKey(state) << endl;
        });
        QObject::connect(stream, &NtfyStream::eventReceived, [](const NtfyEvent &event) {
            if (event.type == NtfyEvent::Keepalive)
                err() << "keepalive" << endl;
        });
    }

    stream->start();
    return app.exec();
}
