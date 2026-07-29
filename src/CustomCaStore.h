#pragma once

#include <QHash>
#include <QList>
#include <QSslCertificate>
#include <QString>

class CustomCaStore final {
public:
    struct Entry {
        QString id;
        QString label;
    };

    CustomCaStore();

    QList<Entry> entries() const;
    QList<QSslCertificate> certificates() const;
    bool addFromFile(const QString &filePath, QString *summary, QString *error);
    bool remove(const QString &id, QString *error);

private:
    struct StoredCertificate {
        Entry entry;
        QSslCertificate certificate;
    };

    void load();
    void save() const;
    QString certificateDirectory() const;
    QString certificatePath(const QString &id) const;
    static bool isCertificateAuthority(const QSslCertificate &certificate);
    static QString certificateId(const QSslCertificate &certificate);
    static QString certificateLabel(const QSslCertificate &certificate, const QString &id);

    QHash<QString, StoredCertificate> m_certificates;
    QList<QString> m_ids;
};
