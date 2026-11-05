// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef NTFYSECRETSCREDENTIALSTORE_H
#define NTFYSECRETSCREDENTIALSTORE_H

#include "ntfycredentials.h"

#include <QScopedPointer>

#include <functional>

namespace Sailfish { namespace Secrets { class Request; class SecretManager; } }

// Credentials in a Sailfish Secrets collection that is:
//  - encrypted at rest with a key derived from the device lock,
//  - kept unlocked once unlocked, so a background process can reconnect while the phone is
//    locked. After a reboot (of sailfish-secretsd, really) it is locked again, and entering the
//    code on the lock screen doesn't change that: only the system's device lock prompt does,
//    which only unlock() brings up,
//  - owner only. Note that sailfish-secretsd identifies the owner by its full command line
//    (/proc/<pid>/cmdline: its /proc/<pid>/exe lookup always comes back empty), so a process must
//    always be started with exactly the same command line to get at its secrets, and any process
//    that fakes that command line can too. The protection that matters is the encryption at rest.
//
// store(), load() and remove() block until the secrets daemon answers, and never prompt: a
// background process must not pop up a prompt nobody asked for.
class NtfySecretsCredentialStore : public NtfyCredentialStore
{
public:
    explicit NtfySecretsCredentialStore(const QString &collectionName);
    ~NtfySecretsCredentialStore() override;

    bool store(const QString &key, const QByteArray &secret, QString *error = nullptr) override;
    bool load(const QString &key, QByteArray *secret, bool *found, QString *error = nullptr) override;
    bool remove(const QString &key, QString *error = nullptr) override;

    // Unlocks a locked collection with the system's device lock prompt, by reading key. Doesn't
    // block: the prompt can stay up for minutes. done is called from the event loop once it has
    // been answered or given up on, with an error unless the collection is unlocked now (a
    // missing key is fine). From then on the other calls work without a prompt, until the next
    // reboot. One at a time.
    void unlock(const QString &key, const std::function<void(bool ok, const QString &error)> &done);

private:
    // Runs request to the end, keeping its error code in m_lastError.
    bool run(Sailfish::Secrets::Request &request, QString *error);
    // The connection to sailfish-secretsd is made once, by SecretManager, and doesn't survive
    // the daemon restarting (on an update, say): every request after that fails. Runs attempt,
    // and once more on a new connection if that's why it failed.
    bool withConnection(const std::function<bool()> &attempt);
    bool connectionLost(int errorCode) const;
    void reconnect();
    bool ensureCollection(QString *error);
    // remove() without withConnection(), for inside one.
    bool removeSecret(const QString &key, QString *error);

    QScopedPointer<Sailfish::Secrets::SecretManager> m_manager;
    QString m_collectionName;
    bool m_collectionReady = false;
    int m_lastError = 0;
    bool m_unlocking = false;
    // The connection turned out to be gone while unlock() still needed it.
    bool m_reconnect = false;
};

#endif // NTFYSECRETSCREDENTIALSTORE_H
