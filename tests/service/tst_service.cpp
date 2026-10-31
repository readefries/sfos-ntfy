// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfyactionrunner.h"
#include "ntfycredentials.h"
#include "ntfydatabase.h"
#include "ntfydatabasekey.h"
#include "ntfydbusservice.h"
#include "ntfydeliverycontroller.h"
#include "ntfysubscriptionmanager.h"

#include "fakentfyserver.h"

#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

namespace {

NtfyEvent message(const QString &id, qint64 time, const QString &topic = QStringLiteral("t"))
{
    return NtfyEvent::fromJson(QStringLiteral(R"({"id":"%1","time":%2,"event":"message","topic":"%3","message":"m %1"})")
                               .arg(id).arg(time).arg(topic).toUtf8());
}

QByteArray line(const QString &id, qint64 time, const QString &topic = QStringLiteral("t"))
{
    return message(id, time, topic).toJson() + '\n';
}

// Calls the service from a separate connection, as the UI would, without blocking the event loop
// that has to answer it.
QDBusMessage callAsync(QDBusConnection connection, const QString &method, const QVariantList &args = QVariantList())
{
    QDBusMessage call = QDBusMessage::createMethodCall(QString::fromLatin1(NtfyDBus::ServiceName),
                                                       QString::fromLatin1(NtfyDBus::ObjectPath),
                                                       QString::fromLatin1(NtfyDBus::Interface), method);
    call.setArguments(args);
    QDBusPendingCallWatcher watcher(connection.asyncCall(call));
    QSignalSpy finished(&watcher, &QDBusPendingCallWatcher::finished);
    if (!finished.wait(5000))
        return QDBusMessage();
    return watcher.reply();
}

QByteArray openLine()
{
    return R"({"id":"o","time":1,"event":"open","topic":"t"})" "\n";
}

class FakeScheduler : public NtfyWakeupScheduler
{
public:
    QList<int> waits;
    int stops = 0;

    void wait(int seconds) override { waits.append(seconds); }
    void stop() override { ++stops; }
    void wake() { emit wokeUp(); }
};

FakeNtfyServer::Response openStream(const QList<QByteArray> &more = QList<QByteArray>())
{
    FakeNtfyServer::Response response;
    response.chunks << R"({"id":"o","time":1,"event":"open","topic":"t"})" "\n" << more;
    response.closeAfter = false;
    return response;
}

class FailingCredentialStore : public NtfyCredentialStore
{
public:
    bool store(const QString &, const QByteArray &, QString *error) override { return fail(error); }
    bool load(const QString &, QByteArray *, bool *found, QString *error) override
    {
        *found = false;
        return fail(error);
    }
    bool remove(const QString &, QString *error) override { return fail(error); }

private:
    static bool fail(QString *error)
    {
        if (error)
            *error = QStringLiteral("collection is locked");
        return false;
    }
};

} // namespace

class TestService : public QObject
{
    Q_OBJECT

private slots:
    void databaseFilePermissions();
    void databaseRefusesNewerSchema();
    void serversAreNormalized();
    void subscriptionsAndMessages();
    void removingServerCascades();
    void pruneKeepsNewest();
    void databaseKeyCreatedOnce();
    void encryptedDatabase();
    void encryptsExistingDatabase();
    void unreadableDatabaseMovedAside();

    void rejectsInvalidInput();
    void storesAndUsesCredentials();
    void resumesAfterRestart();
    void newTopicReconnectsFromLastMessage();
    void removeServerRemovesCredentials();
    void replacingCredentials();
    void unreadableCredentialsDontConnect();
    void lastErrorUntilFixed();

    void deliveryDefaultsAndPersistence();
    void balancedWakeupReconnects();
    void instantLeavesHealthyStreamsAlone();
    void instantRevivesBrokenStreams();
    void batterySaverOnlyPolls();
    void wakeupIsTimeLimited();
    void deliveryStats();

    void actionRunnerSendsRequest();
    void actionRunnerRefuses();
    void actionRunnerReportsFailures();
    void messageLookup();

    void dbusService();
    void dbusRunAction();
};

void TestService::databaseFilePermissions()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/data/ntfy.db");

    NtfyDatabase db;
    QVERIFY2(db.open(path), qPrintable(db.lastError()));
    const QFile::Permissions others = QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup
            | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther;
    const QFile::Permissions filePermissions = QFileInfo(path).permissions();
    QVERIFY(!(filePermissions & others));
    QVERIFY(filePermissions & QFile::ReadOwner && filePermissions & QFile::WriteOwner);
    QVERIFY(!(QFileInfo(dir.path() + QStringLiteral("/data")).permissions() & others));

    QVERIFY(db.addServer(QUrl(QStringLiteral("https://ntfy.sh"))) > 0);
    db.close();

    // Reopening runs the migration check again and keeps the data.
    QVERIFY2(db.open(path), qPrintable(db.lastError()));
    QCOMPARE(db.servers().size(), 1);
}

void TestService::databaseRefusesNewerSchema()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/ntfy.db");
    {
        QSqlDatabase raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("raw"));
        raw.setDatabaseName(path);
        QVERIFY(raw.open());
        QSqlQuery(raw).exec(QStringLiteral("PRAGMA user_version = 99"));
        raw.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("raw"));

    NtfyDatabase db;
    QVERIFY(!db.open(path));
    QVERIFY(db.lastError().contains(QStringLiteral("newer version")));
    QVERIFY(!db.isOpen());
}

void TestService::serversAreNormalized()
{
    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));

    const qint64 id = db.addServer(QUrl(QStringLiteral("https://NTFY.sh/")));
    QVERIFY(id > 0);
    QCOMPARE(db.addServer(QUrl(QStringLiteral("https://ntfy.sh"))), id);
    QCOMPARE(db.server(id).url, QUrl(QStringLiteral("https://ntfy.sh")));
    QVERIFY(db.addServer(QUrl(QStringLiteral("https://example.com/ntfy"))) != id);
    QCOMPARE(db.servers().size(), 2);
}

