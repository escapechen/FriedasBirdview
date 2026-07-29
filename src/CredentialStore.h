#pragma once

#include <QString>

class CredentialStore final {
public:
    bool loadPassword(const QString &key, QString *password, QString *error) const;
    bool savePassword(const QString &key, const QString &password, QString *error) const;
    bool deletePassword(const QString &key, QString *error) const;
};
