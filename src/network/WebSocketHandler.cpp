#include "WebSocketHandler.h"
#include "Logger.h"
#include <QHostAddress>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>

WebSocketHandler::WebSocketHandler(QObject* parent)
    : QObject(parent)
    , m_server(nullptr)
    , m_heartbeatTimer(new QTimer(this))
    , m_port(0)
    , m_clientCounter(0)
{
    setupHeartbeat();
}

WebSocketHandler::~WebSocketHandler()
{
    stop();
}

bool WebSocketHandler::start(quint16 port, const QString& bindAddress)
{
    if (m_server && m_server->isListening()) {
        return true;
    }

    QWebSocketServer::SslMode sslMode = m_useTls
        ? QWebSocketServer::SecureMode
        : QWebSocketServer::NonSecureMode;
    m_server = new QWebSocketServer(QStringLiteral("NetShare"), sslMode, this);

    if (m_useTls) {
        m_server->setSslConfiguration(m_sslConfig);
    }

    QHostAddress address(bindAddress);
    if (!m_server->listen(address, port)) {
        LOG_ERROR("WebSocketHandler: failed to start on %s:%d - %s",
                  qPrintable(bindAddress), port,
                  qPrintable(m_server->errorString()));
        delete m_server;
        m_server = nullptr;
        return false;
    }

    m_port = m_server->serverPort();

    connect(m_server, &QWebSocketServer::newConnection,
            this, &WebSocketHandler::onNewConnection);

    m_heartbeatTimer->start(30000);

    LOG_INFO("WebSocketHandler: started on %s:%d", qPrintable(bindAddress), m_port);
    return true;
}

void WebSocketHandler::stop()
{
    if (!m_server) return;

    m_heartbeatTimer->stop();

    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        it.key()->close();
    }
    m_clients.clear();
    m_subscriptions.clear();

    m_server->close();
    delete m_server;
    m_server = nullptr;

    LOG_INFO("WebSocketHandler: stopped");
}

bool WebSocketHandler::isRunning() const
{
    return m_server && m_server->isListening();
}

quint16 WebSocketHandler::port() const
{
    return m_port;
}

int WebSocketHandler::connectedClientCount() const
{
    return m_clients.size();
}

void WebSocketHandler::setSslConfiguration(const QSslConfiguration& config)
{
    m_sslConfig = config;
}

void WebSocketHandler::setTlsEnabled(bool enabled)
{
    m_useTls = enabled;
}

bool WebSocketHandler::isTlsEnabled() const
{
    return m_useTls;
}

void WebSocketHandler::onNewConnection()
{
    QWebSocket* client = m_server->nextPendingConnection();
    if (!client) return;

    ++m_clientCounter;
    QString clientId = QString("ws_%1").arg(m_clientCounter, 4, 10, QChar('0'));
    m_clients[client] = clientId;

    connect(client, &QWebSocket::textMessageReceived,
            this, &WebSocketHandler::onTextMessageReceived);
    connect(client, &QWebSocket::disconnected,
            this, &WebSocketHandler::onClientDisconnected);

    LOG_INFO("WebSocketHandler: client connected %s from %s",
             qPrintable(clientId),
             qPrintable(client->peerAddress().toString()));

    emit clientConnected(clientId, client->peerAddress().toString());

    QJsonObject welcome;
    welcome["type"] = "welcome";
    welcome["clientId"] = clientId;
    welcome["message"] = "Connected to NetShare";
    sendMessageToClient(client, "welcome", welcome);
}

void WebSocketHandler::onTextMessageReceived(const QString& message)
{
    QWebSocket* client = qobject_cast<QWebSocket*>(sender());
    if (!client || !m_clients.contains(client)) return;

    QString clientId = m_clients[client];

    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) {
        LOG_WARN("WebSocketHandler: invalid JSON from %s", qPrintable(clientId));
        return;
    }

    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();
    QJsonObject data = obj["data"].toObject();

    if (type == "ping") {
        QJsonObject pong;
        pong["timestamp"] = QDateTime::currentDateTime().toMSecsSinceEpoch();
        sendMessageToClient(client, "pong", pong);
        return;
    }

    // Handle subscribe/unsubscribe messages
    if (type == "subscribe") {
        QString token = data["token"].toString();
        if (!token.isEmpty()) {
            subscribeClient(client, token);
            QJsonObject ack;
            ack["token"] = token;
            ack["status"] = "subscribed";
            sendMessageToClient(client, "subscribed", ack);
        }
        return;
    }

    if (type == "unsubscribe") {
        QString token = data["token"].toString();
        if (!token.isEmpty()) {
            unsubscribeClient(client, token);
        }
        return;
    }

    LOG_INFO("WebSocketHandler: message from %s - type: %s",
             qPrintable(clientId), qPrintable(type));

    emit messageReceived(clientId, type, data);
}