void TestService::subscriptionsAndMessages()
{
    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    const qint64 server = db.addServer(QUrl(QStringLiteral("https://ntfy.sh")));
    const qint64 sub = db.addSubscription(server, QStringLiteral("t"), QStringLiteral("Alerts"));
    QVERIFY(sub > 0);
    QCOMPARE(db.addSubscription(server, QStringLiteral("t"), QString()), qint64(0)); // duplicate

    QVERIFY(!db.subscription(sub).lastMessage.isValid());
    QCOMPARE(db.newestMessageTime(server), qint64(0));

    const qint64 first = db.addMessage(sub, message(QStringLiteral("m1"), 100));
    const qint64 second = db.addMessage(sub, message(QStringLiteral("m2"), 200));
    QVERIFY(first > 0 && second > first);
    QCOMPARE(db.addMessage(sub, message(QStringLiteral("m1"), 100)), qint64(0)); // seen before

    NtfySubscriptionRecord record = db.subscription(sub);
    QCOMPARE(record.topic, QStringLiteral("t"));
    QCOMPARE(record.displayName, QStringLiteral("Alerts"));
    QCOMPARE(record.unreadCount, 2);
    QCOMPARE(record.lastMessage.id, QStringLiteral("m2"));
    QCOMPARE(db.newestMessageTime(server), qint64(200));

    const QList<NtfyStoredMessage> newestFirst = db.messages(sub, 10);
    QCOMPARE(newestFirst.size(), 2);
    QCOMPARE(newestFirst.at(0).event.id, QStringLiteral("m2"));
    QCOMPARE(newestFirst.at(1).event.message, QStringLiteral("m m1"));
    const QList<NtfyStoredMessage> older = db.messages(sub, 10, second);
    QCOMPARE(older.size(), 1);
    QCOMPARE(older.at(0).id, first);

    QVERIFY(db.markRead(sub));
    QCOMPARE(db.subscription(sub).unreadCount, 0);
    QVERIFY(db.messages(sub, 1).at(0).read);

    QVERIFY(db.setMuted(sub, true));
    QVERIFY(db.subscription(sub).muted);
}

void TestService::removingServerCascades()
{
    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    const qint64 server = db.addServer(QUrl(QStringLiteral("https://ntfy.sh")));
    const qint64 sub = db.addSubscription(server, QStringLiteral("t"), QString());
    db.addMessage(sub, message(QStringLiteral("m1"), 100));

    QVERIFY(db.removeServer(server));
    QVERIFY(db.subscriptions().isEmpty());
    QVERIFY(db.messages(sub, 10).isEmpty());
    QVERIFY(!db.removeServer(server));
}

void TestService::pruneKeepsNewest()
{
    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    const qint64 server = db.addServer(QUrl(QStringLiteral("https://ntfy.sh")));
    const qint64 a = db.addSubscription(server, QStringLiteral("a"), QString());
    const qint64 b = db.addSubscription(server, QStringLiteral("b"), QString());
    for (int i = 1; i <= 5; ++i)
        db.addMessage(a, message(QStringLiteral("a%1").arg(i), i, QStringLiteral("a")));
    db.addMessage(b, message(QStringLiteral("b1"), 1, QStringLiteral("b")));

    QCOMPARE(db.pruneMessages(2), 3);
    const QList<NtfyStoredMessage> left = db.messages(a, 10);
    QCOMPARE(left.size(), 2);
    QCOMPARE(left.at(0).event.id, QStringLiteral("a5"));
    QCOMPARE(left.at(1).event.id, QStringLiteral("a4"));
    QCOMPARE(db.messages(b, 10).size(), 1);
}

void TestService::databaseKeyCreatedOnce()
{
    NtfyMemoryCredentialStore store;
    QByteArray key, again;
    bool created = false;
    QVERIFY(ntfyDatabaseKey(&store, &key, &created));
    QVERIFY(created);
    QCOMPARE(key.size(), 32);
    QVERIFY(key != QByteArray(32, '\0'));

    QVERIFY(ntfyDatabaseKey(&store, &again, &created));
    QVERIFY(!created);
    QCOMPARE(again, key);

    // While Secrets is locked there's no key, and no new one either.
    FailingCredentialStore locked;
    QString error;
    QVERIFY(!ntfyDatabaseKey(&locked, &key, &created, &error));
    QVERIFY(!created);
    QVERIFY(error.contains(QStringLiteral("collection is locked")));
}

