#include "FrigateMonitor.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMediaDevices>
#include <QSslCertificate>
#include <QSettings>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <numbers>

namespace {
constexpr auto kDefaultServerAddress = "https://frigate.invalid";
constexpr int kMaximumSeenActivityIds = 512;

QString trim(const QString &value)
{
    return value.trimmed();
}

qint64 jsonTimestamp(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    if (!value.isDouble()) {
        return 0;
    }
    return qRound64(value.toDouble());
}

QByteArray alertSoundSamples(FrigateMonitor::AlertSound sound, const QAudioFormat &format)
{
    const int sampleRate = format.sampleRate();
    const int channels = format.channelCount();
    const qsizetype bytesPerFrame = format.bytesPerFrame();
    const qsizetype bytesPerSample = format.bytesPerSample();
    if (sampleRate <= 0 || channels <= 0 || bytesPerFrame <= 0 || bytesPerSample <= 0
        || bytesPerFrame < static_cast<qsizetype>(channels) * bytesPerSample) {
        return {};
    }

    const double duration = sound == FrigateMonitor::AlertSound::Bell ? 0.85 : 0.55;
    const qsizetype frameCount = static_cast<qsizetype>(qRound64(duration * sampleRate));
    if (frameCount == 0 || frameCount > std::numeric_limits<qsizetype>::max() / bytesPerFrame) {
        return {};
    }
    QByteArray data(frameCount * bytesPerFrame, Qt::Uninitialized);
    constexpr double pi = std::numbers::pi_v<double>;

    for (qsizetype frame = 0; frame < frameCount; ++frame) {
        const double time = static_cast<double>(frame) / sampleRate;
        double signal = 0.0;
        switch (sound) {
        case FrigateMonitor::AlertSound::GentleChime:
            signal = 0.62 * std::sin(2.0 * pi * 523.25 * time)
                + 0.28 * std::sin(2.0 * pi * 659.25 * time);
            signal *= std::exp(-4.2 * time);
            break;
        case FrigateMonitor::AlertSound::BrightChime:
            signal = 0.56 * std::sin(2.0 * pi * 880.0 * time)
                + 0.26 * std::sin(2.0 * pi * 1318.51 * time);
            signal *= std::exp(-6.0 * time);
            break;
        case FrigateMonitor::AlertSound::Bell:
            signal = 0.48 * std::sin(2.0 * pi * 659.25 * time)
                + 0.25 * std::sin(2.0 * pi * 1318.51 * time)
                + 0.12 * std::sin(2.0 * pi * 1975.53 * time);
            signal *= std::exp(-3.6 * time);
            break;
        }
        signal = qBound(-0.9, signal * 0.48, 0.9);

        char *output = data.data() + frame * bytesPerFrame;
        for (int channel = 0; channel < channels; ++channel) {
            char *sample = output + static_cast<qsizetype>(channel) * bytesPerSample;
            switch (format.sampleFormat()) {
            case QAudioFormat::UInt8: {
                const quint8 value = qRound((signal + 1.0) * 127.5);
                std::memcpy(sample, &value, sizeof(value));
                break;
            }
            case QAudioFormat::Int16: {
                const qint16 value = static_cast<qint16>(qBound(-32767, qRound(signal * 32767.0), 32767));
                std::memcpy(sample, &value, sizeof(value));
                break;
            }
            case QAudioFormat::Int32: {
                constexpr qint64 minimum = std::numeric_limits<qint32>::min();
                constexpr qint64 maximum = std::numeric_limits<qint32>::max();
                const qint32 value = static_cast<qint32>(qBound(
                    minimum, qRound64(signal * maximum), maximum));
                std::memcpy(sample, &value, sizeof(value));
                break;
            }
            case QAudioFormat::Float: {
                const float value = static_cast<float>(signal);
                std::memcpy(sample, &value, sizeof(value));
                break;
            }
            case QAudioFormat::Unknown:
            case QAudioFormat::NSampleFormats:
                return {};
            }
        }
    }
    return data;
}
}

FrigateMonitor::FrigateMonitor(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    qRegisterMetaType<FrigateMonitor::Activity>();

    auto *cookieJar = new QNetworkCookieJar;
    m_network->setCookieJar(cookieJar);
    m_cookieJar = cookieJar;
    updateTlsConfiguration();

    QSettings settings;
    m_serverAddress = settings.value("connection/serverAddress", QString::fromLatin1(kDefaultServerAddress)).toString();
    QString validationError;
    QString normalizedAddress;
    if (!validateServerAddress(m_serverAddress, &m_baseUrl, &normalizedAddress, &validationError)) {
        m_serverAddress = QString::fromLatin1(kDefaultServerAddress);
        m_baseUrl = QUrl(m_serverAddress);
        settings.setValue("connection/serverAddress", m_serverAddress);
    } else {
        m_serverAddress = normalizedAddress;
    }

    m_username = settings.value("connection/username").toString().trimmed();
    m_overlayDurationSeconds = qBound(5, settings.value("feed/durationSeconds", 20).toInt(), 120);
    m_feedMode = settings.value("feed/mode", "jpeg").toString() == "live" ? FeedMode::LiveStream : FeedMode::Jpeg;
    m_popupTrigger = settings.value("popup/trigger", "selected").toString() == "any"
        ? PopupTrigger::AnyObject
        : PopupTrigger::SelectedClassifications;
    m_popupCooldownEnabled = settings.value("cooldown/popupEnabled", false).toBool();
    m_popupCooldownSeconds = qBound(1, settings.value("cooldown/popupSeconds", 30).toInt(), 3600);
    m_soundAlertEnabled = settings.value("sound/enabled", false).toBool();
    const QString soundStyle = settings.value("sound/style", "gentle").toString();
    m_alertSound = soundStyle == "bright" ? AlertSound::BrightChime
        : soundStyle == "bell" ? AlertSound::Bell
        : AlertSound::GentleChime;
    m_soundAlertVolume = qBound(0, settings.value("sound/volume", 60).toInt(), 100);
    m_soundCooldownEnabled = settings.value("cooldown/soundEnabled", false).toBool();
    m_soundCooldownSeconds = qBound(1, settings.value("cooldown/soundSeconds", 30).toInt(), 3600);

    const QStringList savedClassifications = settings.value(
        "popup/selectedClassifications",
        QStringList{QStringLiteral("bird"), QStringLiteral("cat"), QStringLiteral("bruno")}
    ).toStringList();
    for (const QString &name : savedClassifications) {
        const QString normalized = normalizedName(name);
        if (!normalized.isEmpty()) {
            m_selectedClassifications.insert(normalized);
        }
    }
    m_availableClassifications = selectedClassifications();

    m_pollTimer.setInterval(2000);
    connect(&m_pollTimer, &QTimer::timeout, this, &FrigateMonitor::pollActivity);

    m_overlayTimer.setSingleShot(true);
    connect(&m_overlayTimer, &QTimer::timeout, this, &FrigateMonitor::dismissFeed);
}

