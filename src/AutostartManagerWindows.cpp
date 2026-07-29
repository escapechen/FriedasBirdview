#include "AutostartManager.h"
#include "AppBuildConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

namespace {
constexpr auto kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto kRunValue = FRIEDASBIRDVIEW_APPLICATION_ID;

QString startupCommand()
{
    return QLatin1Char('"') + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + QLatin1Char('"');
}
}

AutostartManager::AutostartManager(QObject *parent)
    : QObject(parent)
{
}

bool AutostartManager::isSupported() const
{
    return true;
}

bool AutostartManager::isEnabled() const
{
    QSettings startup(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    return startup.value(QString::fromLatin1(kRunValue)).toString() == startupCommand();
}

void AutostartManager::setEnabled(bool enabled, const QString &parentWindowId)
{
    Q_UNUSED(parentWindowId)

    QString error;
    if (setNativeEnabled(enabled, &error)) {
        emit changeFinished(enabled, {});
    } else {
        emit changeFinished(isEnabled(), error);
    }
}

bool AutostartManager::setNativeEnabled(bool enabled, QString *error) const
{
    const QString command = startupCommand();
    if (command == QStringLiteral("\"\"")) {
        *error = QStringLiteral("FriedasBirdview could not determine its executable path.");
        return false;
    }

    QSettings startup(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    if (enabled) {
        startup.setValue(QString::fromLatin1(kRunValue), command);
    } else {
        startup.remove(QString::fromLatin1(kRunValue));
    }
    startup.sync();
    if (startup.status() != QSettings::NoError) {
        *error = enabled
            ? QStringLiteral("FriedasBirdview could not enable automatic start for this Windows user.")
            : QStringLiteral("FriedasBirdview could not disable automatic start for this Windows user.");
        return false;
    }
    return true;
}

void AutostartManager::handlePortalResponse(uint response, const QVariantMap &results)
{
    Q_UNUSED(response)
    Q_UNUSED(results)
}
