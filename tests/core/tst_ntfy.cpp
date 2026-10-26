// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfycredentials.h"
#include "ntfymessage.h"
#include "ntfynotificationcontent.h"
#include "ntfypoller.h"
#include "ntfyserver.h"
#include "ntfystream.h"

#include "fakentfyserver.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QtTest>

class TestNtfy : public QObject
{
    Q_OBJECT

private slots:
    void parseMessage();
    void jsonRoundTrip();
    void parseControlEvents_data();
    void parseControlEvents();
    void parseInvalid_data();
    void parseInvalid();
    void priorityDefaults_data();
    void priorityDefaults();

    void notificationContent_data();
    void notificationContent();
    void notificationPriority_data();
    void notificationPriority();

    void subscribeUrl_data();
    void subscribeUrl();
    void authorization();
    void credentialKey();
    void credentialRoundTrip();
    void memoryCredentialStore();

    void streamReassemblesLinesAndResumes();
    void streamStopsOnAccessDenied();
    void streamKeepaliveTimeoutReconnects();
    void streamRefusesRedirects();
    void pollRefusesRedirects();
    void pollCollectsMessages();
    void pollReportsServerError();
};

void TestNtfy::parseMessage()
{
    const QByteArray json = R"({"id":"sPs71M8A2T","time":1643935928,"expires":1643979128,
        "event":"message","topic":"mytopic","priority":5,"tags":["warning","skull"],
        "click":"https://example.com","title":"Unauthorized access","message":"Remote access detected",
        "actions":[{"action":"view","label":"Open portal","url":"https://example.com/portal","clear":true},
                   {"action":"http","label":"Turn off","url":"https://api.example.com/off","method":"PUT",
                    "headers":{"X-Token":"abc"},"body":"{}"}],
        "attachment":{"name":"flower.jpg","type":"image/jpeg","size":309399,"expires":1643946728,
                      "url":"https://ntfy.sh/file/sPs71M8A2T.jpg"}})";

    QString error;
    const NtfyEvent event = NtfyEvent::fromJson(json, &error);
    QVERIFY2(event.isValid(), qPrintable(error));
    QCOMPARE(event.type, NtfyEvent::Message);
    QCOMPARE(event.id, QStringLiteral("sPs71M8A2T"));
    QCOMPARE(event.time, Q_INT64_C(1643935928));
    QCOMPARE(event.expires, Q_INT64_C(1643979128));
    QCOMPARE(event.topic, QStringLiteral("mytopic"));
    QCOMPARE(event.priority, 5);
    QCOMPARE(event.tags, QStringList() << QStringLiteral("warning") << QStringLiteral("skull"));
    QCOMPARE(event.click, QStringLiteral("https://example.com"));
    QCOMPARE(event.title, QStringLiteral("Unauthorized access"));
    QCOMPARE(event.message, QStringLiteral("Remote access detected"));

    QCOMPARE(event.actions.size(), 2);
    QCOMPARE(event.actions.at(0).action, QStringLiteral("view"));
    QCOMPARE(event.actions.at(0).label, QStringLiteral("Open portal"));
    QVERIFY(event.actions.at(0).clear);
    QCOMPARE(event.actions.at(1).method, QStringLiteral("PUT"));
    QCOMPARE(event.actions.at(1).headers.value(QStringLiteral("X-Token")).toString(), QStringLiteral("abc"));
    QCOMPARE(event.actions.at(1).body, QStringLiteral("{}"));

    QVERIFY(event.attachment.isValid());
    QCOMPARE(event.attachment.name, QStringLiteral("flower.jpg"));
    QCOMPARE(event.attachment.type, QStringLiteral("image/jpeg"));
    QCOMPARE(event.attachment.size, Q_INT64_C(309399));
    QCOMPARE(event.attachment.url, QStringLiteral("https://ntfy.sh/file/sPs71M8A2T.jpg"));
}

