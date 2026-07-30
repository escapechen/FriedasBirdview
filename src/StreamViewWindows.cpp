#include "StreamView.h"

#include <QJsonArray>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkCookie>
#include <QPointer>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QTemporaryDir>
#include <QTimer>
#include <QtMath>

#include <WebView2.h>
#include <Shlwapi.h>
#include <windows.h>
#include <wrl.h>

#include <functional>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

void StreamBridge::reportError(const QString &message)
{
    emit errorReported(message.left(300));
}

void StreamBridge::reportAspectRatio(double aspectRatio)
{
    if (qIsFinite(aspectRatio) && aspectRatio >= 0.25 && aspectRatio <= 8.0) {
        emit aspectRatioReported(aspectRatio);
    }
}

void StreamBridge::dismiss()
{
    emit dismissRequested();
}

void StreamBridge::connected()
{
    emit streamConnected();
}

void StreamBridge::fallbackToJpeg(const QString &message)
{
    emit fallbackRequested(message.left(300));
}

void StreamBridge::reportDebug(const QString &message)
{
    emit debugReported(message.left(300));
}

namespace {
QString javaScriptString(const QString &value)
{
    QJsonArray array;
    array.append(value);
    QByteArray json = QJsonDocument(array).toJson(QJsonDocument::Compact);
    json.remove(0, 1);
    json.chop(1);
    return QString::fromUtf8(json);
}

QString windowsStreamHtml(const QUrl &serverUrl, const QString &streamName, quint64 session, int liveRetryTimeoutSeconds)
{
    const QString server = javaScriptString(serverUrl.toString(QUrl::FullyEncoded));
    const QString camera = javaScriptString(streamName);
    const QString firstFrameTimeoutMs = QString::number(qBound(1, liveRetryTimeoutSeconds, 15) * 1000);
    return QStringLiteral(R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html, body { position:fixed; inset:0; width:100%; height:100%; margin:0; background:#050505; overflow:hidden; }
    video { display:block; position:absolute; inset:0; width:100vw; height:100vh; object-fit:contain; cursor:pointer; }
  </style>
</head>
<body>
  <video id="feed" autoplay muted playsinline></video>
  <script>
    const server = %1;
    const camera = %2;
    const session = %3;
    const firstFrameTimeoutMs = %4;
    const video = document.getElementById("feed");
    const videoCodecs = ["avc1.640029", "avc1.64002A", "avc1.640033", "hvc1.1.6.L153.B0", "av01.0.08M.08"];
    const audioCodecs = ["mp4a.40.2", "mp4a.40.5", "flac", "opus"];
    const maxBufferSeconds = 4;
    const keepBufferSeconds = 2.5;
    const maxPendingBytes = 2 * 1024 * 1024;
    const maxRecoveryAttempts = 3;
    const reconnectDelay = 1500;
    let socket;
    let mediaSource;
    let sourceBuffer;
    let pending = [];
    let pendingBytes = 0;
    let firstFrameTimer;
    let restartTimer;
    let stablePlaybackTimer;
    let shouldReconnect = true;
    let isRecovering = false;
    let recoveryAttempts = 0;
    let lastPlaybackTime = -1;
    let lastPlaybackAdvanceAt = Date.now();

    function notify(type, value) {
      window.chrome.webview.postMessage(JSON.stringify({session, type, value}));
    }
    function report(message) { notify("error", message); }
    function debug(message) { notify("debug", message); }
    function fallback(message) {
      debug(`fallback: ${message}`);
      shouldReconnect = false;
      clearTimeout(firstFrameTimer);
      clearTimeout(restartTimer);
      clearTimeout(stablePlaybackTimer);
      closeSocket();
      resetMediaSource();
      notify("fallback", message);
    }
    function reportAspectRatio() {
      if (video.videoWidth > 0 && video.videoHeight > 0) {
        notify("aspectRatio", video.videoWidth / video.videoHeight);
      }
    }
    function closeSocket() {
      if (!socket) return;
      socket.onopen = null;
      socket.onmessage = null;
      socket.onerror = null;
      socket.onclose = null;
      socket.close();
      socket = null;
    }
    function resetMediaSource() {
      sourceBuffer = null;
      mediaSource = null;
      pending = [];
      pendingBytes = 0;
      if (video.src) URL.revokeObjectURL(video.src);
      video.removeAttribute("src");
      video.srcObject = null;
      video.load();
    }
    function recover(reason) {
      if (!shouldReconnect || isRecovering) return;
      isRecovering = true;
      clearTimeout(firstFrameTimer);
      clearTimeout(stablePlaybackTimer);
      recoveryAttempts += 1;
      debug(`recovery ${recoveryAttempts}: ${reason}`);
      if (recoveryAttempts >= maxRecoveryAttempts) {
        fallback("Live stream failed repeatedly. Showing JPEG snapshots.");
        return;
      }
      report(`${reason} Retrying… (${recoveryAttempts}/${maxRecoveryAttempts - 1})`);
      closeSocket();
      resetMediaSource();
      clearTimeout(restartTimer);
      restartTimer = setTimeout(() => {
        isRecovering = false;
        start();
      }, reconnectDelay);
    }
    function trimBuffer() {
      if (!sourceBuffer || sourceBuffer.updating || !sourceBuffer.buffered.length) return false;
      const ranges = sourceBuffer.buffered;
      const start = ranges.start(0);
      const end = ranges.end(ranges.length - 1);
      if (end - start <= maxBufferSeconds) return false;
      const removeEnd = Math.max(start, end - keepBufferSeconds);
      if (removeEnd <= start + 0.05) return false;
      sourceBuffer.remove(start, removeEnd);
      return true;
    }
    function pump() {
      if (!sourceBuffer || sourceBuffer.updating || isRecovering) return;
      try {
        if (trimBuffer() || !pending.length) return;
        const segment = pending.shift();
        pendingBytes -= segment.byteLength;
        sourceBuffer.appendBuffer(segment);
      } catch (error) {
        recover(`Live stream buffer error: ${error.name || error}`);
      }
    }
    function appendSegment(segment) {
      if (!sourceBuffer || isRecovering) return;
      pending.push(segment);
      pendingBytes += segment.byteLength;
      if (pendingBytes > maxPendingBytes) {
        recover("Live stream buffer could not keep up.");
        return;
      }
      pump();
    }
    function keepNearLiveEdge() {
      if (!sourceBuffer?.buffered.length || !Number.isFinite(video.currentTime)) return;
      const end = sourceBuffer.buffered.end(sourceBuffer.buffered.length - 1);
      if (end - video.currentTime > maxBufferSeconds) {
        video.currentTime = Math.max(0, end - 0.5);
      }
    }
    function hasVideoCodec(mimeType) {
      return /(?:avc1|avc3|hvc1|hev1|av01|vp09|vp8)/i.test(mimeType || "");
    }
    function start() {
      if (!shouldReconnect) return;
      debug("starting MSE connection");
      const MediaSourceConstructor = window.ManagedMediaSource || window.MediaSource;
      if (!MediaSourceConstructor) { fallback("This system cannot play Frigate's MSE stream. Showing JPEG snapshots."); return; }
      const supportedVideo = videoCodecs.filter((codec) =>
        MediaSourceConstructor.isTypeSupported(`video/mp4; codecs="${codec}"`)
      );
      if (!supportedVideo.length) { fallback("Windows WebView2 cannot decode a Frigate video codec. Showing JPEG snapshots."); return; }
      const supportedAudio = audioCodecs.filter((codec) =>
        MediaSourceConstructor.isTypeSupported(`video/mp4; codecs="${codec}"`)
      );
      const supported = [...supportedVideo, ...supportedAudio].join();
      const streamURL = new URL("/live/mse/api/ws", server);
      streamURL.protocol = streamURL.protocol === "https:" ? "wss:" : "ws:";
      streamURL.searchParams.set("src", camera);
      let currentSocket;
      try {
        currentSocket = new WebSocket(streamURL.href);
        socket = currentSocket;
      } catch (error) {
        recover(`Live stream connection failed: ${error.message || error}`);
        return;
      }
      currentSocket.binaryType = "arraybuffer";
      currentSocket.onopen = () => {
        if (socket !== currentSocket || !shouldReconnect) return;
        mediaSource = new MediaSourceConstructor();
        mediaSource.addEventListener("sourceopen", () => {
          try { currentSocket.send(JSON.stringify({type:"mse", value:supported})); }
          catch (error) { recover(`Live stream setup failed: ${error.message || error}`); }
        }, {once:true});
        if ("ManagedMediaSource" in window) {
          video.disableRemotePlayback = true;
          video.srcObject = mediaSource;
        } else {
          URL.revokeObjectURL(video.src || "");
          video.src = URL.createObjectURL(mediaSource);
          video.srcObject = null;
        }
        video.play().catch((error) => recover(`Live stream could not start: ${error.message || error}`));
        clearTimeout(firstFrameTimer);
        firstFrameTimer = setTimeout(() => recover("Frigate connected but did not send playable video."), firstFrameTimeoutMs);
      };
      currentSocket.onmessage = (event) => {
        if (socket !== currentSocket || isRecovering || typeof event.data !== "string") return;
        let response;
        try { response = JSON.parse(event.data); }
        catch (_) { recover("Frigate returned an invalid live-stream response."); return; }
        if (response.type !== "mse") return;
        try {
          if (!mediaSource || sourceBuffer) return;
          debug(`announced ${response.value}`);
          if (!hasVideoCodec(response.value)) { fallback("Frigate returned an audio-only live stream. Showing JPEG snapshots."); return; }
          sourceBuffer = mediaSource.addSourceBuffer(response.value);
          if (sourceBuffer.mode) sourceBuffer.mode = "segments";
          sourceBuffer.addEventListener("updateend", () => { keepNearLiveEdge(); pump(); });
          currentSocket.onmessage = (binaryEvent) => {
            if (socket !== currentSocket || isRecovering || typeof binaryEvent.data === "string") return;
            appendSegment(binaryEvent.data);
          };
        } catch (error) {
          recover(`Frigate returned a video codec this system cannot play: ${error.name || error}`);
        }
      };
      currentSocket.onerror = () => recover("Live stream connection failed.");
      currentSocket.onclose = () => recover("Live stream disconnected.");
    }
    video.addEventListener("click", () => notify("dismiss"));
    video.addEventListener("loadeddata", () => {
      clearTimeout(firstFrameTimer);
      debug("decoded first video frame");
      notify("connected");
      reportAspectRatio();
      lastPlaybackTime = video.currentTime;
      lastPlaybackAdvanceAt = Date.now();
      clearTimeout(stablePlaybackTimer);
      stablePlaybackTimer = setTimeout(() => {
        if (Date.now() - lastPlaybackAdvanceAt < 5000) recoveryAttempts = 0;
      }, 10000);
    });
    video.addEventListener("loadedmetadata", reportAspectRatio);
    video.addEventListener("error", () => recover("Live stream decoder error."));
    video.addEventListener("pause", () => { if (socket?.readyState === WebSocket.OPEN && !video.ended) video.play().catch(() => {}); });
    video.addEventListener("timeupdate", () => {
      if (video.currentTime > lastPlaybackTime + 0.05) {
        lastPlaybackTime = video.currentTime;
        lastPlaybackAdvanceAt = Date.now();
      }
      keepNearLiveEdge();
    });
    setInterval(() => {
      if (!shouldReconnect || !socket || socket.readyState !== WebSocket.OPEN || isRecovering) return;
      if (video.readyState >= HTMLMediaElement.HAVE_CURRENT_DATA && Date.now() - lastPlaybackAdvanceAt > 5000) recover("Live stream stalled.");
    }, 2000);
    window.addEventListener("error", (event) => report(`Live stream error: ${event.message || "unknown JavaScript error"}`));
    window.addEventListener("unhandledrejection", (event) => report(`Live stream error: ${event.reason?.message || event.reason || "unknown error"}`));
    window.addEventListener("pagehide", () => {
      shouldReconnect = false;
      clearTimeout(firstFrameTimer);
      clearTimeout(restartTimer);
      clearTimeout(stablePlaybackTimer);
      closeSocket();
    });
    start();
  </script>
</body>
</html>
)HTML").arg(server, camera, QString::number(session), firstFrameTimeoutMs);
}

QString failureMessage(HRESULT result, bool hasCustomCa)
{
    if (hasCustomCa) {
        return QStringLiteral(
            "Windows WebView2 could not start live video. If Frigate uses a private CA, install its issuing CA in the Windows Current User trust store; FriedasBirdview does not bypass certificate validation."
        );
    }
    return QStringLiteral("Windows WebView2 could not start live video (0x%1). Showing JPEG snapshots.")
        .arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
}
}

class StreamViewWindowsBackend final {
public:
    StreamViewWindowsBackend(
        QWidget *host,
        bool hasCustomCa,
        std::function<void(const QString &)> reportError,
        std::function<void(double)> reportAspectRatio,
        std::function<void()> dismiss,
        std::function<void()> connected,
        std::function<void(const QString &)> fallback,
        std::function<void(const QString &)> debug
    )
        : m_host(host)
        , m_hasCustomCa(hasCustomCa)
        , m_reportError(std::move(reportError))
        , m_reportAspectRatio(std::move(reportAspectRatio))
        , m_dismiss(std::move(dismiss))
        , m_connected(std::move(connected))
        , m_fallback(std::move(fallback))
        , m_debug(std::move(debug))
    {
        if (!m_profile.isValid()) {
            m_reportError(QStringLiteral(
                "FriedasBirdview could not create an ephemeral WebView2 profile. Showing JPEG snapshots."
            ));
            return;
        }
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        m_comInitialized = initialized == S_OK || initialized == S_FALSE;
        if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
            m_reportError(failureMessage(initialized, m_hasCustomCa));
            return;
        }
        initialize();
    }

