// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfycredentials.h"
#include "ntfydaemonclient.h"
#include "ntfydatabase.h"
#include "ntfydbusservice.h"
#include "ntfymessagemodel.h"
#include "ntfysubscriptionmanager.h"
#include "ntfysubscriptionmodel.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QtTest>

namespace {

NtfyEvent message(const QString &id, qint64 time, const QString &title = QString())
{
    QJsonObject obj;
    obj.insert(QStringLiteral("event"), QStringLiteral("message"));
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("time"), double(time));
    obj.insert(QStringLiteral("topic"), QStringLiteral("alerts"));
    obj.insert(QStringLiteral("message"), QStringLiteral("body ") + id);
    if (!title.isEmpty())
        obj.insert(QStringLiteral("title"), title);
    obj.insert(QStringLiteral("tags"), QJsonArray::fromStringList(QStringList() << QStringLiteral("warning")));
    return NtfyEvent::fromJson(QJsonDocument(obj).toJson());
}

QString storedJson(qint64 rowId, const NtfyEvent &event)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), double(rowId));
    obj.insert(QStringLiteral("read"), false);
    obj.insert(QStringLiteral("event"), QJsonDocument::fromJson(event.toJson()).object());
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

} // namespace

// Runs the daemon's D-Bus service in-process (without network) and talks to it from a separate
// connection, the way the UI does.
class TestClient : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void subscribeAndList();
    void subscribeErrors();
    void subscriptionModelTracksChanges();
    void messageModelLoadsAndPages();
    void messageModelAddsNewMessages();
    void messageModelAttachments();
    void serversAndLogins();

private:
    qint64 addSubscription(const QString &url = QStringLiteral("https://ntfy.sh"));

    NtfyDatabase *m_db = nullptr;
    NtfyMemoryCredentialStore *m_credentials = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    NtfySubscriptionManager *m_manager = nullptr;
    NtfyDBusService *m_service = nullptr;
    NtfyDaemonClient *m_client = nullptr;
};

void TestClient::initTestCase()
{
    if (!QDBusConnection::sessionBus().isConnected())
        QSKIP("no session bus");
}

void TestClient::init()
{
    m_db = new NtfyDatabase;
    QVERIFY(m_db->open(QStringLiteral(":memory:")));
    m_credentials = new NtfyMemoryCredentialStore;
    m_nam = new QNetworkAccessManager;
    m_manager = new NtfySubscriptionManager(m_db, m_credentials, m_nam);
    m_service = new NtfyDBusService(m_manager);
    QString error;
    QVERIFY2(m_service->registerOn(QDBusConnection::sessionBus(), &error), qPrintable(error));
    m_client = new NtfyDaemonClient(QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("ui")));
}

void TestClient::cleanup()
{
    delete m_client;
    QDBusConnection::disconnectFromBus(QStringLiteral("ui"));
    QDBusConnection::sessionBus().unregisterService(QString::fromLatin1(NtfyDBus::ServiceName));
    QDBusConnection::sessionBus().unregisterObject(QString::fromLatin1(NtfyDBus::ObjectPath));
    delete m_service;
    delete m_manager;
    delete m_nam;
    delete m_credentials;
    delete m_db;
}

qint64 TestClient::addSubscription(const QString &url)
{
    const qint64 server = m_manager->addServer(QUrl(url), QString(), QString(), QString());
    return m_manager->addSubscription(server, QStringLiteral("alerts"), QStringLiteral("Alerts"));
}

void TestClient::subscribeAndList()
{
    QSignalSpy subscribed(m_client, &NtfyDaemonClient::subscribed);
    QSignalSpy failed(m_client, &NtfyDaemonClient::failed);
    m_client->subscribe(QStringLiteral("https://ntfy.example.com:8443/ntfy"), QStringLiteral("alerts"),
                        QString(), QStringLiteral("tk_secret"), QString(), QString());
    QTRY_COMPARE_WITH_TIMEOUT(subscribed.count(), 1, 5000);
    QCOMPARE(failed.count(), 0);
    const qlonglong id = subscribed.at(0).at(0).toLongLong();
    QVERIFY(id > 0);

    // The credentials went into the store, not the database.
    QByteArray stored;
    bool found = false;
    m_credentials->load(ntfyCredentialKey(QUrl(QStringLiteral("https://ntfy.example.com:8443/ntfy"))), &stored, &found);
    QVERIFY(found && stored.contains("tk_secret"));

    NtfySubscriptionModel model;
    model.setClient(m_client);
    QTRY_VERIFY_WITH_TIMEOUT(model.isLoaded(), 5000);
    QCOMPARE(model.rowCount(), 1);
    const QModelIndex row = model.index(0);
    QCOMPARE(model.data(row, NtfySubscriptionModel::SubscriptionIdRole).toLongLong(), id);
    QCOMPARE(model.data(row, NtfySubscriptionModel::TitleRole).toString(), QStringLiteral("alerts"));
    QCOMPARE(model.data(row, NtfySubscriptionModel::ServerNameRole).toString(), QStringLiteral("ntfy.example.com:8443/ntfy"));
    QCOMPARE(model.indexOf(id), 0);
    QCOMPARE(model.get(0).value(QStringLiteral("topic")).toString(), QStringLiteral("alerts"));

    // Subscribing again on the same server reuses it, and keeps its credentials.
    m_client->subscribe(QStringLiteral("https://ntfy.example.com:8443/ntfy/"), QStringLiteral("backups"),
                        QStringLiteral("Backups"), QString(), QString(), QString());
    QTRY_COMPARE_WITH_TIMEOUT(subscribed.count(), 2, 5000);
    QCOMPARE(m_db->servers().size(), 1);
    found = false;
    m_credentials->load(ntfyCredentialKey(QUrl(QStringLiteral("https://ntfy.example.com:8443/ntfy"))), &stored, &found);
    QVERIFY(found && stored.contains("tk_secret"));
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 2, 5000);
    QCOMPARE(model.data(model.index(1), NtfySubscriptionModel::TitleRole).toString(), QStringLiteral("Backups"));
}