void TestNtfy::jsonRoundTrip()
{
    const QByteArray json = R"({"id":"sPs71M8A2T","time":1643935928,"expires":1643979128,
        "event":"message","topic":"mytopic","priority":5,"tags":["warning","skull"],
        "click":"https://example.com","icon":"https://example.com/i.png","title":"Hi","message":"there",
        "content_type":"text/markdown",
        "actions":[{"action":"http","label":"Turn off","url":"https://api.example.com/off","method":"PUT",
                    "headers":{"X-Token":"abc"},"body":"{}","clear":true}],
        "attachment":{"name":"flower.jpg","type":"image/jpeg","size":309399,"expires":1643946728,
                      "url":"https://ntfy.sh/file/sPs71M8A2T.jpg"}})";

    const NtfyEvent original = NtfyEvent::fromJson(json);
    const NtfyEvent copy = NtfyEvent::fromJson(original.toJson());
    QCOMPARE(copy.type, original.type);
    QCOMPARE(copy.id, original.id);
    QCOMPARE(copy.time, original.time);
    QCOMPARE(copy.expires, original.expires);
    QCOMPARE(copy.topic, original.topic);
    QCOMPARE(copy.title, original.title);
    QCOMPARE(copy.message, original.message);
    QCOMPARE(copy.priority, original.priority);
    QCOMPARE(copy.tags, original.tags);
    QCOMPARE(copy.click, original.click);
    QCOMPARE(copy.icon, original.icon);
    QCOMPARE(copy.contentType, original.contentType);
    QCOMPARE(copy.actions.size(), 1);
    QCOMPARE(copy.actions.at(0).method, QStringLiteral("PUT"));
    QCOMPARE(copy.actions.at(0).headers, original.actions.at(0).headers);
    QVERIFY(copy.actions.at(0).clear);
    QCOMPARE(copy.attachment.url, original.attachment.url);
    QCOMPARE(copy.attachment.size, original.attachment.size);

    const NtfyEvent keepalive = NtfyEvent::fromJson(R"({"id":"k","time":5,"event":"keepalive","topic":"t"})");
    QCOMPARE(NtfyEvent::fromJson(keepalive.toJson()).type, NtfyEvent::Keepalive);
}

void TestNtfy::parseControlEvents_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<int>("type");

    QTest::newRow("open") << QByteArray(R"({"id":"a","time":1,"event":"open","topic":"t"})")
                          << int(NtfyEvent::Open);
    QTest::newRow("keepalive") << QByteArray(R"({"id":"b","time":2,"event":"keepalive","topic":"t"})")
                               << int(NtfyEvent::Keepalive);
    QTest::newRow("poll_request") << QByteArray(R"({"id":"c","time":3,"event":"poll_request","topic":"t"})")
                                  << int(NtfyEvent::PollRequest);
    QTest::newRow("future event") << QByteArray(R"({"id":"d","time":4,"event":"message_delete","topic":"t"})")
                                  << int(NtfyEvent::Unknown);
}

void TestNtfy::parseControlEvents()
{
    QFETCH(QByteArray, json);
    QFETCH(int, type);

    const NtfyEvent event = NtfyEvent::fromJson(json);
    QVERIFY(event.isValid());
    QCOMPARE(int(event.type), type);
    QCOMPARE(event.topic, QStringLiteral("t"));
}

void TestNtfy::parseInvalid_data()
{
    QTest::addColumn<QByteArray>("json");

    QTest::newRow("garbage") << QByteArray("not json");
    QTest::newRow("truncated") << QByteArray(R"({"event":"message","mess)");
    QTest::newRow("array") << QByteArray("[1,2,3]");
    QTest::newRow("no event") << QByteArray(R"({"code":40101,"http":401,"error":"unauthorized"})");
}

void TestNtfy::parseInvalid()
{
    QFETCH(QByteArray, json);

    QString error;
    const NtfyEvent event = NtfyEvent::fromJson(json, &error);
    QVERIFY(!event.isValid());
    QVERIFY(!error.isEmpty());
}

void TestNtfy::priorityDefaults_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<int>("priority");

    QTest::newRow("missing") << QByteArray(R"({"event":"message","message":"x"})") << 3;
    QTest::newRow("min") << QByteArray(R"({"event":"message","priority":1})") << 1;
    QTest::newRow("zero") << QByteArray(R"({"event":"message","priority":0})") << 3;
    QTest::newRow("too high") << QByteArray(R"({"event":"message","priority":9})") << 3;
}

void TestNtfy::priorityDefaults()
{
    QFETCH(QByteArray, json);
    QFETCH(int, priority);

    QCOMPARE(NtfyEvent::fromJson(json).priority, priority);
}

