#include "StreamView.h"

#include <QAbstractSocket>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMediaPlayer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQueue>
#include <QRandomGenerator>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslSocket>
#include <QStackedLayout>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVideoWidget>
#include <QtMath>
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
#include <QWebSocket>

#include <functional>

namespace {
constexpr qsizetype kMaximumNativeBufferedBytes = 4LL * 1024 * 1024;
constexpr int kNativePlaybackAttempts = 2;
constexpr int kNativeFirstFrameTimeoutMs = 7000;
constexpr int kNativeStallTimeoutMs = 5000;
constexpr int kNativeFramesBeforeAcceptingPlayback = 3;

class LocalStreamRelay final : public QObject {
public:
    explicit LocalStreamRelay(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] { acceptConnection(); });
    }

    bool start()
    {
        stop();
        if (!m_server.listen(QHostAddress::LocalHost)) {
            return false;
        }
        QRandomGenerator *random = QRandomGenerator::system();
        m_token = QStringLiteral("%1%2")
            .arg(random->generate64(), 16, 16, QLatin1Char('0'))
            .arg(random->generate64(), 16, 16, QLatin1Char('0'));
        return true;
    }

    QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/%2.mp4")
                .arg(m_server.serverPort())
                .arg(m_token));
    }

    void stop()
    {
        if (m_client) {
            m_client->disconnect(this);
            m_client->close();
            m_client->deleteLater();
            m_client = nullptr;
        }
        m_server.close();
        m_pending.clear();
        m_pendingBytes = 0;
        m_request.clear();
        m_token.clear();
        m_responseStarted = false;
    }

    bool append(const QByteArray &chunk)
    {
        if (chunk.isEmpty()) {
            return true;
        }
        if (chunk.size() > kMaximumNativeBufferedBytes - m_pendingBytes) {
            return false;
        }
        if (!m_client || !m_responseStarted) {
            m_pending.enqueue(chunk);
            m_pendingBytes += chunk.size();
            return true;
        }
        if (!writeChunk(chunk)) {
            return false;
        }
        return true;
    }

private:
    void acceptConnection()
    {
        while (QTcpSocket *socket = m_server.nextPendingConnection()) {
            if (m_client) {
                socket->disconnectFromHost();
                socket->deleteLater();
                continue;
            }
            m_client = socket;
            connect(socket, &QTcpSocket::readyRead, this, [this] { readRequest(); });
            connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                if (m_client == socket) {
                    m_client = nullptr;
                    m_responseStarted = false;
                    m_request.clear();
                }
                socket->deleteLater();
            });
        }
    }

    void readRequest()
    {
        if (!m_client || m_responseStarted) {
            return;
        }
        m_request += m_client->readAll();
        if (m_request.size() > 8192) {
            m_client->disconnectFromHost();
            return;
        }
        const qsizetype headerEnd = m_request.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            return;
        }

        const QByteArray firstLine = m_request.left(m_request.indexOf("\r\n"));
        const QByteArray expectedPath = "/" + m_token.toUtf8() + ".mp4";
        const bool validRequest = firstLine.startsWith("GET ")
            && firstLine.mid(4).startsWith(expectedPath + " ");
        if (!validRequest) {
            m_client->write("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n");
            m_client->disconnectFromHost();
            return;
        }

        m_responseStarted = true;
        m_client->write(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: video/mp4\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: keep-alive\r\n\r\n"
        );
        while (!m_pending.isEmpty()) {
            const QByteArray chunk = m_pending.dequeue();
            m_pendingBytes -= chunk.size();
            if (!writeChunk(chunk)) {
                m_client->disconnectFromHost();
                return;
            }
        }
    }

    bool writeChunk(const QByteArray &chunk)
    {
        const qint64 queuedBytes = m_client ? m_client->bytesToWrite() : -1;
        if (queuedBytes < 0
            || chunk.size() > kMaximumNativeBufferedBytes - queuedBytes) {
            return false;
        }
        const QByteArray length = QByteArray::number(chunk.size(), 16);
        return m_client->write(length + "\r\n") >= 0
            && m_client->write(chunk) >= 0
            && m_client->write("\r\n") >= 0;
    }

    QTcpServer m_server;
    QTcpSocket *m_client = nullptr;
    QQueue<QByteArray> m_pending;
    QByteArray m_request;
    QString m_token;
    qsizetype m_pendingBytes = 0;
    bool m_responseStarted = false;
};