void TestService::encryptedDatabase()
{
    if (!NtfyDatabase::isEncryptionAvailable())
        QSKIP("SQLCipher driver not installed");

    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/ntfy.db");
    const QByteArray key(32, 'k');
    {
        NtfyDatabase db;
        QVERIFY2(db.open(path, key), qPrintable(db.lastError()));
        const qint64 server = db.addServer(QUrl(QStringLiteral("https://ntfy.sh")));
        db.addSubscription(server, QStringLiteral("secret-topic"), QString());
    }

    QVERIFY(!NtfyDatabase::isPlaintextFile(path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(!file.readAll().contains("secret-topic"));
    file.close();

    NtfyDatabase db;
    QVERIFY(!db.open(path, QByteArray(32, 'x')));
    QVERIFY(db.lastError().contains(QStringLiteral("wrong key")));
    QVERIFY(!db.isOpen());
    QVERIFY(!db.open(path)); // not readable without a key either
    QVERIFY2(db.open(path, key), qPrintable(db.lastError()));
    QCOMPARE(db.subscriptions().at(0).topic, QStringLiteral("secret-topic"));
}

void TestService::encryptsExistingDatabase()
{
    if (!NtfyDatabase::isEncryptionAvailable())
        QSKIP("SQLCipher driver not installed");

    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/ntfy.db");
    {
        NtfyDatabase plain;
        QVERIFY(plain.open(path));
        const qint64 server = plain.addServer(QUrl(QStringLiteral("https://ntfy.sh")));
        const qint64 sub = plain.addSubscription(server, QStringLiteral("old-topic"), QStringLiteral("Old"));
        plain.addMessage(sub, message(QStringLiteral("m1"), 100, QStringLiteral("old-topic")));
        plain.setSetting(QStringLiteral("delivery/mode"), QStringLiteral("instant"));
    }
    QVERIFY(NtfyDatabase::isPlaintextFile(path));

    const QByteArray key(32, 'k');
    NtfyDatabase db;
    QString notice, error;
    QVERIFY2(ntfyOpenEncryptedDatabase(&db, path, key, &notice, &error), qPrintable(error));
    QCOMPARE(notice, QStringLiteral("encrypted the existing database"));
    QVERIFY(!NtfyDatabase::isPlaintextFile(path));
    QVERIFY(!QFile::exists(path + QStringLiteral(".plaintext")));
    QVERIFY(!QFile::exists(path + QStringLiteral(".encrypting")));
    QCOMPARE(QFileInfo(path).permissions() & (QFile::ReadOther | QFile::ReadGroup), QFile::Permissions());

    // Everything came along, and the schema version too (no migration ran twice).
    QCOMPARE(db.subscriptions().size(), 1);
    QCOMPARE(db.subscriptions().at(0).displayName, QStringLiteral("Old"));
    QCOMPARE(db.messages(db.subscriptions().at(0).id, 10).size(), 1);
    QCOMPARE(db.setting(QStringLiteral("delivery/mode")), QStringLiteral("instant"));
    db.close();

    // Next start: nothing left to convert.
    NtfyDatabase again;
    QVERIFY(ntfyOpenEncryptedDatabase(&again, path, key, &notice, &error));
    QVERIFY(notice.isEmpty());
    QCOMPARE(again.subscriptions().size(), 1);
}

void TestService::unreadableDatabaseMovedAside()
{
    if (!NtfyDatabase::isEncryptionAvailable())
        QSKIP("SQLCipher driver not installed");

    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/ntfy.db");
    {
        NtfyDatabase db;
        QVERIFY(db.open(path, QByteArray(32, 'a')));
        db.addServer(QUrl(QStringLiteral("https://ntfy.sh")));
    }

    NtfyDatabase db;
    QString notice, error;
    QVERIFY2(ntfyOpenEncryptedDatabase(&db, path, QByteArray(32, 'b'), &notice, &error), qPrintable(error));
    QVERIFY(notice.startsWith(QStringLiteral("could not decrypt the database")));
    QVERIFY(db.servers().isEmpty()); // started over
    const QStringList aside = QDir(dir.path()).entryList(QStringList() << QStringLiteral("ntfy.db.unreadable-*"));
    QCOMPARE(aside.size(), 1);

    // The old file is still there, readable with the old key.
    NtfyDatabase old;
    QVERIFY(old.open(dir.path() + QLatin1Char('/') + aside.first(), QByteArray(32, 'a')));
    QCOMPARE(old.servers().size(), 1);
}

void TestService::rejectsInvalidInput()
{
    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    NtfyMemoryCredentialStore credentials;
    QNetworkAccessManager nam;
    NtfySubscriptionManager manager(&db, &credentials, &nam);

    QString error;
    for (const char *url : {"ftp://ntfy.sh", "not a url", "https://", ""}) {
        error.clear();
        QCOMPARE(manager.addServer(QUrl(QString::fromLatin1(url)), QString(), QString(), QString(), &error), qint64(0));
        QVERIFY2(!error.isEmpty(), url);
    }
    QVERIFY(db.servers().isEmpty());

    const qint64 server = manager.addServer(QUrl(QStringLiteral("https://ntfy.sh")), QString(), QString(), QString());
    QVERIFY(server > 0);
    const QStringList badTopics = {QString(), QStringLiteral("has space"), QString(65, QLatin1Char('x')),
                                   QString::fromUtf8("ünïcode"), QStringLiteral("a/b"), QStringLiteral("a,b")};
    for (const QString &topic : badTopics) {
        error.clear();
        QCOMPARE(manager.addSubscription(server, topic, QString(), &error), qint64(0));
        QVERIFY(!error.isEmpty());
    }
    QCOMPARE(manager.addSubscription(server + 1, QStringLiteral("ok"), QString(), &error), qint64(0));
    QVERIFY(manager.addSubscription(server, QString(64, QLatin1Char('x')), QString()) > 0);
    QVERIFY(manager.addSubscription(server, QStringLiteral("a-b_C9"), QString()) > 0);
}

void TestService::storesAndUsesCredentials()
{
    FakeNtfyServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));
    FakeNtfyServer::Response stream;
    stream.chunks << openLine() << line(QStringLiteral("m1"), 100) + line(QStringLiteral("m1"), 100)
                  << line(QStringLiteral("x1"), 101, QStringLiteral("other"));
    stream.closeAfter = false;
    fake.responses << stream;

    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    NtfyMemoryCredentialStore credentials;
    QNetworkAccessManager nam;
    NtfySubscriptionManager manager(&db, &credentials, &nam);
    manager.start();

    const qint64 server = manager.addServer(fake.url(), QStringLiteral("tk_secret"), QString(), QString());
    QVERIFY(server > 0);
    QByteArray stored;
    bool found = false;
    QVERIFY(credentials.load(ntfyCredentialKey(fake.url()), &stored, &found));
    QVERIFY(found);
    QVERIFY(stored.contains("tk_secret"));
    QTest::qWait(100);
    QVERIFY(fake.requestLines.isEmpty()); // nothing to subscribe to yet

    QSignalSpy received(&manager, &NtfySubscriptionManager::messageReceived);
    const qint64 sub = manager.addSubscription(server, QStringLiteral("t"), QString());
    QVERIFY(sub > 0);

    QTRY_COMPARE_WITH_TIMEOUT(manager.serverState(server), NtfyStream::Connected, 5000);
    // Resumes from when the subscription was made, so a restart can't skip anything.
    const QByteArray request = fake.requestLines.at(0);
    QVERIFY2(request.startsWith("GET /t/json?since=") && request.endsWith(" HTTP/1.1"), request.constData());
    const qint64 since = request.mid(18, request.indexOf(' ', 18) - 18).toLongLong();
    QVERIFY2(qAbs(QDateTime::currentMSecsSinceEpoch() / 1000 - since) < 60, request.constData());
    QCOMPARE(fake.authHeaders.at(0), QByteArray("Bearer tk_secret"));

    QTRY_COMPARE_WITH_TIMEOUT(received.count(), 1, 5000);
    QTest::qWait(200); // the duplicate and the message for an unknown topic must not show up
    QCOMPARE(received.count(), 1);
    const NtfySubscriptionRecord record = received.at(0).at(0).value<NtfySubscriptionRecord>();
    const NtfyStoredMessage stored1 = received.at(0).at(1).value<NtfyStoredMessage>();
    QCOMPARE(record.id, sub);
    QCOMPARE(record.unreadCount, 1);
    QCOMPARE(stored1.event.id, QStringLiteral("m1"));
    QCOMPARE(db.messages(sub, 10).size(), 1);
}