QString FrigateMonitor::serverAddress() const
{
    return m_serverAddress;
}

QString FrigateMonitor::username() const
{
    return m_username;
}

int FrigateMonitor::overlayDurationSeconds() const
{
    return m_overlayDurationSeconds;
}

FrigateMonitor::FeedMode FrigateMonitor::feedMode() const
{
    return m_feedMode;
}

FrigateMonitor::PopupTrigger FrigateMonitor::popupTrigger() const
{
    return m_popupTrigger;
}

bool FrigateMonitor::isPopupCooldownEnabled() const
{
    return m_popupCooldownEnabled;
}

int FrigateMonitor::popupCooldownSeconds() const
{
    return m_popupCooldownSeconds;
}

bool FrigateMonitor::isSoundAlertEnabled() const
{
    return m_soundAlertEnabled;
}

FrigateMonitor::AlertSound FrigateMonitor::alertSound() const
{
    return m_alertSound;
}

int FrigateMonitor::soundAlertVolume() const
{
    return m_soundAlertVolume;
}

bool FrigateMonitor::isSoundCooldownEnabled() const
{
    return m_soundCooldownEnabled;
}

int FrigateMonitor::soundCooldownSeconds() const
{
    return m_soundCooldownSeconds;
}

QStringList FrigateMonitor::selectedClassifications() const
{
    QStringList names = m_selectedClassifications.values();
    std::sort(names.begin(), names.end(), [](const QString &left, const QString &right) {
        return QString::localeAwareCompare(left, right) < 0;
    });
    return names;
}

QStringList FrigateMonitor::availableClassifications() const
{
    return m_availableClassifications;
}

bool FrigateMonitor::isLoadingClassifications() const
{
    return m_loadingClassifications;
}

QString FrigateMonitor::classificationLoadError() const
{
    return m_classificationLoadError;
}

bool FrigateMonitor::isMonitoring() const
{
    return m_monitoring;
}

bool FrigateMonitor::isOverlayVisible() const
{
    return m_overlayVisible;
}

QDateTime FrigateMonitor::overlayDismissalTime() const
{
    return m_overlayDismissalTime;
}

FrigateMonitor::ConnectionState FrigateMonitor::connectionState() const
{
    return m_connectionState;
}

QString FrigateMonitor::connectionStateTitle() const
{
    switch (m_connectionState) {
    case ConnectionState::Idle:
        return QStringLiteral("Idle");
    case ConnectionState::Connecting:
        return QStringLiteral("Connecting");
    case ConnectionState::Connected:
        return QStringLiteral("Connected");
    case ConnectionState::Failed:
        return m_connectionDetail.isEmpty() ? QStringLiteral("Connection failed") : m_connectionDetail;
    }
    return QStringLiteral("Unknown");
}

QString FrigateMonitor::lastEventDescription() const
{
    return m_lastEventDescription;
}

FrigateMonitor::Activity FrigateMonitor::activity() const
{
    return m_activity;
}

QString FrigateMonitor::currentFeedCameraName() const
{
    if (m_hasLatestEvent) {
        return m_latestEvent.camera;
    }
    if (m_hasLatestReviewItem) {
        return m_latestReviewItem.camera;
    }
    return QStringLiteral("birdseye");
}

QString FrigateMonitor::currentFeedStreamName() const
{
    return m_liveStreamNames.value(currentFeedCameraName(), currentFeedCameraName());
}

QUrl FrigateMonitor::baseUrl() const
{
    return m_baseUrl;
}

QList<QNetworkCookie> FrigateMonitor::authenticationCookies() const
{
    return m_cookieJar ? m_cookieJar->cookiesForUrl(m_baseUrl) : QList<QNetworkCookie>();
}

QList<CustomCaStore::Entry> FrigateMonitor::customCaCertificates() const
{
    return m_customCaStore.entries();
}

QList<QSslCertificate> FrigateMonitor::customCaTrustAnchors() const
{
    return m_customCaStore.certificates();
}

bool FrigateMonitor::addCustomCaCertificate(const QString &filePath, QString *summary, QString *error)
{
    if (!m_customCaStore.addFromFile(filePath, summary, error)) {
        return false;
    }
    updateTlsConfiguration();
    emit customCaCertificatesChanged();
    emit settingsChanged();
    return true;
}

bool FrigateMonitor::removeCustomCaCertificate(const QString &id, QString *error)
{
    if (!m_customCaStore.remove(id, error)) {
        return false;
    }
    updateTlsConfiguration();
    emit customCaCertificatesChanged();
    emit settingsChanged();
    return true;
}