class NativeVideoWidget final : public QVideoWidget {
public:
    NativeVideoWidget(QWidget *parent, std::function<void()> dismiss)
        : QVideoWidget(parent)
        , m_dismiss(std::move(dismiss))
    {
        setFocusPolicy(Qt::NoFocus);
        setContextMenuPolicy(Qt::NoContextMenu);
        setCursor(Qt::PointingHandCursor);
        setStyleSheet(QStringLiteral("background: #050505;"));
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_dismiss();
            event->accept();
            return;
        }
        QVideoWidget::mouseReleaseEvent(event);
    }

private:
    std::function<void()> m_dismiss;
};

} // namespace

class StreamArtworkWidget final : public QWidget {
public:
    StreamArtworkWidget(QWidget *parent, std::function<void()> dismiss)
        : QWidget(parent)
        , m_artwork(QStringLiteral(":/docs/images/frieda-birdview-hero.png"))
        , m_dismiss(std::move(dismiss))
    {
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QStringLiteral("Live video is reconnecting. Click to dismiss the feed."));
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        painter.fillRect(rect(), QColor(QStringLiteral("#050505")));
        if (m_artwork.isNull()) {
            return;
        }
        const QSize targetSize = m_artwork.size().scaled(size(), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - targetSize.width()) / 2, (height() - targetSize.height()) / 2), targetSize);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(target, m_artwork);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_dismiss();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    QPixmap m_artwork;
    std::function<void()> m_dismiss;
};

class NativeStreamPlayer final : public QObject {
public:
    NativeStreamPlayer(
        const QList<QSslCertificate> &customCaCertificates,
        QWidget *host,
        std::function<void(const QString &)> reportError,
        std::function<void(double)> reportAspectRatio,
        std::function<void()> dismiss,
        std::function<void()> connected,
        std::function<void(const QString &)> failed
    )
        : QObject(host)
        , m_customCaCertificates(customCaCertificates)
        , m_relay(this)
        , m_network(this)
        , m_player(this)
        , m_video(new NativeVideoWidget(host, std::move(dismiss)))
        , m_reportError(std::move(reportError))
        , m_reportAspectRatio(std::move(reportAspectRatio))
        , m_connected(std::move(connected))
        , m_failed(std::move(failed))
    {
        m_player.setVideoOutput(m_video);
        m_firstFrameTimer.setSingleShot(true);
        m_stallTimer.setInterval(2000);

        connect(&m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &) {
                if (m_active && m_mediaStarted && !m_recovering) {
                    recover(QStringLiteral("The system video decoder could not play Frigate's live stream."));
                }
            });
        connect(m_video->videoSink(), &QVideoSink::videoFrameChanged, this,
            [this](const QVideoFrame &frame) { handleVideoFrame(frame); });
        connect(&m_firstFrameTimer, &QTimer::timeout, this, [this] {
            if (m_active && !m_receivedFrame) {
                recover(QStringLiteral("The native video player did not receive a decodable Frigate frame."));
            }
        });
        connect(&m_stallTimer, &QTimer::timeout, this, [this] {
            if (m_active && m_receivedFrame && m_lastFrame.elapsed() > kNativeStallTimeoutMs) {
                recover(QStringLiteral("The native live stream stalled."));
            }
        });
    }

    QWidget *videoWidget() const
    {
        return m_video;
    }

    void start(const QUrl &serverUrl, const QString &streamName, const QList<QNetworkCookie> &cookies)
    {
        stop();
        m_serverUrl = serverUrl;
        m_streamName = streamName;
        m_cookies = cookies;
        m_active = true;
        m_attempt = 0;
        m_transport = NativeTransport::ProgressiveHttp;
        startTransport();
    }

    void stop()
    {
        m_active = false;
        m_recovering = false;
        m_mediaStarted = false;
        m_receivedFrame = false;
        m_firstFrameTimer.stop();
        m_stallTimer.stop();
        closeSocket();
        closeReply();
        resetMediaPlayer();
        m_relay.stop();
    }

