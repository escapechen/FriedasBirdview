#include "MqttClient.h"

#include <QAbstractSocket>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>

#include <limits>

namespace {
constexpr qsizetype kMaximumPacketSize = qsizetype{1024} * 1024;
constexpr int kKeepAliveSeconds = 30;
constexpr int kConnectTimeoutMs = 15000;

QString socketFailureText(QAbstractSocket::SocketError error)
{
    switch (error) {
    case QAbstractSocket::ConnectionRefusedError:
        return QStringLiteral("The MQTT broker refused the connection.");
    case QAbstractSocket::RemoteHostClosedError:
        return QStringLiteral("The MQTT broker closed the connection.");
    case QAbstractSocket::HostNotFoundError:
        return QStringLiteral("The MQTT broker host name could not be resolved.");
    case QAbstractSocket::SocketAccessError:
        return QStringLiteral("Network access to the MQTT broker was denied.");
    case QAbstractSocket::SocketResourceError:
        return QStringLiteral("The system could not allocate a network socket for MQTT.");
    case QAbstractSocket::SocketTimeoutError:
        return QStringLiteral("The MQTT broker connection timed out.");
    case QAbstractSocket::NetworkError:
        return QStringLiteral("A network error interrupted the MQTT broker connection.");
    case QAbstractSocket::SslHandshakeFailedError:
        return QStringLiteral("The MQTT TLS handshake failed.");
    default:
        return QStringLiteral("Could not reach the MQTT broker.");
    }
}

QString connectionRefusalText(quint8 code)
{
    switch (code) {
    case 1:
        return QStringLiteral("MQTT broker rejected the protocol version.");
    case 2:
        return QStringLiteral("MQTT broker rejected the client identifier.");
    case 3:
        return QStringLiteral("MQTT broker is unavailable.");
    case 4:
        return QStringLiteral("MQTT broker rejected the username or password.");
    case 5:
        return QStringLiteral("MQTT broker did not authorize this connection.");
    default:
        return QStringLiteral("MQTT broker rejected the connection.");
    }
}
} // namespace

MqttClient::MqttClient(QObject *parent)
    : QObject(parent), m_reconnectTimer(new QTimer(this)), m_connectTimeoutTimer(new QTimer(this)),
      m_pingTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &MqttClient::beginConnection);

    m_connectTimeoutTimer->setSingleShot(true);
    connect(m_connectTimeoutTimer, &QTimer::timeout, this,
            [this] { reportFailure(QStringLiteral("MQTT broker did not complete the connection in time.")); });

    m_pingTimer->setInterval((kKeepAliveSeconds * 1000) / 2);
    connect(m_pingTimer, &QTimer::timeout, this, &MqttClient::sendPing);
}

MqttClient::~MqttClient()
{
    stop();
}

void MqttClient::setConfiguration(const Configuration &configuration)
{
    const bool changed =
        m_configuration.host != configuration.host || m_configuration.port != configuration.port ||
        m_configuration.username != configuration.username || m_configuration.password != configuration.password ||
        m_configuration.topicPrefix != configuration.topicPrefix || m_configuration.useTls != configuration.useTls;
    m_configuration = configuration;
    if (changed && m_running) {
        disposeSocket();
        beginConnection();
    }
}

void MqttClient::setTlsConfiguration(const QSslConfiguration &configuration)
{
    m_tlsConfiguration = configuration;
    if (m_running && m_configuration.useTls) {
        disposeSocket();
        beginConnection();
    }
}

void MqttClient::start()
{
    if (m_running) {
        return;
    }
    m_running = true;
    m_nextReconnectDelaySeconds = 1;
    beginConnection();
}

void MqttClient::stop()
{
    if (!m_running && !m_socket) {
        return;
    }
    m_running = false;
    m_reconnectTimer->stop();
    m_connectTimeoutTimer->stop();
    m_pingTimer->stop();
    m_receiveBuffer.clear();
    disposeSocket();
    m_waitingForPingResponse = false;
    setStatus(false, QStringLiteral("MQTT delivery is stopped."));
}

