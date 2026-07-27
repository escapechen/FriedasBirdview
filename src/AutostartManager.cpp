#include "AutostartManager.h"
#include "AppBuildConfig.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

namespace {
constexpr auto kDesktopEntryName = FRIEDASBIRDVIEW_APPLICATION_ID ".desktop";
constexpr auto kPortalAutostartEnabled = "startup/portalAutostartEnabled";
constexpr auto kPortalService = "org.freedesktop.portal.Desktop";
constexpr auto kPortalPath = "/org/freedesktop/portal/desktop";
constexpr auto kBackgroundInterface = "org.freedesktop.portal.Background";
constexpr auto kRequestInterface = "org.freedesktop.portal.Request";
}

AutostartManager::AutostartManager(QObject *parent)
    : QObject(parent)
{
}

bool AutostartManager::isSupported() const
{
#if FRIEDASBIRDVIEW_FLATPAK
    return isPortalAvailable();
#else
    return true;
#endif
}

bool AutostartManager::isEnabled() const
{
    if (!isSupported()) {
        return false;
    }

#if FRIEDASBIRDVIEW_FLATPAK
    return QSettings().value(QString::fromLatin1(kPortalAutostartEnabled), false).toBool();
#else
    const QString path = desktopEntryPath();
    if (!QFileInfo(path).isFile()) {
        return false;
    }

    QSettings entry(path, QSettings::IniFormat);
    entry.beginGroup(QStringLiteral("Desktop Entry"));
    const bool enabled = entry.value(QStringLiteral("Type")) == QStringLiteral("Application")
        && !entry.value(QStringLiteral("Exec")).toString().isEmpty()
        && !entry.value(QStringLiteral("Hidden"), false).toBool();
    entry.endGroup();
    return enabled;
#endif
}

void AutostartManager::setEnabled(bool enabled, const QString &parentWindowId)
{
    if (!isSupported()) {
        emit changeFinished(isEnabled(), QStringLiteral(
            "Your desktop does not provide the Background portal required for Flatpak autostart. "
            "Install and enable a current xdg-desktop-portal backend, then try again."
        ));
        return;
    }

#if FRIEDASBIRDVIEW_FLATPAK
    if (m_portalRequestPending) {
        emit changeFinished(isEnabled(), QStringLiteral("An automatic-start request is already waiting for a response from the desktop."));
        return;
    }

    QDBusConnection connection = QDBusConnection::sessionBus();
    const QString token = QStringLiteral("friedasbirdview_%1")
                              .arg(QUuid::createUuid().toString(QUuid::Id128));
    const QString expectedPath = portalRequestPath(token);
    if (!connection.connect(
            QString::fromLatin1(kPortalService), expectedPath,
            QString::fromLatin1(kRequestInterface), QStringLiteral("Response"),
            this, SLOT(handlePortalResponse(uint,QVariantMap)))) {
        emit changeFinished(isEnabled(), QStringLiteral("FriedasBirdview could not listen for the desktop portal response."));
        return;
    }

    m_portalRequestPending = true;
    m_requestedEnabled = enabled;
    m_portalRequestPath = expectedPath;

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), token);
    options.insert(QStringLiteral("reason"), QStringLiteral(
        "Start FriedasBirdview after sign-in so it can monitor Frigate activity."
    ));
    options.insert(QStringLiteral("autostart"), enabled);

    QDBusInterface portal(
        QString::fromLatin1(kPortalService), QString::fromLatin1(kPortalPath),
        QString::fromLatin1(kBackgroundInterface), connection
    );
    const QDBusMessage reply = portal.call(
        QDBus::Block, QStringLiteral("RequestBackground"), parentWindowId, options
    );
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
        finishPortalRequest();
        emit changeFinished(isEnabled(), QStringLiteral(
            "The desktop could not process the automatic-start request. "
            "Check that xdg-desktop-portal is running, then try again."
        ));
        return;
    }

    const QDBusObjectPath request = qvariant_cast<QDBusObjectPath>(reply.arguments().first());
    if (request.path().isEmpty()) {
        finishPortalRequest();
        emit changeFinished(isEnabled(), QStringLiteral("The desktop returned an invalid automatic-start request."));
        return;
    }
    if (request.path() != m_portalRequestPath) {
        connection.disconnect(
            QString::fromLatin1(kPortalService), m_portalRequestPath,
            QString::fromLatin1(kRequestInterface), QStringLiteral("Response"),
            this, SLOT(handlePortalResponse(uint,QVariantMap))
        );
        if (!connection.connect(
                QString::fromLatin1(kPortalService), request.path(),
                QString::fromLatin1(kRequestInterface), QStringLiteral("Response"),
                this, SLOT(handlePortalResponse(uint,QVariantMap)))) {
            finishPortalRequest();
            emit changeFinished(isEnabled(), QStringLiteral("FriedasBirdview could not listen for the desktop portal response."));
            return;
        }
        m_portalRequestPath = request.path();
    }
