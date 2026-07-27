#include "AppController.h"
#include "AppBuildConfig.h"

#include <QApplication>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
#if defined(Q_OS_LINUX)
    // FriedasBirdview is a host application and does not use XDG portal APIs.
    // Qt 6.10 currently attempts to register every host Qt app with the portal
    // registry, which is noisy on Plasma when that registry cannot resolve a
    // per-user desktop entry. This is separate from the StatusNotifier tray
    // protocol, which remains enabled. Keep portals available in sandboxes,
    // where they are needed for file access and desktop integration.
    const bool isSandboxed = !qEnvironmentVariableIsEmpty("FLATPAK_ID")
        || !qEnvironmentVariableIsEmpty("SNAP")
        || QFile::exists(QStringLiteral("/.flatpak-info"));
    if (!isSandboxed && qEnvironmentVariableIsEmpty("QT_NO_XDG_DESKTOP_PORTAL")) {
        qputenv("QT_NO_XDG_DESKTOP_PORTAL", "1");
    }

#if FRIEDASBIRDVIEW_FLATPAK
    // Use the document portal for QFileDialog. It lets the user select a
    // public CA from a protected host path without granting the application
    // unrestricted access to that path.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME")) {
        qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
    }
#endif

    // Wayland deliberately does not let normal application windows restore an
    // absolute position. Plasma starts Xwayland and exports DISPLAY, so use the
    // Qt X11 backend for this persistent, non-activating overlay. An explicit
    // QT_QPA_PLATFORM setting always takes precedence.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
        && !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
        && !qEnvironmentVariableIsEmpty("DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", "xcb");
    }
#endif
    // When portal integration is active (for example in a sandbox), it looks
    // up this ID while QApplication starts. It is also the desktop-entry ID
    // used by the rest of the desktop integration.
    QGuiApplication::setDesktopFileName(QStringLiteral(FRIEDASBIRDVIEW_APPLICATION_ID));
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationDomain(QStringLiteral("org.friedasbirdview"));
    QCoreApplication::setOrganizationName(QStringLiteral("FriedasBirdview"));
    QCoreApplication::setApplicationName(QStringLiteral("FriedasBirdview"));
    app.setApplicationDisplayName(QStringLiteral("FriedasBirdview"));
    app.setApplicationName(QStringLiteral("FriedasBirdview"));
    app.setWindowIcon(QIcon(QStringLiteral(":/resources/icons/friedasbirdview.png")));
    app.setQuitOnLastWindowClosed(false);

    AppController controller;
    controller.start();
    return app.exec();
}