    ~StreamViewWindowsBackend()
    {
        if (m_controller) {
            m_controller->Close();
        }
        m_webView.Reset();
        m_controller.Reset();
        m_environment.Reset();
        if (m_comInitialized) {
            CoUninitialize();
        }
    }

    void start(
        const QUrl &serverUrl,
        const QString &streamName,
        const QList<QNetworkCookie> &cookies,
        int liveRetryTimeoutSeconds
    )
    {
        if (!serverUrl.isValid() || streamName.trimmed().isEmpty()) {
            m_reportError(QStringLiteral("A valid Frigate stream is not available."));
            return;
        }
        m_pending = {serverUrl, streamName, cookies, qBound(1, liveRetryTimeoutSeconds, 15), ++m_session};
        if (m_webView) {
            loadPending();
        }
    }

    void stop()
    {
        ++m_session;
        m_pending = {};
        if (m_webView) {
            m_webView->Navigate(L"about:blank");
        }
    }

    void resize(const QSize &size)
    {
        if (!m_controller || !m_host) {
            return;
        }

        // Qt widget sizes are device-independent pixels, but WebView2 expects
        // physical client pixels.  Reading the HWND bounds avoids a half-size
        // player on high-DPI Windows displays.
        RECT bounds {};
        const HWND hostWindow = reinterpret_cast<HWND>(m_host->winId());
        if (!GetClientRect(hostWindow, &bounds)) {
            const qreal scale = m_host->devicePixelRatio();
            bounds.right = qRound(size.width() * scale);
            bounds.bottom = qRound(size.height() * scale);
        }
        m_controller->put_Bounds(bounds);
    }

private:
    struct PendingRequest {
        QUrl serverUrl;
        QString streamName;
        QList<QNetworkCookie> cookies;
        int liveRetryTimeoutSeconds = 5;
        quint64 session = 0;

