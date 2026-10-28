// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfydatabasekey.h"

#include "ntfycredentials.h"
#include "ntfydatabase.h"

#include <QDateTime>
#include <QFile>

namespace {
const QString KeyName = QStringLiteral("databasekey");
const int KeySize = 32;

// QRandomGenerator only exists since Qt 5.10.
bool randomBytes(int size, QByteArray *bytes)
{
    QFile random(QStringLiteral("/dev/urandom"));
    if (!random.open(QIODevice::ReadOnly))
        return false;
    *bytes = random.read(size);
    return bytes->size() == size;
}
} // namespace

bool ntfyDatabaseKey(NtfyCredentialStore *store, QByteArray *key, bool *created, QString *error)
{
    *created = false;
    bool found = false;
    QString storeError;
    if (!store->load(KeyName, key, &found, &storeError)) {
        if (error)
            *error = QStringLiteral("could not read the database key: ") + storeError;
        return false;
    }
    if (found && key->size() == KeySize)
        return true;

    if (!randomBytes(KeySize, key)) {
        if (error)
            *error = QStringLiteral("could not generate a database key");
        return false;
    }
    if (!store->store(KeyName, *key, &storeError)) {
        key->clear();
        if (error)
            *error = QStringLiteral("could not store the database key: ") + storeError;
        return false;
    }
    *created = true;
    return true;
}

bool ntfyOpenEncryptedDatabase(NtfyDatabase *database, const QString &path, const QByteArray &key,
                               QString *notice, QString *error)
{
    notice->clear();

    if (NtfyDatabase::isPlaintextFile(path)) {
        QString encryptError;
        if (!NtfyDatabase::encryptFile(path, key, &encryptError)) {
            if (error)
                *error = encryptError;
            return false;
        }
        *notice = QStringLiteral("encrypted the existing database");
    }

    if (database->open(path, key))
        return true;
    // Only a key that doesn't fit is a reason to start over; anything else (a missing driver, a
    // full disk) must not cost the user their data.
    if (!QFile::exists(path) || !database->lastError().contains(QLatin1String("wrong key"))) {
        if (error)
            *error = database->lastError();
        return false;
    }

    // Unreadable with our key. Keep it rather than delete it, then start over.
    const QString aside = path + QStringLiteral(".unreadable-")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmmss"));
    if (!QFile::rename(path, aside)) {
        if (error)
            *error = database->lastError() + QStringLiteral(" (and could not move it aside)");
        return false;
    }
    QFile::remove(path + QStringLiteral("-wal"));
    QFile::remove(path + QStringLiteral("-shm"));
    *notice = QStringLiteral("could not decrypt the database, moved it to %1 and started over").arg(aside);

    if (!database->open(path, key)) {
        if (error)
            *error = database->lastError();
        return false;
    }
    return true;
}