bool MqttClient::isConnected() const
{
    return m_connected;
}

QString MqttClient::statusText() const
{
    return m_status;
}

void MqttClient::beginConnection()
{
    if (!m_running) {
        return;
    }
    if (m_configuration.host.isEmpty()) {
        setStatus(false, QStringLiteral("Enter an MQTT broker host before enabling MQTT delivery."));
        return;
    }

    disposeSocket();
    m_receiveBuffer.clear();
    m_connectPacketSent = false;
    m_waitingForPingResponse = false;
    setStatus(false, QStringLiteral("Connecting to MQTT…"));
    m_connectTimeoutTimer->start(kConnectTimeoutMs);

    if (m_configuration.useTls) {
        auto *socket = new QSslSocket(this);
        socket->setSslConfiguration(m_tlsConfiguration);
        socket->setPeerVerifyName(m_configuration.host);
        m_socket = socket;
        connect(socket, &QSslSocket::encrypted, this, &MqttClient::sendConnectPacket);
        connect(socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &) {
            // Do not call ignoreSslErrors(): custom CA support changes only
            // the trusted roots, so host-name and expiry validation still
            // apply.
            reportFailure(QStringLiteral("TLS validation for the MQTT broker failed."));
        });
        socket->connectToHostEncrypted(m_configuration.host, m_configuration.port);
    } else {
        auto *socket = new QTcpSocket(this);
        m_socket = socket;
        connect(socket, &QTcpSocket::connected, this, &MqttClient::sendConnectPacket);
        socket->connectToHost(m_configuration.host, m_configuration.port);
    }

    connect(m_socket, &QIODevice::readyRead, this, &MqttClient::processIncomingData);
    connect(m_socket, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        if (m_socket) {
            reportFailure(socketFailureText(error));
        }
    });
    connect(m_socket, &QAbstractSocket::disconnected, this, [this] {
        if (m_running) {
            reportFailure(QStringLiteral("MQTT broker connection closed."));
        }
    });
}

void MqttClient::disposeSocket()
{
    if (!m_socket) {
        return;
    }
    QTcpSocket *socket = m_socket;
    m_socket = nullptr;
    socket->disconnect(this);
    if (socket->state() != QAbstractSocket::UnconnectedState) {
        socket->abort();
    }
    socket->deleteLater();
}

