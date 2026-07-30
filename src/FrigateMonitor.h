#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonValue>
#include <QList>
#include <QNetworkCookie>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QSslConfiguration>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include "CustomCaStore.h"
#include "CredentialStore.h"
#include "LivePlaybackMethod.h"
#include "MqttClient.h"

class QNetworkAccessManager;
class QNetworkCookieJar;
class QNetworkReply;
class QNetworkRequest;
class QAudioSink;
class QBuffer;

class FrigateMonitor final : public QObject {
    Q_OBJECT

public:
    enum class FeedMode { Jpeg, LiveStream };
    Q_ENUM(FeedMode)

    enum class PopupTrigger { SelectedClassifications, AnyObject };
    Q_ENUM(PopupTrigger)

    enum class AlertSound { GentleChime, BrightChime, Bell };
    Q_ENUM(AlertSound)

    enum class EventDeliveryMode { HttpPolling, Mqtt };
    Q_ENUM(EventDeliveryMode)

    enum class ConnectionState { Idle, Connecting, Connected, Failed };
    Q_ENUM(ConnectionState)

    struct Activity {
        QString title;
        QString confidence;
        QString camera;
        QDateTime timestamp;
    };

    explicit FrigateMonitor(QObject *parent = nullptr);

    QString serverAddress() const;
    QString username() const;
    int overlayDurationSeconds() const;
    FeedMode feedMode() const;
    LivePlaybackMethod livePlaybackMethod() const;
    int liveStartupTimeoutSeconds() const;
    bool isLiveDebugEnabled() const;
    bool isFastEventPollingEnabled() const;
    EventDeliveryMode eventDeliveryMode() const;
    QString mqttBrokerHost() const;
    int mqttBrokerPort() const;
    bool mqttUsesTls() const;
    QString mqttUsername() const;
    QString mqttTopicPrefix() const;
    QString eventDeliveryStatus() const;
    bool isMqttVerificationInProgress() const;
    QString mqttVerificationStatus() const;
    PopupTrigger popupTrigger() const;
    bool isPopupCooldownEnabled() const;
    int popupCooldownSeconds() const;
    bool isSoundAlertEnabled() const;
    AlertSound alertSound() const;
    int soundAlertVolume() const;
    bool isSoundCooldownEnabled() const;
    int soundCooldownSeconds() const;
    QStringList selectedClassifications() const;
    QStringList availableClassifications() const;
    bool isLoadingClassifications() const;
    QString classificationLoadError() const;
    bool isMonitoring() const;
    bool isOverlayVisible() const;
    QDateTime overlayDismissalTime() const;
    ConnectionState connectionState() const;
    QString connectionStateTitle() const;
    QString lastEventDescription() const;
    Activity activity() const;
    QString currentFeedCameraName() const;
    QString currentFeedStreamName() const;
    QUrl baseUrl() const;
    QList<QNetworkCookie> authenticationCookies() const;
    QList<CustomCaStore::Entry> customCaCertificates() const;
    QList<QSslCertificate> customCaTrustAnchors() const;

    void setOverlayDurationSeconds(int seconds);
    void setFeedMode(FeedMode mode);
    void setLivePlaybackMethod(LivePlaybackMethod method);
    void setLiveStartupTimeoutSeconds(int seconds);
    void setLiveDebugEnabled(bool enabled);
    void setFastEventPollingEnabled(bool enabled);
    void setEventDeliveryMode(EventDeliveryMode mode);
    void setPopupTrigger(PopupTrigger trigger);
    void setPopupCooldownEnabled(bool enabled);
    void setPopupCooldownSeconds(int seconds);
    void setSoundAlertEnabled(bool enabled);
    void setAlertSound(AlertSound sound);
    void setSoundAlertVolume(int volume);
    void setSoundCooldownEnabled(bool enabled);
    void setSoundCooldownSeconds(int seconds);
    void setClassification(const QString &name, bool selected);
    void refreshAvailableClassifications();
    bool addCustomCaCertificate(const QString &filePath, QString *summary, QString *error);
    bool removeCustomCaCertificate(const QString &id, QString *error);