void TestNtfy::notificationContent_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<QString>("summary");
    QTest::addColumn<QString>("body");

    QTest::newRow("title and message")
            << QByteArray(R"({"event":"message","topic":"t","title":"Backup","message":"done"})")
            << QStringLiteral("Backup") << QStringLiteral("done");
    QTest::newRow("topic as fallback title")
            << QByteArray(R"({"event":"message","topic":"alerts","message":"disk full"})")
            << QStringLiteral("alerts") << QStringLiteral("disk full");
    QTest::newRow("emoji and plain tags")
            << QByteArray(R"({"event":"message","topic":"t","title":"Login","message":"from 1.2.3.4",)"
                          R"("tags":["warning","ssh","skull","prod"]})")
            << QString::fromUtf8("⚠️ 💀 Login") << QStringLiteral("from 1.2.3.4\nTags: ssh, prod");
    QTest::newRow("attachment only")
            << QByteArray(R"({"event":"message","topic":"t","attachment":{"name":"report.pdf",)"
                          R"("url":"https://ntfy.sh/file/x.pdf"}})")
            << QStringLiteral("t") << QStringLiteral("report.pdf");
}

void TestNtfy::notificationContent()
{
    QFETCH(QByteArray, json);
    QFETCH(QString, summary);
    QFETCH(QString, body);

    const NtfyNotificationContent content = NtfyNotificationContent::fromMessage(NtfyEvent::fromJson(json));
    QCOMPARE(content.summary, summary);
    QCOMPARE(content.body, body);
}

void TestNtfy::notificationPriority_data()
{
    QTest::addColumn<int>("priority");
    QTest::addColumn<int>("urgency");
    QTest::addColumn<bool>("popup");

    QTest::newRow("min") << 1 << int(NtfyNotificationContent::Low) << false;
    QTest::newRow("low") << 2 << int(NtfyNotificationContent::Low) << false;
    QTest::newRow("default") << 3 << int(NtfyNotificationContent::Normal) << true;
    QTest::newRow("high") << 4 << int(NtfyNotificationContent::Normal) << true;
    QTest::newRow("max") << 5 << int(NtfyNotificationContent::Critical) << true;
}

void TestNtfy::notificationPriority()
{
    QFETCH(int, priority);
    QFETCH(int, urgency);
    QFETCH(bool, popup);

    NtfyEvent message;
    message.type = NtfyEvent::Message;
    message.priority = priority;
    message.time = 1643935928;

    const NtfyNotificationContent content = NtfyNotificationContent::fromMessage(message);
    QCOMPARE(int(content.urgency), urgency);
    QCOMPARE(content.popup, popup);
    QCOMPARE(content.timestamp.toMSecsSinceEpoch(), Q_INT64_C(1643935928000));
}

void TestNtfy::subscribeUrl_data()
{
    QTest::addColumn<QString>("base");
    QTest::addColumn<QStringList>("topics");
    QTest::addColumn<QString>("since");
    QTest::addColumn<bool>("poll");
    QTest::addColumn<QString>("expected");

    QTest::newRow("plain") << QStringLiteral("https://ntfy.sh") << QStringList{QStringLiteral("a")} << QString() << false
                           << QStringLiteral("https://ntfy.sh/a/json");
    QTest::newRow("trailing slash") << QStringLiteral("https://ntfy.sh/") << QStringList{QStringLiteral("a")} << QString()
                                    << false << QStringLiteral("https://ntfy.sh/a/json");
    QTest::newRow("multiple topics") << QStringLiteral("https://ntfy.sh") << QStringList{QStringLiteral("a"), QStringLiteral("b")}
                                     << QString() << false << QStringLiteral("https://ntfy.sh/a,b/json");
    QTest::newRow("sub path") << QStringLiteral("https://example.com/ntfy/") << QStringList{QStringLiteral("a")} << QString()
                              << false << QStringLiteral("https://example.com/ntfy/a/json");
    QTest::newRow("since") << QStringLiteral("https://ntfy.sh") << QStringList{QStringLiteral("a")} << QStringLiteral("abc123") << false
                           << QStringLiteral("https://ntfy.sh/a/json?since=abc123");
    QTest::newRow("poll") << QStringLiteral("https://ntfy.sh") << QStringList{QStringLiteral("a")} << QStringLiteral("all") << true
                          << QStringLiteral("https://ntfy.sh/a/json?since=all&poll=1");
}

void TestNtfy::subscribeUrl()
{
    QFETCH(QString, base);
    QFETCH(QStringList, topics);
    QFETCH(QString, since);
    QFETCH(bool, poll);
    QFETCH(QString, expected);

    NtfyServerConfig server;
    server.baseUrl = QUrl(base);
    QCOMPARE(server.subscribeUrl(topics, since, poll).toString(), expected);
}

