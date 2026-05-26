#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QSslSocket>
#include <QSslConfiguration>
#include <QTcpSocket>
#include <QMap>
#include <QString>
#include <QByteArray>
#include <functional>
#include <QHash>
#include <QFile>

class HttpRequest
{
public:
    QString method;
    QString path;
    QString version;
    QMap<QString, QString> headers;
    QMap<QString, QString> queryParams;
    QByteArray body;

    QString remoteAddress;
    quint16 remotePort;
};

class HttpResponse
{
public:
    int statusCode;
    QString statusMessage;
    QMap<QString, QString> headers;
    QByteArray body;

    // Streaming response support - file is read and sent in chunks
    bool isStreaming = false;
    QString filePath;           // Path to file for streaming response
    qint64 streamOffset = 0;    // Start offset (for Range requests)
    qint64 streamLength = -1;   // Number of bytes to send (-1 = entire file from offset)
    QString deleteAfterSend;    // Temp file to delete after sending (e.g. zip)

    static HttpResponse ok(const QByteArray& body, const QString& contentType = "text/html");
    static HttpResponse badRequest(const QString& message = "Bad Request");
    static HttpResponse notFound(const QString& message = "Not Found");
    static HttpResponse internalError(const QString& message = "Internal Server Error");
    static HttpResponse redirect(const QString& url);
    static HttpResponse fileResponse(const QByteArray& data, const QString& fileName,
                                      const QString& contentType = "application/octet-stream",
                                      const QString& rangeHeader = QString());

    // Create a streaming file response (no data loaded into memory)
    static HttpResponse streamingFileResponse(const QString& filePath, const QString& fileName,
                                               const QString& contentType = "application/octet-stream",
                                               const QString& rangeHeader = QString(),
                                               const QString& deleteAfter = QString());
};

using RouteHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

// Streaming route handler: called with each body chunk as data arrives
using StreamingBodyHandler = std::function<void(QTcpSocket* socket, const HttpRequest& request, const QByteArray& chunk, bool isLast)>;

// Progress callback for streaming file send
using StreamProgressCallback = std::function<void(qint64 bytesSent)>;

class HttpServer : public QObject
{
    Q_OBJECT

public:
    explicit HttpServer(QObject* parent = nullptr);
    ~HttpServer() override;

    bool start(quint16 port, const QString& address = "0.0.0.0");
    void stop();

    bool isRunning() const;
    quint16 port() const;

    void setRootPath(const QString& path);
    QString rootPath() const;

    void setSslConfiguration(const QSslConfiguration& config);
    void setTlsEnabled(bool enabled);
    bool isTlsEnabled() const;

    void addRoute(const QString& method, const QString& pathPattern, RouteHandler handler);
    void addStreamingRoute(const QString& method, const QString& pathPattern, StreamingBodyHandler handler);
    void setDefaultHandler(RouteHandler handler);

    // Public so streaming handlers can send responses
    void sendResponse(QTcpSocket* socket, const HttpResponse& response);
    // Cancel a streaming request (clean up context without sending response)
    void cancelStreamingRequest(QTcpSocket* socket);

signals:
    void serverStarted(quint16 port);
    void serverStopped();
    void requestReceived(const HttpRequest& request);
    void errorOccurred(const QString& error);
    void streamingSocketDisconnected(QTcpSocket* socket);

public slots:
    void handleRequest(const HttpRequest& request, HttpResponse& response);

protected:
    void handleClientSocket(QTcpSocket* socket, const QByteArray& buffer);

private:
    void setupSocketSignals(QTcpSocket* socket);
    void parseRequest(const QByteArray& data, HttpRequest& request);
    void parseRequestHeaders(const QByteArray& headerData, HttpRequest& request);
    bool parseFirstLine(const QByteArray& line, QString& method, QString& path, QString& version);
    bool matchRoute(const QString& method, const QString& path, RouteHandler& handler);
    bool matchStreamingRoute(const QString& method, const QString& path, StreamingBodyHandler& handler);
    void sendStreamingResponse(QTcpSocket* socket, const HttpResponse& response);

    QTcpServer* m_server;
    QString m_address;
    quint16 m_port;
    QString m_rootPath;
    bool m_running;
    bool m_useTls = false;
    QSslConfiguration m_sslConfig;

    struct Route {
        QString method;
        QString pathPattern;
        RouteHandler handler;
    };
    QList<Route> m_routes;
    RouteHandler m_defaultHandler;

    struct StreamingRoute {
        QString method;
        QString pathPattern;
        StreamingBodyHandler handler;
    };
    QList<StreamingRoute> m_streamingRoutes;

    struct StreamingContext {
        HttpRequest request;
        qint64 bodyReceived;
        qint64 contentLength;
        StreamingBodyHandler handler;
        bool completed = false;
    };
    QHash<QTcpSocket*, StreamingContext*> m_streamingContexts;

    // Download streaming state
    struct StreamingDownloadState {
        QFile* file = nullptr;
        qint64 bytesSent = 0;
        qint64 totalBytes = 0;
        StreamProgressCallback progressCallback;
        QString deleteAfterSend;
    };
    QHash<QTcpSocket*, StreamingDownloadState> m_downloadContexts;

    QHash<QTcpSocket*, QByteArray> m_requestBuffers;
    QHash<QTcpSocket*, qint64> m_expectedBodySize;
};

#endif