void TestService::resumesAfterRestart()
{
    FakeNtfyServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));
    FakeNtfyServer::Response stream;
    stream.chunks << openLine();
    stream.closeAfter = false;
    fake.responses << stream;

    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    const qint64 server = db.addServer(fake.url());
    const qint64 sub = db.addSubscription(server, QStringLiteral("t"), QString());
    db.addMessage(sub, message(QStringLiteral("m1"), 100));
    db.addMessage(sub, message(QStringLiteral("m2"), 250));

    NtfyMemoryCredentialStore credentials;
    QNetworkAccessManager nam;
    NtfySubscriptionManager manager(&db, &credentials, &nam);
    manager.start();

    QTRY_COMPARE_WITH_TIMEOUT(fake.requestLines.size(), 1, 5000);
    QCOMPARE(fake.requestLines.at(0), QByteArray("GET /t/json?since=250 HTTP/1.1"));
    QVERIFY(fake.authHeaders.at(0).isEmpty());
}

void TestService::newTopicReconnectsFromLastMessage()
{
    FakeNtfyServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));
    FakeNtfyServer::Response first;
    first.chunks << openLine() << line(QStringLiteral("m1"), 100);
    first.closeAfter = false;
    FakeNtfyServer::Response second = first;
    second.chunks = QList<QByteArray>() << openLine();
    fake.responses << first << second;

    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    NtfyMemoryCredentialStore credentials;
    QNetworkAccessManager nam;
    NtfySubscriptionManager manager(&db, &credentials, &nam);
    manager.start();
    const qint64 server = manager.addServer(fake.url(), QString(), QString(), QString());
    manager.addSubscription(server, QStringLiteral("t"), QString());

    QSignalSpy received(&manager, &NtfySubscriptionManager::messageReceived);
    QTRY_COMPARE_WITH_TIMEOUT(received.count(), 1, 5000);

    manager.addSubscription(server, QStringLiteral("a"), QString());
    QTRY_COMPARE_WITH_TIMEOUT(fake.requestLines.size(), 2, 5000);
    QCOMPARE(fake.requestLines.at(1), QByteArray("GET /a,t/json?since=m1 HTTP/1.1"));
}

void TestService::removeServerRemovesCredentials()
{
    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    NtfyMemoryCredentialStore credentials;
    QNetworkAccessManager nam;
    NtfySubscriptionManager manager(&db, &credentials, &nam);

    const QUrl url(QStringLiteral("https://ntfy.example.com"));
    const qint64 server = manager.addServer(url, QString(), QStringLiteral("phil"), QStringLiteral("pw"));
    manager.addSubscription(server, QStringLiteral("t"), QString());

    QSignalSpy changed(&manager, &NtfySubscriptionManager::subscriptionsChanged);
    QVERIFY(manager.removeServer(server));
    QCOMPARE(changed.count(), 1);
    QVERIFY(db.servers().isEmpty());
    QVERIFY(db.subscriptions().isEmpty());

    QByteArray stored;
    bool found = true;
    QVERIFY(credentials.load(ntfyCredentialKey(url), &stored, &found));
    QVERIFY(!found);
    QVERIFY(!manager.removeServer(server));
}

void TestService::replacingCredentials()
{
    FakeNtfyServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));
    FakeNtfyServer::Response stream;
    stream.chunks << openLine();
    stream.closeAfter = false;
    fake.responses << stream << stream << stream;

    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    NtfyMemoryCredentialStore credentials;
    QNetworkAccessManager nam;
    NtfySubscriptionManager manager(&db, &credentials, &nam);
    manager.start();

    const qint64 server = manager.addServer(fake.url(), QStringLiteral("tk_old"), QString(), QString());
    manager.addSubscription(server, QStringLiteral("t"), QString());
    QTRY_COMPARE_WITH_TIMEOUT(fake.requestLines.size(), 1, 5000);

    QCOMPARE(manager.addServer(fake.url(), QStringLiteral("tk_new"), QString(), QString()), server);
    QTRY_COMPARE_WITH_TIMEOUT(fake.requestLines.size(), 2, 5000);
    QCOMPARE(fake.authHeaders.at(1), QByteArray("Bearer tk_new"));

    // Re-adding without credentials makes it anonymous.
    QCOMPARE(manager.addServer(fake.url(), QString(), QString(), QString()), server);
    QTRY_COMPARE_WITH_TIMEOUT(fake.requestLines.size(), 3, 5000);
    QVERIFY(fake.authHeaders.at(2).isEmpty());
    QByteArray stored;
    bool found = true;
    QVERIFY(credentials.load(ntfyCredentialKey(fake.url()), &stored, &found));
    QVERIFY(!found);
}

void TestService::unreadableCredentialsDontConnect()
{
    FakeNtfyServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));

    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    const qint64 server = db.addServer(fake.url());
    db.addSubscription(server, QStringLiteral("t"), QString());

    FailingCredentialStore credentials;
    QNetworkAccessManager nam;
    NtfySubscriptionManager manager(&db, &credentials, &nam);
    QSignalSpy errors(&manager, &NtfySubscriptionManager::serverError);
    manager.start();

    QCOMPARE(errors.count(), 1);
    QVERIFY(errors.at(0).at(1).toString().contains(QStringLiteral("collection is locked")));
    QTest::qWait(200);
    QVERIFY(fake.requestLines.isEmpty());

    QString error;
    QCOMPARE(manager.addServer(QUrl(QStringLiteral("https://ntfy.sh")), QStringLiteral("tk_x"), QString(),
                               QString(), &error), qint64(0));
    QVERIFY(error.startsWith(QStringLiteral("could not store credentials")));
    QCOMPARE(db.servers().size(), 1); // the failed server wasn't added
}