        explicit operator bool() const { return serverUrl.isValid() && !streamName.isEmpty(); }
    };

    void initialize()
    {
        if (!m_host) {
            return;
        }
        const std::wstring profilePath = m_profile.path().toStdWString();
        const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
            nullptr,
            profilePath.c_str(),
            nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this](HRESULT environmentResult, ICoreWebView2Environment *environment) -> HRESULT {
                    if (FAILED(environmentResult) || !environment) {
                        m_reportError(failureMessage(environmentResult, m_hasCustomCa));
                        return S_OK;
                    }
                    m_environment = environment;
                    const HRESULT controllerResult = m_environment->CreateCoreWebView2Controller(
                        reinterpret_cast<HWND>(m_host->winId()),
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [this](HRESULT result, ICoreWebView2Controller *controller) -> HRESULT {
                                completeController(result, controller);
                                return S_OK;
                            }
                        ).Get()
                    );
                    if (FAILED(controllerResult)) {
                        m_reportError(failureMessage(controllerResult, m_hasCustomCa));
                    }
                    return S_OK;
                }
            ).Get()
        );
        if (FAILED(result)) {
            m_reportError(failureMessage(result, m_hasCustomCa));
        }
    }

    void completeController(HRESULT result, ICoreWebView2Controller *controller)
    {
        if (FAILED(result) || !controller) {
            m_reportError(failureMessage(result, m_hasCustomCa));
            return;
        }
        m_controller = controller;
        if (FAILED(m_controller->get_CoreWebView2(&m_webView)) || !m_webView) {
            m_reportError(QStringLiteral("Windows WebView2 did not create a live-video view. Showing JPEG snapshots."));
            return;
        }
        ComPtr<ICoreWebView2Settings> settings;
        if (SUCCEEDED(m_webView->get_Settings(&settings)) && settings) {
            settings->put_AreDefaultContextMenusEnabled(FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_IsZoomControlEnabled(FALSE);
        }
        m_webView->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        m_webView->add_WebResourceRequested(
            Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                [this](ICoreWebView2 *, ICoreWebView2WebResourceRequestedEventArgs *args) -> HRESULT {
                    providePlayerDocument(args);
                    return S_OK;
                }
            ).Get(),
            &m_webResourceToken
        );
        m_webView->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [this](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
                    handleMessage(args);
                    return S_OK;
                }
            ).Get(),
            &m_webMessageToken
        );
        m_webView->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [this](ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
                    BOOL success = FALSE;
                    if (SUCCEEDED(args->get_IsSuccess(&success)) && !success) {
                        m_reportError(failureMessage(E_FAIL, m_hasCustomCa));
                    }
                    return S_OK;
                }
            ).Get(),
            &m_navigationToken
        );
        m_controller->put_IsVisible(TRUE);
        resize(m_host->size());
        if (m_pending) {
            loadPending();
        }
    }

    void loadPending()
    {
        if (!m_webView || !m_pending) {
            return;
        }
        ComPtr<ICoreWebView2_2> webView2;
        ComPtr<ICoreWebView2CookieManager> cookieManager;
        if (SUCCEEDED(m_webView.As(&webView2)) && webView2
            && SUCCEEDED(webView2->get_CookieManager(&cookieManager)) && cookieManager) {
            for (const QNetworkCookie &cookie : m_pending.cookies) {
                const QString name = QString::fromUtf8(cookie.name());
                const QString domain = cookie.domain().isEmpty() ? m_pending.serverUrl.host() : cookie.domain();
                const QString path = cookie.path().isEmpty() ? QStringLiteral("/") : cookie.path();
                const QString value = QString::fromUtf8(cookie.value());
                ComPtr<ICoreWebView2Cookie> webCookie;
                if (SUCCEEDED(cookieManager->CreateCookie(
                        reinterpret_cast<LPCWSTR>(name.utf16()),
                        reinterpret_cast<LPCWSTR>(value.utf16()),
                        reinterpret_cast<LPCWSTR>(domain.utf16()),
                        reinterpret_cast<LPCWSTR>(path.utf16()),
                        &webCookie
                    )) && webCookie) {
                    webCookie->put_IsHttpOnly(cookie.isHttpOnly());
                    webCookie->put_IsSecure(cookie.isSecure());
                    cookieManager->AddOrUpdateCookie(webCookie.Get());
                }
            }
        }
        m_documentHtml = windowsStreamHtml(
            m_pending.serverUrl,
            m_pending.streamName,
            m_pending.session,
            m_pending.liveRetryTimeoutSeconds
        );
        m_documentUrl = m_pending.serverUrl.resolved(QUrl(QStringLiteral("/.friedasbirdview-live")));
        m_webView->Navigate(reinterpret_cast<LPCWSTR>(m_documentUrl.toString(QUrl::FullyEncoded).utf16()));
    }

    void providePlayerDocument(ICoreWebView2WebResourceRequestedEventArgs *args)
    {
        if (!args || m_documentUrl.isEmpty() || !m_environment) {
            return;
        }
        ComPtr<ICoreWebView2WebResourceRequest> request;
        if (FAILED(args->get_Request(&request)) || !request) {
            return;
        }
        LPWSTR uri = nullptr;
        if (FAILED(request->get_Uri(&uri)) || !uri) {
            return;
        }
        const QString requestedUrl = QString::fromWCharArray(uri);
        CoTaskMemFree(uri);
        if (requestedUrl != m_documentUrl.toString(QUrl::FullyEncoded)) {
            return;
        }
        const QByteArray html = m_documentHtml.toUtf8();
        ComPtr<IStream> stream;
        stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE *>(html.constData()), static_cast<UINT>(html.size())));
        if (!stream) {
            return;
        }
        ComPtr<ICoreWebView2WebResourceResponse> response;
        if (SUCCEEDED(m_environment->CreateWebResourceResponse(
                stream.Get(), 200, L"OK", L"Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\n", &response
            )) && response) {
            args->put_Response(response.Get());
        }
    }

    void handleMessage(ICoreWebView2WebMessageReceivedEventArgs *args)
    {
        LPWSTR rawMessage = nullptr;
        if (!args || FAILED(args->TryGetWebMessageAsString(&rawMessage)) || !rawMessage) {
            return;
        }
        const QJsonDocument document = QJsonDocument::fromJson(QString::fromWCharArray(rawMessage).toUtf8());
        CoTaskMemFree(rawMessage);
        const QJsonObject message = document.object();
        if (message.value(QStringLiteral("session")).toVariant().toULongLong() != m_session) {
            return;
        }
        const QString type = message.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("error")) {
            m_reportError(message.value(QStringLiteral("value")).toString().left(300));
        } else if (type == QStringLiteral("debug")) {
            m_debug(message.value(QStringLiteral("value")).toString().left(300));
        } else if (type == QStringLiteral("aspectRatio")) {
            const double ratio = message.value(QStringLiteral("value")).toDouble();
            if (qIsFinite(ratio) && ratio >= 0.25 && ratio <= 8.0) {
                m_reportAspectRatio(ratio);
            }
        } else if (type == QStringLiteral("dismiss")) {
            m_dismiss();
        } else if (type == QStringLiteral("connected")) {
            m_connected();
        } else if (type == QStringLiteral("fallback")) {
            m_fallback(message.value(QStringLiteral("value")).toString().left(300));
        }
    }

    QPointer<QWidget> m_host;
    bool m_hasCustomCa = false;
    QTemporaryDir m_profile;
    bool m_comInitialized = false;
    quint64 m_session = 0;
    PendingRequest m_pending;
    QUrl m_documentUrl;
    QString m_documentHtml;
    ComPtr<ICoreWebView2Environment> m_environment;
    ComPtr<ICoreWebView2Controller> m_controller;
    ComPtr<ICoreWebView2> m_webView;
    EventRegistrationToken m_webResourceToken {};
    EventRegistrationToken m_webMessageToken {};
    EventRegistrationToken m_navigationToken {};
    std::function<void(const QString &)> m_reportError;
    std::function<void(double)> m_reportAspectRatio;
    std::function<void()> m_dismiss;
    std::function<void()> m_connected;
    std::function<void(const QString &)> m_fallback;
    std::function<void(const QString &)> m_debug;
};

