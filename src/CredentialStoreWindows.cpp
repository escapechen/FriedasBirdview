#include "CredentialStore.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>

#include <limits>

namespace {
constexpr auto kTargetPrefix = "org.friedasbirdview.FriedasBirdview/";

QString targetName(const QString &key)
{
    return QString::fromLatin1(kTargetPrefix) + key;
}
}

bool CredentialStore::loadPassword(const QString &key, QString *password, QString *error) const
{
    PCREDENTIALW credential = nullptr;
    const QString target = targetName(key);
    if (!CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0, &credential)) {
        *error = GetLastError() == ERROR_NOT_FOUND
            ? QStringLiteral("Enter the Frigate password.")
            : QStringLiteral("Windows Credential Manager could not be accessed.");
        return false;
    }

    const QByteArray value(reinterpret_cast<const char *>(credential->CredentialBlob), credential->CredentialBlobSize);
    *password = QString::fromUtf8(value);
    CredFree(credential);
    return true;
}

bool CredentialStore::savePassword(const QString &key, const QString &password, QString *error) const
{
    const QByteArray value = password.toUtf8();
    if (value.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE
        || value.size() > std::numeric_limits<DWORD>::max()) {
        *error = QStringLiteral("The Frigate password is too large for Windows Credential Manager.");
        return false;
    }

    const QString target = targetName(key);
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(target.utf16()));
    credential.CredentialBlobSize = static_cast<DWORD>(value.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(value.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if (!CredWriteW(&credential, 0)) {
        *error = QStringLiteral("Could not save the Frigate password in Windows Credential Manager.");
        return false;
    }
    return true;
}

bool CredentialStore::deletePassword(const QString &key, QString *error) const
{
    const QString target = targetName(key);
    if (CredDeleteW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0)
        || GetLastError() == ERROR_NOT_FOUND) {
        return true;
    }
    *error = QStringLiteral("Could not remove the old Frigate password from Windows Credential Manager.");
    return false;
}