void TestClient::subscribeErrors()
{
    QSignalSpy subscribed(m_client, &NtfyDaemonClient::subscribed);
    QSignalSpy failed(m_client, &NtfyDaemonClient::failed);

    m_client->subscribe(QStringLiteral("ftp://nope"), QStringLiteral("alerts"), QString(), QString(), QString(), QString());
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
    QCOMPARE(failed.at(0).at(0).toString(), QStringLiteral("not a valid http(s) server URL"));

    m_client->subscribe(QStringLiteral("https://ntfy.sh"), QStringLiteral("no spaces"), QString(), QString(), QString(), QString());
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 2, 5000);
    QVERIFY(failed.at(1).at(0).toString().startsWith(QStringLiteral("topics are")));
    QCOMPARE(subscribed.count(), 0);
}

void TestClient::subscriptionModelTracksChanges()
{
    const qint64 sub = addSubscription(QStringLiteral("http://192.168.1.5:8080"));
    m_db->addMessage(sub, message(QStringLiteral("m1"), 100, QStringLiteral("Disk")));
    m_db->addMessage(sub, message(QStringLiteral("m2"), 200));

    NtfySubscriptionModel model;
    model.setClient(m_client);
    QTRY_VERIFY_WITH_TIMEOUT(model.isLoaded(), 5000);
    const QModelIndex row = model.index(0);
    QCOMPARE(model.data(row, NtfySubscriptionModel::TitleRole).toString(), QStringLiteral("Alerts"));
    QCOMPARE(model.data(row, NtfySubscriptionModel::ServerNameRole).toString(), QStringLiteral("http://192.168.1.5:8080"));
    QCOMPARE(model.data(row, NtfySubscriptionModel::UnreadRole).toInt(), 2);
    QCOMPARE(model.data(row, NtfySubscriptionModel::LastMessageRole).toString(), QStringLiteral("body m2"));
    QCOMPARE(model.data(row, NtfySubscriptionModel::LastTimeRole).toDateTime().toMSecsSinceEpoch(), qint64(200000));
    QCOMPARE(model.totalUnread(), 2);

    QSignalSpy unreadChanged(&model, &NtfySubscriptionModel::totalUnreadChanged);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    m_client->markRead(sub);
    QTRY_COMPARE_WITH_TIMEOUT(model.totalUnread(), 0, 5000);
    QCOMPARE(unreadChanged.count(), 1);
    QCOMPARE(reset.count(), 0); // updated in place

    // Muted subscriptions don't count towards the total.
    m_db->addMessage(sub, message(QStringLiteral("m3"), 300, QStringLiteral("Hot")));
    m_client->setMuted(sub, true);
    QTRY_VERIFY_WITH_TIMEOUT(model.data(model.index(0), NtfySubscriptionModel::MutedRole).toBool(), 5000);
    QCOMPARE(model.data(model.index(0), NtfySubscriptionModel::UnreadRole).toInt(), 1);
    QCOMPARE(model.totalUnread(), 0);
    QCOMPARE(model.data(model.index(0), NtfySubscriptionModel::LastMessageRole).toString(),
             QString::fromUtf8("⚠️ Hot: body m3"));

    m_client->unsubscribe(sub);
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 0, 5000);
}

void TestClient::messageModelLoadsAndPages()
{
    const qint64 sub = addSubscription();
    for (int i = 1; i <= 60; ++i)
        m_db->addMessage(sub, message(QStringLiteral("m%1").arg(i), i, i == 60 ? QStringLiteral("Last") : QString()));

    NtfyMessageModel model;
    model.setClient(m_client);
    model.setSubscriptionId(sub);
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), NtfyMessageModel::PageSize, 5000);
    QVERIFY(!model.isLoading());
    QVERIFY(model.canLoadMore());

    const QModelIndex newest = model.index(0);
    QCOMPARE(model.data(newest, NtfyMessageModel::MessageIdRole).toString(), QStringLiteral("m60"));
    QCOMPARE(model.data(newest, NtfyMessageModel::TitleRole).toString(), QString::fromUtf8("⚠️ Last"));
    QCOMPARE(model.data(newest, NtfyMessageModel::BodyRole).toString(), QStringLiteral("body m60"));
    QCOMPARE(model.data(model.index(1), NtfyMessageModel::TitleRole).toString(), QString()); // no title, no topic
    QCOMPARE(model.data(newest, NtfyMessageModel::PriorityRole).toInt(), 3);

    model.loadMore();
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 60, 5000);
    QVERIFY(!model.canLoadMore());
    QCOMPARE(model.data(model.index(59), NtfyMessageModel::MessageIdRole).toString(), QStringLiteral("m1"));
}