StreamView::StreamView(const QList<QSslCertificate> &customCaCertificates, QWidget *parent)
    : QWidget(parent)
    , m_backgroundRetryTimer(this)
    , m_windowsBackend(std::make_unique<StreamViewWindowsBackend>(
          this,
          !customCaCertificates.isEmpty(),
          [this](const QString &message) {
              updateLiveStatus(QStringLiteral("Windows WebView2 MSE"), QStringLiteral("retrying"));
              writeDebug(QStringLiteral("WebView2: %1").arg(message));
              emit errorChanged(message);
          },
          [this](double ratio) { emit aspectRatioChanged(ratio); },
          [this] { emit dismissRequested(); },
          [this] { markStreamConnected(); },
          [this](const QString &message) { scheduleBackgroundRetry(message); },
          [this](const QString &message) { writeDebug(QStringLiteral("WebView2: %1").arg(message)); }
      ))
{
    setFocusPolicy(Qt::NoFocus);
    m_backgroundRetryTimer.setSingleShot(true);
    connect(&m_backgroundRetryTimer, &QTimer::timeout, this, [this] {
        if (m_streamActive) {
            startSelectedPlayer();
        }
    });
}

StreamView::~StreamView() = default;

void StreamView::start(
    const QUrl &serverUrl,
    const QString &streamName,
    const QString &snapshotCameraName,
    const QList<QNetworkCookie> &cookies,
    LivePlaybackMethod method,
    int liveRetryTimeoutSeconds,
    bool debugEnabled
)
{
    stop();
    if (!serverUrl.isValid() || streamName.trimmed().isEmpty() || snapshotCameraName.trimmed().isEmpty()) {
        emit errorChanged(QStringLiteral("A valid Frigate stream is not available."));
        return;
    }
    m_streamActive = true;
    m_pendingServerUrl = serverUrl;
    m_pendingStreamName = streamName;
    m_pendingSnapshotCameraName = snapshotCameraName;
    m_pendingCookies = cookies;
    m_selectedMethod = method;
    m_liveRetryTimeoutSeconds = qBound(1, liveRetryTimeoutSeconds, 15);
    m_debugEnabled = debugEnabled;
    writeDebug(QStringLiteral("session started player=webview2-mse retry-timeout=%1s")
        .arg(m_liveRetryTimeoutSeconds));
    startSelectedPlayer();
}