void TestNtfy::authorization()
{
    NtfyServerConfig server;
    QVERIFY(server.authorizationHeader().isEmpty());
    QVERIFY(server.request(QUrl(QStringLiteral("https://ntfy.sh"))).rawHeader("Authorization").isEmpty());

    server.username = QStringLiteral("phil");
    server.password = QStringLiteral("mypass");
    QCOMPARE(server.authorizationHeader(), QByteArray("Basic cGhpbDpteXBhc3M="));

    server.token = QStringLiteral("tk_AgQdq7mVBoFD37zQVN29RhuMzNIz2");
    QCOMPARE(server.authorizationHeader(), QByteArray("Bearer tk_AgQdq7mVBoFD37zQVN29RhuMzNIz2"));
    QCOMPARE(server.request(QUrl(QStringLiteral("https://ntfy.sh"))).rawHeader("Authorization"),
             server.authorizationHeader());
}

void TestNtfy::credentialKey()
{
    const QString key = ntfyCredentialKey(QUrl(QStringLiteral("https://ntfy.sh")));
    QCOMPARE(key.size(), 6 + 64); // "server" + hex sha256
    QVERIFY(!key.contains(QStringLiteral("ntfy"))); // doesn't reveal the server
    QCOMPARE(ntfyCredentialKey(QUrl(QStringLiteral("https://NTFY.sh/"))), key);
    QCOMPARE(ntfyCredentialKey(QUrl(QStringLiteral("https://user:pw@ntfy.sh"))), key);
    QVERIFY(ntfyCredentialKey(QUrl(QStringLiteral("http://ntfy.sh"))) != key);
    QVERIFY(ntfyCredentialKey(QUrl(QStringLiteral("https://ntfy.sh:8443"))) != key);
    QVERIFY(ntfyCredentialKey(QUrl(QStringLiteral("https://example.com/ntfy"))) != key);
}

void TestNtfy::credentialRoundTrip()
{
    NtfyServerConfig withToken;
    withToken.token = QStringLiteral("tk_abc");
    NtfyServerConfig decoded;
    decoded.username = QStringLiteral("stale");
    QVERIFY(ntfyDecodeCredentials(ntfyEncodeCredentials(withToken), &decoded));
    QCOMPARE(decoded.token, QStringLiteral("tk_abc"));
    QVERIFY(decoded.username.isEmpty());

    NtfyServerConfig withPassword;
    withPassword.username = QStringLiteral("phil");
    withPassword.password = QString::fromUtf8("p@ss:wörd");
    QVERIFY(ntfyDecodeCredentials(ntfyEncodeCredentials(withPassword), &decoded));
    QVERIFY(decoded.token.isEmpty());
    QCOMPARE(decoded.username, QStringLiteral("phil"));
    QCOMPARE(decoded.password, QString::fromUtf8("p@ss:wörd"));

    QVERIFY(!ntfyDecodeCredentials("garbage", &decoded));
}

void TestNtfy::memoryCredentialStore()
{
    NtfyMemoryCredentialStore store;
    QByteArray secret;
    bool found = true;
    QVERIFY(store.load(QStringLiteral("k"), &secret, &found));
    QVERIFY(!found);

    QVERIFY(store.store(QStringLiteral("k"), "one"));
    QVERIFY(store.store(QStringLiteral("k"), "two"));
    QVERIFY(store.load(QStringLiteral("k"), &secret, &found));
    QVERIFY(found);
    QCOMPARE(secret, QByteArray("two"));

    QVERIFY(store.remove(QStringLiteral("k")));
    QVERIFY(store.remove(QStringLiteral("k")));
    QVERIFY(store.load(QStringLiteral("k"), &secret, &found));
    QVERIFY(!found);
}

