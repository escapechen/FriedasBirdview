#include "SettingsDialog.h"

#include "AppBuildConfig.h"
#include "AutostartManager.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {
QString customCaToolTip()
{
    QString text = QStringLiteral(
        "Add only a public root or issuing CA that issued Frigate’s HTTPS certificate. "
        "Host-name, expiry, and certificate-chain validation remain enabled."
    );
#if FRIEDASBIRDVIEW_FLATPAK
    text += QStringLiteral(
        "\n\nFlatpak uses your system file chooser to grant access to the selected file. "
        "It opens in Downloads; use the chooser’s location control to select /etc/ssl/certs. "
        "If your desktop does not allow that folder, copy the public PEM or DER file to Downloads first."
    );
#endif
    return text;
}

QString customCaInitialDirectory()
{
#if FRIEDASBIRDVIEW_FLATPAK
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
#else
    return {};
#endif
}
}

SettingsDialog::SettingsDialog(FrigateMonitor *monitor, AutostartManager *autostart, QWidget *parent)
    : QDialog(parent)
    , m_monitor(monitor)
    , m_autostart(autostart)
{
    setWindowTitle(QStringLiteral("FriedasBirdview Settings"));
    setModal(false);
    setMinimumWidth(500);
    resize(540, 740);

    auto *dialogLayout = new QVBoxLayout(this);
    dialogLayout->setContentsMargins(6, 4, 6, 4);
    dialogLayout->setSpacing(4);
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scrollArea);
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(4);
    layout->setAlignment(Qt::AlignTop);
    // The settings form is taller than a short Plasma window once it contains
    // several classifications.  Preserve its minimum geometry and scroll the
    // form rather than allowing a child widget to overflow a later control.
    layout->setSizeConstraint(QLayout::SetMinimumSize);

    const auto compactGroup = [](QGroupBox *group) {
        group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    };

    auto *startupGroup = new QGroupBox(QStringLiteral("Startup"), this);
    compactGroup(startupGroup);
    auto *startupLayout = new QVBoxLayout(startupGroup);
    startupLayout->setContentsMargins(6, 2, 6, 6);
    m_autostartEnabled = new QCheckBox(QStringLiteral("Start FriedasBirdview automatically when I sign in"), startupGroup);
    m_autostartEnabled->setToolTip(QStringLiteral(
        "Creates a standard XDG desktop-autostart entry that starts the tray app with monitoring enabled."
    ));
    startupLayout->addWidget(m_autostartEnabled);
    m_autostartHint = new QLabel(startupGroup);
    m_autostartHint->setWordWrap(true);
    m_autostartHint->setStyleSheet(QStringLiteral("color: palette(mid);"));
    m_autostartHint->setVisible(false);
    startupLayout->addWidget(m_autostartHint);
    layout->addWidget(startupGroup);

    auto *connectionGroup = new QGroupBox(QStringLiteral("Frigate connection"), this);
    compactGroup(connectionGroup);
    auto *connectionLayout = new QFormLayout(connectionGroup);
    connectionLayout->setContentsMargins(6, 2, 6, 6);
    connectionLayout->setVerticalSpacing(2);
    m_serverAddress = new QLineEdit(connectionGroup);
    m_serverAddress->setPlaceholderText(QStringLiteral("https://frigate.example.net:8971"));
    auto *applyButton = new QPushButton(QStringLiteral("Apply"), connectionGroup);
    auto *serverLayout = new QHBoxLayout;
    serverLayout->addWidget(m_serverAddress);
    serverLayout->addWidget(applyButton);
    connectionLayout->addRow(QStringLiteral("Server:"), serverLayout);
    m_username = new QLineEdit(connectionGroup);
    m_username->setPlaceholderText(QStringLiteral("Username (optional)"));
    connectionLayout->addRow(QStringLiteral("Username:"), m_username);
    m_password = new QLineEdit(connectionGroup);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(QStringLiteral("Password (leave blank to keep the saved password)"));
    m_password->setToolTip(QStringLiteral("Passwords are stored only in KDE Wallet, never in the app settings."));
    connectionLayout->addRow(QStringLiteral("Password:"), m_password);
    layout->addWidget(connectionGroup);

    auto *certificateGroup = new QGroupBox(QStringLiteral("Custom certificate authorities"), this);
    compactGroup(certificateGroup);
    certificateGroup->setToolTip(customCaToolTip());
    auto *certificateLayout = new QVBoxLayout(certificateGroup);
    certificateLayout->setContentsMargins(6, 2, 6, 6);
    certificateLayout->setSpacing(2);
    m_customCaCertificates = new QListWidget(certificateGroup);
    m_customCaCertificates->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_customCaCertificates->setFixedHeight(56);
    m_customCaCertificates->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    certificateLayout->addWidget(m_customCaCertificates);
    auto *certificateControls = new QHBoxLayout;
    auto *addCustomCa = new QPushButton(QStringLiteral("Add CA certificate…"), certificateGroup);
    addCustomCa->setToolTip(certificateGroup->toolTip());
    m_removeCustomCa = new QPushButton(QStringLiteral("Remove selected"), certificateGroup);
    certificateControls->addWidget(addCustomCa);
    certificateControls->addWidget(m_removeCustomCa);
    certificateControls->addStretch();
    certificateLayout->addLayout(certificateControls);
    m_customCaHint = new QLabel(certificateGroup);
    m_customCaHint->setWordWrap(true);
    m_customCaHint->setStyleSheet(QStringLiteral("color: palette(mid);"));
    m_customCaHint->setVisible(false);
    certificateLayout->addWidget(m_customCaHint);
    layout->addWidget(certificateGroup);

    auto *feedGroup = new QGroupBox(QStringLiteral("Feed"), this);
    compactGroup(feedGroup);
    auto *feedLayout = new QFormLayout(feedGroup);
    feedLayout->setContentsMargins(6, 2, 6, 6);
    feedLayout->setVerticalSpacing(2);
    m_duration = new QSpinBox(feedGroup);
    m_duration->setRange(5, 120);
    m_duration->setSingleStep(5);
    m_duration->setSuffix(QStringLiteral(" seconds"));
    feedLayout->addRow(QStringLiteral("Keep feed open:"), m_duration);
    m_feedMode = new QComboBox(feedGroup);
    m_feedMode->addItem(QStringLiteral("JPEG snapshots — one image every 0.5 seconds"), static_cast<int>(FrigateMonitor::FeedMode::Jpeg));
    m_feedMode->addItem(QStringLiteral("Live stream — low-latency go2rtc MSE video"), static_cast<int>(FrigateMonitor::FeedMode::LiveStream));
    feedLayout->addRow(QStringLiteral("Mode:"), m_feedMode);
    layout->addWidget(feedGroup);

    auto *soundGroup = new QGroupBox(QStringLiteral("Sound alerts"), this);
    compactGroup(soundGroup);
    soundGroup->setToolTip(QStringLiteral(
        "Sounds play only for newly detected matching activity, never repeatedly for the same event."
    ));
    auto *soundLayout = new QFormLayout(soundGroup);
    soundLayout->setContentsMargins(6, 2, 6, 6);
    soundLayout->setVerticalSpacing(2);
    m_soundAlertEnabled = new QCheckBox(QStringLiteral("Play a sound for a new popup"), soundGroup);
    soundLayout->addRow(m_soundAlertEnabled);
    m_alertSound = new QComboBox(soundGroup);
    m_alertSound->addItem(QStringLiteral("Gentle chime"), static_cast<int>(FrigateMonitor::AlertSound::GentleChime));
    m_alertSound->addItem(QStringLiteral("Bright chime"), static_cast<int>(FrigateMonitor::AlertSound::BrightChime));
    m_alertSound->addItem(QStringLiteral("Bell"), static_cast<int>(FrigateMonitor::AlertSound::Bell));
    auto *soundControls = new QHBoxLayout;
    soundControls->addWidget(m_alertSound, 1);
    m_previewSoundAlert = new QPushButton(QStringLiteral("Preview"), soundGroup);
    soundControls->addWidget(m_previewSoundAlert);
    soundLayout->addRow(QStringLiteral("Sound:"), soundControls);
    m_soundAlertVolume = new QSlider(Qt::Horizontal, soundGroup);
    m_soundAlertVolume->setRange(0, 100);
    m_soundAlertVolume->setSingleStep(5);
    m_soundAlertVolumeLabel = new QLabel(soundGroup);
    m_soundAlertVolumeLabel->setMinimumWidth(38);
    auto *volumeControls = new QHBoxLayout;
    volumeControls->addWidget(m_soundAlertVolume, 1);
    volumeControls->addWidget(m_soundAlertVolumeLabel);
    soundLayout->addRow(QStringLiteral("Volume:"), volumeControls);
    m_soundCooldownEnabled = new QCheckBox(QStringLiteral("Enable"), soundGroup);
    m_soundCooldownEnabled->setToolTip(QStringLiteral(
        "Suppresses automatic alert sounds for the selected interval. Preview always remains available."
    ));
    m_soundCooldownSeconds = new QSpinBox(soundGroup);
    m_soundCooldownSeconds->setRange(1, 3600);
    m_soundCooldownSeconds->setSingleStep(5);
    m_soundCooldownSeconds->setSuffix(QStringLiteral(" seconds"));
    m_soundCooldownSeconds->setMaximumWidth(130);
    auto *soundCooldownControls = new QHBoxLayout;
    soundCooldownControls->addWidget(m_soundCooldownEnabled);
    soundCooldownControls->addWidget(m_soundCooldownSeconds);
    soundCooldownControls->addStretch();
    soundLayout->addRow(QStringLiteral("Sound cooldown:"), soundCooldownControls);
    layout->addWidget(soundGroup);

    auto *popupGroup = new QGroupBox(QStringLiteral("Popup triggers"), this);
    compactGroup(popupGroup);
    auto *popupLayout = new QVBoxLayout(popupGroup);
    popupLayout->setContentsMargins(6, 2, 6, 6);
    popupLayout->setSpacing(2);
    auto *triggerForm = new QFormLayout;
    m_popupTrigger = new QComboBox(popupGroup);
    m_popupTrigger->addItem(QStringLiteral("Selected classifications"), static_cast<int>(FrigateMonitor::PopupTrigger::SelectedClassifications));
    m_popupTrigger->addItem(QStringLiteral("Any tracked object"), static_cast<int>(FrigateMonitor::PopupTrigger::AnyObject));
    triggerForm->addRow(QStringLiteral("Popup for:"), m_popupTrigger);
    m_popupCooldownEnabled = new QCheckBox(QStringLiteral("Enable"), popupGroup);
    m_popupCooldownEnabled->setToolTip(QStringLiteral(
        "Suppresses opening or resetting the feed popup for the selected interval. Manual Show Feed remains available."
    ));
    m_popupCooldownSeconds = new QSpinBox(popupGroup);
    m_popupCooldownSeconds->setRange(1, 3600);
    m_popupCooldownSeconds->setSingleStep(5);
    m_popupCooldownSeconds->setSuffix(QStringLiteral(" seconds"));
    m_popupCooldownSeconds->setMaximumWidth(130);
    auto *popupCooldownControls = new QHBoxLayout;
    popupCooldownControls->addWidget(m_popupCooldownEnabled);
    popupCooldownControls->addWidget(m_popupCooldownSeconds);
    popupCooldownControls->addStretch();
    triggerForm->addRow(QStringLiteral("Popup cooldown:"), popupCooldownControls);
    popupLayout->addLayout(triggerForm);

    m_classificationSection = new QWidget(popupGroup);
    auto *classificationLayout = new QVBoxLayout(m_classificationSection);
    classificationLayout->setContentsMargins(0, 0, 0, 0);
    auto *classificationHeader = new QHBoxLayout;
    classificationHeader->addWidget(new QLabel(QStringLiteral("Classifications"), m_classificationSection));
    classificationHeader->addStretch();
    m_refreshClassifications = new QPushButton(QStringLiteral("Refresh from Frigate"), m_classificationSection);
    classificationHeader->addWidget(m_refreshClassifications);
    classificationLayout->addLayout(classificationHeader);
    m_classifications = new QListWidget(m_classificationSection);
    // Keep the controls below the list visible.  QListWidget has an expanding
    // vertical size policy by default, which let it consume the popup group's
    // spare height and visually cover the add row on Plasma.
    m_classifications->setFixedHeight(96);
    m_classifications->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_classifications->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_classifications->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    classificationLayout->addWidget(m_classifications);
    auto *addLayout = new QHBoxLayout;
    m_newClassification = new QLineEdit(m_classificationSection);
    m_newClassification->setPlaceholderText(QStringLiteral("Add a classification, for example Frieda"));
    auto *addButton = new QPushButton(QStringLiteral("Add"), m_classificationSection);
    addLayout->addWidget(m_newClassification);
    addLayout->addWidget(addButton);
    classificationLayout->addLayout(addLayout);
    m_classificationHint = new QLabel(m_classificationSection);
    m_classificationHint->setWordWrap(true);
    m_classificationHint->setStyleSheet(QStringLiteral("color: palette(mid);"));
    classificationLayout->addWidget(m_classificationHint);
    popupLayout->addWidget(m_classificationSection);
    layout->addWidget(popupGroup);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto *controls = new QHBoxLayout;
    m_monitorButton = new QPushButton(this);
    auto *showFeedButton = new QPushButton(QStringLiteral("Show Feed"), this);
    auto *closeButton = new QPushButton(QStringLiteral("Close"), this);
    controls->addWidget(m_monitorButton);
    controls->addWidget(showFeedButton);
    controls->addStretch();
    controls->addWidget(closeButton);
    layout->addLayout(controls);

    scrollArea->setWidget(content);
    dialogLayout->addWidget(scrollArea);

    connect(applyButton, &QPushButton::clicked, this, &SettingsDialog::applySettings);
    connect(m_serverAddress, &QLineEdit::returnPressed, this, &SettingsDialog::applySettings);
    connect(m_autostartEnabled, &QCheckBox::toggled, this, &SettingsDialog::setAutostartEnabled);
    connect(m_autostart, &AutostartManager::changeFinished,
        this, &SettingsDialog::handleAutostartChangeFinished);
    connect(m_duration, &QSpinBox::valueChanged, this, [this](int seconds) {
        m_monitor->setOverlayDurationSeconds(seconds);
    });
    connect(m_feedMode, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_monitor->setFeedMode(static_cast<FrigateMonitor::FeedMode>(m_feedMode->itemData(index).toInt()));
    });
    connect(m_soundAlertEnabled, &QCheckBox::toggled, m_monitor, &FrigateMonitor::setSoundAlertEnabled);
    connect(m_alertSound, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_monitor->setAlertSound(static_cast<FrigateMonitor::AlertSound>(m_alertSound->itemData(index).toInt()));
    });
    connect(m_soundAlertVolume, &QSlider::valueChanged, m_monitor, &FrigateMonitor::setSoundAlertVolume);
    connect(m_previewSoundAlert, &QPushButton::clicked, m_monitor, &FrigateMonitor::previewSoundAlert);
    connect(m_soundCooldownEnabled, &QCheckBox::toggled, m_monitor, &FrigateMonitor::setSoundCooldownEnabled);
    connect(m_soundCooldownSeconds, &QSpinBox::valueChanged, m_monitor, &FrigateMonitor::setSoundCooldownSeconds);
    connect(m_popupTrigger, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_monitor->setPopupTrigger(static_cast<FrigateMonitor::PopupTrigger>(m_popupTrigger->itemData(index).toInt()));
    });
    connect(m_popupCooldownEnabled, &QCheckBox::toggled, m_monitor, &FrigateMonitor::setPopupCooldownEnabled);
    connect(m_popupCooldownSeconds, &QSpinBox::valueChanged, m_monitor, &FrigateMonitor::setPopupCooldownSeconds);
    connect(m_refreshClassifications, &QPushButton::clicked, m_monitor, &FrigateMonitor::refreshAvailableClassifications);
    connect(addButton, &QPushButton::clicked, this, &SettingsDialog::addClassification);
    connect(m_newClassification, &QLineEdit::returnPressed, this, &SettingsDialog::addClassification);
    connect(m_classifications, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        m_monitor->setClassification(item->text(), item->checkState() == Qt::Checked);
    });
    connect(addCustomCa, &QPushButton::clicked, this, &SettingsDialog::addCustomCaCertificates);
    connect(m_removeCustomCa, &QPushButton::clicked, this, &SettingsDialog::removeCustomCaCertificates);
    connect(m_customCaCertificates, &QListWidget::itemSelectionChanged, this, [this] {
        m_removeCustomCa->setEnabled(!m_customCaCertificates->selectedItems().isEmpty());
    });
    connect(m_monitorButton, &QPushButton::clicked, m_monitor, &FrigateMonitor::toggleMonitoring);
    connect(showFeedButton, &QPushButton::clicked, m_monitor, &FrigateMonitor::showFeed);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::hide);
    connect(m_monitor, &FrigateMonitor::settingsChanged, this, &SettingsDialog::synchronize);
    connect(m_monitor, &FrigateMonitor::classificationsChanged, this, &SettingsDialog::refreshClassificationList);
    connect(m_monitor, &FrigateMonitor::customCaCertificatesChanged, this, &SettingsDialog::refreshCustomCaList);

    synchronize();
    refreshClassificationList();
    refreshCustomCaList();
}