void FrigateMonitor::setOverlayDurationSeconds(int seconds)
{
    const int bounded = qBound(5, seconds, 120);
    if (m_overlayDurationSeconds == bounded) {
        return;
    }
    m_overlayDurationSeconds = bounded;
    QSettings().setValue("feed/durationSeconds", bounded);
    if (m_overlayVisible) {
        m_overlayDismissalTime = QDateTime::currentDateTime().addSecs(bounded);
        m_overlayTimer.start(bounded * 1000);
        emit overlayDismissalTimeChanged(m_overlayDismissalTime);
    }
    emit settingsChanged();
}

void FrigateMonitor::setFeedMode(FeedMode mode)
{
    if (m_feedMode == mode) {
        return;
    }
    m_feedMode = mode;
    QSettings().setValue("feed/mode", mode == FeedMode::LiveStream ? "live" : "jpeg");
    emit settingsChanged();
    emit streamSessionChanged();
}

void FrigateMonitor::setPopupTrigger(PopupTrigger trigger)
{
    if (m_popupTrigger == trigger) {
        return;
    }
    m_popupTrigger = trigger;
    QSettings().setValue("popup/trigger", trigger == PopupTrigger::AnyObject ? "any" : "selected");
    emit settingsChanged();
}

void FrigateMonitor::setPopupCooldownEnabled(bool enabled)
{
    if (m_popupCooldownEnabled == enabled) {
        return;
    }
    m_popupCooldownEnabled = enabled;
    QSettings().setValue("cooldown/popupEnabled", enabled);
    emit settingsChanged();
}

void FrigateMonitor::setPopupCooldownSeconds(int seconds)
{
    const int bounded = qBound(1, seconds, 3600);
    if (m_popupCooldownSeconds == bounded) {
        return;
    }
    m_popupCooldownSeconds = bounded;
    QSettings().setValue("cooldown/popupSeconds", bounded);
    emit settingsChanged();
}

void FrigateMonitor::setSoundAlertEnabled(bool enabled)
{
    if (m_soundAlertEnabled == enabled) {
        return;
    }
    m_soundAlertEnabled = enabled;
    QSettings().setValue("sound/enabled", enabled);
    if (!enabled) {
        stopSoundAlert();
    }
    emit settingsChanged();
}

void FrigateMonitor::setAlertSound(AlertSound sound)
{
    if (m_alertSound == sound) {
        return;
    }
    m_alertSound = sound;
    QString style;
    switch (sound) {
    case AlertSound::GentleChime:
        style = QStringLiteral("gentle");
        break;
    case AlertSound::BrightChime:
        style = QStringLiteral("bright");
        break;
    case AlertSound::Bell:
        style = QStringLiteral("bell");
        break;
    }
    QSettings().setValue("sound/style", style);
    emit settingsChanged();
}

void FrigateMonitor::setSoundAlertVolume(int volume)
{
    const int bounded = qBound(0, volume, 100);
    if (m_soundAlertVolume == bounded) {
        return;
    }
    m_soundAlertVolume = bounded;
    QSettings().setValue("sound/volume", bounded);
    if (m_alertSoundPlayer) {
        m_alertSoundPlayer->setVolume(static_cast<qreal>(bounded) / 100.0);
    }
    emit settingsChanged();
}

void FrigateMonitor::setSoundCooldownEnabled(bool enabled)
{
    if (m_soundCooldownEnabled == enabled) {
        return;
    }
    m_soundCooldownEnabled = enabled;
    QSettings().setValue("cooldown/soundEnabled", enabled);
    emit settingsChanged();
}

void FrigateMonitor::setSoundCooldownSeconds(int seconds)
{
    const int bounded = qBound(1, seconds, 3600);
    if (m_soundCooldownSeconds == bounded) {
        return;
    }
    m_soundCooldownSeconds = bounded;
    QSettings().setValue("cooldown/soundSeconds", bounded);
    emit settingsChanged();
}

void FrigateMonitor::setClassification(const QString &name, bool selected)
{
    const QString normalized = normalizedName(name);
    if (normalized.isEmpty()) {
        return;
    }

    if (selected) {
        m_selectedClassifications.insert(normalized);
        if (!m_availableClassifications.contains(normalized, Qt::CaseInsensitive)) {
            m_availableClassifications.append(displayName(name));
            std::sort(m_availableClassifications.begin(), m_availableClassifications.end(), [](const QString &left, const QString &right) {
                return QString::localeAwareCompare(left, right) < 0;
            });
        }
    } else {
        m_selectedClassifications.remove(normalized);
    }
    QSettings().setValue("popup/selectedClassifications", selectedClassifications());
    emit classificationsChanged();
}

