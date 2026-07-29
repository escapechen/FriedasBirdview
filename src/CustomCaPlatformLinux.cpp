#include "CustomCaPlatform.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

namespace {
constexpr auto kNssNicknamePrefix = "FriedasBirdview-CA-";

QString nssDatabasePath()
{
    return QDir(QDir::homePath()).filePath(QStringLiteral(".pki/nssdb"));
}

QString nssNickname(const QString &id)
{
    return QString::fromLatin1(kNssNicknamePrefix) + id;
}

bool runCertutil(const QStringList &arguments, QString *error)
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

bool initializeNssDatabase(QString *error)
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
}

bool CustomCaPlatform::install(const QString &id, const QString &certificatePath, QString *error)
{
    if (!initializeNssDatabase(error)) {
        return false;
    }
    const QString database = QStringLiteral("sql:") + nssDatabasePath();
    return runCertutil({
        QStringLiteral("-A"), QStringLiteral("-d"), database,
        QStringLiteral("-n"), nssNickname(id),
        QStringLiteral("-t"), QStringLiteral("C,,"),
        QStringLiteral("-i"), certificatePath,
    }, error);
}

bool CustomCaPlatform::remove(const QString &id, QString *error)
{
    return runCertutil({
        QStringLiteral("-D"), QStringLiteral("-d"), QStringLiteral("sql:") + nssDatabasePath(),
        QStringLiteral("-n"), nssNickname(id),
    }, error);
}
