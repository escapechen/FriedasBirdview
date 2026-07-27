#include "CustomCaStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <algorithm>

namespace {
constexpr auto kSettingsIds = "tls/customCaIds";
constexpr auto kSettingsLabelPrefix = "tls/customCaLabels/";
constexpr auto kNssNicknamePrefix = "FriedasBirdview-CA-";

bool isSafeId(const QString &id)
{
    if (id.size() != 64) {
        return false;
    }
    for (const QChar character : id) {
        if (!character.isDigit() && (character < QLatin1Char('a') || character > QLatin1Char('f'))) {
            return false;
        }
    }
    return true;
}
}

CustomCaStore::CustomCaStore()
{
    load();
}

QList<CustomCaStore::Entry> CustomCaStore::entries() const
{
    QList<Entry> result;
    result.reserve(m_ids.size());
    for (const QString &id : m_ids) {
        const auto it = m_certificates.constFind(id);
        if (it != m_certificates.cend()) {
            result.append(it->entry);
        }
    }
    return result;
}

QList<QSslCertificate> CustomCaStore::certificates() const
{
    QList<QSslCertificate> result;
    result.reserve(m_ids.size());
    for (const QString &id : m_ids) {
        const auto it = m_certificates.constFind(id);
        if (it != m_certificates.cend()) {
            result.append(it->certificate);
        }
    }
    return result;
}

bool CustomCaStore::addFromFile(const QString &filePath, QString *summary, QString *error)
{
    QFile input(filePath);
    if (!input.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("The selected certificate file could not be read.");
        return false;
    }
    const QByteArray data = input.readAll();
    QList<QSslCertificate> parsed = QSslCertificate::fromData(data, QSsl::Pem);
    if (parsed.isEmpty()) {
        parsed = QSslCertificate::fromData(data, QSsl::Der);
    }
    if (parsed.isEmpty()) {
        *error = QStringLiteral("The selected file does not contain a PEM or DER certificate.");
        return false;
    }

    QList<StoredCertificate> pending;
    for (const QSslCertificate &certificate : parsed) {
        if (certificate.isNull() || !isCertificateAuthority(certificate)) {
            *error = QStringLiteral("Only CA certificates may be trusted. Select the Frigate root or issuing CA, not its server certificate.");
            return false;
        }
        const QString id = certificateId(certificate);
        if (m_certificates.contains(id) || std::any_of(pending.cbegin(), pending.cend(), [&id](const StoredCertificate &entry) {
                return entry.entry.id == id;
            })) {
            continue;
        }
        pending.append({{id, certificateLabel(certificate, id)}, certificate});
    }

    if (pending.isEmpty()) {
        *summary = QStringLiteral("That CA certificate is already trusted by FriedasBirdview.");
        return true;
    }
    if (!QDir().mkpath(certificateDirectory())) {
        *error = QStringLiteral("FriedasBirdview could not create its certificate storage directory.");
        return false;
    }

    QList<StoredCertificate> completed;
    for (const StoredCertificate &entry : pending) {
        const QString path = certificatePath(entry.entry.id);
        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly) || output.write(entry.certificate.toPem()) < 0 || !output.commit()) {
            *error = QStringLiteral("FriedasBirdview could not save the selected CA certificate.");
            break;
        }
        QString nssError;
        if (!importIntoNss(entry.entry.id, path, &nssError)) {
            QFile::remove(path);
            *error = nssError;
            break;
        }
        completed.append(entry);
    }

    if (completed.size() != pending.size()) {
        for (const StoredCertificate &entry : completed) {
            QString ignored;
            removeFromNss(entry.entry.id, &ignored);
            QFile::remove(certificatePath(entry.entry.id));
        }
        return false;
    }

    for (const StoredCertificate &entry : completed) {
        m_ids.append(entry.entry.id);
        m_certificates.insert(entry.entry.id, entry);
    }
    save();
    *summary = QStringLiteral("Added %1 custom CA certificate%2. Restart FriedasBirdview before using live video.")
                   .arg(completed.size())
                   .arg(completed.size() == 1 ? QString() : QStringLiteral("s"));
    return true;
}

bool CustomCaStore::remove(const QString &id, QString *error)
{
    if (!isSafeId(id) || !m_certificates.contains(id)) {
        *error = QStringLiteral("That custom CA certificate is no longer available.");
        return false;
    }
    if (!removeFromNss(id, error)) {
        return false;
    }
    if (!QFile::remove(certificatePath(id))) {
        *error = QStringLiteral("The certificate was removed from WebEngine trust, but its local copy could not be removed.");
        return false;
    }
    m_certificates.remove(id);
    m_ids.removeAll(id);
    save();
    return true;
}

