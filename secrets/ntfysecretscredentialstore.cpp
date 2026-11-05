// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ntfysecretscredentialstore.h"

#include <Secrets/createcollectionrequest.h>
#include <Secrets/deletesecretrequest.h>
#include <Secrets/result.h>
#include <Secrets/secret.h>
#include <Secrets/secretmanager.h>
#include <Secrets/storedsecretrequest.h>
#include <Secrets/storesecretrequest.h>

using namespace Sailfish::Secrets;

namespace {

// Encrypted storage plugin: the same name for storage and encryption selects it.
const QString &storagePlugin()
{
    return SecretManager::DefaultEncryptedStoragePluginName;
}

} // namespace

NtfySecretsCredentialStore::NtfySecretsCredentialStore(const QString &collectionName)
    : m_manager(new SecretManager)
    , m_collectionName(collectionName)
{
}

NtfySecretsCredentialStore::~NtfySecretsCredentialStore()
{
}

bool NtfySecretsCredentialStore::run(Request &request, QString *error)
{
    request.startRequest();
    request.waitForFinished();

    const Result result = request.result();
    m_lastError = result.errorCode();
    if (result.code() == Result::Succeeded)
        return true;
    if (error)
        *error = result.errorMessage();
    return false;
}

bool NtfySecretsCredentialStore::connectionLost(int errorCode) const
{
    // A request over a connection that's gone fails with the D-Bus error ("Not connected to
    // D-Bus server", say) as a DaemonError.
    return errorCode == Result::DaemonError || errorCode == Result::SecretManagerNotInitializedError
            || !m_manager->isInitialized();
}

void NtfySecretsCredentialStore::reconnect()
{
    qWarning("lost the connection to sailfish-secretsd, reconnecting");
    // The old manager goes first, in case managers share a connection that only closes with the
    // last of them.
    m_manager.reset();
    m_manager.reset(new SecretManager);
    // A restarted daemon has it locked again, at least.
    m_collectionReady = false;
    m_reconnect = false;
}

bool NtfySecretsCredentialStore::withConnection(const std::function<bool()> &attempt)
{
    // Not while unlock() is under way: its request still uses the manager.
    if (m_unlocking)
        return attempt();
    if (m_reconnect)
        reconnect();

    m_lastError = Result::NoError;
    if (attempt())
        return true;
    if (!connectionLost(m_lastError))
        return false;
    reconnect();
    return attempt();
}

bool NtfySecretsCredentialStore::ensureCollection(QString *error)
{
    if (m_collectionReady)
        return true;

    CreateCollectionRequest request;
    request.setManager(m_manager.data());
    request.setCollectionName(m_collectionName);
    request.setCollectionLockType(CreateCollectionRequest::DeviceLock);
    request.setDeviceLockUnlockSemantic(SecretManager::DeviceLockKeepUnlocked);
    request.setAccessControlMode(SecretManager::OwnerOnlyMode);
    request.setUserInteractionMode(SecretManager::PreventInteraction);
    request.setStoragePluginName(storagePlugin());
    request.setEncryptionPluginName(storagePlugin());

    if (run(request, error) || m_lastError == Result::CollectionAlreadyExistsError)
        m_collectionReady = true;
    return m_collectionReady;
}

bool NtfySecretsCredentialStore::store(const QString &key, const QByteArray &secret, QString *error)
{
    return withConnection([&]() {
        if (!ensureCollection(error))
            return false;

        // Storing over an existing secret fails, so replace it explicitly.
        if (!removeSecret(key, error))
            return false;

        Secret value(Secret::Identifier(key, m_collectionName, storagePlugin()));
        value.setType(Secret::TypeBlob);
        value.setData(secret);

        StoreSecretRequest request;
        request.setManager(m_manager.data());
        request.setSecretStorageType(StoreSecretRequest::CollectionSecret);
        request.setUserInteractionMode(SecretManager::PreventInteraction);
        request.setSecret(value);
        return run(request, error);
    });
}

bool NtfySecretsCredentialStore::load(const QString &key, QByteArray *secret, bool *found, QString *error)
{
    return withConnection([&]() {
        *found = false;
        secret->clear();
        if (!ensureCollection(error))
            return false;

        StoredSecretRequest request;
        request.setManager(m_manager.data());
        request.setIdentifier(Secret::Identifier(key, m_collectionName, storagePlugin()));
        request.setUserInteractionMode(SecretManager::PreventInteraction);

        if (!run(request, error))
            return m_lastError == Result::InvalidSecretError; // no such secret

        *found = true;
        *secret = request.secret().data();
        return true;
    });
}

void NtfySecretsCredentialStore::unlock(const QString &key, const std::function<void(bool, const QString &)> &done)
{
    QString error;
    if (!withConnection([&]() { return ensureCollection(&error); })) {
        done(false, error);
        return;
    }

    // Started, not waited for (unlike run()): waitForFinished() doesn't run the event loop, so
    // the whole process would stand still, D-Bus calls unanswered, while the prompt is up.
    StoredSecretRequest *request = new StoredSecretRequest;
    request->setManager(m_manager.data());
    request->setIdentifier(Secret::Identifier(key, m_collectionName, storagePlugin()));
    request->setUserInteractionMode(SecretManager::SystemInteraction);
    m_unlocking = true;
    // The request itself is the context: the connection goes with it.
    QObject::connect(request, &Request::statusChanged, request, [this, request, done]() {
        if (request->status() != Request::Finished)
            return;
        // Set before statusChanged() is emitted.
        const Result result = request->result();
        // Not delete: we're inside one of its signals.
        request->deleteLater();
        m_unlocking = false;
        // Not finding the key also means the collection was opened.
        if (result.code() == Result::Succeeded || result.errorCode() == Result::InvalidSecretError) {
            done(true, QString());
            return;
        }
        // Not now, with the request still around: before the next call.
        if (connectionLost(result.errorCode()))
            m_reconnect = true;
        done(false, result.errorMessage());
    });
    request->startRequest();
}

bool NtfySecretsCredentialStore::remove(const QString &key, QString *error)
{
    return withConnection([&]() { return removeSecret(key, error); });
}

bool NtfySecretsCredentialStore::removeSecret(const QString &key, QString *error)
{
    if (!ensureCollection(error))
        return false;

    DeleteSecretRequest request;
    request.setManager(m_manager.data());
    request.setIdentifier(Secret::Identifier(key, m_collectionName, storagePlugin()));
    request.setUserInteractionMode(SecretManager::PreventInteraction);

    return run(request, error) || m_lastError == Result::InvalidSecretError;
}