void TestNtfy::streamReassemblesLinesAndResumes()
{
    FakeNtfyServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));

    FakeNtfyServer::Response first;
    first.chunks << R"({"id":"o1","time":1,"event":"open","topic":"t"})" "\n"
                 << R"({"id":"m1","time":2,"event":"mess)"
                 << R"(age","topic":"t","message":"hello"})" "\n" R"({"id":"k1","time":3,"event":"keepalive","topic":"t"})" "\n";
    FakeNtfyServer::Response second;
    second.chunks << R"({"id":"o2","time":4,"event":"open","topic":"t"})" "\n"
                  << R"({"id":"m2","time":5,"event":"message","topic":"t","message":"world"})" "\n";
    second.closeAfter = false;
    fake.responses << first << second;

    QNetworkAccessManager nam;
    NtfyStream stream(&nam);
    NtfyServerConfig server;
    server.baseUrl = fake.url();
    server.token = QStringLiteral("tk_test");
    stream.setServer(server);
    stream.setTopics(QStringList{QStringLiteral("t")});
    stream.setSince(QStringLiteral("10m"));

    QSignalSpy messages(&stream, &NtfyStream::messageReceived);
    QSignalSpy events(&stream, &NtfyStream::eventReceived);
    stream.start();

    QTRY_COMPARE_WITH_TIMEOUT(messages.count(), 2, 5000);
    QCOMPARE(messages.at(0).at(0).value<NtfyEvent>().message, QStringLiteral("hello"));
    QCOMPARE(messages.at(1).at(0).value<NtfyEvent>().message, QStringLiteral("world"));
    QCOMPARE(events.count(), 5); // open, message, keepalive, open, message
    QCOMPARE(stream.lastMessageId(), QStringLiteral("m2"));
    QCOMPARE(stream.state(), NtfyStream::Connected);

    // First connect uses the configured since, the reconnect resumes after the last message.
    QCOMPARE(fake.requestLines.size(), 2);
    QCOMPARE(fake.requestLines.at(0), QByteArray("GET /t/json?since=10m HTTP/1.1"));
    QCOMPARE(fake.requestLines.at(1), QByteArray("GET /t/json?since=m1 HTTP/1.1"));
    QCOMPARE(fake.authHeaders.at(0), QByteArray("Bearer tk_test"));

    stream.stop();
    QCOMPARE(stream.state(), NtfyStream::Disconnected);
}

void TestNtfy::streamStopsOnAccessDenied()
{
    FakeNtfyServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));
    FakeNtfyServer::Response denied;
    denied.status = 401;
    denied.chunks << R"({"code":40101,"http":401,"error":"unauthorized"})" "\n";
    fake.responses << denied;

    QNetworkAccessManager nam;
    NtfyStream stream(&nam);
    NtfyServerConfig server;
    server.baseUrl = fake.url();
    stream.setServer(server);
    stream.setTopics(QStringList{QStringLiteral("t")});

    QSignalSpy denials(&stream, &NtfyStream::accessDenied);
    QSignalSpy errors(&stream, &NtfyStream::errorOccurred);
    QSignalSpy events(&stream, &NtfyStream::eventReceived);
    stream.start();

    QTRY_COMPARE_WITH_TIMEOUT(denials.count(), 1, 5000);
    QCOMPARE(denials.at(0).at(0).toInt(), 401);
    QCOMPARE(errors.at(0).at(0).toString(), QStringLiteral("HTTP 401: unauthorized"));
    QCOMPARE(events.count(), 0);
    QCOMPARE(stream.state(), NtfyStream::Disconnected);

    QTest::qWait(1500); // would have reconnected after 1s if it retried
    QCOMPARE(fake.requestLines.size(), 1);
}

void TestNtfy::streamKeepaliveTimeoutReconnects()
{
    FakeNtfyServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));
    FakeNtfyServer::Response silent;
    silent.chunks << R"({"id":"o1","time":1,"event":"open","topic":"t"})" "\n";
    silent.closeAfter = false;
    fake.responses << silent << silent;

    QNetworkAccessManager nam;
    NtfyStream stream(&nam);
    NtfyServerConfig server;
    server.baseUrl = fake.url();
    stream.setServer(server);
    stream.setTopics(QStringList{QStringLiteral("t")});
    stream.setKeepaliveTimeout(300);

    bool waitedToReconnect = false;
    connect(&stream, &NtfyStream::stateChanged, [&waitedToReconnect](NtfyStream::State state) {
        if (state == NtfyStream::WaitingToReconnect)
            waitedToReconnect = true;
    });
    stream.start();

    QTRY_COMPARE_WITH_TIMEOUT(fake.requestLines.size(), 2, 5000);
    QVERIFY(waitedToReconnect);
    stream.stop();
}