void TestService::lastErrorUntilFixed()
{
    FakeNtfyServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));
    FakeNtfyServer::Response denied;
    denied.status = 401;
    denied.chunks << R"({"code":40101,"http":401,"error":"unauthorized"})" "\n";
    fake.responses << denied << openStream();

    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    NtfyMemoryCredentialStore credentials;
    QNetworkAccessManager nam;
    NtfySubscriptionManager manager(&db, &credentials, &nam);
    manager.start();
    const qint64 server = manager.addServer(fake.url(), QStringLiteral("tk_wrong"), QString(), QString());
    manager.addSubscription(server, QStringLiteral("t"), QString());

    QTRY_COMPARE_WITH_TIMEOUT(manager.lastError(server), QStringLiteral("HTTP 401: unauthorized"), 5000);
    QTest::qWait(1500); // it doesn't retry by itself
    QCOMPARE(fake.requestLines.size(), 1);

    // Fixing the login reconnects the stream that gave up, and clears the error.
    manager.addServer(fake.url(), QStringLiteral("tk_right"), QString(), QString());
    QTRY_COMPARE_WITH_TIMEOUT(manager.serverState(server), NtfyStream::Connected, 5000);
    QCOMPARE(fake.authHeaders.at(1), QByteArray("Bearer tk_right"));
    QVERIFY(manager.lastError(server).isEmpty());
}

void TestService::deliveryDefaultsAndPersistence()
{
    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    NtfyMemoryCredentialStore credentials;
    QNetworkAccessManager nam;
    NtfySubscriptionManager manager(&db, &credentials, &nam);
    FakeScheduler scheduler;
    NtfyDeliveryController delivery(&manager, &scheduler);

    delivery.start();
    QCOMPARE(delivery.mode(), NtfyDeliveryController::Balanced);
    QCOMPARE(delivery.interval(), 300);
    QCOMPARE(scheduler.waits, QList<int>() << 300);
    QVERIFY(manager.isStarted());

    QString error;
    QVERIFY(!delivery.setDelivery(NtfyDeliveryController::Balanced, 30, &error));
    QCOMPARE(error, QStringLiteral("interval for balanced must be one of 300, 600, 900 seconds"));
    QCOMPARE(delivery.interval(), 300);

    QSignalSpy changed(&delivery, &NtfyDeliveryController::deliveryChanged);
    QVERIFY(delivery.setDelivery(NtfyDeliveryController::BatterySaver, 3600, &error));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(scheduler.waits.last(), 3600);
    QVERIFY(!manager.isStreaming());

    // A restarted daemon picks the stored choice up again.
    NtfySubscriptionManager manager2(&db, &credentials, &nam);
    FakeScheduler scheduler2;
    NtfyDeliveryController delivery2(&manager2, &scheduler2);
    delivery2.start();
    QCOMPARE(delivery2.mode(), NtfyDeliveryController::BatterySaver);
    QCOMPARE(delivery2.interval(), 3600);
    QCOMPARE(scheduler2.waits, QList<int>() << 3600);

    // Garbage in the settings falls back to the defaults.
    db.setSetting(QStringLiteral("delivery/mode"), QStringLiteral("warp"));
    NtfySubscriptionManager manager3(&db, &credentials, &nam);
    FakeScheduler scheduler3;
    NtfyDeliveryController delivery3(&manager3, &scheduler3);
    delivery3.start();
    QCOMPARE(delivery3.mode(), NtfyDeliveryController::Balanced);
    QCOMPARE(delivery3.interval(), 300);
}

void TestService::balancedWakeupReconnects()
{
    FakeNtfyServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));
    fake.responses << openStream() << openStream();

    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    const qint64 server = db.addServer(fake.url());
    db.addSubscription(server, QStringLiteral("t"), QString());
    NtfyMemoryCredentialStore credentials;
    QNetworkAccessManager nam;
    NtfySubscriptionManager manager(&db, &credentials, &nam);
    FakeScheduler scheduler;
    NtfyDeliveryController delivery(&manager, &scheduler);
    delivery.setTimings(5000, 50);

    delivery.start();
    QTRY_COMPARE_WITH_TIMEOUT(manager.serverState(server), NtfyStream::Connected, 5000);
    QCOMPARE(scheduler.waits.size(), 1);

    scheduler.wake();
    QTRY_COMPARE_WITH_TIMEOUT(fake.requestLines.size(), 2, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(scheduler.waits.size(), 2, 5000); // back to sleep once reconnected
    QCOMPARE(scheduler.waits.last(), 300);
    QCOMPARE(manager.serverState(server), NtfyStream::Connected);
}

void TestService::instantLeavesHealthyStreamsAlone()
{
    FakeNtfyServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));
    fake.responses << openStream();

    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    const qint64 server = db.addServer(fake.url());
    db.addSubscription(server, QStringLiteral("t"), QString());
    db.setSetting(QStringLiteral("delivery/mode"), QStringLiteral("instant"));
    db.setSetting(QStringLiteral("delivery/interval"), QStringLiteral("30"));
    NtfyMemoryCredentialStore credentials;
    QNetworkAccessManager nam;
    NtfySubscriptionManager manager(&db, &credentials, &nam);
    FakeScheduler scheduler;
    NtfyDeliveryController delivery(&manager, &scheduler);
    delivery.setTimings(5000, 50);

    delivery.start();
    QCOMPARE(scheduler.waits, QList<int>() << 30);
    QTRY_COMPARE_WITH_TIMEOUT(manager.serverState(server), NtfyStream::Connected, 5000);

    scheduler.wake();
    QTRY_COMPARE_WITH_TIMEOUT(scheduler.waits.size(), 2, 5000);
    QCOMPARE(fake.requestLines.size(), 1); // the healthy stream was kept
}

void TestService::instantRevivesBrokenStreams()
{
    FakeNtfyServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));
    FakeNtfyServer::Response failure;
    failure.status = 500;
    fake.responses << failure << openStream();

    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    const qint64 server = db.addServer(fake.url());
    db.addSubscription(server, QStringLiteral("t"), QString());
    db.setSetting(QStringLiteral("delivery/mode"), QStringLiteral("instant"));
    db.setSetting(QStringLiteral("delivery/interval"), QStringLiteral("30"));
    NtfyMemoryCredentialStore credentials;
    QNetworkAccessManager nam;
    NtfySubscriptionManager manager(&db, &credentials, &nam);
    FakeScheduler scheduler;
    NtfyDeliveryController delivery(&manager, &scheduler);
    delivery.setTimings(5000, 50);

    delivery.start();
    QTRY_COMPARE_WITH_TIMEOUT(manager.serverState(server), NtfyStream::WaitingToReconnect, 5000);

    // The wake-up reconnects right away instead of waiting out the backoff.
    QElapsedTimer timer;
    timer.start();
    scheduler.wake();
    QTRY_COMPARE_WITH_TIMEOUT(fake.requestLines.size(), 2, 5000);
    QVERIFY(timer.elapsed() < 900);
    QTRY_COMPARE_WITH_TIMEOUT(scheduler.waits.size(), 2, 5000);
    QCOMPARE(manager.serverState(server), NtfyStream::Connected);
}

