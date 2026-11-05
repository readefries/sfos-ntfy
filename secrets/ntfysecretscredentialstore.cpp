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

bool run(Request &request, Result::ErrorCode *errorCode, QString *error)
{
    request.startRequest();
    request.waitForFinished();

    const Result result = request.result();
    if (errorCode)
        *errorCode = result.errorCode();
    if (result.code() == Result::Succeeded)
        return true;
    if (error)
        *error = result.errorMessage();
    return false;
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

    Result::ErrorCode code = Result::NoError;
    if (run(request, &code, error) || code == Result::CollectionAlreadyExistsError)
        m_collectionReady = true;
    return m_collectionReady;
}

bool NtfySecretsCredentialStore::store(const QString &key, const QByteArray &secret, QString *error)
{
    if (!ensureCollection(error))
        return false;

    // Storing over an existing secret fails, so replace it explicitly.
    if (!remove(key, error))
        return false;

    Secret value(Secret::Identifier(key, m_collectionName, storagePlugin()));
    value.setType(Secret::TypeBlob);
    value.setData(secret);

    StoreSecretRequest request;
    request.setManager(m_manager.data());
    request.setSecretStorageType(StoreSecretRequest::CollectionSecret);
    request.setUserInteractionMode(SecretManager::PreventInteraction);
    request.setSecret(value);
    return run(request, nullptr, error);
}

bool NtfySecretsCredentialStore::load(const QString &key, QByteArray *secret, bool *found, QString *error)
{
    *found = false;
    secret->clear();
    if (!ensureCollection(error))
        return false;

    StoredSecretRequest request;
    request.setManager(m_manager.data());
    request.setIdentifier(Secret::Identifier(key, m_collectionName, storagePlugin()));
    request.setUserInteractionMode(SecretManager::PreventInteraction);

    Result::ErrorCode code = Result::NoError;
    if (!run(request, &code, error))
        return code == Result::InvalidSecretError; // no such secret

    *found = true;
    *secret = request.secret().data();
    return true;
}

void NtfySecretsCredentialStore::unlock(const QString &key, const std::function<void(bool, const QString &)> &done)
{
    QString error;
    if (!ensureCollection(&error)) {
        done(false, error);
        return;
    }

    // Started, not waited for (unlike run()): waitForFinished() doesn't run the event loop, so
    // the whole process would stand still, D-Bus calls unanswered, while the prompt is up.
    StoredSecretRequest *request = new StoredSecretRequest;
    request->setManager(m_manager.data());
    request->setIdentifier(Secret::Identifier(key, m_collectionName, storagePlugin()));
    request->setUserInteractionMode(SecretManager::SystemInteraction);
    // The request itself is the context: the connection goes with it.
    QObject::connect(request, &Request::statusChanged, request, [request, done]() {
        if (request->status() != Request::Finished)
            return;
        // Set before statusChanged() is emitted.
        const Result result = request->result();
        // Not delete: we're inside one of its signals.
        request->deleteLater();
        // Not finding the key also means the collection was opened.
        if (result.code() == Result::Succeeded || result.errorCode() == Result::InvalidSecretError)
            done(true, QString());
        else
            done(false, result.errorMessage());
    });
    request->startRequest();
}

bool NtfySecretsCredentialStore::remove(const QString &key, QString *error)
{
    if (!ensureCollection(error))
        return false;

    DeleteSecretRequest request;
    request.setManager(m_manager.data());
    request.setIdentifier(Secret::Identifier(key, m_collectionName, storagePlugin()));
    request.setUserInteractionMode(SecretManager::PreventInteraction);

    Result::ErrorCode code = Result::NoError;
    return run(request, &code, error) || code == Result::InvalidSecretError;
}