#else
    QString error;
    if (setNativeEnabled(enabled, &error)) {
        emit changeFinished(enabled, {});
    } else {
        emit changeFinished(isEnabled(), error);
    }
#endif
}

bool AutostartManager::setNativeEnabled(bool enabled, QString *error) const
{
    const QString path = desktopEntryPath();
    if (enabled) {
        const QFileInfo info(path);
        if (!QDir().mkpath(info.dir().path())) {
            *error = QStringLiteral("FriedasBirdview could not create the desktop autostart directory.");
            return false;
        }

        QSaveFile entry(path);
        if (!entry.open(QIODevice::WriteOnly)
            || entry.write(desktopEntryContents().toUtf8()) < 0
            || !entry.commit()) {
            *error = QStringLiteral("FriedasBirdview could not enable automatic start.");
            return false;
        }
        return true;
    }

    if (!QFile::exists(path) || QFile::remove(path)) {
        return true;
    }
    *error = QStringLiteral("FriedasBirdview could not disable automatic start.");
    return false;
}

bool AutostartManager::isPortalAvailable()
{
    const QDBusConnection connection = QDBusConnection::sessionBus();
    if (!connection.isConnected() || !connection.interface()) {
        return false;
    }
    const QDBusReply<bool> reply = connection.interface()->isServiceRegistered(QString::fromLatin1(kPortalService));
    return reply.isValid() && reply.value();
}

QString AutostartManager::portalRequestPath(const QString &token)
{
    QString sender = QDBusConnection::sessionBus().baseService();
    sender.remove(QLatin1Char(':'));
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    return QStringLiteral("/org/freedesktop/portal/desktop/request/") + sender + QLatin1Char('/') + token;
}

void AutostartManager::handlePortalResponse(uint response, const QVariantMap &results)
{
    if (!m_portalRequestPending) {
        return;
    }

    const bool requestedEnabled = m_requestedEnabled;
    finishPortalRequest();
    const bool granted = response == 0
        && results.value(QStringLiteral("autostart")).toBool() == requestedEnabled;
    if (granted) {
        QSettings settings;
        settings.setValue(QString::fromLatin1(kPortalAutostartEnabled), requestedEnabled);
        settings.sync();
        emit changeFinished(requestedEnabled, {});
        return;
    }

    emit changeFinished(isEnabled(), requestedEnabled
            ? QStringLiteral("Automatic start was not granted by the desktop portal.")
            : QStringLiteral("The desktop portal did not confirm that automatic start was disabled."));
}

void AutostartManager::finishPortalRequest()
{
    if (!m_portalRequestPath.isEmpty()) {
        QDBusConnection::sessionBus().disconnect(
            QString::fromLatin1(kPortalService), m_portalRequestPath,
            QString::fromLatin1(kRequestInterface), QStringLiteral("Response"),
            this, SLOT(handlePortalResponse(uint,QVariantMap))
        );
    }
    m_portalRequestPending = false;
    m_portalRequestPath.clear();
}

QString AutostartManager::desktopEntryPath() const
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
        .filePath(QStringLiteral("autostart/") + QString::fromLatin1(kDesktopEntryName));
}

QString AutostartManager::desktopEntryContents() const
{
    return QStringLiteral(
               "[Desktop Entry]\n"
               "Type=Application\n"
               "Name=FriedasBirdview\n"
               "Comment=Frigate activity companion\n"
               "Exec=%1\n"
               "Icon=" FRIEDASBIRDVIEW_APPLICATION_ID "\n"
               "Terminal=false\n"
               "StartupNotify=false\n"
           )
        .arg(escapeExecArgument(QCoreApplication::applicationFilePath()));
}

QString AutostartManager::escapeExecArgument(const QString &argument)
{
    QString escaped = argument;
    escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    escaped.replace(QLatin1Char('"'), QLatin1String("\\\""));
    escaped.replace(QLatin1Char('$'), QLatin1String("\\$"));
    escaped.replace(QLatin1Char('`'), QLatin1String("\\`"));
    escaped.replace(QLatin1Char('%'), QLatin1String("%%"));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}