void WebSocketHandler::onClientDisconnected()
{
    QWebSocket* client = qobject_cast<QWebSocket*>(sender());
    if (!client) return;

    QString clientId = m_clients.value(client);
    QString address = client->peerAddress().toString();

    // Remove from all subscriptions
    unsubscribeClientFromAll(client);

    m_clients.remove(client);
    client->deleteLater();

    LOG_INFO("WebSocketHandler: client disconnected %s", qPrintable(clientId));
    emit clientDisconnected(clientId, address);
}

void WebSocketHandler::broadcastMessage(const QString& type, const QJsonObject& data)
{
    QJsonObject msg;
    msg["type"] = type;
    msg["data"] = data;
    msg["timestamp"] = QDateTime::currentDateTime().toMSecsSinceEpoch();

    QByteArray json = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    QString text = QString::fromUtf8(json);

    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        it.key()->sendTextMessage(text);
    }
}

void WebSocketHandler::sendMessageToClient(QWebSocket* client, const QString& type, const QJsonObject& data)
{
    if (!client || client->state() != QAbstractSocket::ConnectedState) return;

    QJsonObject msg;
    msg["type"] = type;
    msg["data"] = data;
    msg["timestamp"] = QDateTime::currentDateTime().toMSecsSinceEpoch();

    QByteArray json = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    client->sendTextMessage(QString::fromUtf8(json));
}

void WebSocketHandler::broadcastToSubscribers(const QString& token, const QString& type, const QJsonObject& data)
{
    if (!m_subscriptions.contains(token)) return;

    QJsonObject msg;
    msg["type"] = type;
    msg["data"] = data;
    msg["timestamp"] = QDateTime::currentDateTime().toMSecsSinceEpoch();

    QByteArray json = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    QString text = QString::fromUtf8(json);

    const QSet<QWebSocket*>& subscribers = m_subscriptions[token];
    for (QWebSocket* client : subscribers) {
        if (client->state() == QAbstractSocket::ConnectedState) {
            client->sendTextMessage(text);
        }
    }
}

void WebSocketHandler::subscribeClient(QWebSocket* client, const QString& token)
{
    m_subscriptions[token].insert(client);
    LOG_INFO("WebSocketHandler: client %s subscribed to token %s",
             qPrintable(m_clients.value(client, "unknown")), qPrintable(token));
}

void WebSocketHandler::unsubscribeClient(QWebSocket* client, const QString& token)
{
    if (m_subscriptions.contains(token)) {
        m_subscriptions[token].remove(client);
        if (m_subscriptions[token].isEmpty()) {
            m_subscriptions.remove(token);
        }
    }
}

void WebSocketHandler::unsubscribeClientFromAll(QWebSocket* client)
{
    // Remove client from all token subscriptions
    QStringList emptyTokens;
    for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
        it.value().remove(client);
        if (it.value().isEmpty()) {
            emptyTokens.append(it.key());
        }
    }
    for (const QString& token : emptyTokens) {
        m_subscriptions.remove(token);
    }
}

void WebSocketHandler::setupHeartbeat()
{
    connect(m_heartbeatTimer, &QTimer::timeout, this, &WebSocketHandler::sendHeartbeat);
}

void WebSocketHandler::sendHeartbeat()
{
    QList<QWebSocket*> toRemove;

    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.key()->state() == QAbstractSocket::ConnectedState) {
            QJsonObject ping;
            ping["timestamp"] = QDateTime::currentDateTime().toMSecsSinceEpoch();
            sendMessageToClient(it.key(), "heartbeat", ping);
        } else {
            toRemove.append(it.key());
        }
    }

    for (QWebSocket* client : toRemove) {
        QString clientId = m_clients.value(client);
        unsubscribeClientFromAll(client);
        m_clients.remove(client);
        client->deleteLater();
        emit clientDisconnected(clientId, QString());
    }
}
