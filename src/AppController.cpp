#include "AppController.h"

#include "OverlayWindow.h"
#include "SettingsDialog.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QIcon>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>
#include <QStyle>
#include <QSystemTrayIcon>

namespace {
QIcon normalIcon()
{
    return QIcon(QStringLiteral(":/resources/icons/friedasbirdview.png"));
}
}

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_monitor(this)
    , m_autostart(this)
    , m_overlay(new OverlayWindow)
    , m_settings(new SettingsDialog(&m_monitor, &m_autostart))
    , m_tray(new QSystemTrayIcon(normalIcon(), this))
    , m_menu(new QMenu)
{
    m_overlay->setMonitor(&m_monitor);
    m_tray->setToolTip(QStringLiteral("FriedasBirdview"));
    m_tray->setContextMenu(m_menu);

    connect(&m_monitor, &FrigateMonitor::overlayVisibilityChanged, this, [this](bool visible) {
        visible ? m_overlay->present() : m_overlay->hide();
    });
    connect(&m_monitor, &FrigateMonitor::connectionStateChanged, this, [this](FrigateMonitor::ConnectionState state) {
        updateTray(state);
        rebuildMenu();
    });
    connect(&m_monitor, &FrigateMonitor::settingsChanged, this, &AppController::rebuildMenu);
    connect(&m_monitor, &FrigateMonitor::monitoringChanged, this, &AppController::rebuildMenu);

    rebuildMenu();
}

AppController::~AppController()
{
    m_tray->hide();
    m_tray->setContextMenu(nullptr);
    delete m_menu;
    delete m_settings;
    delete m_overlay;
}

void AppController::start()
{
    m_tray->show();
    m_monitor.start();
}

void AppController::rebuildMenu()
{
    if (!m_statusAction) {
        // Plasma reads this action tree through the D-Bus menu protocol. Keep
        // it stable for the lifetime of the tray icon; only update state below.
        QAction *title = m_menu->addAction(QStringLiteral("FriedasBirdview"));
        title->setEnabled(false);
        m_statusAction = m_menu->addAction(QString());
        m_statusAction->setEnabled(false);
        m_lastEventAction = m_menu->addAction(QString());
        m_lastEventAction->setEnabled(false);
        m_menu->addSeparator();

        QAction *settingsAction = m_menu->addAction(QStringLiteral("Settings…"));
        settingsAction->setShortcut(QKeySequence::Preferences);
        connect(settingsAction, &QAction::triggered, m_settings, &SettingsDialog::showSettings);
        QAction *aboutAction = m_menu->addAction(QStringLiteral("About FriedasBirdview"));
        connect(aboutAction, &QAction::triggered, this, &AppController::showAbout);

        auto *durationMenu = new QMenu(QStringLiteral("Keep Feed Open"), m_menu);
        auto *durationGroup = new QActionGroup(durationMenu);
        durationGroup->setExclusive(true);
        for (const int seconds : {5, 10, 20, 30, 60, 120}) {
            QAction *action = durationMenu->addAction(QStringLiteral("%1 seconds").arg(seconds));
            action->setCheckable(true);
            durationGroup->addAction(action);
            m_durationActions.insert(seconds, action);
            connect(action, &QAction::triggered, this, [this, seconds] {
                m_monitor.setOverlayDurationSeconds(seconds);
            });
        }
        m_menu->addMenu(durationMenu);
        m_menu->addSeparator();

        m_monitoringAction = m_menu->addAction(QString());
        connect(m_monitoringAction, &QAction::triggered, &m_monitor, &FrigateMonitor::toggleMonitoring);
        QAction *showFeedAction = m_menu->addAction(QStringLiteral("Show Feed"));
        connect(showFeedAction, &QAction::triggered, &m_monitor, &FrigateMonitor::showFeed);
        m_menu->addSeparator();
        QAction *quitAction = m_menu->addAction(QStringLiteral("Quit FriedasBirdview"));
        connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
    }

    m_statusAction->setText(QStringLiteral("Status: %1").arg(m_monitor.connectionStateTitle()));
    m_lastEventAction->setText(QStringLiteral("Last: %1").arg(m_monitor.lastEventDescription()));
    for (auto it = m_durationActions.cbegin(); it != m_durationActions.cend(); ++it) {
        it.value()->setChecked(it.key() == m_monitor.overlayDurationSeconds());
    }
    m_monitoringAction->setText(m_monitor.isMonitoring()
        ? QStringLiteral("Pause Monitoring")
        : QStringLiteral("Start Monitoring"));
}

void AppController::updateTray(FrigateMonitor::ConnectionState state)
{
    if (state == FrigateMonitor::ConnectionState::Failed) {
        m_tray->setIcon(QApplication::style()->standardIcon(QStyle::SP_MessageBoxCritical));
    } else {
        m_tray->setIcon(normalIcon());
    }
    m_tray->setToolTip(QStringLiteral("FriedasBirdview — %1").arg(m_monitor.connectionStateTitle()));

    const bool wasFailed = m_lastConnectionState == FrigateMonitor::ConnectionState::Failed;
    if (state == FrigateMonitor::ConnectionState::Failed && !wasFailed) {
        m_tray->showMessage(
            QStringLiteral("FriedasBirdview: connection lost"),
            m_monitor.connectionStateTitle(),
            QSystemTrayIcon::Critical,
            4000
        );
    } else if (wasFailed && state == FrigateMonitor::ConnectionState::Connected) {
        m_tray->showMessage(
            QStringLiteral("FriedasBirdview: connection restored"),
            QStringLiteral("Frigate is available again."),
            QSystemTrayIcon::Information,
            4000
        );
    }
    m_lastConnectionState = state;
}

void AppController::showAbout()
{
    QMessageBox::about(
        m_settings,
        QStringLiteral("About FriedasBirdview"),
        QStringLiteral(
            "<h3>FriedasBirdview</h3>"
            "<p>A small KDE Plasma activity companion compatible with Frigate.</p>"
            "<p>Built by Marcel Kühn with OpenAI Codex<br>"
            "GPT-5.6 Terra · Extra High reasoning</p>"
            "<p>Released under the MIT License.</p>"
        )
    );
}
