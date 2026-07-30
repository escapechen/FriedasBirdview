#pragma once

#include <QList>
#include <QNetworkCookie>
#include <QSslCertificate>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QWidget>

#include "LivePlaybackMethod.h"

#include <memory>

class QWebChannel;
class QWebEnginePage;
class QWebEngineProfile;
class QWebEngineView;
class QStackedLayout;
class NativeStreamPlayer;
class StreamViewWindowsBackend;

class StreamBridge final : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    Q_INVOKABLE void reportError(const QString &message);
    Q_INVOKABLE void reportAspectRatio(double aspectRatio);
    Q_INVOKABLE void dismiss();
    Q_INVOKABLE void connected();
    Q_INVOKABLE void fallbackToJpeg(const QString &message);
    Q_INVOKABLE void reportDebug(const QString &message);

signals:
    void errorReported(const QString &message);
    void aspectRatioReported(double aspectRatio);
    void dismissRequested();
    void streamConnected();
    void fallbackRequested(const QString &message);
    void debugReported(const QString &message);
};

class StreamView final : public QWidget {
    Q_OBJECT

public:
    explicit StreamView(const QList<QSslCertificate> &customCaCertificates, QWidget *parent = nullptr);
    ~StreamView() override;

    void start(
        const QUrl &serverUrl,
        const QString &streamName,
        const QString &snapshotCameraName,
        const QList<QNetworkCookie> &cookies,
        LivePlaybackMethod method,
        int liveRetryTimeoutSeconds,
        bool debugEnabled
    );
    void stop();

signals:
    void errorChanged(const QString &message);
    void aspectRatioChanged(double aspectRatio);
    void dismissRequested();
    void streamConnected();
    void jpegFallbackRequested(const QString &message);
    // Native Qt Multimedia needs the outer snapshot widget while it starts.
    // The WebEngine compatibility player instead keeps an equivalent JPEG
    // layer inside its visible page, so Chromium must not see its video as a
    // background tab.
    void jpegPreviewLocationChanged(bool insideLivePlayer);
    void liveStatusChanged(const QString &method, const QString &state);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void startSelectedPlayer();
    void scheduleBackgroundRetry(const QString &message);
    void markStreamConnected();
    void updateLiveStatus(const QString &method, const QString &state);
    void writeDebug(const QString &message) const;

    QTimer m_backgroundRetryTimer;
    bool m_streamActive = false;
    int m_liveRetryTimeoutSeconds = 5;
    bool m_debugEnabled = false;
    LivePlaybackMethod m_selectedMethod = LivePlaybackMethod::NativeMse;
    QString m_liveStatusMethod;
    QString m_liveStatusState;
    QUrl m_pendingServerUrl;
    QString m_pendingStreamName;
    QString m_pendingSnapshotCameraName;
    QList<QNetworkCookie> m_pendingCookies;

#if defined(Q_OS_WIN)
    std::unique_ptr<StreamViewWindowsBackend> m_windowsBackend;
#else
    QString streamHtml(
        const QUrl &serverUrl,
        const QString &streamName,
        const QString &snapshotCameraName,
        int liveRetryTimeoutSeconds
    ) const;
    void startWebEngineFallback();
    void loadHtmlWhenCookiesAreReady(int loadId, const QString &html, const QUrl &serverUrl);
    void cookieAdded(const QNetworkCookie &cookie);

    QList<QSslCertificate> m_customCaCertificates;
    std::unique_ptr<NativeStreamPlayer> m_nativePlayer;
    QWebEngineProfile *m_profile = nullptr;
    QWebEngineView *m_view = nullptr;
    QStackedLayout *m_layout = nullptr;
    QWebEnginePage *m_page = nullptr;
    QWebChannel *m_channel = nullptr;
    StreamBridge *m_bridge = nullptr;
    int m_loadId = 0;
    int m_cookieLoadId = 0;
    QSet<QByteArray> m_pendingCookieNames;
    QString m_pendingHtml;
    bool m_usingNativePlayer = false;
    bool m_browserFallbackActive = false;
#endif
};
