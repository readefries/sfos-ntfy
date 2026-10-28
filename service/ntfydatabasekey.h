// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef NTFYDATABASEKEY_H
#define NTFYDATABASEKEY_H

#include <QByteArray>
#include <QString>

class NtfyCredentialStore;
class NtfyDatabase;

// The daemon database is encrypted with a random key that lives next to the credentials (on the
// device: in Sailfish Secrets, unavailable until the first unlock after boot).

// Loads the key, creating and storing a new one on first use. Fails if the store can't be read,
// e.g. while Secrets is still locked; the daemon then exits and systemd retries.
bool ntfyDatabaseKey(NtfyCredentialStore *store, QByteArray *key, bool *created, QString *error = nullptr);

// Opens the database at path with key:
//  - an unencrypted database from an earlier version gets encrypted first,
//  - one that can't be decrypted (the key was lost, say after the Secrets collection was reset) is
//    moved aside and replaced by an empty one, with *notice saying so.
bool ntfyOpenEncryptedDatabase(NtfyDatabase *database, const QString &path, const QByteArray &key,
                               QString *notice, QString *error = nullptr);

#endif // NTFYDATABASEKEY_H