void CustomCaStore::load()
{
    const QSettings settings;
    const QStringList ids = settings.value(QString::fromLatin1(kSettingsIds)).toStringList();
    for (const QString &id : ids) {
        if (!isSafeId(id)) {
            continue;
        }
        QFile certificateFile(certificatePath(id));
        if (!certificateFile.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QList<QSslCertificate> parsed = QSslCertificate::fromData(certificateFile.readAll(), QSsl::Pem);
        if (parsed.size() != 1 || parsed.first().isNull() || !isCertificateAuthority(parsed.first())) {
            continue;
        }
        const QString label = settings.value(QString::fromLatin1(kSettingsLabelPrefix) + id,
            certificateLabel(parsed.first(), id)).toString();
        m_ids.append(id);
        m_certificates.insert(id, {{id, label}, parsed.first()});
    }
}

void CustomCaStore::save() const
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(kSettingsIds), m_ids);
    settings.remove(QString::fromLatin1(kSettingsLabelPrefix));
    for (const QString &id : m_ids) {
        const auto it = m_certificates.constFind(id);
        if (it != m_certificates.cend()) {
            settings.setValue(QString::fromLatin1(kSettingsLabelPrefix) + id, it->entry.label);
        }
    }
    settings.sync();
}

QString CustomCaStore::certificateDirectory() const
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("ca-certificates"));
}

QString CustomCaStore::certificatePath(const QString &id) const
{
    return QDir(certificateDirectory()).filePath(id + QStringLiteral(".pem"));
}

QString CustomCaStore::nssDatabasePath() const
{
    return QDir(QDir::homePath()).filePath(QStringLiteral(".pki/nssdb"));
}

QString CustomCaStore::nssNickname(const QString &id) const
{
    return QString::fromLatin1(kNssNicknamePrefix) + id;
}

bool CustomCaStore::initializeNssDatabase(QString *error) const
{
    const QString database = nssDatabasePath();
    if (!QDir().mkpath(database)) {
        *error = QStringLiteral("FriedasBirdview could not create the WebEngine certificate database.");
        return false;
    }
    if (QFileInfo::exists(QDir(database).filePath(QStringLiteral("cert9.db")))) {
        return true;
    }
    return runCertutil({QStringLiteral("-N"), QStringLiteral("-d"), QStringLiteral("sql:") + database,
        QStringLiteral("--empty-password")}, error);
}

bool CustomCaStore::importIntoNss(const QString &id, const QString &path, QString *error) const
{
    if (!initializeNssDatabase(error)) {
        return false;
    }
    const QString database = QStringLiteral("sql:") + nssDatabasePath();
    const QStringList arguments{
        QStringLiteral("-A"), QStringLiteral("-d"), database,
        QStringLiteral("-n"), nssNickname(id),
        QStringLiteral("-t"), QStringLiteral("C,,"),
        QStringLiteral("-i"), path,
    };
    return runCertutil(arguments, error);
}

bool CustomCaStore::removeFromNss(const QString &id, QString *error) const
{
    const QStringList arguments{
        QStringLiteral("-D"), QStringLiteral("-d"), QStringLiteral("sql:") + nssDatabasePath(),
        QStringLiteral("-n"), nssNickname(id),
    };
    return runCertutil(arguments, error);
}

bool CustomCaStore::runCertutil(const QStringList &arguments, QString *error) const
{
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("certutil"));
    if (executable.isEmpty()) {
        *error = QStringLiteral("The NSS certutil tool is required to configure Qt WebEngine trust, but it is not installed.");
        return false;
    }
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(executable, arguments);
    if (!process.waitForStarted(3000)) {
        *error = QStringLiteral("FriedasBirdview could not start the NSS certificate tool.");
        return false;
    }
    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished(1000);
        *error = QStringLiteral("The NSS certificate tool did not finish in time.");
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        *error = QStringLiteral("The selected CA certificate could not be added to Qt WebEngine trust.");
        return false;
    }
    return true;
}

bool CustomCaStore::isCertificateAuthority(const QSslCertificate &certificate)
{
    const QByteArray der = certificate.toDer();
    const auto *data = reinterpret_cast<const unsigned char *>(der.constData());
    X509 *x509 = d2i_X509(nullptr, &data, der.size());
    if (!x509) {
        return false;
    }
    const bool result = X509_check_ca(x509) > 0;
    X509_free(x509);
    return result;
}

QString CustomCaStore::certificateId(const QSslCertificate &certificate)
{
    return QString::fromLatin1(QCryptographicHash::hash(certificate.toDer(), QCryptographicHash::Sha256).toHex());
}

QString CustomCaStore::certificateLabel(const QSslCertificate &certificate, const QString &id)
{
    QString name = certificate.subjectInfo(QSslCertificate::CommonName).join(QStringLiteral(", "));
    if (name.isEmpty()) {
        name = certificate.issuerInfo(QSslCertificate::CommonName).join(QStringLiteral(", "));
    }
    if (name.isEmpty()) {
        name = QStringLiteral("Custom certificate authority");
    }
    return QStringLiteral("%1 (%2)").arg(name, id.left(12));
}