void FrigateMonitor::refreshAvailableClassifications()
{
    if (m_loadingClassifications) {
        return;
    }

    m_loadingClassifications = true;
    m_classificationLoadError.clear();
    emit classificationsChanged();

    const quint64 generation = m_generation;
    auto completeWithError = [this, generation](const QString &error) {
        if (generation != m_generation) {
            return;
        }
        m_loadingClassifications = false;
        m_classificationLoadError = error;
        emit classificationsChanged();
    };

    auto requestClassifications = [this, generation, completeWithError] {
        if (generation != m_generation) {
            return;
        }

        struct ClassificationResponses {
            bool labelsFinished = false;
            bool subLabelsFinished = false;
            QString error;
            QStringList labels;
            QStringList subLabels;
        };
        const auto responses = std::make_shared<ClassificationResponses>();
        const auto finalize = [this, generation, responses] {
            if (generation != m_generation || !responses->labelsFinished || !responses->subLabelsFinished) {
                return;
            }
            m_loadingClassifications = false;
            if (!responses->error.isEmpty()) {
                m_classificationLoadError = responses->error;
                emit classificationsChanged();
                return;
            }

            QHash<QString, QString> uniqueNames;
            const QStringList allNames = responses->labels + responses->subLabels + selectedClassifications();
            for (const QString &name : allNames) {
                const QString normalized = normalizedName(name);
                if (!normalized.isEmpty() && !uniqueNames.contains(normalized)) {
                    uniqueNames.insert(normalized, displayName(name));
                }
            }
            m_availableClassifications = uniqueNames.values();
            std::sort(m_availableClassifications.begin(), m_availableClassifications.end(), [](const QString &left, const QString &right) {
                return QString::localeAwareCompare(left, right) < 0;
            });
            m_classificationLoadError.clear();
            emit classificationsChanged();
        };

        const auto makeRequest = [this](const QString &path) {
            QNetworkRequest request = requestForUrl(endpoint(path));
            return m_network->get(request);
        };

        QNetworkReply *labelsReply = makeRequest(QStringLiteral("api/labels"));
        connect(labelsReply, &QNetworkReply::finished, this, [this, generation, labelsReply, responses, finalize] {
            if (generation == m_generation) {
                if (labelsReply->error() != QNetworkReply::NoError) {
                    responses->error = responseError(labelsReply);
                } else {
                    const QJsonDocument document = QJsonDocument::fromJson(labelsReply->readAll());
                    if (!document.isArray()) {
                        responses->error = QStringLiteral("Frigate returned invalid classification data.");
                    } else {
                        for (const auto &value : document.array()) {
                            if (value.isString()) {
                                responses->labels.append(value.toString());
                            }
                        }
                    }
                }
                responses->labelsFinished = true;
                finalize();
            }
            labelsReply->deleteLater();
        });

        QUrl subLabelsUrl = endpoint(QStringLiteral("api/sub_labels"));
        QUrlQuery query(subLabelsUrl);
        query.addQueryItem(QStringLiteral("split_joined"), QStringLiteral("1"));
        subLabelsUrl.setQuery(query);
        QNetworkRequest subLabelsRequest = requestForUrl(subLabelsUrl);
        QNetworkReply *subLabelsReply = m_network->get(subLabelsRequest);
        connect(subLabelsReply, &QNetworkReply::finished, this, [this, generation, subLabelsReply, responses, finalize] {
            if (generation == m_generation) {
                if (subLabelsReply->error() != QNetworkReply::NoError) {
                    if (responses->error.isEmpty()) {
                        responses->error = responseError(subLabelsReply);
                    }
                } else {
                    const QJsonDocument document = QJsonDocument::fromJson(subLabelsReply->readAll());
                    if (!document.isArray()) {
                        if (responses->error.isEmpty()) {
                            responses->error = QStringLiteral("Frigate returned invalid classification data.");
                        }
                    } else {
                        for (const auto &value : document.array()) {
                            if (value.isString()) {
                                responses->subLabels.append(value.toString());
                            }
                        }
                    }
                }
                responses->subLabelsFinished = true;
                finalize();
            }
            subLabelsReply->deleteLater();
        });
    };

    if (m_username.isEmpty() || m_hasAuthenticatedSession) {
        requestClassifications();
        return;
    }

    QString password;
    QString error;
    if (!loadPassword(m_username, &password, &error)) {
        completeWithError(error);
        return;
    }

    QNetworkRequest request = requestForUrl(endpoint(QStringLiteral("api/login")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QByteArray body = QJsonDocument(QJsonObject{
        {QStringLiteral("user"), m_username},
        {QStringLiteral("password"), password},
    }).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_network->post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, generation, reply, requestClassifications, completeWithError] {
        if (generation == m_generation) {
            if (reply->error() != QNetworkReply::NoError) {
                completeWithError(responseError(reply));
            } else {
                m_hasAuthenticatedSession = true;
                requestClassifications();
            }
        }
        reply->deleteLater();
    });
}

bool FrigateMonitor::applyConnectionSettings(const QString &address, const QString &newUsername, const QString &password)
{
    QUrl validatedUrl;
    QString normalizedAddress;
    QString error;
    if (!validateServerAddress(address, &validatedUrl, &normalizedAddress, &error)) {
        setConnectionState(ConnectionState::Failed, error);
        return false;
    }
    if (!updateCredentials(newUsername, password, &error)) {
        setConnectionState(ConnectionState::Failed, error);
        return false;
    }

    const bool wasMonitoring = m_monitoring;
    if (wasMonitoring) {
        stop();
    }

    m_baseUrl = validatedUrl;
    m_serverAddress = normalizedAddress;
    QSettings settings;
    settings.setValue("connection/serverAddress", m_serverAddress);
    settings.setValue("connection/username", m_username);
    resetSession();
    emit settingsChanged();
    emit classificationsChanged();

    if (wasMonitoring) {
        start();
    }
    return true;
}

void FrigateMonitor::start()
{
    if (m_monitoring) {
        return;
    }
    m_monitoring = true;
    m_pollInFlight = false;
    m_pollTimer.start();
    setConnectionState(ConnectionState::Connecting);
    emit monitoringChanged(true);
    pollActivity();
}

void FrigateMonitor::stop()
{
    if (!m_monitoring) {
        return;
    }
    m_monitoring = false;
    m_pollInFlight = false;
    m_pollTimer.stop();
    setConnectionState(ConnectionState::Idle);
    emit monitoringChanged(false);
}

void FrigateMonitor::toggleMonitoring()
{
    m_monitoring ? stop() : start();
}

void FrigateMonitor::showFeed()
{
    if (!m_overlayVisible) {
        m_overlayVisible = true;
        emit overlayVisibilityChanged(true);
    }
    m_overlayDismissalTime = QDateTime::currentDateTime().addSecs(m_overlayDurationSeconds);
    m_overlayTimer.start(m_overlayDurationSeconds * 1000);
    emit overlayDismissalTimeChanged(m_overlayDismissalTime);
}