private:
    enum class NativeTransport {
        ProgressiveHttp,
        MseWebSocket,
    };

    void resetMediaPlayer()
    {
        // Clear both the FFmpeg source and QVideoWidget's last frame before a
        // new MSE session. Keeping the established QVideoWidget output avoids
        // a Qt Multimedia backend regression where a standalone QVideoSink
        // never receives frames on this platform.
        m_player.stop();
        m_player.setSource(QUrl());
        m_video->videoSink()->setVideoFrame(QVideoFrame());
        m_candidateFrameSize = QSize();
        m_candidateFrameCount = 0;
        m_lastFrame.invalidate();
    }

    void handleVideoFrame(const QVideoFrame &frame)
    {
        if (!m_active || !m_mediaStarted || !frame.isValid()) {
            return;
        }

        const QSize frameSize = frame.size();
        if (!frameSize.isValid()) {
            return;
        }

        // Do not show or trust a single frame. It can be a delayed frame left
        // in an FFmpeg decoder while a live fMP4 session is being replaced.
        // A real stream yields several consecutive frames of the same size
        // almost immediately.
        if (frameSize != m_candidateFrameSize) {
            m_candidateFrameSize = frameSize;
            m_candidateFrameCount = 0;
        }
        ++m_candidateFrameCount;
        m_lastFrame.restart();
        if (m_candidateFrameCount < kNativeFramesBeforeAcceptingPlayback) {
            return;
        }

        if (!m_receivedFrame) {
            m_receivedFrame = true;
            m_firstFrameTimer.stop();
            m_connected();
            m_reportAspectRatio(static_cast<double>(frameSize.width()) / frameSize.height());
        }
    }

    void closeSocket()
    {
        if (!m_socket) {
            return;
        }
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    void closeReply()
    {
        if (!m_reply) {
            return;
        }
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    QSslConfiguration tlsConfiguration() const
    {
        QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();
        QList<QSslCertificate> trusted = configuration.caCertificates();
        QSet<QByteArray> knownCertificates;
        for (const QSslCertificate &certificate : trusted) {
            knownCertificates.insert(certificate.toDer());
        }
        for (const QSslCertificate &certificate : m_customCaCertificates) {
            if (!knownCertificates.contains(certificate.toDer())) {
                trusted.append(certificate);
                knownCertificates.insert(certificate.toDer());
            }
        }
        configuration.setCaCertificates(trusted);
        configuration.setPeerVerifyMode(QSslSocket::VerifyPeer);
        return configuration;
    }

    QNetworkRequest streamRequest(const QString &path, bool webSocket) const
    {
        QUrl streamUrl = m_serverUrl;
        streamUrl.setPath(path);
        QUrlQuery streamQuery;
        streamQuery.addQueryItem(QStringLiteral("src"), m_streamName);
        streamUrl.setQuery(streamQuery);
        if (webSocket) {
            streamUrl.setScheme(m_serverUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0
                    ? QStringLiteral("wss")
                    : QStringLiteral("ws"));
        }

        QNetworkRequest request(streamUrl);
        request.setSslConfiguration(tlsConfiguration());
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        QStringList cookieParts;
        for (const QNetworkCookie &cookie : m_cookies) {
            if (!cookie.name().isEmpty()) {
                cookieParts.append(QString::fromLatin1(cookie.toRawForm(QNetworkCookie::NameAndValueOnly)));
            }
        }
        if (!cookieParts.isEmpty()) {
            request.setRawHeader("Cookie", cookieParts.join(QStringLiteral("; ")).toLatin1());
        }
        return request;
    }

    void startMediaPlayback()
    {
        m_mediaStarted = true;
        m_player.setSource(m_relay.url());
        m_player.play();
        m_firstFrameTimer.start(kNativeFirstFrameTimeoutMs);
        m_stallTimer.start();
    }

    void startProgressiveHttp()
    {
        // go2rtc's progressive MP4 endpoint contains the same authenticated
        // camera stream as its MSE WebSocket, but is an ordinary fMP4 HTTP
        // stream. FFmpeg can demux it directly instead of being asked to
        // emulate a browser MediaSource buffer.
        startMediaPlayback();
        QNetworkReply *const reply = m_network.get(streamRequest(QStringLiteral("/live/mse/api/stream.mp4"), false));
        m_reply = reply;

        connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply] {
            if (m_reply != reply || !m_active || m_recovering) {
                return;
            }
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status >= 400) {
                recover(QStringLiteral("Frigate's progressive MP4 endpoint returned HTTP %1.").arg(status));
            }
        });
        connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
            if (m_reply != reply || !m_active || m_recovering) {
                return;
            }
            const QByteArray data = reply->readAll();
            if (!data.isEmpty() && !m_relay.append(data)) {
                recover(QStringLiteral("The native video player could not keep up with Frigate's live stream."));
            }
        });
        connect(reply, &QNetworkReply::errorOccurred, this,
            [this, reply](QNetworkReply::NetworkError) {
                if (m_reply == reply && m_active && !m_recovering) {
                    recover(QStringLiteral("Could not open Frigate's progressive MP4 stream."));
                }
            });
        connect(reply, &QNetworkReply::sslErrors, this, [this, reply](const QList<QSslError> &) {
            // Custom CAs are included in streamRequest(). Do not bypass a
            // hostname, chain, or expiry error for the HTTP stream.
            if (m_reply == reply && m_active && !m_recovering) {
                recover(QStringLiteral("The native live-stream TLS certificate was rejected."));
            }
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            if (m_reply != reply) {
                return;
            }
            if (m_active && !m_recovering) {
                recover(QStringLiteral("Frigate's progressive MP4 stream ended."));
            }
        });
    }

    void startTransport()
    {
        if (!m_active) {
            return;
        }
        if (!m_relay.start()) {
            m_active = false;
            m_failed(QStringLiteral("Could not start the native live-stream relay."));
            return;
        }

        m_mediaStarted = false;
        m_receivedFrame = false;
        m_candidateFrameSize = QSize();
        m_candidateFrameCount = 0;
        if (m_transport == NativeTransport::ProgressiveHttp) {
            startProgressiveHttp();
        } else {
            openMseSocket();
        }
    }

    void createSocket(const QString &origin)
    {
        m_socket = new QWebSocket(origin, QWebSocketProtocol::VersionLatest, this);
        QWebSocket *const socket = m_socket;
        connect(socket, &QWebSocket::connected, this, [this, socket] {
            if (!m_active || m_recovering || m_socket != socket) {
                return;
            }
            const QJsonDocument request(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("mse")},
                // go2rtc chooses an fMP4 rendition from the codecs a player
                // advertises. Qt Multimedia delegates decoding to the system
                // FFmpeg backend, so this does not depend on Chromium codecs.
                {QStringLiteral("value"), QStringLiteral(
                    "avc1.640029,avc1.64002A,avc1.640033,hvc1.1.6.L153.B0,av01.0.08M.08,mp4a.40.2,mp4a.40.5,flac,opus"
                )},
            });
            socket->sendTextMessage(QString::fromUtf8(request.toJson(QJsonDocument::Compact)));
        });
        connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString &message) {
            if (m_socket == socket) {
                handleMseResponse(message);
            }
        });
        connect(socket, &QWebSocket::binaryMessageReceived, this, [this, socket](const QByteArray &message) {
            if (m_socket != socket || !m_active || m_recovering || !m_mediaStarted || !m_relay.append(message)) {
                if (m_socket == socket && m_active && !m_recovering) {
                    recover(QStringLiteral("The native video player could not keep up with Frigate's live stream."));
                }
            }
        });
        connect(socket, &QWebSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
            if (m_socket == socket && m_active && !m_recovering) {
                recover(QStringLiteral("Could not connect the native video player to Frigate."));
            }
        });
        connect(socket, &QWebSocket::sslErrors, this, [this, socket](const QList<QSslError> &) {
            // Do not call ignoreSslErrors(): custom CAs are added to the TLS
            // configuration below, and hostname, date, and chain validation
            // must remain enforced for the WebSocket connection.
            if (m_socket == socket && m_active && !m_recovering) {
                recover(QStringLiteral("The native live-stream TLS certificate was rejected."));
            }
        });
        connect(socket, &QWebSocket::disconnected, this, [this, socket] {
            if (m_socket == socket && m_active && !m_recovering) {
                recover(QStringLiteral("The native live stream disconnected."));
            }
        });
    }

    void openMseSocket()
    {
        QUrl originUrl;
        originUrl.setScheme(m_serverUrl.scheme());
        originUrl.setHost(m_serverUrl.host());
        if (m_serverUrl.port() >= 0) {
            originUrl.setPort(m_serverUrl.port());
        }
        createSocket(originUrl.toString(QUrl::FullyEncoded));
        m_socket->setSslConfiguration(tlsConfiguration());
        m_socket->open(streamRequest(QStringLiteral("/live/mse/api/ws"), true));
    }

    void handleMseResponse(const QString &message)
    {
        if (!m_active || m_recovering || m_mediaStarted) {
            return;
        }
        const QJsonDocument document = QJsonDocument::fromJson(message.toUtf8());
        const QJsonObject response = document.object();
        if (response.value(QStringLiteral("type")).toString() != QStringLiteral("mse")) {
            return;
        }
        const QString mimeType = response.value(QStringLiteral("value")).toString();
        if (!mimeType.startsWith(QStringLiteral("video/mp4"), Qt::CaseInsensitive)
            || !mimeType.contains(QStringLiteral("codecs="), Qt::CaseInsensitive)) {
            recover(QStringLiteral("Frigate did not return a compatible native video stream."));
            return;
        }

        startMediaPlayback();
    }

    void recover(const QString &reason)
    {
        if (!m_active || m_recovering) {
            return;
        }
        m_recovering = true;
        m_firstFrameTimer.stop();
        m_stallTimer.stop();
        closeSocket();
        closeReply();
        resetMediaPlayer();
        m_relay.stop();

        if (m_transport == NativeTransport::ProgressiveHttp && !m_receivedFrame) {
            m_transport = NativeTransport::MseWebSocket;
            m_attempt = 0;
            m_reportError(reason + QStringLiteral(" Trying the native MSE relay…"));
            QTimer::singleShot(1200, this, [this] {
                if (!m_active) {
                    return;
                }
                m_recovering = false;
                startTransport();
            });
            return;
        }

        if (++m_attempt < kNativePlaybackAttempts) {
            m_reportError(reason + QStringLiteral(" Retrying with the native decoder…"));
            QTimer::singleShot(1200, this, [this] {
                if (!m_active) {
                    return;
                }
                m_recovering = false;
                startTransport();
            });
            return;
        }

        m_active = false;
        m_recovering = false;
        m_failed(reason);
    }

    QList<QSslCertificate> m_customCaCertificates;
    QWebSocket *m_socket = nullptr;
    LocalStreamRelay m_relay;
    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QMediaPlayer m_player;
    NativeVideoWidget *m_video = nullptr;
    QTimer m_firstFrameTimer;
    QTimer m_stallTimer;
    QElapsedTimer m_lastFrame;
    QUrl m_serverUrl;
    QString m_streamName;
    QList<QNetworkCookie> m_cookies;
    std::function<void(const QString &)> m_reportError;
    std::function<void(double)> m_reportAspectRatio;
    std::function<void()> m_connected;
    std::function<void(const QString &)> m_failed;
    NativeTransport m_transport = NativeTransport::ProgressiveHttp;
    int m_attempt = 0;
    QSize m_candidateFrameSize;
    int m_candidateFrameCount = 0;
    bool m_active = false;
    bool m_recovering = false;
    bool m_mediaStarted = false;
    bool m_receivedFrame = false;
};

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
    , m_customCaCertificates(customCaCertificates)
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
        if (m_browserFallbackActive) {
            emit errorChanged(QStringLiteral("The compatibility live-stream player could not start."));
        }
    });

    m_profile->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    m_profile->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);

    m_channel->registerObject(QStringLiteral("streamBridge"), m_bridge);
    m_page->setWebChannel(m_channel);
    m_view->setPage(m_page);
    m_view->setContextMenuPolicy(Qt::NoContextMenu);
    m_view->setFocusPolicy(Qt::NoFocus);
    setFocusPolicy(Qt::NoFocus);

    m_nativePlayer = std::make_unique<NativeStreamPlayer>(
        m_customCaCertificates,
        this,
        [this](const QString &message) {
            if (m_usingNativePlayer && m_layout && m_artwork) {
                m_layout->setCurrentWidget(m_artwork);
            }
            emit errorChanged(message);
        },
        [this](double ratio) {
            if (qIsFinite(ratio) && ratio >= 0.25 && ratio <= 8.0) {
                emit aspectRatioChanged(ratio);
            }
        },
        [this] { emit dismissRequested(); },
        [this] {
            if (m_usingNativePlayer && m_layout && m_nativePlayer) {
                m_layout->setCurrentWidget(m_nativePlayer->videoWidget());
            }
            emit streamConnected();
        },
        [this](const QString &message) {
            if (!m_usingNativePlayer) {
                return;
            }
            m_usingNativePlayer = false;
            emit errorChanged(message + QStringLiteral(" Trying the compatibility player…"));
            startWebEngineFallback();
        }
    );

    connect(m_page, &QWebEnginePage::certificateError, this, [this](const QWebEngineCertificateError &error) {
        if (!m_browserFallbackActive) {
            return;
        }
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

    m_layout = new QStackedLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->addWidget(m_nativePlayer->videoWidget());
    m_artwork = new StreamArtworkWidget(this, [this] { emit dismissRequested(); });
    m_layout->addWidget(m_artwork);
    m_layout->addWidget(m_view);
    m_layout->setCurrentWidget(m_artwork);

    connect(m_bridge, &StreamBridge::errorReported, this, [this](const QString &message) {
        if (m_browserFallbackActive) {
            emit errorChanged(message);
        }
    });
    connect(m_bridge, &StreamBridge::aspectRatioReported, this, [this](double aspectRatio) {
        if (m_browserFallbackActive) {
            emit aspectRatioChanged(aspectRatio);
        }
    });
    connect(m_bridge, &StreamBridge::dismissRequested, this, [this] {
        if (m_browserFallbackActive) {
            emit dismissRequested();
        }
    });
    connect(m_bridge, &StreamBridge::streamConnected, this, [this] {
        if (m_browserFallbackActive) {
            emit streamConnected();
        }
    });
    connect(m_bridge, &StreamBridge::fallbackRequested, this, [this](const QString &message) {
        if (m_browserFallbackActive) {
            emit jpegFallbackRequested(message);
        }
    });
    connect(m_profile->cookieStore(), &QWebEngineCookieStore::cookieAdded, this, &StreamView::cookieAdded);
}

StreamView::~StreamView() = default;

void StreamView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

void StreamView::start(const QUrl &serverUrl, const QString &streamName, const QList<QNetworkCookie> &cookies)
{
    if (!serverUrl.isValid() || streamName.trimmed().isEmpty()) {
        emit errorChanged(QStringLiteral("A valid Frigate stream is not available."));
        return;
    }

    ++m_loadId;
    m_cookieLoadId = 0;
    m_pendingCookieNames.clear();
    m_pendingHtml.clear();
    m_pendingServerUrl = serverUrl;
    m_pendingStreamName = streamName;
    m_pendingCookies = cookies;
    m_usingNativePlayer = true;
    m_browserFallbackActive = false;
    m_view->setHtml(QStringLiteral("<!doctype html><title>Native live playback</title>"));
    m_layout->setCurrentWidget(m_artwork);
    m_nativePlayer->start(serverUrl, streamName, cookies);
}

void StreamView::stop()
{
    ++m_loadId;
    m_usingNativePlayer = false;
    m_browserFallbackActive = false;
    m_cookieLoadId = 0;
    m_pendingCookieNames.clear();
    m_pendingHtml.clear();
    m_pendingCookies.clear();
    m_nativePlayer->stop();
    m_view->setHtml(QStringLiteral("<!doctype html><title>Stopped</title>"));
}

void StreamView::startWebEngineFallback()
{
    if (!m_pendingServerUrl.isValid() || m_pendingStreamName.trimmed().isEmpty()) {
        emit jpegFallbackRequested(QStringLiteral("A compatible Frigate live stream is not available. Showing JPEG snapshots."));
        return;
    }

    m_nativePlayer->stop();
    m_browserFallbackActive = true;
    m_layout->setCurrentWidget(m_view);
    const int loadId = m_loadId;
    const QString html = streamHtml(m_pendingServerUrl, m_pendingStreamName);
    m_cookieLoadId = loadId;
    m_pendingCookieNames.clear();
    m_pendingHtml = html;
    for (const QNetworkCookie &cookie : m_pendingCookies) {
        m_pendingCookieNames.insert(cookie.name());
        m_profile->cookieStore()->setCookie(cookie, m_pendingServerUrl);
    }
    if (m_pendingCookieNames.isEmpty()) {
        loadHtmlWhenCookiesAreReady(loadId, html, m_pendingServerUrl);
        return;
    }
    // Cookie writes are asynchronous. Wait for Chromium to confirm the cookies before
    // opening the compatibility MSE WebSocket; the timeout prevents a broken
    // session service from leaving the feed in a permanent loading state.
    QTimer::singleShot(1500, this, [this, loadId, html] {
        loadHtmlWhenCookiesAreReady(loadId, html, m_pendingServerUrl);
    });
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
    // go2rtc selects its output from the codecs the client advertises. Keep
    // video and audio separate: an audio-only capability list must never open
    // an MSE feed that can only render a black video surface.
    const videoCodecs = ["avc1.640029", "avc1.64002A", "avc1.640033", "hvc1.1.6.L153.B0", "av01.0.08M.08"];
    const audioCodecs = ["mp4a.40.2", "mp4a.40.5", "flac", "opus"];
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
    let announcedMimeType = "";
    let renderer = navigator.userAgent.includes("Windows") ? "Windows software renderer" : "system renderer";

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
    function hasVideoCodec(mimeType) {
      return /(?:avc1|avc3|hvc1|hev1|av01|vp09|vp8)/i.test(mimeType || "");
    }
    function start() {
      if (!shouldReconnect) return;
      const MediaSourceConstructor = window.ManagedMediaSource || window.MediaSource;
      if (!MediaSourceConstructor) { fallback("This system cannot play Frigate's MSE stream. Showing JPEG snapshots."); return; }
      const supportedVideo = videoCodecs.filter((codec) =>
        MediaSourceConstructor.isTypeSupported(`video/mp4; codecs="${codec}"`)
      );
      if (!supportedVideo.length) {
        fallback("This Qt WebEngine installation cannot decode a Frigate video codec. Showing JPEG snapshots.");
        return;
      }
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
        firstFrameTimer = setTimeout(() => {
          const type = announcedMimeType || "the announced stream codec";
          recover(`Frigate connected but ${renderer} did not decode ${type}.`);
        }, 6000);
      };
      currentSocket.onmessage = (event) => {
        if (socket !== currentSocket || isRecovering || typeof event.data !== "string") return;
        let response;
        try { response = JSON.parse(event.data); }
        catch (_) { recover("Frigate returned an invalid live-stream response."); return; }
        if (response.type !== "mse") return;
        try {
          if (!mediaSource || sourceBuffer) return;
          announcedMimeType = response.value;
          if (!hasVideoCodec(announcedMimeType)) {
            fallback("Frigate returned an audio-only live stream. Showing JPEG snapshots.");
            return;
          }
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
    video.addEventListener("error", () => {
      const mediaError = video.error;
      const detail = mediaError ? ` (code ${mediaError.code})` : "";
      recover(`Live stream decoder error${detail}.`);
    });
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
