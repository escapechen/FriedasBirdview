#include "CredentialStore.h"

#include <KWallet>

#include <memory>

namespace {
constexpr auto kWalletFolder = "FriedasBirdview";
}

bool CredentialStore::loadPassword(const QString &key, QString *password, QString *error) const
{
    if (!KWallet::Wallet::isEnabled()) {
        *error = QStringLiteral("KDE Wallet is unavailable. Enable KWallet to use a Frigate login.");
        return false;
    }
    std::unique_ptr<KWallet::Wallet> wallet(KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0));
    if (!wallet || !wallet->isOpen() || !wallet->hasFolder(QString::fromLatin1(kWalletFolder))
        || !wallet->setFolder(QString::fromLatin1(kWalletFolder)) || !wallet->hasEntry(key)
        || wallet->readPassword(key, *password) != 0) {
        *error = QStringLiteral("Enter the Frigate password.");
        return false;
    }
    return true;
}

bool CredentialStore::savePassword(const QString &key, const QString &password, QString *error) const
{
    if (!KWallet::Wallet::isEnabled()) {
        *error = QStringLiteral("KDE Wallet is unavailable. Enable KWallet to use a Frigate login.");
        return false;
    }
    std::unique_ptr<KWallet::Wallet> wallet(KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0));
    if (!wallet || !wallet->isOpen()) {
        *error = QStringLiteral("Could not access KDE Wallet.");
        return false;
    }
    const QString folder = QString::fromLatin1(kWalletFolder);
    if (!wallet->hasFolder(folder) && !wallet->createFolder(folder)) {
        *error = QStringLiteral("Could not create the FriedasBirdview KDE Wallet folder.");
        return false;
    }
    if (!wallet->setFolder(folder) || wallet->writePassword(key, password) != 0) {
        *error = QStringLiteral("Could not save the Frigate password in KDE Wallet.");
        return false;
    }
    return true;
}

bool CredentialStore::deletePassword(const QString &key, QString *error) const
{
    if (!KWallet::Wallet::isEnabled()) {
        return true;
    }
    std::unique_ptr<KWallet::Wallet> wallet(KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0));
    if (!wallet || !wallet->isOpen()) {
        *error = QStringLiteral("Could not access KDE Wallet.");
        return false;
    }
    const QString folder = QString::fromLatin1(kWalletFolder);
    if (!wallet->hasFolder(folder)) {
        return true;
    }
    if (!wallet->setFolder(folder)) {
        *error = QStringLiteral("Could not access the FriedasBirdview KDE Wallet folder.");
        return false;
    }
    if (wallet->hasEntry(key) && wallet->removeEntry(key) != 0) {
        *error = QStringLiteral("Could not remove the old Frigate password from KDE Wallet.");
        return false;
    }
    return true;
}
