#pragma once

#include <QDialog>

#include "FrigateMonitor.h"

class AutostartManager;
class QLabel;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QSlider;
class QWidget;
class QTabWidget;

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(FrigateMonitor *monitor, AutostartManager *autostart, QWidget *parent = nullptr);

    void showSettings();

private:
    void synchronize();
    void refreshClassificationList();
    void refreshCustomCaList();
    void updateSoundAlertControls();
    void updateCooldownControls();
    void updateEventDeliveryControls();
    void updateAutostartControls();
    void setAutostartEnabled(bool enabled);
    void handleAutostartChangeFinished(bool enabled, const QString &error);
    QString autostartPortalParentWindowId() const;
    void applySettings();
    void applyMqttSettings();
    void verifyMqttConnection();
    bool saveMqttSettings();
    void markMqttSettingsDirty();
    void addClassification();
    void addCustomCaCertificates();
    void removeCustomCaCertificates();

    FrigateMonitor *m_monitor = nullptr;
    AutostartManager *m_autostart = nullptr;
    QLineEdit *m_serverAddress = nullptr;
    QLineEdit *m_username = nullptr;
    QLineEdit *m_password = nullptr;
    QCheckBox *m_autostartEnabled = nullptr;
    QLabel *m_autostartHint = nullptr;
    QSpinBox *m_duration = nullptr;
    QComboBox *m_feedMode = nullptr;
    QComboBox *m_livePlaybackMethod = nullptr;
    QSpinBox *m_liveStartupTimeout = nullptr;
    QCheckBox *m_liveDebugEnabled = nullptr;
    QCheckBox *m_fastEventPollingEnabled = nullptr;
    QTabWidget *m_tabs = nullptr;
    QComboBox *m_eventDeliveryMode = nullptr;
    QWidget *m_httpPollingSection = nullptr;
    QWidget *m_mqttSettingsSection = nullptr;
    QLineEdit *m_mqttBrokerHost = nullptr;
    QSpinBox *m_mqttBrokerPort = nullptr;
    QCheckBox *m_mqttUseTls = nullptr;
    QLineEdit *m_mqttUsername = nullptr;
    QLineEdit *m_mqttPassword = nullptr;
    QLineEdit *m_mqttTopicPrefix = nullptr;
    QPushButton *m_applyMqttSettings = nullptr;
    QPushButton *m_verifyMqttConnection = nullptr;
    QLabel *m_mqttVerificationStatus = nullptr;
    QLabel *m_eventDeliveryStatus = nullptr;
    bool m_mqttSettingsDirty = false;
    QString m_mqttDraftError;
    QCheckBox *m_soundAlertEnabled = nullptr;
    QComboBox *m_alertSound = nullptr;
    QSlider *m_soundAlertVolume = nullptr;
    QLabel *m_soundAlertVolumeLabel = nullptr;
    QPushButton *m_previewSoundAlert = nullptr;
    QCheckBox *m_soundCooldownEnabled = nullptr;
    QSpinBox *m_soundCooldownSeconds = nullptr;
    QComboBox *m_popupTrigger = nullptr;
    QCheckBox *m_popupCooldownEnabled = nullptr;
    QSpinBox *m_popupCooldownSeconds = nullptr;
    QWidget *m_classificationSection = nullptr;
    QListWidget *m_classifications = nullptr;
    QLineEdit *m_newClassification = nullptr;
    QPushButton *m_refreshClassifications = nullptr;
    QLabel *m_classificationHint = nullptr;
    QListWidget *m_customCaCertificates = nullptr;
    QLabel *m_customCaHint = nullptr;
    QPushButton *m_removeCustomCa = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_monitorButton = nullptr;
};