void FrigateMonitor::dismissFeed()
{
    m_overlayTimer.stop();
    m_overlayDismissalTime = {};
    emit overlayDismissalTimeChanged(m_overlayDismissalTime);
    if (!m_overlayVisible) {
        return;
    }
    m_overlayVisible = false;
    emit overlayVisibilityChanged(false);
}

void FrigateMonitor::requestSnapshot()
{
    QUrl snapshotUrl = endpoint(QStringLiteral("api/%1/latest.jpg").arg(currentFeedCameraName()));
    QUrlQuery query(snapshotUrl);
    query.addQueryItem(QStringLiteral("t"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    snapshotUrl.setQuery(query);

    QNetworkRequest request = requestForUrl(snapshotUrl);
    request.setRawHeader("Cache-Control", "no-cache");
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply->error() != QNetworkReply::NoError) {
            emit snapshotFailed(responseError(reply));
        } else {
            const QByteArray image = reply->readAll();
            if (image.isEmpty()) {
                emit snapshotFailed(QStringLiteral("Frigate returned an empty snapshot."));
            } else {
                emit snapshotReady(image);
            }
        }
        reply->deleteLater();
    });
}

void FrigateMonitor::pollActivity()
{
    if (!m_monitoring || m_pollInFlight) {
        return;
    }
    m_pollInFlight = true;
    authenticateThenPoll();
}

void FrigateMonitor::authenticateThenPoll()
{
    const quint64 generation = m_generation;
    const auto pollAfterAuthentication = [this, generation](bool authenticated, const QString &error) {
        if (generation != m_generation || !m_monitoring) {
            m_pollInFlight = false;
            return;
        }
        if (!authenticated) {
            m_pollInFlight = false;
            setConnectionState(ConnectionState::Failed, error);
            return;
        }
        refreshLiveStreamNamesIfNeeded();
        startPollRequests();
    };

    if (m_username.isEmpty() || m_hasAuthenticatedSession) {
        pollAfterAuthentication(true, {});
        return;
    }

    QString password;
    QString error;
    if (!loadPassword(m_username, &password, &error)) {
        pollAfterAuthentication(false, error);
        return;
    }

    QNetworkRequest request = requestForUrl(endpoint(QStringLiteral("api/login")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QByteArray body = QJsonDocument(QJsonObject{
        {QStringLiteral("user"), m_username},
        {QStringLiteral("password"), password},
    }).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_network->post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, generation, reply, pollAfterAuthentication] {
        if (generation == m_generation) {
            if (reply->error() != QNetworkReply::NoError) {
                pollAfterAuthentication(false, responseError(reply));
            } else {
                m_hasAuthenticatedSession = true;
                emit streamSessionChanged();
                pollAfterAuthentication(true, {});
            }
        }
        reply->deleteLater();
    });
}

void FrigateMonitor::startPollRequests()
{
    const quint64 generation = m_generation;
    m_pollResult = {};

    QUrl eventsUrl = endpoint(QStringLiteral("api/events"));
    QUrlQuery eventsQuery(eventsUrl);
    eventsQuery.addQueryItem(QStringLiteral("limit"), QStringLiteral("25"));
    eventsUrl.setQuery(eventsQuery);
    QNetworkRequest eventsRequest = requestForUrl(eventsUrl);
    QNetworkReply *eventsReply = m_network->get(eventsRequest);
    connect(eventsReply, &QNetworkReply::finished, this, [this, generation, eventsReply] {
        if (generation == m_generation && m_monitoring) {
            if (eventsReply->error() != QNetworkReply::NoError) {
                m_pollResult.error = responseError(eventsReply);
            } else {
                bool parsed = false;
                const QList<Event> events = parseEvents(eventsReply->readAll(), &parsed);
                if (parsed) {
                    handleEvents(events);
                } else {
                    m_pollResult.error = QStringLiteral("Frigate returned invalid event data.");
                }
            }
            m_pollResult.eventsFinished = true;
            finishPollIfComplete();
        }
        eventsReply->deleteLater();
    });

    QUrl reviewsUrl = endpoint(QStringLiteral("api/review"));
    QUrlQuery reviewsQuery(reviewsUrl);
    reviewsQuery.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
    reviewsUrl.setQuery(reviewsQuery);
    QNetworkRequest reviewsRequest = requestForUrl(reviewsUrl);
    QNetworkReply *reviewsReply = m_network->get(reviewsRequest);
    connect(reviewsReply, &QNetworkReply::finished, this, [this, generation, reviewsReply] {
        if (generation == m_generation && m_monitoring) {
            if (reviewsReply->error() != QNetworkReply::NoError) {
                if (m_pollResult.error.isEmpty()) {
                    m_pollResult.error = responseError(reviewsReply);
                }
            } else {
                bool parsed = false;
                const QList<ReviewItem> items = parseReviewItems(reviewsReply->readAll(), &parsed);
                if (parsed) {
                    handleReviewItems(items);
                } else if (m_pollResult.error.isEmpty()) {
                    m_pollResult.error = QStringLiteral("Frigate returned invalid review data.");
                }
            }
            m_pollResult.reviewsFinished = true;
            finishPollIfComplete();
        }
        reviewsReply->deleteLater();
    });
}

void FrigateMonitor::finishPollIfComplete()
{
    if (!m_pollResult.eventsFinished || !m_pollResult.reviewsFinished) {
        return;
    }
    m_pollInFlight = false;
    if (m_pollResult.error.isEmpty()) {
        setConnectionState(ConnectionState::Connected);
    } else {
        setConnectionState(ConnectionState::Failed, m_pollResult.error);
    }
}

