#include "StreamView.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QtMath>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEngineCertificateError>
#include <QWebEngineCookieStore>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#if FRIEDASBIRDVIEW_WEBENGINE_PROFILE_CUSTOM_CA
#include <QWebEngineProfileBuilder>
#endif
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <functional>

namespace {
class StreamPage final : public QWebEnginePage {
public:
    StreamPage(QWebEngineProfile *profile, QObject *parent, std::function<void()> scriptError)
        : QWebEnginePage(profile, parent)
        , m_scriptError(std::move(scriptError))
    {
    }

protected:
    void javaScriptConsoleMessage(
        JavaScriptConsoleMessageLevel level,
        const QString &,
        int,
        const QString &
    ) override
    {
        if (level == ErrorMessageLevel) {
            m_scriptError();
        }
    }

private:
    std::function<void()> m_scriptError;
};

QString javaScriptString(const QString &value)
{
    QJsonArray array;
    array.append(value);
    QByteArray json = QJsonDocument(array).toJson(QJsonDocument::Compact);
    json.remove(0, 1);
    json.chop(1);
    return QString::fromUtf8(json);
}
}

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

StreamView::StreamView(const QList<QSslCertificate> &customCaCertificates, QWidget *parent)
    : QWidget(parent)
    , m_view(new QWebEngineView(this))
    , m_channel(new QWebChannel(this))
    , m_bridge(new StreamBridge(this))
{
#if FRIEDASBIRDVIEW_WEBENGINE_PROFILE_CUSTOM_CA
    QWebEngineProfileBuilder profileBuilder;
    profileBuilder.setAdditionalTrustedCertificates(customCaCertificates);
    profileBuilder.setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
    profileBuilder.setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
    m_profile = profileBuilder.createProfile(QStringLiteral("friedasbirdview-stream"), this);
#else
    Q_UNUSED(customCaCertificates)
    m_profile = new QWebEngineProfile(this);
    m_profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
#endif
    m_page = new StreamPage(m_profile, m_view, [this] {
        emit errorChanged(QStringLiteral("The live-stream player could not start."));
    });

    m_profile->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    m_profile->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);

    m_channel->registerObject(QStringLiteral("streamBridge"), m_bridge);
    m_page->setWebChannel(m_channel);
    m_view->setPage(m_page);
    m_view->setContextMenuPolicy(Qt::NoContextMenu);
    m_view->setFocusPolicy(Qt::NoFocus);
    setFocusPolicy(Qt::NoFocus);

    connect(m_page, &QWebEnginePage::certificateError, this, [this](const QWebEngineCertificateError &error) {
        if (error.type() == QWebEngineCertificateError::CertificateAuthorityInvalid) {
            emit errorChanged(QStringLiteral(
                "The live-stream certificate is not trusted. Add Frigate’s issuing CA in Settings, then restart FriedasBirdview."
            ));
        } else {
            emit errorChanged(QStringLiteral(
                "The live-stream TLS certificate was rejected. Check its name, date, and trust chain."
            ));
        }
        // Deliberately do not accept the certificate: both HTTPS and WSS must
        // retain normal Chromium certificate validation.
    });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    connect(m_bridge, &StreamBridge::errorReported, this, &StreamView::errorChanged);
    connect(m_bridge, &StreamBridge::aspectRatioReported, this, &StreamView::aspectRatioChanged);
    connect(m_bridge, &StreamBridge::dismissRequested, this, &StreamView::dismissRequested);
    connect(m_bridge, &StreamBridge::streamConnected, this, &StreamView::streamConnected);
    connect(m_bridge, &StreamBridge::fallbackRequested, this, &StreamView::jpegFallbackRequested);
    connect(m_profile->cookieStore(), &QWebEngineCookieStore::cookieAdded, this, &StreamView::cookieAdded);
}

void StreamView::start(const QUrl &serverUrl, const QString &streamName, const QList<QNetworkCookie> &cookies)
{
    if (!serverUrl.isValid() || streamName.trimmed().isEmpty()) {
        emit errorChanged(QStringLiteral("A valid Frigate stream is not available."));
        return;
    }

    const int loadId = ++m_loadId;
    const QString html = streamHtml(serverUrl, streamName);
    m_cookieLoadId = loadId;
    m_pendingCookieNames.clear();
    m_pendingHtml = html;
    m_pendingServerUrl = serverUrl;
    for (const QNetworkCookie &cookie : cookies) {
        m_pendingCookieNames.insert(cookie.name());
        m_profile->cookieStore()->setCookie(cookie, serverUrl);
    }
    if (m_pendingCookieNames.isEmpty()) {
        loadHtmlWhenCookiesAreReady(loadId, html, serverUrl);
        return;
    }
    // Cookie writes are asynchronous. Wait for Chromium to confirm the cookies before
    // opening the MSE WebSocket; the fallback prevents a broken wallet/session service
    // from leaving the feed in a permanent loading state.
    QTimer::singleShot(1500, this, [this, loadId, html, serverUrl] {
        loadHtmlWhenCookiesAreReady(loadId, html, serverUrl);
    });
}

void StreamView::stop()
{
    ++m_loadId;
    m_cookieLoadId = 0;
    m_pendingCookieNames.clear();
    m_view->setHtml(QStringLiteral("<!doctype html><title>Stopped</title>"));
}