void TestService::batterySaverOnlyPolls()
{
    FakeNtfyServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));
    FakeNtfyServer::Response cached;
    cached.chunks << line(QStringLiteral("m1"), QDateTime::currentMSecsSinceEpoch() / 1000);
    FakeNtfyServer::Response empty;
    fake.responses << cached << empty;

    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    const qint64 server = db.addServer(fake.url());
    const qint64 sub = db.addSubscription(server, QStringLiteral("t"), QString());
    db.setSetting(QStringLiteral("delivery/mode"), QStringLiteral("battery-saver"));
    db.setSetting(QStringLiteral("delivery/interval"), QStringLiteral("900"));
    NtfyMemoryCredentialStore credentials;
    QNetworkAccessManager nam;
    NtfySubscriptionManager manager(&db, &credentials, &nam);
    FakeScheduler scheduler;
    NtfyDeliveryController delivery(&manager, &scheduler);
    delivery.setTimings(5000, 50);

    QSignalSpy received(&manager, &NtfySubscriptionManager::messageReceived);
    bool streamed = false;
    connect(&manager, &NtfySubscriptionManager::serverStateChanged, [&streamed](qint64, NtfyStream::State state) {
        if (state != NtfyStream::Disconnected)
            streamed = true;
    });
    delivery.start();
    QCOMPARE(scheduler.waits, QList<int>() << 900);
    QVERIFY(!streamed); // no stream, not even briefly while starting

    // Catches up straight away, from when the subscription was made.
    QTRY_COMPARE_WITH_TIMEOUT(received.count(), 1, 5000);
    QVERIFY(fake.requestLines.at(0).startsWith("GET /t/json?since="));
    QVERIFY(fake.requestLines.at(0).endsWith("&poll=1 HTTP/1.1"));
    QCOMPARE(db.messages(sub, 10).size(), 1);
    QCOMPARE(manager.serverState(server), NtfyStream::Disconnected); // no stream

    scheduler.wake();
    QTRY_COMPARE_WITH_TIMEOUT(scheduler.waits.size(), 2, 5000);
    QCOMPARE(fake.requestLines.size(), 2);
    QVERIFY(fake.requestLines.at(1).contains("poll=1"));
}

void TestService::wakeupIsTimeLimited()
{
    // Accepts the connection but never sends "open": the stream never becomes connected.
    FakeNtfyServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));
    FakeNtfyServer::Response silent;
    silent.closeAfter = false;
    fake.responses << silent << silent;

    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    const qint64 server = db.addServer(fake.url());
    db.addSubscription(server, QStringLiteral("t"), QString());
    NtfyMemoryCredentialStore credentials;
    QNetworkAccessManager nam;
    NtfySubscriptionManager manager(&db, &credentials, &nam);
    FakeScheduler scheduler;
    NtfyDeliveryController delivery(&manager, &scheduler);
    delivery.setTimings(300, 50);
    delivery.start();

    scheduler.wake();
    QTest::qWait(100);
    QCOMPARE(scheduler.waits.size(), 1); // still awake, waiting for the stream
    QTRY_COMPARE_WITH_TIMEOUT(scheduler.waits.size(), 2, 2000); // gave up and went back to sleep
}

void TestService::deliveryStats()
{
    FakeNtfyServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));
    const qint64 now = QDateTime::currentMSecsSinceEpoch() / 1000;
    fake.responses << openStream(QList<QByteArray>() << line(QStringLiteral("m1"), now - 10)
                                                     << line(QStringLiteral("m2"), now - 2));

    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    const qint64 server = db.addServer(fake.url());
    db.addSubscription(server, QStringLiteral("t"), QString());
    NtfyMemoryCredentialStore credentials;
    QNetworkAccessManager nam;
    NtfySubscriptionManager manager(&db, &credentials, &nam);
    FakeScheduler scheduler;
    NtfyDeliveryController delivery(&manager, &scheduler);
    delivery.setTimings(5000, 50);
    delivery.start();

    QTRY_COMPARE_WITH_TIMEOUT(delivery.stats().messages, 2, 5000);
    QVERIFY(delivery.stats().maxDelay >= 10 && delivery.stats().maxDelay <= 12);
    QVERIFY(delivery.stats().totalDelay >= 12 && delivery.stats().totalDelay <= 16);
    QCOMPARE(delivery.stats().wakeups, 0);
    scheduler.wake();
    QCOMPARE(delivery.stats().wakeups, 1);
    QVERIFY(delivery.stats().lastWakeup >= now);
}

NtfyAction httpAction(const QUrl &url, const QString &method = QString())
{
    NtfyAction action;
    action.action = QStringLiteral("http");
    action.label = QStringLiteral("Open garage");
    action.url = url.toString();
    action.method = method;
    action.body = QStringLiteral("{\"door\":\"open\"}");
    action.headers.insert(QStringLiteral("X-Auth"), QStringLiteral("garage-key"));
    return action;
}