void FrigateMonitor::refreshLiveStreamNamesIfNeeded()
{
    if (m_loadingLiveStreamNames || m_hasLoadedLiveStreamNames) {
        return;
    }
    m_loadingLiveStreamNames = true;
    const quint64 generation = m_generation;
    QNetworkRequest request = requestForUrl(endpoint(QStringLiteral("api/config")));
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, generation, reply] {
        if (generation == m_generation) {
            m_loadingLiveStreamNames = false;
            if (reply->error() == QNetworkReply::NoError) {
                const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
                const QJsonObject cameras = document.object().value(QStringLiteral("cameras")).toObject();
                QHash<QString, QString> streamNames;
                for (auto it = cameras.begin(); it != cameras.end(); ++it) {
                    const QJsonObject streams = it.value().toObject()
                        .value(QStringLiteral("live")).toObject()
                        .value(QStringLiteral("streams")).toObject();
                    for (auto stream = streams.begin(); stream != streams.end(); ++stream) {
                        const QString streamName = stream.value().toString().trimmed();
                        if (!streamName.isEmpty()) {
                            streamNames.insert(it.key(), streamName);
                            break;
                        }
                    }
                }
                m_liveStreamNames = streamNames;
                m_hasLoadedLiveStreamNames = true;
                emit streamSessionChanged();
            }
            // A missing config endpoint must never remove the JPEG fallback.
        }
        reply->deleteLater();
    });
}

void FrigateMonitor::setConnectionState(ConnectionState state, const QString &detail)
{
    const QString safeDetail = state == ConnectionState::Failed ? detail : QString();
    if (m_connectionState == state && m_connectionDetail == safeDetail) {
        return;
    }
    m_connectionState = state;
    m_connectionDetail = safeDetail;
    emit connectionStateChanged(state);
    emit settingsChanged();
}

void FrigateMonitor::showActivity(const Activity &activity)
{
    const QDateTime now = QDateTime::currentDateTime();
    m_activity = activity;
    emit activityChanged(m_activity);
    if (!m_popupCooldownEnabled || !isWithinCooldown(m_lastPopupTime, m_popupCooldownSeconds, now)) {
        showFeed();
        m_lastPopupTime = now;
    }
    if (m_soundAlertEnabled) {
        playSoundAlert(false);
    }
}

bool FrigateMonitor::isWithinCooldown(const QDateTime &lastTrigger, int seconds, const QDateTime &now) const
{
    if (!lastTrigger.isValid() || seconds <= 0) {
        return false;
    }
    const qint64 elapsed = lastTrigger.msecsTo(now);
    return elapsed >= 0 && elapsed < static_cast<qint64>(seconds) * 1000;
}

void FrigateMonitor::previewSoundAlert()
{
    playSoundAlert(true);
}

void FrigateMonitor::playSoundAlert(bool force)
{
    const QDateTime now = QDateTime::currentDateTime();
    if (!force) {
        if (isWithinCooldown(m_lastSoundAlertTime, 1, now)
            || (m_soundCooldownEnabled
                && isWithinCooldown(m_lastSoundNotificationTime, m_soundCooldownSeconds, now))) {
            return;
        }
    }

    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        return;
    }
    const QAudioFormat format = device.preferredFormat();
    if (!format.isValid()) {
        return;
    }

    const QByteArray samples = alertSoundSamples(m_alertSound, format);
    if (samples.isEmpty()) {
        return;
    }

    stopSoundAlert();
    m_alertSoundBuffer = new QBuffer(this);
    m_alertSoundBuffer->setData(samples);
    m_alertSoundBuffer->open(QIODevice::ReadOnly);

    m_alertSoundPlayer = new QAudioSink(device, format, this);
    m_alertSoundPlayer->setVolume(static_cast<qreal>(m_soundAlertVolume) / 100.0);
    QAudioSink *const player = m_alertSoundPlayer;
    connect(player, &QAudioSink::stateChanged, this, [this, player](QAudio::State state) {
        if ((state == QAudio::IdleState || state == QAudio::StoppedState) && player == m_alertSoundPlayer) {
            stopSoundAlert();
        }
    });
    m_lastSoundAlertTime = now;
    if (!force) {
        m_lastSoundNotificationTime = now;
    }
    m_alertSoundPlayer->start(m_alertSoundBuffer);
}

void FrigateMonitor::stopSoundAlert()
{
    QAudioSink *const player = m_alertSoundPlayer;
    QBuffer *const buffer = m_alertSoundBuffer;
    m_alertSoundPlayer = nullptr;
    m_alertSoundBuffer = nullptr;

    if (player) {
        player->stop();
        player->deleteLater();
    }
    if (buffer) {
        buffer->close();
        buffer->deleteLater();
    }
}

void FrigateMonitor::resetSession()
{
    ++m_generation;
    m_hasAuthenticatedSession = false;
    m_hasLoadedLiveStreamNames = false;
    m_loadingLiveStreamNames = false;
    m_liveStreamNames.clear();
    m_seenEventIds.clear();
    m_seenEventOrder.clear();
    m_seenReviewIds.clear();
    m_seenReviewOrder.clear();
    m_availableClassifications = selectedClassifications();
    m_classificationLoadError.clear();
    m_loadingClassifications = false;

    auto *cookieJar = new QNetworkCookieJar;
    m_network->setCookieJar(cookieJar);
    m_cookieJar = cookieJar;
    emit streamSessionChanged();
}

QUrl FrigateMonitor::endpoint(const QString &path) const
{
    QUrl url = m_baseUrl;
    url.setPath(QStringLiteral("/") + path);
    url.setQuery(QString());
    return url;
}

QNetworkRequest FrigateMonitor::requestForUrl(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setSslConfiguration(m_tlsConfiguration);
    return request;
}

