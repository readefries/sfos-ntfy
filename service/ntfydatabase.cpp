// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfydatabase.h"

#include "ntfyserver.h"

#include <QAtomicInt>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

namespace {

const int SchemaVersion = 2;

QAtomicInt connectionCounter;

QSqlDatabase connection(const QString &name)
{
    return QSqlDatabase::database(name, false);
}

} // namespace

NtfyDatabase::NtfyDatabase()
    : m_connectionName(QStringLiteral("ntfy-%1").arg(connectionCounter.fetchAndAddRelaxed(1)))
{
}

NtfyDatabase::~NtfyDatabase()
{
    close();
}

namespace {

// "x'<hex>'": a raw 256-bit key, so SQLCipher skips its passphrase key derivation.
QString rawKeyLiteral(const QByteArray &key)
{
    return QStringLiteral("\"x'%1'\"").arg(QString::fromLatin1(key.toHex()));
}

QString sqlString(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

} // namespace

bool NtfyDatabase::isPlaintextFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return file.read(16) == QByteArray("SQLite format 3\0", 16);
}

bool NtfyDatabase::isEncryptionAvailable()
{
    return QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLCIPHER"));
}

bool NtfyDatabase::encryptFile(const QString &path, const QByteArray &key, QString *error)
{
    const QString connectionName = QStringLiteral("ntfy-encrypt-%1").arg(connectionCounter.fetchAndAddRelaxed(1));
    const QString encryptedPath = path + QStringLiteral(".encrypting");
    for (const QString &leftover : {encryptedPath, encryptedPath + QStringLiteral("-wal"),
                                    encryptedPath + QStringLiteral("-shm")})
        QFile::remove(leftover);

    QString failure;
    {
        // SQLCipher without a key reads plaintext databases.
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLCIPHER"), connectionName);
        db.setDatabaseName(path);
        if (!db.open()) {
            failure = db.lastError().text();
        } else {
            QSqlQuery query(db);
            int version = 0;
            if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next())
                version = query.value(0).toInt();
            const QStringList steps = {
                QStringLiteral("ATTACH DATABASE %1 AS encrypted KEY %2").arg(sqlString(encryptedPath), rawKeyLiteral(key)),
                QStringLiteral("SELECT sqlcipher_export('encrypted')"),
                QStringLiteral("PRAGMA encrypted.user_version = %1").arg(version),
                QStringLiteral("DETACH DATABASE encrypted"),
            };
            for (const QString &step : steps) {
                if (!query.exec(step)) {
                    failure = query.lastError().text();
                    break;
                }
            }
            query.finish();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);

    if (failure.isEmpty()) {
        // Swap the files, then drop the plaintext copy. (On flash storage that doesn't guarantee
        // the old blocks are gone, but it's what we can do; home encryption covers the rest.)
        const QString plaintextPath = path + QStringLiteral(".plaintext");
        QFile::remove(plaintextPath);
        if (!QFile::rename(path, plaintextPath) || !QFile::rename(encryptedPath, path)) {
            failure = QStringLiteral("could not replace the database file");
            QFile::rename(plaintextPath, path);
        } else {
            QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
            for (const QString &old : {plaintextPath, plaintextPath + QStringLiteral("-wal"),
                                       plaintextPath + QStringLiteral("-shm"), path + QStringLiteral("-wal"),
                                       path + QStringLiteral("-shm")})
                QFile::remove(old);
        }
    }

    if (!failure.isEmpty()) {
        QFile::remove(encryptedPath);
        if (error)
            *error = QStringLiteral("encrypt database: ") + failure;
        return false;
    }
    return true;
}

bool NtfyDatabase::open(const QString &path, const QByteArray &key)
{
    close();

    const QString driver = key.isEmpty() ? QStringLiteral("QSQLITE") : QStringLiteral("QSQLCIPHER");
    if (!QSqlDatabase::isDriverAvailable(driver))
        return fail(QStringLiteral("open"), QStringLiteral("SQL driver %1 is not installed").arg(driver));

    const bool inMemory = path == QLatin1String(":memory:");
    if (!inMemory) {
        const QFileInfo info(path);
        QDir dir = info.absoluteDir();
        if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
            return fail(QStringLiteral("create directory"), dir.absolutePath());
        QFile::setPermissions(dir.absolutePath(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

        // Create the file ourselves so it never exists with default permissions.
        if (!info.exists()) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly))
                return fail(QStringLiteral("create database"), file.errorString());
        }
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    }

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(driver, m_connectionName);
        db.setDatabaseName(path);
        if (!db.open()) {
            const QString error = db.lastError().text();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(m_connectionName);
            return fail(QStringLiteral("open"), error);
        }
    }

    if (!key.isEmpty()) {
        // Must come first. A wrong key only shows when something gets read.
        bool readable = exec(QStringLiteral("PRAGMA key = %1").arg(rawKeyLiteral(key)));
        if (readable) {
            QSqlQuery check(connection(m_connectionName));
            readable = check.exec(QStringLiteral("SELECT count(*) FROM sqlite_master"));
        }
        if (!readable) {
            close();
            return fail(QStringLiteral("open"), QStringLiteral("wrong key, or not an encrypted database"));
        }
    }

    if (!exec(QStringLiteral("PRAGMA foreign_keys = ON")) || !migrate()) {
        const QString error = m_error;
        close();
        m_error = error;
        return false;
    }
    if (!inMemory)
        exec(QStringLiteral("PRAGMA journal_mode = WAL"));

    m_error.clear();
    return true;
}