void SettingsDialog::showSettings()
{
    synchronize();
    m_monitor->refreshAvailableClassifications();
    show();
    raise();
    activateWindow();
}

void SettingsDialog::synchronize()
{
    if (!m_serverAddress->hasFocus()) {
        m_serverAddress->setText(m_monitor->serverAddress());
    }
    if (!m_username->hasFocus()) {
        m_username->setText(m_monitor->username());
    }
    {
        const QSignalBlocker blockDuration(m_duration);
        const QSignalBlocker blockFeed(m_feedMode);
        const QSignalBlocker blockTrigger(m_popupTrigger);
        const QSignalBlocker blockSoundEnabled(m_soundAlertEnabled);
        const QSignalBlocker blockAlertSound(m_alertSound);
        const QSignalBlocker blockSoundVolume(m_soundAlertVolume);
        const QSignalBlocker blockSoundCooldownEnabled(m_soundCooldownEnabled);
        const QSignalBlocker blockSoundCooldownSeconds(m_soundCooldownSeconds);
        const QSignalBlocker blockPopupCooldownEnabled(m_popupCooldownEnabled);
        const QSignalBlocker blockPopupCooldownSeconds(m_popupCooldownSeconds);
        m_duration->setValue(m_monitor->overlayDurationSeconds());
        m_feedMode->setCurrentIndex(m_feedMode->findData(static_cast<int>(m_monitor->feedMode())));
        m_popupTrigger->setCurrentIndex(m_popupTrigger->findData(static_cast<int>(m_monitor->popupTrigger())));
        m_soundAlertEnabled->setChecked(m_monitor->isSoundAlertEnabled());
        m_alertSound->setCurrentIndex(m_alertSound->findData(static_cast<int>(m_monitor->alertSound())));
        m_soundAlertVolume->setValue(m_monitor->soundAlertVolume());
        m_soundCooldownEnabled->setChecked(m_monitor->isSoundCooldownEnabled());
        m_soundCooldownSeconds->setValue(m_monitor->soundCooldownSeconds());
        m_popupCooldownEnabled->setChecked(m_monitor->isPopupCooldownEnabled());
        m_popupCooldownSeconds->setValue(m_monitor->popupCooldownSeconds());
    }
    updateAutostartControls();
    updateSoundAlertControls();
    updateCooldownControls();
    m_classificationSection->setVisible(m_monitor->popupTrigger() == FrigateMonitor::PopupTrigger::SelectedClassifications);
    m_monitorButton->setText(m_monitor->isMonitoring() ? QStringLiteral("Pause Monitoring") : QStringLiteral("Start Monitoring"));
    m_status->setText(QStringLiteral("Status: %1\nLast: %2").arg(m_monitor->connectionStateTitle(), m_monitor->lastEventDescription()));
    refreshCustomCaList();
}