    /// Validates and stores settings. A non-empty password is kept only in the OS credential store.
    bool applyConnectionSettings(const QString &address, const QString &username, const QString &password);
    /// Stores broker connection details. A non-empty password is kept only in the OS credential store.
    bool applyMqttSettings(const QString &host, int port, bool useTls, const QString &username,
        const QString &password, const QString &topicPrefix);
    void verifyMqttConnection();

public slots:
    void start();
    void stop();
    void toggleMonitoring();
    void showFeed();
    void dismissFeed();
    void requestSnapshot();
    void previewSoundAlert();

signals:
    void connectionStateChanged(FrigateMonitor::ConnectionState state);
    void monitoringChanged(bool monitoring);
    void overlayVisibilityChanged(bool visible);
    void overlayDismissalTimeChanged(const QDateTime &time);
    void activityChanged(const FrigateMonitor::Activity &activity);
    void settingsChanged();
    void classificationsChanged();
    void snapshotReady(const QByteArray &image);
    void snapshotFailed(const QString &message);
    void streamSessionChanged();
    void customCaCertificatesChanged();

private:
    struct Event {
        QString id;
        QString camera;
        QString label;
        QString subLabel;
        qint64 startTime = 0;
        double topScore = -1;
    };

    struct ReviewItem {
        QString id;
        QString camera;
        QStringList objectNames;
        qint64 startTime = 0;
        qint64 endTime = 0;
    };

    struct PollResult {
        bool eventsFinished = false;
        bool reviewsFinished = false;
        QString error;
    };

    void pollActivity();
    void authenticateThenPoll();
    void startPollRequests();
    void finishPollIfComplete();
    void startMqttDelivery();
    void stopMqttDelivery();
    bool mqttConfiguration(MqttClient::Configuration *configuration, QString *error) const;
    void completeMqttVerification(const QString &status);
    void handleMqttMessage(const QString &topic, const QByteArray &payload);
    void refreshLiveStreamNamesIfNeeded();
    void setConnectionState(ConnectionState state, const QString &detail = {});
    void showActivity(const Activity &activity);
    bool isWithinCooldown(const QDateTime &lastTrigger, int seconds, const QDateTime &now) const;
    void playSoundAlert(bool force);
    void stopSoundAlert();
    void resetSession();
    void updateTlsConfiguration();
    bool markActivitySeen(QSet<QString> &ids, QQueue<QString> &order, const QString &id);

    QUrl endpoint(const QString &path) const;
    QNetworkRequest requestForUrl(const QUrl &url) const;
    QString responseError(const QNetworkReply *reply) const;
    bool isRelevant(const Event &event) const;
    bool isRelevant(const ReviewItem &item) const;
    void handleEvents(const QList<Event> &events);
    void handleReviewItems(const QList<ReviewItem> &items);
    QList<Event> parseEvents(const QByteArray &data, bool *ok) const;
    QList<ReviewItem> parseReviewItems(const QByteArray &data, bool *ok) const;
    Event parseEventObject(const QJsonObject &object) const;
    ReviewItem parseReviewItemObject(const QJsonObject &object) const;
    QStringList parseStringList(const QJsonValue &value) const;
    QString normalizedName(const QString &name) const;
    QString displayName(const QString &name) const;
    QString describeTimestamp(const QDateTime &time) const;
    bool validateServerAddress(const QString &address, QUrl *url, QString *normalized, QString *error) const;
    bool validateMqttSettings(const QString &host, int port, const QString &topicPrefix,
        QString *normalizedHost, QString *normalizedTopicPrefix, QString *error) const;