void TestClient::messageModelAddsNewMessages()
{
    const qint64 sub = addSubscription();
    const qint64 first = m_db->addMessage(sub, message(QStringLiteral("m1"), 100));

    NtfyMessageModel model;
    model.setClient(m_client);
    model.setSubscriptionId(sub);
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 1, 5000);

    const NtfyEvent next = message(QStringLiteral("m2"), 200);
    const qint64 second = m_db->addMessage(sub, next);
    emit m_service->MessageReceived(sub, storedJson(second, next));
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 2, 5000);
    QCOMPARE(model.data(model.index(0), NtfyMessageModel::MessageIdRole).toString(), QStringLiteral("m2"));

    // Duplicates and other subscriptions' messages are ignored.
    emit m_service->MessageReceived(sub, storedJson(second, next));
    emit m_service->MessageReceived(sub + 1, storedJson(second + 1, message(QStringLiteral("x"), 300)));
    emit m_service->MessageReceived(sub, storedJson(first, message(QStringLiteral("m1"), 100)));
    QTest::qWait(200);
    QCOMPARE(model.rowCount(), 2);
}

void TestClient::messageModelAttachments()
{
    const qint64 sub = addSubscription();
    const qint64 now = QDateTime::currentMSecsSinceEpoch() / 1000;
    for (const auto &spec : {qMakePair(QStringLiteral("fresh"), now + 3600), qMakePair(QStringLiteral("old"), now - 60)}) {
        NtfyEvent event = message(spec.first, 100);
        event.attachment.name = spec.first + QStringLiteral(".png");
        event.attachment.url = QStringLiteral("https://ntfy.sh/file/") + spec.first + QStringLiteral(".png");
        event.attachment.size = 6000;
        event.attachment.expires = spec.second;
        m_db->addMessage(sub, event);
    }

    NtfyMessageModel model;
    model.setClient(m_client);
    model.setSubscriptionId(sub);
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 2, 5000);
    QCOMPARE(model.data(model.index(0), NtfyMessageModel::AttachmentNameRole).toString(), QStringLiteral("old.png"));
    QVERIFY(model.data(model.index(0), NtfyMessageModel::AttachmentExpiredRole).toBool());
    QCOMPARE(model.data(model.index(1), NtfyMessageModel::AttachmentSizeRole).toLongLong(), qint64(6000));
    QVERIFY(!model.data(model.index(1), NtfyMessageModel::AttachmentExpiredRole).toBool());
}

void TestClient::serversAndLogins()
{
    addSubscription(QStringLiteral("https://ntfy.example.com"));
    const QUrl url(QStringLiteral("https://ntfy.example.com"));
    QByteArray stored;
    bool found = false;

    QSignalSpy servers(m_client, &NtfyDaemonClient::serversLoaded);
    m_client->fetchServers();
    QTRY_COMPARE_WITH_TIMEOUT(servers.count(), 1, 5000);
    const QVariantList list = servers.at(0).at(0).toList();
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.at(0).toMap().value(QStringLiteral("url")).toString(), QStringLiteral("https://ntfy.example.com"));
    QCOMPARE(list.at(0).toMap().value(QStringLiteral("state")).toString(), QStringLiteral("disconnected"));

    QSignalSpy updated(m_client, &NtfyDaemonClient::serverUpdated);
    m_client->updateServer(url.toString(), QString(), QStringLiteral("phil"), QStringLiteral("pw"));
    QTRY_COMPARE_WITH_TIMEOUT(updated.count(), 1, 5000);
    m_credentials->load(ntfyCredentialKey(url), &stored, &found);
    QVERIFY(found && stored.contains("phil"));

    // An explicit update without credentials removes the login.
    m_client->updateServer(url.toString(), QString(), QString(), QString());
    QTRY_COMPARE_WITH_TIMEOUT(updated.count(), 2, 5000);
    m_credentials->load(ntfyCredentialKey(url), &stored, &found);
    QVERIFY(!found);

    const qlonglong serverId = list.at(0).toMap().value(QStringLiteral("id")).toLongLong();
    QSignalSpy changed(m_client, &NtfyDaemonClient::subscriptionsChanged);
    m_client->removeServer(serverId);
    QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 5000);
    QVERIFY(m_db->servers().isEmpty());
}

QTEST_GUILESS_MAIN(TestClient)
#include "tst_client.moc"
