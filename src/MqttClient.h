#pragma once

#include <QByteArray>
#include <QObject>
#include <QSslConfiguration>
#include <QString>

class QTcpSocket;
class QTimer;

// A deliberately small MQTT 3.1.1 subscriber.  Frigate only needs the
// client-side CONNECT, SUBSCRIBE, PING, and PUBLISH subset; keeping that
// subset here avoids making the desktop packages depend on an optional Qt
// module while retaining Qt Network's normal TLS verification.
class MqttClient final : public QObject
{
    Q_OBJECT

  public:
    struct Configuration {
        QString host;
        quint16 port = 8883;
        QString username;
        QString password;
        QString topicPrefix = QStringLiteral("frigate");
        bool useTls = true;
    };

    explicit MqttClient(QObject *parent = nullptr);
    ~MqttClient() override;

    void setConfiguration(const Configuration &configuration);
    void setTlsConfiguration(const QSslConfiguration &configuration);
    void start();
    void stop();

    bool isConnected() const;
    QString statusText() const;

signals:
    void connectionStateChanged(bool connected, const QString &detail);
    void connectionFailed(const QString &detail);
    void messageReceived(const QString &topic, const QByteArray &payload);

  private:
    void beginConnection();
    void disposeSocket();
    void sendConnectPacket();
    void sendSubscriptions();
    void sendPing();
    void writePacket(quint8 header, const QByteArray &payload);
    void processIncomingData();
    bool processPacket(quint8 header, const QByteArray &payload);
    void handlePublish(quint8 header, const QByteArray &payload);
    void reportFailure(const QString &detail);
    void scheduleReconnect();
    void setStatus(bool connected, const QString &detail);

    static QByteArray encodeString(const QString &value);
    static QByteArray encodeRemainingLength(qsizetype length);
    static bool readString(const QByteArray &data, qsizetype *offset, QString *value);

    Configuration m_configuration;
    QSslConfiguration m_tlsConfiguration;
    QTcpSocket *m_socket = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QTimer *m_connectTimeoutTimer = nullptr;
    QTimer *m_pingTimer = nullptr;
    QByteArray m_receiveBuffer;
    bool m_running = false;
    bool m_connectPacketSent = false;
    bool m_waitingForPingResponse = false;
    bool m_connected = false;
    int m_nextReconnectDelaySeconds = 1;
    quint16 m_nextPacketIdentifier = 1;
    quint16 m_subscriptionPacketIdentifier = 0;
    QString m_status;
};