void NtfyDatabase::close()
{
    if (!QSqlDatabase::contains(m_connectionName))
        return;
    {
        QSqlDatabase db = connection(m_connectionName);
        db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool NtfyDatabase::isOpen() const
{
    return QSqlDatabase::contains(m_connectionName) && connection(m_connectionName).isOpen();
}

bool NtfyDatabase::fail(const QString &context, const QString &error) const
{
    m_error = context + QStringLiteral(": ") + error;
    return false;
}

bool NtfyDatabase::exec(const QString &sql)
{
    QSqlQuery query(connection(m_connectionName));
    if (!query.exec(sql))
        return fail(sql.left(40), query.lastError().text());
    return true;
}

bool NtfyDatabase::migrate()
{
    QSqlQuery versionQuery(connection(m_connectionName));
    if (!versionQuery.exec(QStringLiteral("PRAGMA user_version")) || !versionQuery.next())
        return fail(QStringLiteral("read schema version"), versionQuery.lastError().text());
    const int version = versionQuery.value(0).toInt();
    versionQuery.finish();

    if (version > SchemaVersion)
        return fail(QStringLiteral("migrate"),
                    QStringLiteral("database is from a newer version (%1)").arg(version));
    if (version == SchemaVersion)
        return true;

    QSqlDatabase db = connection(m_connectionName);
    db.transaction();
    bool ok = true;
    if (version < 1) {
        ok = exec(QStringLiteral(
                     "CREATE TABLE servers ("
                     " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                     " url TEXT NOT NULL UNIQUE,"
                     " created INTEGER NOT NULL DEFAULT (strftime('%s','now')))"))
            && exec(QStringLiteral(
                        "CREATE TABLE subscriptions ("
                        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                        " server_id INTEGER NOT NULL REFERENCES servers(id) ON DELETE CASCADE,"
                        " topic TEXT NOT NULL,"
                        " display_name TEXT NOT NULL DEFAULT '',"
                        " muted INTEGER NOT NULL DEFAULT 0,"
                        " created INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
                        " UNIQUE (server_id, topic))"))
            && exec(QStringLiteral(
                        "CREATE TABLE messages ("
                        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                        " subscription_id INTEGER NOT NULL REFERENCES subscriptions(id) ON DELETE CASCADE,"
                        " message_id TEXT NOT NULL,"
                        " time INTEGER NOT NULL,"
                        " read INTEGER NOT NULL DEFAULT 0,"
                        " event TEXT NOT NULL,"
                        " UNIQUE (subscription_id, message_id))"))
            && exec(QStringLiteral("CREATE INDEX messages_by_subscription ON messages (subscription_id, id)"));
    }
    if (ok && version < 2) {
        ok = exec(QStringLiteral("CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)"));
    }
    ok = ok && exec(QStringLiteral("PRAGMA user_version = %1").arg(SchemaVersion));
    if (!ok) {
        const QString error = m_error;
        db.rollback();
        m_error = error;
        return false;
    }
    return db.commit() || fail(QStringLiteral("commit migration"), db.lastError().text());
}

qint64 NtfyDatabase::addServer(const QUrl &url)
{
    const QString normalized = ntfyNormalizeServerUrl(url).toString();
    QSqlQuery query(connection(m_connectionName));
    query.prepare(QStringLiteral("INSERT OR IGNORE INTO servers (url) VALUES (?)"));
    query.addBindValue(normalized);
    if (!query.exec()) {
        fail(QStringLiteral("add server"), query.lastError().text());
        return 0;
    }

    query.prepare(QStringLiteral("SELECT id FROM servers WHERE url = ?"));
    query.addBindValue(normalized);
    if (!query.exec() || !query.next()) {
        fail(QStringLiteral("find server"), query.lastError().text());
        return 0;
    }
    return query.value(0).toLongLong();
}

bool NtfyDatabase::removeServer(qint64 serverId)
{
    QSqlQuery query(connection(m_connectionName));
    query.prepare(QStringLiteral("DELETE FROM servers WHERE id = ?"));
    query.addBindValue(serverId);
    if (!query.exec())
        return fail(QStringLiteral("remove server"), query.lastError().text());
    return query.numRowsAffected() > 0;
}

QList<NtfyServerRecord> NtfyDatabase::servers() const
{
    QList<NtfyServerRecord> result;
    QSqlQuery query(connection(m_connectionName));
    if (!query.exec(QStringLiteral("SELECT id, url FROM servers ORDER BY id"))) {
        fail(QStringLiteral("list servers"), query.lastError().text());
        return result;
    }
    while (query.next()) {
        NtfyServerRecord record;
        record.id = query.value(0).toLongLong();
        record.url = QUrl(query.value(1).toString());
        result.append(record);
    }
    return result;
}

NtfyServerRecord NtfyDatabase::server(qint64 serverId) const
{
    for (const NtfyServerRecord &record : servers()) {
        if (record.id == serverId)
            return record;
    }
    return NtfyServerRecord();
}

qint64 NtfyDatabase::addSubscription(qint64 serverId, const QString &topic, const QString &displayName)
{
    QSqlQuery query(connection(m_connectionName));
    query.prepare(QStringLiteral("INSERT INTO subscriptions (server_id, topic, display_name) VALUES (?, ?, ?)"));
    query.addBindValue(serverId);
    query.addBindValue(topic);
    query.addBindValue(displayName.isNull() ? QStringLiteral("") : displayName); // null QString binds as NULL
    if (!query.exec()) {
        fail(QStringLiteral("add subscription"), query.lastError().text());
        return 0;
    }
    return query.lastInsertId().toLongLong();
}

bool NtfyDatabase::removeSubscription(qint64 subscriptionId)
{
    QSqlQuery query(connection(m_connectionName));
    query.prepare(QStringLiteral("DELETE FROM subscriptions WHERE id = ?"));
    query.addBindValue(subscriptionId);
    if (!query.exec())
        return fail(QStringLiteral("remove subscription"), query.lastError().text());
    return query.numRowsAffected() > 0;
}

bool NtfyDatabase::setMuted(qint64 subscriptionId, bool muted)
{
    QSqlQuery query(connection(m_connectionName));
    query.prepare(QStringLiteral("UPDATE subscriptions SET muted = ? WHERE id = ?"));
    query.addBindValue(muted ? 1 : 0);
    query.addBindValue(subscriptionId);
    if (!query.exec())
        return fail(QStringLiteral("mute subscription"), query.lastError().text());
    return query.numRowsAffected() > 0;
}

QList<NtfySubscriptionRecord> NtfyDatabase::subscriptions(qint64 serverId) const
{
    QList<NtfySubscriptionRecord> result;
    QSqlQuery query(connection(m_connectionName));
    query.prepare(QStringLiteral(
            "SELECT s.id, s.server_id, s.topic, s.display_name, s.muted,"
            " (SELECT COUNT(*) FROM messages m WHERE m.subscription_id = s.id AND m.read = 0),"
            " (SELECT m.event FROM messages m WHERE m.subscription_id = s.id ORDER BY m.id DESC LIMIT 1)"
            " FROM subscriptions s WHERE (? = 0 OR s.server_id = ?) ORDER BY s.id"));
    query.addBindValue(serverId);
    query.addBindValue(serverId);
    if (!query.exec()) {
        fail(QStringLiteral("list subscriptions"), query.lastError().text());
        return result;
    }
    while (query.next()) {
        NtfySubscriptionRecord record;
        record.id = query.value(0).toLongLong();
        record.serverId = query.value(1).toLongLong();
        record.topic = query.value(2).toString();
        record.displayName = query.value(3).toString();
        record.muted = query.value(4).toInt() != 0;
        record.unreadCount = query.value(5).toInt();
        if (!query.value(6).isNull())
            record.lastMessage = NtfyEvent::fromJson(query.value(6).toByteArray());
        result.append(record);
    }
    return result;
}

NtfySubscriptionRecord NtfyDatabase::subscription(qint64 subscriptionId) const
{
    for (const NtfySubscriptionRecord &record : subscriptions()) {
        if (record.id == subscriptionId)
            return record;
    }
    return NtfySubscriptionRecord();
}

qint64 NtfyDatabase::addMessage(qint64 subscriptionId, const NtfyEvent &message)
{
    QSqlQuery query(connection(m_connectionName));
    query.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO messages (subscription_id, message_id, time, event) VALUES (?, ?, ?, ?)"));
    query.addBindValue(subscriptionId);
    query.addBindValue(message.id);
    query.addBindValue(message.time);
    query.addBindValue(QString::fromUtf8(message.toJson()));
    if (!query.exec()) {
        fail(QStringLiteral("add message"), query.lastError().text());
        return -1;
    }
    if (query.numRowsAffected() == 0)
        return 0;
    return query.lastInsertId().toLongLong();
}

QList<NtfyStoredMessage> NtfyDatabase::messages(qint64 subscriptionId, int limit, qint64 beforeId) const
{
    QList<NtfyStoredMessage> result;
    QSqlQuery query(connection(m_connectionName));
    query.prepare(QStringLiteral(
            "SELECT id, read, event FROM messages WHERE subscription_id = ? AND (? = 0 OR id < ?)"
            " ORDER BY id DESC LIMIT ?"));
    query.addBindValue(subscriptionId);
    query.addBindValue(beforeId);
    query.addBindValue(beforeId);
    query.addBindValue(limit);
    if (!query.exec()) {
        fail(QStringLiteral("list messages"), query.lastError().text());
        return result;
    }
    while (query.next()) {
        NtfyStoredMessage message;
        message.id = query.value(0).toLongLong();
        message.subscriptionId = subscriptionId;
        message.read = query.value(1).toInt() != 0;
        message.event = NtfyEvent::fromJson(query.value(2).toByteArray());
        result.append(message);
    }
    return result;
}

NtfyStoredMessage NtfyDatabase::message(qint64 messageId) const
{
    NtfyStoredMessage message;
    QSqlQuery query(connection(m_connectionName));
    query.prepare(QStringLiteral("SELECT subscription_id, read, event FROM messages WHERE id = ?"));
    query.addBindValue(messageId);
    if (!query.exec()) {
        fail(QStringLiteral("read message"), query.lastError().text());
        return message;
    }
    if (query.next()) {
        message.id = messageId;
        message.subscriptionId = query.value(0).toLongLong();
        message.read = query.value(1).toInt() != 0;
        message.event = NtfyEvent::fromJson(query.value(2).toByteArray());
    }
    return message;
}

bool NtfyDatabase::markRead(qint64 subscriptionId)
{
    QSqlQuery query(connection(m_connectionName));
    query.prepare(QStringLiteral("UPDATE messages SET read = 1 WHERE subscription_id = ? AND read = 0"));
    query.addBindValue(subscriptionId);
    if (!query.exec())
        return fail(QStringLiteral("mark read"), query.lastError().text());
    return true;
}

qint64 NtfyDatabase::newestMessageTime(qint64 serverId) const
{
    QSqlQuery query(connection(m_connectionName));
    query.prepare(QStringLiteral(
            "SELECT MAX(m.time) FROM messages m JOIN subscriptions s ON s.id = m.subscription_id"
            " WHERE s.server_id = ?"));
    query.addBindValue(serverId);
    if (!query.exec() || !query.next()) {
        fail(QStringLiteral("newest message"), query.lastError().text());
        return 0;
    }
    return query.value(0).toLongLong();
}

int NtfyDatabase::pruneMessages(int keepPerSubscription)
{
    QSqlQuery query(connection(m_connectionName));
    query.prepare(QStringLiteral(
            "DELETE FROM messages WHERE id IN ("
            " SELECT m.id FROM messages m WHERE"
            "  (SELECT COUNT(*) FROM messages n WHERE n.subscription_id = m.subscription_id AND n.id > m.id) >= ?)"));
    query.addBindValue(keepPerSubscription);
    if (!query.exec()) {
        fail(QStringLiteral("prune messages"), query.lastError().text());
        return 0;
    }
    return query.numRowsAffected();
}

QString NtfyDatabase::setting(const QString &key, const QString &defaultValue) const
{
    QSqlQuery query(connection(m_connectionName));
    query.prepare(QStringLiteral("SELECT value FROM settings WHERE key = ?"));
    query.addBindValue(key);
    if (!query.exec()) {
        fail(QStringLiteral("read setting"), query.lastError().text());
        return defaultValue;
    }
    return query.next() ? query.value(0).toString() : defaultValue;
}

bool NtfyDatabase::setSetting(const QString &key, const QString &value)
{
    QSqlQuery query(connection(m_connectionName));
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)"));
    query.addBindValue(key);
    query.addBindValue(value.isNull() ? QStringLiteral("") : value);
    if (!query.exec())
        return fail(QStringLiteral("write setting"), query.lastError().text());
    return true;
}

qint64 NtfyDatabase::resumeTime(qint64 serverId) const
{
    const qint64 newest = newestMessageTime(serverId);
    if (newest > 0)
        return newest;

    QSqlQuery query(connection(m_connectionName));
    query.prepare(QStringLiteral("SELECT MIN(created) FROM subscriptions WHERE server_id = ?"));
    query.addBindValue(serverId);
    if (!query.exec() || !query.next()) {
        fail(QStringLiteral("resume time"), query.lastError().text());
        return 0;
    }
    return query.value(0).toLongLong();
}