void SettingsDialog::updateAutostartControls()
{
    const QSignalBlocker blocker(m_autostartEnabled);
    if (!m_autostart->isSupported()) {
        m_autostartEnabled->setChecked(false);
        m_autostartEnabled->setEnabled(false);
        m_autostartEnabled->setToolTip(QStringLiteral("Automatic start needs the desktop Background portal."));
        m_autostartHint->setText(QStringLiteral(
            "Automatic start is unavailable because this desktop does not provide the Background portal required by Flatpak apps."
        ));
        m_autostartHint->setStyleSheet(QStringLiteral("color: palette(mid);"));
        m_autostartHint->setVisible(true);
        return;
    }

    m_autostartEnabled->setEnabled(true);
#if FRIEDASBIRDVIEW_FLATPAK
    m_autostartEnabled->setToolTip(QStringLiteral(
        "Asks your desktop to start FriedasBirdview after sign-in. The desktop will ask you to approve it."
    ));
#else
    m_autostartEnabled->setToolTip(QStringLiteral(
        "Creates a standard XDG desktop-autostart entry that starts the tray app with monitoring enabled."
    ));
#endif
    m_autostartEnabled->setChecked(m_autostart->isEnabled());
    m_autostartHint->clear();
    m_autostartHint->setVisible(false);
    m_autostartHint->setStyleSheet(QStringLiteral("color: palette(mid);"));
}