void StreamView::loadHtmlWhenCookiesAreReady(int loadId, const QString &html, const QUrl &serverUrl)
{
    if (loadId != m_loadId || loadId != m_cookieLoadId) {
        return;
    }
    m_cookieLoadId = 0;
    m_pendingCookieNames.clear();
    m_view->setHtml(html, serverUrl);
}

void StreamView::cookieAdded(const QNetworkCookie &cookie)
{
    if (m_cookieLoadId != m_loadId || m_cookieLoadId == 0) {
        return;
    }
    if (m_pendingCookieNames.remove(cookie.name()) && m_pendingCookieNames.isEmpty()) {
        loadHtmlWhenCookiesAreReady(m_cookieLoadId, m_pendingHtml, m_pendingServerUrl);
    }
}

QString StreamView::streamHtml(const QUrl &serverUrl, const QString &streamName) const
{
    const QString server = javaScriptString(serverUrl.toString(QUrl::FullyEncoded));
    const QString camera = javaScriptString(streamName);
    return QStringLiteral(R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <script src="qrc:///qtwebchannel/qwebchannel.js"></script>
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
    const video = document.getElementById("feed");
    const codecs = ["avc1.640029", "avc1.64002A", "avc1.640033", "hvc1.1.6.L153.B0", "mp4a.40.2", "mp4a.40.5", "flac", "opus"];
    const maxBufferSeconds = 4;
    const keepBufferSeconds = 2.5;
    const maxPendingBytes = 2 * 1024 * 1024;
    const maxRecoveryAttempts = 3;
    const reconnectDelay = 1500;
    let bridge;
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

    function report(message) { if (bridge) bridge.reportError(message); }
    function fallback(message) {
      shouldReconnect = false;
      clearTimeout(firstFrameTimer);
      clearTimeout(restartTimer);
      clearTimeout(stablePlaybackTimer);
      closeSocket();
      resetMediaSource();
      if (bridge) bridge.fallbackToJpeg(message);
    }
    function reportAspectRatio() {
      if (video.videoWidth > 0 && video.videoHeight > 0 && bridge) {
        bridge.reportAspectRatio(video.videoWidth / video.videoHeight);
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
    function recoverBufferError(error) {
      const name = error?.name || "UnknownError";
      if (name === "QuotaExceededError") {
        recover("Live stream buffer is full.");
      } else if (name === "InvalidStateError") {
        recover("Live stream buffer entered an invalid state.");
      } else {
        recover(`Live stream buffer error: ${name}.`);
      }
    }
    function pump() {
      if (!sourceBuffer || sourceBuffer.updating || isRecovering) return;
      try {
        if (trimBuffer()) return;
        if (!pending.length) return;
        const segment = pending.shift();
        pendingBytes -= segment.byteLength;
        sourceBuffer.appendBuffer(segment);
      } catch (error) {
        recoverBufferError(error);
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
    function start() {
      if (!shouldReconnect) return;
      const MediaSourceConstructor = window.ManagedMediaSource || window.MediaSource;
      if (!MediaSourceConstructor) { fallback("This system cannot play Frigate's MSE stream. Showing JPEG snapshots."); return; }
      const supported = codecs.filter((codec) =>
        MediaSourceConstructor.isTypeSupported(`video/mp4; codecs="${codec}"`)
      ).join();
      if (!supported) { fallback("No compatible Frigate video codec is available. Showing JPEG snapshots."); return; }

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
          try {
            currentSocket.send(JSON.stringify({type:"mse", value:supported}));
          } catch (error) {
            recover(`Live stream setup failed: ${error.message || error}`);
          }
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
        firstFrameTimer = setTimeout(() => recover("Frigate connected but did not send playable video."), 6000);
      };
      currentSocket.onmessage = (event) => {
        if (socket !== currentSocket || isRecovering || typeof event.data !== "string") return;
        let response;
        try { response = JSON.parse(event.data); }
        catch (_) { recover("Frigate returned an invalid live-stream response."); return; }
        if (response.type !== "mse") return;
        try {
          if (!mediaSource || sourceBuffer) return;
          sourceBuffer = mediaSource.addSourceBuffer(response.value);
          if (sourceBuffer.mode) sourceBuffer.mode = "segments";
          sourceBuffer.addEventListener("updateend", () => {
            keepNearLiveEdge();
            pump();
          });
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
    video.addEventListener("click", () => bridge && bridge.dismiss());
    video.addEventListener("loadeddata", () => {
      clearTimeout(firstFrameTimer);
      if (bridge) bridge.connected();
      reportAspectRatio();
      lastPlaybackTime = video.currentTime;
      lastPlaybackAdvanceAt = Date.now();
      clearTimeout(stablePlaybackTimer);
      stablePlaybackTimer = setTimeout(() => {
        if (Date.now() - lastPlaybackAdvanceAt < 5000) recoveryAttempts = 0;
      }, 10000);
    });
    video.addEventListener("loadedmetadata", reportAspectRatio);
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
      if (video.readyState >= HTMLMediaElement.HAVE_CURRENT_DATA && Date.now() - lastPlaybackAdvanceAt > 5000) {
        recover("Live stream stalled.");
      }
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
    new QWebChannel(qt.webChannelTransport, (channel) => { bridge = channel.objects.streamBridge; start(); });
  </script>
</body>
</html>
)HTML").arg(server, camera);
}