void FrigateMonitor::updateTlsConfiguration()
{
    m_tlsConfiguration = QSslConfiguration::defaultConfiguration();
    QList<QSslCertificate> trusted = m_tlsConfiguration.caCertificates();
    QSet<QByteArray> knownCertificates;
    for (const QSslCertificate &certificate : trusted) {
        knownCertificates.insert(certificate.toDer());
    }
    for (const QSslCertificate &certificate : m_customCaStore.certificates()) {
        if (!knownCertificates.contains(certificate.toDer())) {
            trusted.append(certificate);
            knownCertificates.insert(certificate.toDer());
        }
    }
    m_tlsConfiguration.setCaCertificates(trusted);
}

bool FrigateMonitor::markActivitySeen(QSet<QString> &ids, QQueue<QString> &order, const QString &id)
{
    if (ids.contains(id)) {
        return false;
    }
    ids.insert(id);
    order.enqueue(id);
    while (order.size() > kMaximumSeenActivityIds) {
        ids.remove(order.dequeue());
    }
    return true;
}

QString FrigateMonitor::responseError(const QNetworkReply *reply) const
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 401) {
        return QStringLiteral("Frigate rejected the username or password.");
    }
    if (status > 0) {
        return QStringLiteral("Frigate returned HTTP %1.").arg(status);
    }
    if (reply->error() == QNetworkReply::SslHandshakeFailedError) {
        return QStringLiteral("TLS validation for Frigate failed.");
    }
    return QStringLiteral("Could not reach Frigate.");
}

bool FrigateMonitor::isRelevant(const Event &event) const
{
    if (m_popupTrigger == PopupTrigger::AnyObject) {
        return !normalizedName(event.label).isEmpty();
    }
    return m_selectedClassifications.contains(normalizedName(event.label))
        || m_selectedClassifications.contains(normalizedName(event.subLabel));
}

bool FrigateMonitor::isRelevant(const ReviewItem &item) const
{
    if (m_popupTrigger == PopupTrigger::AnyObject) {
        return !item.objectNames.isEmpty();
    }
    return std::any_of(item.objectNames.cbegin(), item.objectNames.cend(), [this](const QString &name) {
        return m_selectedClassifications.contains(normalizedName(name));
    });
}

void FrigateMonitor::handleEvents(const QList<Event> &events)
{
    const Event *newest = nullptr;
    for (const Event &event : events) {
        if (isRelevant(event) && (!newest || event.startTime > newest->startTime)) {
            newest = &event;
        }
    }
    if (!newest) {
        if (!m_hasLatestReviewItem) {
            m_lastEventDescription = QStringLiteral("No recent monitored activity");
            emit settingsChanged();
        }
        return;
    }

    m_latestEvent = *newest;
    m_hasLatestEvent = true;
    const QDateTime startedAt = QDateTime::fromSecsSinceEpoch(newest->startTime);
    const QString label = displayName(newest->subLabel.isEmpty() ? newest->label : newest->subLabel);
    m_lastEventDescription = QStringLiteral("%1 on %2 at %3").arg(label, newest->camera, describeTimestamp(startedAt));
    emit settingsChanged();

    const bool isNew = markActivitySeen(m_seenEventIds, m_seenEventOrder, newest->id);
    const qint64 age = QDateTime::currentSecsSinceEpoch() - newest->startTime;
    if (isNew && age < 90) {
        Activity activity{label, {}, newest->camera, startedAt};
        if (newest->topScore >= 0) {
            const double percent = newest->topScore <= 1.0 ? newest->topScore * 100.0 : newest->topScore;
            activity.confidence = QStringLiteral("%1%").arg(qRound(qBound(0.0, percent, 100.0)));
        }
        showActivity(activity);
    }
}

void FrigateMonitor::handleReviewItems(const QList<ReviewItem> &items)
{
    const ReviewItem *newest = nullptr;
    for (const ReviewItem &item : items) {
        const qint64 activityTime = item.endTime > 0 ? item.endTime : item.startTime;
        const qint64 newestTime = newest ? (newest->endTime > 0 ? newest->endTime : newest->startTime) : 0;
        if (isRelevant(item) && (!newest || activityTime > newestTime)) {
            newest = &item;
        }
    }
    if (!newest) {
        return;
    }

    m_latestReviewItem = *newest;
    m_hasLatestReviewItem = true;
    const qint64 activityTime = newest->endTime > 0 ? newest->endTime : newest->startTime;
    const QDateTime activityDate = QDateTime::fromSecsSinceEpoch(activityTime);
    QStringList labels;
    for (const QString &name : newest->objectNames) {
        labels.append(displayName(name));
    }
    const QString title = labels.isEmpty() ? QStringLiteral("Activity") : labels.join(QStringLiteral(", "));
    m_lastEventDescription = QStringLiteral("%1 on %2 at %3").arg(title, newest->camera, describeTimestamp(activityDate));
    emit settingsChanged();

    const bool isNew = markActivitySeen(m_seenReviewIds, m_seenReviewOrder, newest->id);
    const qint64 age = QDateTime::currentSecsSinceEpoch() - activityTime;
    if (isNew && age < 120) {
        showActivity(Activity{title, {}, newest->camera, activityDate});
    }
}

QList<FrigateMonitor::Event> FrigateMonitor::parseEvents(const QByteArray &data, bool *ok) const
{
    *ok = false;
    const QJsonDocument document = QJsonDocument::fromJson(data);
    if (!document.isArray()) {
        return {};
    }
    QList<Event> events;
    for (const auto &value : document.array()) {
        const QJsonObject object = value.toObject();
        Event event{
            object.value(QStringLiteral("id")).toString(),
            object.value(QStringLiteral("camera")).toString(),
            object.value(QStringLiteral("label")).toString(),
            object.value(QStringLiteral("sub_label")).toString(),
            jsonTimestamp(object, QStringLiteral("start_time")),
            object.value(QStringLiteral("top_score")).isDouble() ? object.value(QStringLiteral("top_score")).toDouble() : -1,
        };
        if (!event.id.isEmpty() && !event.camera.isEmpty() && !event.label.isEmpty() && event.startTime > 0) {
            events.append(event);
        }
    }
    *ok = true;
    return events;
}

