// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef NTFYCREDENTIALS_H
#define NTFYCREDENTIALS_H

#include "ntfyserver.h"

#include <QByteArray>
#include <QHash>
#include <QString>

// Storage for server credentials. On the device this is backed by Sailfish Secrets
// (see secrets/), which ties the secrets to the calling executable.
class NtfyCredentialStore
{
public:
    virtual ~NtfyCredentialStore() {}

    // Replaces any existing secret under the same key.
    virtual bool store(const QString &key, const QByteArray &secret, QString *error = nullptr) = 0;
    // Returns false on failure. A missing secret is not a failure: *found is set to false instead.
    virtual bool load(const QString &key, QByteArray *secret, bool *found, QString *error = nullptr) = 0;
    // Removing a missing secret succeeds.
    virtual bool remove(const QString &key, QString *error = nullptr) = 0;
};

// In-memory store, for tests and for running without a secrets service.
class NtfyMemoryCredentialStore : public NtfyCredentialStore
{
public:
    bool store(const QString &key, const QByteArray &secret, QString *error = nullptr) override;
    bool load(const QString &key, QByteArray *secret, bool *found, QString *error = nullptr) override;
    bool remove(const QString &key, QString *error = nullptr) override;

private:
    QHash<QString, QByteArray> m_secrets;
};

// Secret name for a server. Hashed, so that secret metadata doesn't reveal which servers are used.
QString ntfyCredentialKey(const QUrl &serverUrl);

// Serialize the token or username/password of a server config, and back.
QByteArray ntfyEncodeCredentials(const NtfyServerConfig &server);
bool ntfyDecodeCredentials(const QByteArray &data, NtfyServerConfig *server);

#endif // NTFYCREDENTIALS_H