void SettingsDialog::setAutostartEnabled(bool enabled)
{
    m_autostartEnabled->setEnabled(false);
    m_autostartHint->setText(QStringLiteral("Asking the desktop to update automatic start…"));
    m_autostartHint->setStyleSheet(QStringLiteral("color: palette(mid);"));
    m_autostartHint->setVisible(true);
    m_autostart->setEnabled(enabled, autostartPortalParentWindowId());
}

void SettingsDialog::handleAutostartChangeFinished(bool enabled, const QString &error)
{
    const QSignalBlocker blocker(m_autostartEnabled);
    m_autostartEnabled->setChecked(enabled);
    if (error.isEmpty()) {
        updateAutostartControls();
        return;
    }

    m_autostartEnabled->setEnabled(m_autostart->isSupported());
    m_autostartHint->setText(error);
    m_autostartHint->setStyleSheet(QStringLiteral("color: #b3261e;"));
    m_autostartHint->setVisible(true);
}

QString SettingsDialog::autostartPortalParentWindowId() const
{
#if FRIEDASBIRDVIEW_FLATPAK
    const WId windowId = winId();
    if (windowId != 0) {
        return QStringLiteral("x11:%1").arg(static_cast<quintptr>(windowId), 0, 16);
    }
#endif
    return {};
}