void TestService::actionRunnerSendsRequest()
{
    FakeNtfyServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));
    FakeNtfyServer::Response ok;
    ok.status = 204;
    fake.responses << ok;

    QNetworkAccessManager nam;
    NtfyActionRunner runner(&nam);
    bool done = false, success = false;
    int status = 0;
    runner.run(httpAction(QUrl(fake.url().toString() + QStringLiteral("/door"))), [&](bool o, int s, const QString &) {
        done = true;
        success = o;
        status = s;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY(success);
    QCOMPARE(status, 204);
    QCOMPARE(fake.requestLines.at(0), QByteArray("POST /door HTTP/1.1")); // ntfy's default method
    QVERIFY(fake.requests.at(0).contains("X-Auth: garage-key"));
    QVERIFY(fake.requests.at(0).endsWith("{\"door\":\"open\"}"));
    QVERIFY(fake.authHeaders.at(0).isEmpty()); // our ntfy credentials stay out of it
}

void TestService::actionRunnerRefuses()
{
    QString error;
    NtfyAction view;
    view.action = QStringLiteral("view");
    view.url = QStringLiteral("https://example.com");
    QVERIFY(!NtfyActionRunner::canRun(view, &error));
    QVERIFY(!NtfyActionRunner::canRun(httpAction(QUrl(QStringLiteral("ftp://example.com/x"))), &error));
    QVERIFY(!NtfyActionRunner::canRun(httpAction(QUrl(QStringLiteral("file:///etc/passwd"))), &error));
    QVERIFY(!NtfyActionRunner::canRun(httpAction(QUrl(QStringLiteral("https://"))), &error));
    QVERIFY(NtfyActionRunner::canRun(httpAction(QUrl(QStringLiteral("http://192.168.1.1/x"))), &error));

    QNetworkAccessManager nam;
    NtfyActionRunner runner(&nam);
    bool done = false, success = true;
    runner.run(view, [&](bool o, int, const QString &) { done = true; success = o; });
    QVERIFY(done); // refused right away
    QVERIFY(!success);
}

void TestService::actionRunnerReportsFailures()
{
    FakeNtfyServer elsewhere;
    QVERIFY(elsewhere.listen(QHostAddress::LocalHost));
    FakeNtfyServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));
    FakeNtfyServer::Response failure;
    failure.status = 500;
    FakeNtfyServer::Response redirect;
    redirect.status = 302;
    redirect.headers = "Location: " + elsewhere.url().toEncoded() + "/\r\n";
    FakeNtfyServer::Response silent;
    silent.silent = true;
    fake.responses << failure << redirect << silent;

    QNetworkAccessManager nam;
    NtfyActionRunner runner(&nam);
    runner.setTimeout(300);
    QList<QPair<bool, QString>> results;
    auto record = [&](bool o, int, const QString &error) { results.append(qMakePair(o, error)); };
    runner.run(httpAction(fake.url(), QStringLiteral("put")), record);
    QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 5000);
    runner.run(httpAction(fake.url()), record);
    QTRY_COMPARE_WITH_TIMEOUT(results.size(), 2, 5000);
    runner.run(httpAction(fake.url()), record);
    QTRY_COMPARE_WITH_TIMEOUT(results.size(), 3, 5000);

    QCOMPARE(fake.requestLines.at(0), QByteArray("PUT / HTTP/1.1"));
    QCOMPARE(results.at(0), qMakePair(false, QStringLiteral("HTTP 500")));
    QCOMPARE(results.at(1), qMakePair(false, QStringLiteral("HTTP 302")));
    QVERIFY(elsewhere.requestLines.isEmpty()); // redirect not followed
    QCOMPARE(results.at(2), qMakePair(false, QStringLiteral("timed out")));
}

void TestService::messageLookup()
{
    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    const qint64 server = db.addServer(QUrl(QStringLiteral("https://ntfy.sh")));
    const qint64 sub = db.addSubscription(server, QStringLiteral("t"), QString());
    const qint64 row = db.addMessage(sub, message(QStringLiteral("m1"), 100));

    const NtfyStoredMessage found = db.message(row);
    QVERIFY(found.isValid());
    QCOMPARE(found.subscriptionId, sub);
    QCOMPARE(found.event.id, QStringLiteral("m1"));
    QVERIFY(!db.message(row + 1).isValid());
}