void TestNtfy::streamRefusesRedirects()
{
    FakeNtfyServer elsewhere;
    QVERIFY(elsewhere.listen(QHostAddress::LocalHost));
    FakeNtfyServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));
    FakeNtfyServer::Response redirect;
    redirect.status = 302;
    redirect.headers = "Location: " + elsewhere.url().toEncoded() + "/t/json\r\n";
    fake.responses << redirect;

    QNetworkAccessManager nam;
    NtfyStream stream(&nam);
    NtfyServerConfig server;
    server.baseUrl = fake.url();
    server.token = QStringLiteral("tk_secret");
    stream.setServer(server);
    stream.setTopics(QStringList{QStringLiteral("t")});

    QSignalSpy refused(&stream, &NtfyStream::redirectRefused);
    QSignalSpy errors(&stream, &NtfyStream::errorOccurred);
    stream.start();

    QTRY_COMPARE_WITH_TIMEOUT(refused.count(), 1, 5000);
    QCOMPARE(refused.at(0).at(0).toUrl(), QUrl(elsewhere.url().toString() + QStringLiteral("/t/json")));
    QVERIFY(errors.at(0).at(0).toString().startsWith(QStringLiteral("HTTP 302: server redirects to")));
    QCOMPARE(stream.state(), NtfyStream::Disconnected);

    QTest::qWait(1500); // would have reconnected after 1s if it retried
    QCOMPARE(fake.requestLines.size(), 1);
    QVERIFY(elsewhere.requestLines.isEmpty()); // the token never left for the other server
}

void TestNtfy::pollRefusesRedirects()
{
    FakeNtfyServer elsewhere;
    QVERIFY(elsewhere.listen(QHostAddress::LocalHost));
    FakeNtfyServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));
    FakeNtfyServer::Response redirect;
    redirect.status = 307;
    redirect.headers = "Location: " + elsewhere.url().toEncoded() + "/t/json?poll=1\r\n";
    fake.responses << redirect;

    QNetworkAccessManager nam;
    NtfyPoller poller(&nam);
    NtfyServerConfig server;
    server.baseUrl = fake.url();
    server.token = QStringLiteral("tk_secret");
    poller.setServer(server);

    QSignalSpy finished(&poller, &NtfyPoller::finished);
    poller.poll(QStringList{QStringLiteral("t")}, QString());

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QVERIFY(!finished.at(0).at(0).toBool());
    QVERIFY(finished.at(0).at(2).toString().startsWith(QStringLiteral("HTTP 307: server redirects to")));
    QTest::qWait(200);
    QVERIFY(elsewhere.requestLines.isEmpty());
}

void TestNtfy::pollCollectsMessages()
{
    FakeNtfyServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));
    FakeNtfyServer::Response cached;
    cached.chunks << R"({"id":"m1","time":1,"event":"message","topic":"t","message":"one"})" "\n"
                     R"({"id":"m2","time":2,"event":"message","topic":"t","message":"two"})" "\n";
    fake.responses << cached;

    QNetworkAccessManager nam;
    NtfyPoller poller(&nam);
    NtfyServerConfig server;
    server.baseUrl = fake.url();
    poller.setServer(server);

    QSignalSpy messages(&poller, &NtfyPoller::messageReceived);
    QSignalSpy finished(&poller, &NtfyPoller::finished);
    poller.poll(QStringList{QStringLiteral("t")}, QStringLiteral("m0"));

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(fake.requestLines.at(0), QByteArray("GET /t/json?since=m0&poll=1 HTTP/1.1"));
    QCOMPARE(messages.count(), 2);
    QVERIFY(finished.at(0).at(0).toBool());
    QCOMPARE(finished.at(0).at(1).toString(), QStringLiteral("m2"));
    QVERIFY(!poller.isRunning());
}

void TestNtfy::pollReportsServerError()
{
    FakeNtfyServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));
    FakeNtfyServer::Response limited;
    limited.status = 429;
    limited.chunks << R"({"code":42901,"http":429,"error":"limit reached: too many requests"})";
    fake.responses << limited;

    QNetworkAccessManager nam;
    NtfyPoller poller(&nam);
    NtfyServerConfig server;
    server.baseUrl = fake.url();
    poller.setServer(server);

    QSignalSpy finished(&poller, &NtfyPoller::finished);
    poller.poll(QStringList{QStringLiteral("t")}, QString());

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QVERIFY(!finished.at(0).at(0).toBool());
    QCOMPARE(finished.at(0).at(2).toString(), QStringLiteral("HTTP 429: limit reached: too many requests"));
}

QTEST_GUILESS_MAIN(TestNtfy)
#include "tst_ntfy.moc"