void SettingsDialog::updateSoundAlertControls()
{
    const bool enabled = m_soundAlertEnabled->isChecked();
    m_alertSound->setEnabled(enabled);
    m_soundAlertVolume->setEnabled(enabled);
    m_previewSoundAlert->setEnabled(enabled);
    m_soundAlertVolumeLabel->setEnabled(enabled);
    m_soundAlertVolumeLabel->setText(QStringLiteral("%1%").arg(m_soundAlertVolume->value()));
}

void SettingsDialog::updateCooldownControls()
{
    const bool soundAlertsEnabled = m_soundAlertEnabled->isChecked();
    m_soundCooldownEnabled->setEnabled(soundAlertsEnabled);
    m_soundCooldownSeconds->setEnabled(soundAlertsEnabled && m_soundCooldownEnabled->isChecked());
    m_popupCooldownSeconds->setEnabled(m_popupCooldownEnabled->isChecked());
}

void SettingsDialog::refreshClassificationList()
{
    const QSignalBlocker blocker(m_classifications);
    m_classifications->clear();
    for (const QString &name : m_monitor->availableClassifications()) {
        auto *item = new QListWidgetItem(name, m_classifications);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m_monitor->selectedClassifications().contains(name, Qt::CaseInsensitive) ? Qt::Checked : Qt::Unchecked);
    }
    m_refreshClassifications->setEnabled(!m_monitor->isLoadingClassifications());
    if (m_monitor->isLoadingClassifications()) {
        m_classificationHint->setText(QStringLiteral("Loading classifications from Frigate…"));
        m_classificationHint->setVisible(true);
    } else if (!m_monitor->classificationLoadError().isEmpty()) {
        m_classificationHint->setText(m_monitor->classificationLoadError());
        m_classificationHint->setVisible(true);
    } else {
        m_classificationHint->clear();
        m_classificationHint->setVisible(false);
    }
}