void TestService::dbusService()
{
    if (!QDBusConnection::sessionBus().isConnected())
        QSKIP("no session bus");

    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    NtfyMemoryCredentialStore credentials;
    QNetworkAccessManager nam;
    NtfySubscriptionManager manager(&db, &credentials, &nam);
    NtfyDBusService service(&manager);
    QString error;
    QVERIFY2(service.registerOn(QDBusConnection::sessionBus(), &error), qPrintable(error));

    // A second daemon (its own connection) can't take over.
    {
        QDBusConnection other = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("second"));
        NtfyDBusService second(&manager);
        QVERIFY(!second.registerOn(other, &error));
        QVERIFY2(error.contains(QStringLiteral("already running")), qPrintable(error));
    }
    QDBusConnection::disconnectFromBus(QStringLiteral("second"));

    QDBusConnection client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("client"));
    QVERIFY(client.isConnected());

    QDBusMessage reply = callAsync(client, QStringLiteral("AddServer"),
                                   QVariantList() << QStringLiteral("ftp://nope") << QString() << QString() << QString());
    QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
    QCOMPARE(reply.errorMessage(), QStringLiteral("not a valid http(s) server URL"));

    reply = callAsync(client, QStringLiteral("AddServer"),
                      QVariantList() << QStringLiteral("https://ntfy.example.com") << QStringLiteral("tk_secret")
                                     << QString() << QString());
    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    const qlonglong server = reply.arguments().at(0).toLongLong();
    QVERIFY(server > 0);

    reply = callAsync(client, QStringLiteral("AddSubscription"),
                      QVariantList() << server << QStringLiteral("bad topic") << QString());
    QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
    reply = callAsync(client, QStringLiteral("AddSubscription"),
                      QVariantList() << server << QStringLiteral("alerts") << QStringLiteral("Alerts"));
    const qlonglong sub = reply.arguments().at(0).toLongLong();
    QVERIFY(sub > 0);
    db.addMessage(sub, message(QStringLiteral("m1"), 100, QStringLiteral("alerts")));

    reply = callAsync(client, QStringLiteral("Servers"));
    const QString servers = reply.arguments().at(0).toString();
    QVERIFY(servers.contains(QStringLiteral("https://ntfy.example.com")));
    QVERIFY(!servers.contains(QStringLiteral("tk_secret"))); // credentials never come back out

    reply = callAsync(client, QStringLiteral("Subscriptions"));
    const QJsonArray subscriptions = QJsonDocument::fromJson(reply.arguments().at(0).toString().toUtf8()).array();
    QCOMPARE(subscriptions.size(), 1);
    const QJsonObject subscription = subscriptions.at(0).toObject();
    QCOMPARE(subscription.value(QStringLiteral("topic")).toString(), QStringLiteral("alerts"));
    QCOMPARE(subscription.value(QStringLiteral("displayName")).toString(), QStringLiteral("Alerts"));
    QCOMPARE(subscription.value(QStringLiteral("unread")).toInt(), 1);
    QCOMPARE(subscription.value(QStringLiteral("lastMessage")).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("m1"));

    reply = callAsync(client, QStringLiteral("Messages"), QVariantList() << sub << 50 << qlonglong(0));
    const QJsonArray messages = QJsonDocument::fromJson(reply.arguments().at(0).toString().toUtf8()).array();
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages.at(0).toObject().value(QStringLiteral("event")).toObject()
             .value(QStringLiteral("message")).toString(), QStringLiteral("m m1"));

    FakeScheduler scheduler;
    NtfyDeliveryController delivery(&manager, &scheduler);
    service.setDeliveryController(&delivery);
    reply = callAsync(client, QStringLiteral("SetDelivery"), QVariantList() << QStringLiteral("warp") << 30);
    QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
    reply = callAsync(client, QStringLiteral("SetDelivery"), QVariantList() << QStringLiteral("instant") << 900);
    QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
    reply = callAsync(client, QStringLiteral("SetDelivery"), QVariantList() << QStringLiteral("instant") << 30);
    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    reply = callAsync(client, QStringLiteral("Delivery"));
    const QJsonObject deliveryJson = QJsonDocument::fromJson(reply.arguments().at(0).toString().toUtf8()).object();
    QCOMPARE(deliveryJson.value(QStringLiteral("mode")).toString(), QStringLiteral("instant"));
    QCOMPARE(deliveryJson.value(QStringLiteral("interval")).toInt(), 30);
    QCOMPARE(deliveryJson.value(QStringLiteral("options")).toObject().value(QStringLiteral("battery-saver")).toArray().size(), 3);
    QCOMPARE(deliveryJson.value(QStringLiteral("defaults")).toObject().value(QStringLiteral("balanced")).toInt(), 300);
    reply = callAsync(client, QStringLiteral("DeliveryStats"));
    QVERIFY(reply.arguments().at(0).toString().contains(QStringLiteral("\"wakeups\":0")));

    QSignalSpy activated(&service, &NtfyDBusService::notificationActivated);
    QSignalSpy read(&manager, &NtfySubscriptionManager::subscriptionRead);
    reply = callAsync(client, QStringLiteral("NotificationActivated"), QVariantList() << sub);
    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    QCOMPARE(activated.count(), 1);
    QCOMPARE(db.subscription(sub).unreadCount, 0);
    QCOMPARE(read.count(), 1); // its notifications go
    QCOMPARE(read.at(0).at(0).toLongLong(), qint64(sub));

    reply = callAsync(client, QStringLiteral("RemoveServer"), QVariantList() << server);
    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    QVERIFY(db.servers().isEmpty());
    bool found = true;
    QByteArray stored;
    credentials.load(ntfyCredentialKey(QUrl(QStringLiteral("https://ntfy.example.com"))), &stored, &found);
    QVERIFY(!found);

    QDBusConnection::disconnectFromBus(QStringLiteral("client"));
    QDBusConnection::sessionBus().unregisterService(QString::fromLatin1(NtfyDBus::ServiceName));
    QDBusConnection::sessionBus().unregisterObject(QString::fromLatin1(NtfyDBus::ObjectPath));
}

void TestService::dbusRunAction()
{
    if (!QDBusConnection::sessionBus().isConnected())
        QSKIP("no session bus");

    FakeNtfyServer target;
    QVERIFY(target.listen(QHostAddress::LocalHost));
    FakeNtfyServer::Response ok;
    ok.status = 200;
    target.responses << ok;

    NtfyDatabase db;
    QVERIFY(db.open(QStringLiteral(":memory:")));
    const qint64 server = db.addServer(QUrl(QStringLiteral("https://ntfy.sh")));
    const qint64 sub = db.addSubscription(server, QStringLiteral("t"), QString());
    NtfyEvent event = message(QStringLiteral("m1"), 100);
    NtfyAction view;
    view.action = QStringLiteral("view");
    view.label = QStringLiteral("Open");
    view.url = QStringLiteral("https://example.com");
    event.actions << view << httpAction(QUrl(target.url().toString() + QStringLiteral("/hook")));
    const qint64 row = db.addMessage(sub, event);

    NtfyMemoryCredentialStore credentials;
    QNetworkAccessManager nam;
    NtfySubscriptionManager manager(&db, &credentials, &nam);
    NtfyDBusService service(&manager);
    NtfyActionRunner actions(&nam);
    service.setActionRunner(&actions);
    QString error;
    QVERIFY2(service.registerOn(QDBusConnection::sessionBus(), &error), qPrintable(error));
    QDBusConnection client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("client"));

    QDBusMessage reply = callAsync(client, QStringLiteral("RunAction"), QVariantList() << qlonglong(row) << 1);
    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    QCOMPARE(reply.arguments().at(0).toInt(), 200);
    QCOMPARE(target.requestLines.at(0), QByteArray("POST /hook HTTP/1.1"));

    reply = callAsync(client, QStringLiteral("RunAction"), QVariantList() << qlonglong(row) << 0);
    QCOMPARE(reply.type(), QDBusMessage::ErrorMessage); // "view" is for the UI
    reply = callAsync(client, QStringLiteral("RunAction"), QVariantList() << qlonglong(row) << 2);
    QCOMPARE(reply.errorMessage(), QStringLiteral("no such action"));
    reply = callAsync(client, QStringLiteral("RunAction"), QVariantList() << qlonglong(row + 1) << 0);
    QCOMPARE(reply.errorMessage(), QStringLiteral("no such action"));

    QDBusConnection::disconnectFromBus(QStringLiteral("client"));
    QDBusConnection::sessionBus().unregisterService(QString::fromLatin1(NtfyDBus::ServiceName));
    QDBusConnection::sessionBus().unregisterObject(QString::fromLatin1(NtfyDBus::ObjectPath));
}

QTEST_GUILESS_MAIN(TestService)
#include "tst_service.moc"
