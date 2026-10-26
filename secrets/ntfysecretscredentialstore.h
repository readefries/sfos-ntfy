// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef NTFYSECRETSCREDENTIALSTORE_H
#define NTFYSECRETSCREDENTIALSTORE_H

#include "ntfycredentials.h"

#include <QScopedPointer>

namespace Sailfish { namespace Secrets { class SecretManager; } }

// Credentials in a Sailfish Secrets collection that is:
//  - encrypted at rest with a key derived from the device lock,
//  - unlocked after the first device unlock after boot and then kept unlocked, so a background
//    process can reconnect while the phone is locked,
//  - owner only. Note that sailfish-secretsd identifies the owner by its full command line
//    (/proc/<pid>/cmdline: its /proc/<pid>/exe lookup always comes back empty), so a process must
//    always be started with exactly the same command line to get at its secrets, and any process
//    that fakes that command line can too. The protection that matters is the encryption at rest.
//
// Calls block until the secrets daemon answers.
class NtfySecretsCredentialStore : public NtfyCredentialStore
{
public:
    explicit NtfySecretsCredentialStore(const QString &collectionName);
    ~NtfySecretsCredentialStore() override;

    bool store(const QString &key, const QByteArray &secret, QString *error = nullptr) override;
    bool load(const QString &key, QByteArray *secret, bool *found, QString *error = nullptr) override;
    bool remove(const QString &key, QString *error = nullptr) override;

private:
    bool ensureCollection(QString *error);

    QScopedPointer<Sailfish::Secrets::SecretManager> m_manager;
    QString m_collectionName;
    bool m_collectionReady = false;
};

#endif // NTFYSECRETSCREDENTIALSTORE_H
