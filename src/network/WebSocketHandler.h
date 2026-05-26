#ifndef WEBSOCKETHANDLER_H
#define WEBSOCKETHANDLER_H

#include <QObject>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QSslConfiguration>
#include <QList>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>

class WebSocketHandler : public QObject
{
    Q_OBJECT

public:
    explicit WebSocketHandler(QObject* parent = nullptr);
    ~WebSocketHandler() override;

    void setSslConfiguration(const QSslConfiguration& config);
    void setTlsEnabled(bool enabled);
    bool isTlsEnabled() const;

    bool start(quint16 port, const QString& bindAddress = "0.0.0.0");
    void stop();

    bool isRunning() const;
    quint16 port() const;

    void broadcastMessage(const QString& type, const QJsonObject& data);
    void sendMessageToClient(QWebSocket* client, const QString& type, const QJsonObject& data);

    // Send message only to clients subscribed to a specific token
    void broadcastToSubscribers(const QString& token, const QString& type, const QJsonObject& data);

    int connectedClientCount() const;

signals:
    void clientConnected(const QString& clientId, const QString& address);
    void clientDisconnected(const QString& clientId, const QString& address);
    void messageReceived(const QString& clientId, const QString& type, const QJsonObject& data);

private slots:
    void onNewConnection();
    void onTextMessageReceived(const QString& message);
    void onClientDisconnected();

private:
    void setupHeartbeat();
    void sendHeartbeat();
    void subscribeClient(QWebSocket* client, const QString& token);
    void unsubscribeClient(QWebSocket* client, const QString& token);
    void unsubscribeClientFromAll(QWebSocket* client);

    QWebSocketServer* m_server;
    QMap<QWebSocket*, QString> m_clients;
    QTimer* m_heartbeatTimer;
    quint16 m_port;
    int m_clientCounter;
    bool m_useTls = false;
    QSslConfiguration m_sslConfig;

    // Token-based subscriptions: token -> set of WebSocket clients
    QMap<QString, QSet<QWebSocket*>> m_subscriptions;
};

#endif