void SettingsDialog::refreshCustomCaList()
{
    const QSignalBlocker blocker(m_customCaCertificates);
    m_customCaCertificates->clear();
    for (const CustomCaStore::Entry &entry : m_monitor->customCaCertificates()) {
        auto *item = new QListWidgetItem(entry.label, m_customCaCertificates);
        item->setData(Qt::UserRole, entry.id);
    }
    m_removeCustomCa->setEnabled(false);
    m_customCaHint->setVisible(!m_customCaHint->text().isEmpty());
}

void SettingsDialog::applySettings()
{
    if (m_monitor->applyConnectionSettings(m_serverAddress->text(), m_username->text(), m_password->text())) {
        m_password->clear();
        synchronize();
        m_monitor->refreshAvailableClassifications();
    }
}

void SettingsDialog::addClassification()
{
    const QString name = m_newClassification->text().trimmed();
    if (name.isEmpty()) {
        return;
    }
    m_monitor->setClassification(name, true);
    m_newClassification->clear();
}

void SettingsDialog::addCustomCaCertificates()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("Add certificate authority"),
        customCaInitialDirectory(),
        QStringLiteral("Certificate files (*.pem *.crt *.cer *.der);;All files (*)")
    );
    if (files.isEmpty()) {
        return;
    }

    QStringList results;
    QStringList failures;
    for (const QString &file : files) {
        QString summary;
        QString error;
        if (m_monitor->addCustomCaCertificate(file, &summary, &error)) {
            results.append(summary);
        } else {
            failures.append(error);
        }
    }
    m_customCaHint->setText(failures.isEmpty() ? results.join(QLatin1Char('\n')) : failures.join(QLatin1Char('\n')));
    m_customCaHint->setStyleSheet(failures.isEmpty()
        ? QStringLiteral("color: palette(mid);")
        : QStringLiteral("color: #b3261e;"));
    refreshCustomCaList();
}

void SettingsDialog::removeCustomCaCertificates()
{
    const QList<QListWidgetItem *> selected = m_customCaCertificates->selectedItems();
    if (selected.isEmpty()) {
        return;
    }
    QStringList failures;
    int removed = 0;
    for (QListWidgetItem *item : selected) {
        QString error;
        if (m_monitor->removeCustomCaCertificate(item->data(Qt::UserRole).toString(), &error)) {
            ++removed;
        } else {
            failures.append(error);
        }
    }
    m_customCaHint->setText(failures.isEmpty()
        ? QStringLiteral("Removed %1 custom CA certificate%2. Restart FriedasBirdview before using live video.")
              .arg(removed)
              .arg(removed == 1 ? QString() : QStringLiteral("s"))
        : failures.join(QLatin1Char('\n')));
    m_customCaHint->setStyleSheet(failures.isEmpty()
        ? QStringLiteral("color: palette(mid);")
        : QStringLiteral("color: #b3261e;"));
    refreshCustomCaList();
}