void StreamView::stop()
{
    m_streamActive = false;
    m_backgroundRetryTimer.stop();
    m_windowsBackend->stop();
}

void StreamView::startSelectedPlayer()
{
    if (!m_streamActive) {
        return;
    }
    m_backgroundRetryTimer.stop();
    // WebView2 has no hidden-page playback policy here. Keep the outer Qt JPEG
    // preview until it reports a decoded frame.
    emit jpegPreviewLocationChanged(false);
    updateLiveStatus(QStringLiteral("Windows WebView2 MSE"), QStringLiteral("connecting"));
    m_windowsBackend->start(
        m_pendingServerUrl,
        m_pendingStreamName,
        m_pendingCookies,
        m_liveRetryTimeoutSeconds
    );
}

void StreamView::scheduleBackgroundRetry(const QString &message)
{
    if (!m_streamActive) {
        return;
    }
    m_windowsBackend->stop();
    emit jpegPreviewLocationChanged(false);
    updateLiveStatus(QStringLiteral("Windows WebView2 MSE"), QStringLiteral("retrying in 2 seconds"));
    writeDebug(QStringLiteral("background retry scheduled reason=%1").arg(message.left(180)));
    emit errorChanged(message + QStringLiteral(" JPEG snapshots remain visible; retrying live video."));
    m_backgroundRetryTimer.start(2000);
}

void StreamView::markStreamConnected()
{
    if (!m_streamActive) {
        return;
    }
    m_backgroundRetryTimer.stop();
    updateLiveStatus(QStringLiteral("Windows WebView2 MSE"), QStringLiteral("playing"));
    writeDebug(QStringLiteral("live video is playing"));
    emit streamConnected();
}

void StreamView::updateLiveStatus(const QString &method, const QString &state)
{
    if (m_liveStatusMethod == method && m_liveStatusState == state) {
        return;
    }
    m_liveStatusMethod = method;
    m_liveStatusState = state;
    emit liveStatusChanged(method, state);
}

void StreamView::writeDebug(const QString &message) const
{
    if (m_debugEnabled) {
        QString safeMessage = message.left(300);
        safeMessage.replace(
            QRegularExpression(QStringLiteral(R"((?:https?|wss?)://[^\s]+)")),
            QStringLiteral("<url>")
        );
        qInfo().noquote() << QStringLiteral("FriedasBirdview live: %1").arg(safeMessage);
    }
}

void StreamView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_windowsBackend->resize(event->size());
    QTimer::singleShot(0, this, [this] {
        if (m_windowsBackend) {
            m_windowsBackend->resize(size());
        }
    });
}
