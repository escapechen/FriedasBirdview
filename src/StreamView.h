#pragma once

#include <QList>
#include <QList>
#include <QNetworkCookie>
#include <QSslCertificate>
#include <QSet>
#include <QUrl>
#include <QWidget>

class QWebChannel;
class QWebEnginePage;
class QWebEngineProfile;
class QWebEngineView;

class StreamBridge final : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    Q_INVOKABLE void reportError(const QString &message);
    Q_INVOKABLE void reportAspectRatio(double aspectRatio);
    Q_INVOKABLE void dismiss();
    Q_INVOKABLE void connected();
    Q_INVOKABLE void fallbackToJpeg(const QString &message);

signals:
    void errorReported(const QString &message);
    void aspectRatioReported(double aspectRatio);
    void dismissRequested();
    void streamConnected();
    void fallbackRequested(const QString &message);
};

class StreamView final : public QWidget {
    Q_OBJECT

public:
    explicit StreamView(const QList<QSslCertificate> &customCaCertificates, QWidget *parent = nullptr);

    void start(const QUrl &serverUrl, const QString &streamName, const QList<QNetworkCookie> &cookies);
    void stop();

signals:
    void errorChanged(const QString &message);
    void aspectRatioChanged(double aspectRatio);
    void dismissRequested();
    void streamConnected();
    void jpegFallbackRequested(const QString &message);

private:
    QString streamHtml(const QUrl &serverUrl, const QString &streamName) const;
    void loadHtmlWhenCookiesAreReady(int loadId, const QString &html, const QUrl &serverUrl);
    void cookieAdded(const QNetworkCookie &cookie);

    QWebEngineProfile *m_profile = nullptr;
    QWebEngineView *m_view = nullptr;
    QWebEnginePage *m_page = nullptr;
    QWebChannel *m_channel = nullptr;
    StreamBridge *m_bridge = nullptr;
    int m_loadId = 0;
    int m_cookieLoadId = 0;
    QSet<QByteArray> m_pendingCookieNames;
    QString m_pendingHtml;
    QUrl m_pendingServerUrl;
};
