#pragma once

#include <QHash>
#include <QObject>

#include "AutostartManager.h"
#include "FrigateMonitor.h"

class QAction;
class OverlayWindow;
class QMenu;
class QSystemTrayIcon;
class SettingsDialog;

class AppController final : public QObject {
    Q_OBJECT

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    void start();

private:
    void rebuildMenu();
    void updateTray(FrigateMonitor::ConnectionState state);
    void showAbout();

    FrigateMonitor m_monitor;
    AutostartManager m_autostart;
    OverlayWindow *m_overlay = nullptr;
    SettingsDialog *m_settings = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_statusAction = nullptr;
    QAction *m_lastEventAction = nullptr;
    QAction *m_monitoringAction = nullptr;
    QHash<int, QAction *> m_durationActions;
    FrigateMonitor::ConnectionState m_lastConnectionState = FrigateMonitor::ConnectionState::Idle;
};
