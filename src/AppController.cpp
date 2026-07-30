#include "AppController.h"

#include "AppBuildConfig.h"
#include "OverlayWindow.h"
#include "SettingsDialog.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFrame>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QVBoxLayout>

namespace {
constexpr int kConnectionNotificationGraceMs = 10000;

QIcon normalIcon()
{
    return QIcon(QStringLiteral(":/resources/icons/friedasbirdview.png"));
}
}

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_monitor(this)
    , m_autostart(this)
    , m_overlay(new OverlayWindow(m_monitor.customCaTrustAnchors()))
    , m_settings(new SettingsDialog(&m_monitor, &m_autostart))
    , m_tray(new QSystemTrayIcon(normalIcon(), this))
    , m_menu(new QMenu)
{
    m_overlay->setMonitor(&m_monitor);
    m_tray->setToolTip(QStringLiteral("FriedasBirdview"));
    m_tray->setContextMenu(m_menu);

    // A short broker reconnect is common when a laptop wakes or a network
    // briefly roams. Keep the tray state accurate immediately, but wait
    // before creating a desktop notification (which plays a sound on Windows).
    m_connectionLossTimer.setSingleShot(true);
    connect(&m_connectionLossTimer, &QTimer::timeout, this, [this] {
        if (!m_initialConnectionEstablished
            || m_lastConnectionState != FrigateMonitor::ConnectionState::Failed) {
            return;
        }

        m_connectionLossNotificationShown = true;
        m_tray->showMessage(
            QStringLiteral("FriedasBirdview: connection lost"),
            m_monitor.connectionStateTitle(),
            QSystemTrayIcon::Critical,
            4000
        );
    });

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

    // Startup naturally goes through Connecting and, with MQTT, can briefly
    // report a transient socket state before the broker has completed its
    // handshake. The tray colour and menu still reflect that state, but do
    // not present a misleading lost/restored pair until the app has made its
    // first successful connection.
    if (!m_initialConnectionEstablished) {
        if (state == FrigateMonitor::ConnectionState::Connected) {
            m_initialConnectionEstablished = true;
        }
        m_lastConnectionState = state;
        return;
    }

    if (state == FrigateMonitor::ConnectionState::Failed && !wasFailed) {
        m_connectionLossNotificationShown = false;
        m_connectionLossTimer.start(kConnectionNotificationGraceMs);
    } else if (state == FrigateMonitor::ConnectionState::Connected) {
        m_connectionLossTimer.stop();
        const bool notifyRestored = m_connectionLossNotificationShown;
        m_connectionLossNotificationShown = false;
        if (notifyRestored) {
            m_tray->showMessage(
                QStringLiteral("FriedasBirdview: connection restored"),
                QStringLiteral("Frigate is available again."),
                QSystemTrayIcon::Information,
                4000
            );
        }
    } else if (state == FrigateMonitor::ConnectionState::Idle) {
        m_connectionLossTimer.stop();
        m_connectionLossNotificationShown = false;
    }
    m_lastConnectionState = state;
}

void AppController::showAbout()
{
    QDialog dialog(m_settings);
    dialog.setWindowTitle(QStringLiteral("About FriedasBirdview"));
    dialog.setWindowIcon(normalIcon());
    dialog.setMinimumWidth(520);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(32, 28, 32, 24);
    layout->setSpacing(12);

    auto *icon = new QLabel(&dialog);
    icon->setPixmap(normalIcon().pixmap(112, 112));
    icon->setAlignment(Qt::AlignHCenter);
    layout->addWidget(icon);

    auto *title = new QLabel(QStringLiteral("FriedasBirdview"), &dialog);
    QFont titleFont = title->font();
    titleFont.setPointSize(22);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignHCenter);
    layout->addWidget(title);

    auto *version = new QLabel(
        QStringLiteral("Version %1").arg(QString::fromLatin1(FRIEDASBIRDVIEW_VERSION)),
        &dialog
    );
    version->setAlignment(Qt::AlignHCenter);
    layout->addWidget(version);

    auto *description = new QLabel(
        QStringLiteral("A small %1 activity companion compatible with Frigate.")
            .arg(QString::fromLatin1(FRIEDASBIRDVIEW_ABOUT_PLATFORM)),
        &dialog
    );
    description->setWordWrap(true);
    description->setAlignment(Qt::AlignHCenter);
    layout->addWidget(description);

    auto *separator = new QFrame(&dialog);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    layout->addWidget(separator);

    auto *links = new QLabel(
        QStringLiteral(
            "<a href=\"https://github.com/escapechen/FriedasBirdview\">"
            "FriedasBirdview on GitHub</a><br>"
            "<a href=\"https://github.com/escapechen/FriedasBirdview/blob/main/CHANGELOG.md\">"
            "View changelog</a>"
        ),
        &dialog
    );
    links->setTextFormat(Qt::RichText);
    links->setTextInteractionFlags(Qt::TextBrowserInteraction);
    links->setOpenExternalLinks(true);
    links->setAlignment(Qt::AlignHCenter);
    layout->addWidget(links);

    auto *credits = new QLabel(
        QStringLiteral(
            "Built by Marcel Kühn with OpenAI Codex<br>"
            "GPT-5.6 Terra · Extra High reasoning<br><br>"
            "With thanks to Frigate and its open-source community.<br><br>"
            "Released under the MIT License."
        ),
        &dialog
    );
    credits->setTextFormat(Qt::RichText);
    credits->setAlignment(Qt::AlignHCenter);
    layout->addWidget(credits);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
}
