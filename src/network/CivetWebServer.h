#ifndef CIVETWEBSERVER_H
#define CIVETWEBSERVER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include <QJsonObject>
#include <functional>
#include "civetweb.h"

struct HttpRequestInfo
{
    QString method;
    QString uri;
    QString queryString;
    QMap<QString, QString> headers;
    QByteArray body;
    QString remoteAddress;
};

class CivetWebServer : public QObject
{
    Q_OBJECT

public:
    using RouteHandler = std::function<int(mg_connection*, const HttpRequestInfo&)>;
    using StreamingHandler = std::function<int(mg_connection*, const HttpRequestInfo&,
                                                const QByteArray& chunk, bool isLast)>;
    using WsConnectHandler = std::function<int(const mg_connection*)>;
    using WsReadyHandler   = std::function<void(mg_connection*)>;
    using WsDataHandler    = std::function<int(mg_connection*, int, char*, size_t)>;
    using WsCloseHandler   = std::function<void(const mg_connection*)>;

    explicit CivetWebServer(QObject* parent = nullptr);
    ~CivetWebServer() override;

    bool start(quint16 port, const QString& bindAddress = QStringLiteral("0.0.0.0"));
    void stop();
    bool isRunning() const;
    quint16 port() const;

    void addRoute(const QString& method, const QString& uri, RouteHandler handler);
    void addStreamingRoute(const QString& method, const QString& uri, StreamingHandler handler);
    void setDefaultHandler(RouteHandler handler);

    void enableWebSocket(const QString& path,
                         WsConnectHandler onConnect,
                         WsReadyHandler   onReady,
                         WsDataHandler    onData,
                         WsCloseHandler   onClose);

    void broadcastToSubscribers(const QString& token, const QString& type,
                                const QJsonObject& data);
    void subscribeClient(mg_connection* conn, const QString& token);
    void unsubscribeClient(mg_connection* conn, const QString& token);
    void unsubscribeClientFromAll(mg_connection* conn);
    int connectedClientCount() const;

    void setSslCertificate(const QString& certPath, const QString& keyPath);
    void setTlsEnabled(bool enabled);

    static void sendJsonResponse(mg_connection* conn, int status, const QByteArray& json);
    static void sendHtmlResponse(mg_connection* conn, int status, const QByteArray& html);
    static void sendFileResponse(mg_connection* conn, const QString& filePath,
                                  const QString& mimeType, const QString& fileName);
    static void sendStreamingFileResponse(mg_connection* conn, const QString& filePath,
                                           const QString& mimeType, const QString& fileName,
                                           const QString& rangeHeader);
    static HttpRequestInfo fromCivetWeb(mg_connection* conn, const mg_request_info* ri);

signals:
    void serverStarted(quint16 port);
    void serverStopped();
    void errorOccurred(const QString& error);
    void streamingConnDisconnected(mg_connection* conn);
    void wsClientConnected(mg_connection* conn, const QString& remoteAddress);
    void wsClientDisconnected(mg_connection* conn);
    void wsMessageReceived(mg_connection* conn, int opCode, const QByteArray& data);

private:
    static int staticBeginRequestHandler(mg_connection* conn, void* cbdata);
    static int staticWsConnectHandler(const mg_connection* conn, void* cbdata);
    static void staticWsReadyHandler(mg_connection* conn, void* cbdata);
    static int staticWsDataHandler(mg_connection* conn, int op, char* data, size_t len, void* cbdata);
    static void staticWsCloseHandler(const mg_connection* conn, void* cbdata);
    static int staticLogMessage(const mg_connection* conn, const char* message);

    void setupHeartbeat();
    void sendHeartbeat();
    void checkHeartbeatTimeout();
    int beginRequestHandler(mg_connection* conn);

    mg_context* m_ctx = nullptr;
    quint16 m_port = 0;
    QString m_bindAddress;
    bool m_running = false;
    bool m_tlsEnabled = false;
    QString m_certPath;
    QString m_keyPath;

    struct Route {
        QString method;
        QString uri;
        RouteHandler handler;
    };
    QList<Route> m_routes;

    struct StreamingRoute {
        QString method;
        QString uri;
        StreamingHandler handler;
    };
    QList<StreamingRoute> m_streamingRoutes;

    RouteHandler m_defaultHandler;

    WsConnectHandler m_wsConnectHandler;
    WsReadyHandler   m_wsReadyHandler;
    WsDataHandler    m_wsDataHandler;
    WsCloseHandler   m_wsCloseHandler;
    QString          m_wsPath;

    QMap<QString, QSet<mg_connection*>> m_wsClients;
    QMap<mg_connection*, QSet<QString>> m_wsSubscriptions;
    QMap<mg_connection*, qint64> m_wsLastPong;

    QTimer* m_heartbeatTimer = nullptr;
};

#endif