QList<FrigateMonitor::ReviewItem> FrigateMonitor::parseReviewItems(const QByteArray &data, bool *ok) const
{
    *ok = false;
    const QJsonDocument document = QJsonDocument::fromJson(data);
    if (!document.isArray()) {
        return {};
    }
    QList<ReviewItem> items;
    for (const auto &value : document.array()) {
        const QJsonObject object = value.toObject();
        const QJsonObject reviewData = object.value(QStringLiteral("data")).toObject();
        ReviewItem item{
            object.value(QStringLiteral("id")).toString(),
            object.value(QStringLiteral("camera")).toString(),
            parseStringList(reviewData.value(QStringLiteral("objects"))) + parseStringList(reviewData.value(QStringLiteral("sub_labels"))),
            jsonTimestamp(object, QStringLiteral("start_time")),
            jsonTimestamp(object, QStringLiteral("end_time")),
        };
        if (!item.id.isEmpty() && !item.camera.isEmpty() && item.startTime > 0) {
            items.append(item);
        }
    }
    *ok = true;
    return items;
}

QStringList FrigateMonitor::parseStringList(const QJsonValue &value) const
{
    QStringList strings;
    if (value.isArray()) {
        for (const auto &entry : value.toArray()) {
            if (entry.isString()) {
                strings.append(entry.toString());
            }
        }
    } else if (value.isObject()) {
        for (const auto &entry : value.toObject()) {
            if (entry.isString()) {
                strings.append(entry.toString());
            }
        }
    }
    return strings;
}

QString FrigateMonitor::normalizedName(const QString &name) const
{
    return trim(name).toCaseFolded();
}

QString FrigateMonitor::displayName(const QString &name) const
{
    QString result = trim(name);
    if (result.isEmpty()) {
        return QStringLiteral("Activity");
    }
    result[0] = result.at(0).toUpper();
    return result;
}

QString FrigateMonitor::describeTimestamp(const QDateTime &time) const
{
    return QLocale().toString(time.toLocalTime().time(), QLocale::ShortFormat);
}

bool FrigateMonitor::validateServerAddress(const QString &address, QUrl *url, QString *normalized, QString *error) const
{
    QString candidate = address.trimmed();
    if (candidate.isEmpty()) {
        *error = QStringLiteral("Enter a server address.");
        return false;
    }
    if (!candidate.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        && !candidate.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        candidate.prepend(QStringLiteral("https://"));
    }

    QUrl parsed(candidate, QUrl::StrictMode);
    if (!parsed.isValid()) {
        *error = QStringLiteral("Enter a valid Frigate server address.");
        return false;
    }
    if (parsed.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) != 0
        && parsed.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0) {
        *error = QStringLiteral("Use http:// or https://.");
        return false;
    }
    if (parsed.host().isEmpty()) {
        *error = QStringLiteral("Enter a host or IP address.");
        return false;
    }
    if (!parsed.userInfo().isEmpty() || !parsed.query().isEmpty() || !parsed.fragment().isEmpty()
        || (!parsed.path().isEmpty() && parsed.path() != QStringLiteral("/"))) {
        *error = QStringLiteral("Use a server address without a path, credentials, or query.");
        return false;
    }
    if (parsed.port(-1) > 65535 || parsed.port(-1) == 0) {
        *error = QStringLiteral("Use a numeric port from 1 to 65535.");
        return false;
    }

    parsed.setPath(QString());
    parsed.setQuery(QString());
    parsed.setFragment(QString());
    *url = parsed;
    *normalized = parsed.toString(QUrl::FullyEncoded);
    return true;
}

bool FrigateMonitor::updateCredentials(const QString &newUsername, const QString &password, QString *error)
{
    const QString normalizedUsername = newUsername.trimmed();
    const QString previousUsername = m_username;
    if (normalizedUsername.isEmpty()) {
        if (!password.isEmpty()) {
            *error = QStringLiteral("Enter a username with the password.");
            return false;
        }
        if (!previousUsername.isEmpty() && !deletePassword(previousUsername, error)) {
            return false;
        }
        m_username.clear();
        return true;
    }

    if (password.isEmpty()) {
        if (!hasStoredPassword(normalizedUsername, error)) {
            if (error->isEmpty()) {
                *error = QStringLiteral("Enter the Frigate password.");
            }
            return false;
        }
    } else if (!savePassword(normalizedUsername, password, error)) {
        return false;
    }

    if (previousUsername != normalizedUsername && !previousUsername.isEmpty()
        && !deletePassword(previousUsername, error)) {
        return false;
    }
    m_username = normalizedUsername;
    return true;
}

bool FrigateMonitor::loadPassword(const QString &username, QString *password, QString *error) const
{
    return m_credentials.loadPassword(credentialKey(username), password, error);
}

bool FrigateMonitor::hasStoredPassword(const QString &username, QString *error) const
{
    QString password;
    return loadPassword(username, &password, error);
}

bool FrigateMonitor::savePassword(const QString &username, const QString &password, QString *error) const
{
    return m_credentials.savePassword(credentialKey(username), password, error);
}

bool FrigateMonitor::deletePassword(const QString &username, QString *error) const
{
    return m_credentials.deletePassword(credentialKey(username), error);
}

QString FrigateMonitor::credentialKey(const QString &username) const
{
    return QStringLiteral("frigate-password-")
        + QString::fromLatin1(QCryptographicHash::hash(username.toUtf8(), QCryptographicHash::Sha256).toHex());
}