void MqttClient::sendConnectPacket()
{
    if (!m_socket || m_connectPacketSent) {
        return;
    }
    QByteArray payload = encodeString(QStringLiteral("MQTT"));
    payload.append(char(4)); // MQTT 3.1.1
    quint8 flags = 0x02;     // clean session
    if (!m_configuration.username.isEmpty()) {
        flags |= 0x80;
    }
    if (!m_configuration.password.isEmpty()) {
        flags |= 0x40;
    }
    payload.append(char(flags));
    payload.append(char((kKeepAliveSeconds >> 8) & 0xff));
    payload.append(char(kKeepAliveSeconds & 0xff));
    payload.append(
        encodeString(QStringLiteral("friedasbirdview-") + QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!m_configuration.username.isEmpty()) {
        payload.append(encodeString(m_configuration.username));
    }
    if (!m_configuration.password.isEmpty()) {
        payload.append(encodeString(m_configuration.password));
    }
    if (payload.isEmpty()) {
        reportFailure(QStringLiteral("MQTT connection details are too large."));
        return;
    }
    m_connectPacketSent = true;
    writePacket(0x10, payload);
}

void MqttClient::sendSubscriptions()
{
    QString prefix = m_configuration.topicPrefix;
    while (prefix.endsWith(QLatin1Char('/'))) {
        prefix.chop(1);
    }
    QByteArray payload;
    m_subscriptionPacketIdentifier = m_nextPacketIdentifier++;
    if (m_nextPacketIdentifier == 0) {
        ++m_nextPacketIdentifier;
    }
    payload.append(char((m_subscriptionPacketIdentifier >> 8) & 0xff));
    payload.append(char(m_subscriptionPacketIdentifier & 0xff));
    for (const QString &topic : {prefix + QStringLiteral("/events"), prefix + QStringLiteral("/reviews")}) {
        const QByteArray encodedTopic = encodeString(topic);
        if (encodedTopic.isEmpty()) {
            reportFailure(QStringLiteral("MQTT topic is too large."));
            return;
        }
        payload.append(encodedTopic);
        payload.append(char(0)); // requested QoS 0
    }
    writePacket(0x82, payload);
}

void MqttClient::sendPing()
{
    if (!m_connected || !m_socket) {
        return;
    }
    if (m_waitingForPingResponse) {
        reportFailure(QStringLiteral("MQTT broker did not answer its keep-alive check."));
        return;
    }
    m_waitingForPingResponse = true;
    writePacket(0xc0, {});
}

void MqttClient::writePacket(quint8 header, const QByteArray &payload)
{
    if (!m_socket || payload.size() > kMaximumPacketSize) {
        reportFailure(QStringLiteral("MQTT packet is too large."));
        return;
    }
    QByteArray packet;
    packet.reserve(1 + 4 + payload.size());
    packet.append(char(header));
    packet.append(encodeRemainingLength(payload.size()));
    packet.append(payload);
    if (m_socket->write(packet) != packet.size()) {
        reportFailure(QStringLiteral("Could not send data to the MQTT broker."));
    }
}

void MqttClient::processIncomingData()
{
    if (!m_socket) {
        return;
    }
    m_receiveBuffer.append(m_socket->readAll());
    if (m_receiveBuffer.size() > kMaximumPacketSize + 5) {
        reportFailure(QStringLiteral("MQTT broker sent an unexpectedly large packet."));
        return;
    }

    while (!m_receiveBuffer.isEmpty()) {
        if (m_receiveBuffer.size() < 2) {
            return;
        }
        qsizetype offset = 1;
        qsizetype remainingLength = 0;
        int multiplier = 1;
        bool remainingLengthComplete = false;
        for (int count = 0; count < 4; ++count) {
            if (offset >= m_receiveBuffer.size()) {
                return;
            }
            const quint8 encoded = static_cast<quint8>(m_receiveBuffer.at(offset++));
            remainingLength += static_cast<qsizetype>(encoded & 0x7f) * multiplier;
            if ((encoded & 0x80) == 0) {
                remainingLengthComplete = true;
                break;
            }
            multiplier *= 128;
        }
        if (!remainingLengthComplete || remainingLength > kMaximumPacketSize) {
            reportFailure(QStringLiteral("MQTT broker sent an invalid packet."));
            return;
        }
        if (m_receiveBuffer.size() - offset < remainingLength) {
            return;
        }
        const quint8 header = static_cast<quint8>(m_receiveBuffer.at(0));
        const QByteArray payload = m_receiveBuffer.mid(offset, remainingLength);
        m_receiveBuffer.remove(0, offset + remainingLength);
        if (!processPacket(header, payload)) {
            return;
        }
    }
}

bool MqttClient::processPacket(quint8 header, const QByteArray &payload)
{
    switch (header >> 4) {
    case 2: // CONNACK
        if (payload.size() != 2 || payload.at(0) != 0) {
            reportFailure(QStringLiteral("MQTT broker sent an invalid connection response."));
            return false;
        }
        if (static_cast<quint8>(payload.at(1)) != 0) {
            reportFailure(connectionRefusalText(static_cast<quint8>(payload.at(1))));
            return false;
        }
        m_connectTimeoutTimer->stop();
        sendSubscriptions();
        return true;
    case 3: // PUBLISH
        handlePublish(header, payload);
        return m_socket != nullptr;
    case 9: // SUBACK
        if (payload.size() < 3) {
            reportFailure(QStringLiteral("MQTT broker sent an invalid subscription response."));
            return false;
        }
        if (static_cast<quint8>(payload.at(0)) != ((m_subscriptionPacketIdentifier >> 8) & 0xff) ||
            static_cast<quint8>(payload.at(1)) != (m_subscriptionPacketIdentifier & 0xff)) {
            reportFailure(QStringLiteral("MQTT broker returned an unexpected subscription response."));
            return false;
        }
        for (qsizetype index = 2; index < payload.size(); ++index) {
            if (static_cast<quint8>(payload.at(index)) == 0x80) {
                reportFailure(QStringLiteral("MQTT broker did not authorize the Frigate event topics."));
                return false;
            }
        }
        m_nextReconnectDelaySeconds = 1;
        m_pingTimer->start();
        setStatus(true, QStringLiteral("MQTT delivery connected."));
        return true;
    case 13: // PINGRESP
        m_waitingForPingResponse = false;
        return true;
    default:
        // Frigate's event topics need no acknowledgement beyond QoS 1 PUBLISH.
        return true;
    }
}

void MqttClient::handlePublish(quint8 header, const QByteArray &payload)
{
    const quint8 qos = (header >> 1) & 0x03;
    if (qos == 2) {
        reportFailure(QStringLiteral("MQTT broker used unsupported QoS 2 delivery."));
        return;
    }
    qsizetype offset = 0;
    QString topic;
    if (!readString(payload, &offset, &topic)) {
        reportFailure(QStringLiteral("MQTT broker sent an invalid event message."));
        return;
    }
    quint16 packetIdentifier = 0;
    if (qos == 1) {
        if (offset + 2 > payload.size()) {
            reportFailure(QStringLiteral("MQTT broker sent an invalid event message."));
            return;
        }
        packetIdentifier = (static_cast<quint8>(payload.at(offset)) << 8) | static_cast<quint8>(payload.at(offset + 1));
        offset += 2;
    }
    emit messageReceived(topic, payload.mid(offset));
    if (qos == 1) {
        QByteArray acknowledgement;
        acknowledgement.append(char((packetIdentifier >> 8) & 0xff));
        acknowledgement.append(char(packetIdentifier & 0xff));
        writePacket(0x40, acknowledgement);
    }
}

void MqttClient::reportFailure(const QString &detail)
{
    m_connectTimeoutTimer->stop();
    m_pingTimer->stop();
    m_waitingForPingResponse = false;
    setStatus(false, detail);
    emit connectionFailed(detail);
    disposeSocket();
    scheduleReconnect();
}

void MqttClient::scheduleReconnect()
{
    if (!m_running || m_reconnectTimer->isActive()) {
        return;
    }
    const int delay = m_nextReconnectDelaySeconds;
    m_nextReconnectDelaySeconds = qMin(m_nextReconnectDelaySeconds * 2, 30);
    m_reconnectTimer->start(delay * 1000);
}

void MqttClient::setStatus(bool connected, const QString &detail)
{
    if (m_connected == connected && m_status == detail) {
        return;
    }
    m_connected = connected;
    m_status = detail;
    emit connectionStateChanged(connected, detail);
}

QByteArray MqttClient::encodeString(const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    if (utf8.size() > std::numeric_limits<quint16>::max()) {
        return {};
    }
    QByteArray result;
    result.reserve(2 + utf8.size());
    result.append(char((utf8.size() >> 8) & 0xff));
    result.append(char(utf8.size() & 0xff));
    result.append(utf8);
    return result;
}

QByteArray MqttClient::encodeRemainingLength(qsizetype length)
{
    QByteArray encoded;
    do {
        const int digit = static_cast<int>(length % qsizetype{128});
        length /= 128;
        encoded.append(char(digit | (length > 0 ? 0x80 : 0)));
    } while (length > 0);
    return encoded;
}

bool MqttClient::readString(const QByteArray &data, qsizetype *offset, QString *value)
{
    if (*offset + 2 > data.size()) {
        return false;
    }
    const qsizetype length = (static_cast<quint8>(data.at(*offset)) << 8) | static_cast<quint8>(data.at(*offset + 1));
    *offset += 2;
    if (length > data.size() - *offset) {
        return false;
    }
    *value = QString::fromUtf8(data.constData() + *offset, length);
    *offset += length;
    return true;
}