    bool updateCredentials(const QString &newUsername, const QString &password, QString *error);
    bool loadPassword(const QString &username, QString *password, QString *error) const;
    bool hasStoredPassword(const QString &username, QString *error) const;
    bool savePassword(const QString &username, const QString &password, QString *error) const;
    bool deletePassword(const QString &username, QString *error) const;
    QString credentialKey(const QString &username) const;
    bool updateMqttCredentials(const QString &host, const QString &username, const QString &password, QString *error);
    bool loadMqttPassword(QString *password, QString *error) const;
    bool hasStoredMqttPassword(const QString &host, const QString &username, QString *error) const;
    bool saveMqttPassword(const QString &host, const QString &username, const QString &password, QString *error) const;
    bool deleteMqttPassword(const QString &host, const QString &username, QString *error) const;
    QString mqttCredentialKey(const QString &host, const QString &username) const;

    QNetworkAccessManager *m_network = nullptr;
    QNetworkCookieJar *m_cookieJar = nullptr;
    CredentialStore m_credentials;
    CustomCaStore m_customCaStore;
    QSslConfiguration m_tlsConfiguration;
    QTimer m_pollTimer;
    QTimer m_overlayTimer;
    QAudioSink *m_alertSoundPlayer = nullptr;
    QBuffer *m_alertSoundBuffer = nullptr;
    MqttClient *m_mqttClient = nullptr;
    MqttClient *m_mqttVerifier = nullptr;
    QTimer m_mqttVerificationTimer;

    QUrl m_baseUrl;
    QString m_serverAddress;
    QString m_username;
    int m_overlayDurationSeconds = 20;
    FeedMode m_feedMode = FeedMode::Jpeg;
    LivePlaybackMethod m_livePlaybackMethod = LivePlaybackMethod::NativeMse;
    int m_liveStartupTimeoutSeconds = 5;
    bool m_liveDebugEnabled = false;
    bool m_fastEventPollingEnabled = false;
    EventDeliveryMode m_eventDeliveryMode = EventDeliveryMode::HttpPolling;
    QString m_mqttBrokerHost;
    int m_mqttBrokerPort = 8883;
    bool m_mqttUseTls = true;
    QString m_mqttUsername;
    QString m_mqttTopicPrefix = QStringLiteral("frigate");
    bool m_mqttVerificationInProgress = false;
    QString m_mqttVerificationStatus;
    PopupTrigger m_popupTrigger = PopupTrigger::SelectedClassifications;
    bool m_popupCooldownEnabled = false;
    int m_popupCooldownSeconds = 30;
    QDateTime m_lastPopupTime;
    bool m_soundAlertEnabled = false;
    AlertSound m_alertSound = AlertSound::GentleChime;
    int m_soundAlertVolume = 60;
    bool m_soundCooldownEnabled = false;
    int m_soundCooldownSeconds = 30;
    QDateTime m_lastSoundAlertTime;
    QDateTime m_lastSoundNotificationTime;
    QSet<QString> m_selectedClassifications;
    QStringList m_availableClassifications;
    bool m_loadingClassifications = false;
    QString m_classificationLoadError;
    bool m_monitoring = false;
    bool m_overlayVisible = false;
    QDateTime m_overlayDismissalTime;
    ConnectionState m_connectionState = ConnectionState::Idle;
    QString m_connectionDetail;
    QString m_lastEventDescription = QStringLiteral("No event detected");
    Activity m_activity;
    Event m_latestEvent;
    ReviewItem m_latestReviewItem;
    bool m_hasLatestEvent = false;
    bool m_hasLatestReviewItem = false;
    QSet<QString> m_seenEventIds;
    QQueue<QString> m_seenEventOrder;
    QSet<QString> m_seenReviewIds;
    QQueue<QString> m_seenReviewOrder;
    QHash<QString, QString> m_liveStreamNames;
    bool m_hasLoadedLiveStreamNames = false;
    bool m_loadingLiveStreamNames = false;
    bool m_hasAuthenticatedSession = false;
    bool m_pollInFlight = false;
    PollResult m_pollResult;
    quint64 m_generation = 0;
};

Q_DECLARE_METATYPE(FrigateMonitor::Activity)
