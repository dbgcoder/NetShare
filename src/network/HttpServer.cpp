#include "HttpServer.h"
#include "Logger.h"
#include <QRegularExpression>
#include <QTimer>
#include <QFileInfo>

// ---------------------------------------------------------------------------
// SslAwareServer: a QTcpServer subclass that creates QSslSocket instances.
// If TLS is enabled, each new socket automatically starts the SSL handshake.
// ---------------------------------------------------------------------------
class SslAwareServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit SslAwareServer(QObject* parent = nullptr)
        : QTcpServer(parent) {}

    void setUseTls(bool useTls) { m_useTls = useTls; }
    void setSslConfiguration(const QSslConfiguration& config) { m_sslConfig = config; }

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        if (m_useTls) {
            QSslSocket* socket = new QSslSocket(this);
            if (socket->setSocketDescriptor(socketDescriptor)) {
                socket->setSslConfiguration(m_sslConfig);
                socket->startServerEncryption();
                addPendingConnection(socket);
            } else {
                delete socket;
            }
        } else {
            QTcpSocket* socket = new QTcpSocket(this);
            if (socket->setSocketDescriptor(socketDescriptor)) {
                addPendingConnection(socket);
            } else {
                delete socket;
            }
        }
    }

private:
    bool m_useTls = false;
    QSslConfiguration m_sslConfig;
};

HttpServer::HttpServer(QObject* parent)
    : QObject(parent)
    , m_server(new SslAwareServer(this))
    , m_port(8080)
    , m_running(false)
    , m_useTls(false)
{
    connect(m_server, &QTcpServer::newConnection, this, [this]() {
        while (m_server->hasPendingConnections()) {
            QTcpSocket* socket = m_server->nextPendingConnection();

            if (m_useTls) {
                QSslSocket* sslSocket = qobject_cast<QSslSocket*>(socket);
                if (sslSocket && !sslSocket->isEncrypted()) {
                    connect(sslSocket, &QSslSocket::encrypted, this, [this, sslSocket]() {
                        setupSocketSignals(sslSocket);
                    });
                    connect(sslSocket, &QSslSocket::errorOccurred, this,
                        [this, sslSocket](QAbstractSocket::SocketError error) {
                            if (error == QAbstractSocket::SslHandshakeFailedError) {
                                LOG_ERROR("SSL handshake failed for client");
                                sslSocket->disconnectFromHost();
                            }
                        });
                } else if (sslSocket) {
                    setupSocketSignals(sslSocket);
                }
            } else {
                setupSocketSignals(socket);
            }
        }
    });
}

void HttpServer::setupSocketSignals(QTcpSocket* socket)
{
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        // Check if this socket is already in streaming mode
        if (m_streamingContexts.contains(socket)) {
            auto* ctx = m_streamingContexts[socket];
            if (ctx->completed) return;

            QByteArray chunk = socket->readAll();
            ctx->bodyReceived += chunk.size();
            bool isLast = (ctx->bodyReceived >= ctx->contentLength);

            // Remove from map BEFORE calling handler to prevent double-free.
            // The handler may call sendResponse() → disconnectFromHost() which
            // can synchronously trigger the disconnected signal, whose handler
            // also deletes the StreamingContext.
            if (isLast) {
                ctx->completed = true;
                m_streamingContexts.remove(socket);
            }

            ctx->handler(socket, ctx->request, chunk, isLast);

            if (isLast) {
                delete ctx;
            }
            return;
        }

        QByteArray& buffer = m_requestBuffers[socket];
        buffer.append(socket->readAll());

        int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;

        qint64& expectedSize = m_expectedBodySize[socket];
        if (expectedSize == 0) {
            QByteArray headerData = buffer.left(headerEnd);
            QList<QByteArray> lines = headerData.split('\n');
            for (const QByteArray& line : lines) {
                QString l = QString::fromUtf8(line).trimmed();
                if (l.startsWith("Content-Length:", Qt::CaseInsensitive)) {
                    expectedSize = l.mid(15).trimmed().toLongLong();
                    break;
                }
            }
            if (expectedSize == 0) {
                handleClientSocket(socket, buffer);
                m_requestBuffers.remove(socket);
                m_expectedBodySize.remove(socket);
                return;
            }
        }

        qint64 bodyStart = headerEnd + 4;
        qint64 bodyReceived = buffer.size() - bodyStart;

        QString method, path, version;
        QByteArray headerData = buffer.left(headerEnd);
        QList<QByteArray> headerLines = headerData.split('\n');
        if (!headerLines.isEmpty()) {
            parseFirstLine(headerLines[0], method, path, version);
        }

        StreamingBodyHandler streamingHandler;
        bool isStreamingRoute = matchStreamingRoute(method, path, streamingHandler);

        // Force streaming for large request bodies (> 1MB)
        // This prevents m_requestBuffers from buffering the entire body in memory.
        // If the route is not registered as streaming, reject with 413.
        static const qint64 STREAMING_THRESHOLD = 1 * 1024 * 1024; // 1MB
        bool forceStreaming = (expectedSize > STREAMING_THRESHOLD);

        if (forceStreaming && !isStreamingRoute) {
            LOG_WARN("Large body (%lld bytes) on non-streaming route: %s %s - rejecting with 413",
                     expectedSize, qPrintable(method), qPrintable(path));
            HttpResponse response;
            response.statusCode = 413;
            response.statusMessage = "Payload Too Large";
            response.body = "Request body exceeds 1MB but route is not registered for streaming";
            sendResponse(socket, response);
            m_requestBuffers.remove(socket);
            m_expectedBodySize.remove(socket);
            return;
        }

        if (isStreamingRoute) {
            if (forceStreaming) {
                LOG_INFO("Streaming route for large body: %s %s Content-Length=%lld",
                         qPrintable(method), qPrintable(path), expectedSize);
            }

            StreamingContext* ctx = new StreamingContext;
            parseRequestHeaders(headerData, ctx->request);
            ctx->request.remoteAddress = socket->peerAddress().toString();
            ctx->request.remotePort = socket->peerPort();
            ctx->bodyReceived = bodyReceived;
            ctx->contentLength = expectedSize;
            ctx->handler = streamingHandler;
            m_streamingContexts.insert(socket, ctx);

            // Extract initial chunk BEFORE removing buffer from map.
            // buffer is a reference to m_requestBuffers[socket]; once removed,
            // the reference becomes dangling and buffer.mid() would be UB.
            QByteArray initialChunk = buffer.mid(bodyStart);
            m_requestBuffers.remove(socket);
            m_expectedBodySize.remove(socket);

            bool isLast = (ctx->bodyReceived >= ctx->contentLength);

            // Remove from map BEFORE calling handler to prevent double-free
            if (isLast) {
                ctx->completed = true;
                m_streamingContexts.remove(socket);
            }

            ctx->handler(socket, ctx->request, initialChunk, isLast);

            if (isLast) {
                delete ctx;
            }
            return;
        }

        if (bodyReceived >= expectedSize) {
            handleClientSocket(socket, buffer);
            m_requestBuffers.remove(socket);
            m_expectedBodySize.remove(socket);
        }
    });

    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
        if (m_streamingContexts.contains(socket)) {
            emit streamingSocketDisconnected(socket);
        }
        m_requestBuffers.remove(socket);
        m_expectedBodySize.remove(socket);
        if (m_streamingContexts.contains(socket)) {
            auto* ctx = m_streamingContexts[socket];
            ctx->completed = true;
            delete ctx;
            m_streamingContexts.remove(socket);
        }
        if (m_downloadContexts.contains(socket)) {
            auto& dlState = m_downloadContexts[socket];
            if (dlState.file) {
                dlState.file->close();
                delete dlState.file;
            }
            if (!dlState.deleteAfterSend.isEmpty()) {
                QFile::remove(dlState.deleteAfterSend);
            }
            m_downloadContexts.remove(socket);
        }
        socket->deleteLater();
    });
}

void HttpServer::setSslConfiguration(const QSslConfiguration& config)
{
    m_sslConfig = config;
    auto* sslServer = qobject_cast<SslAwareServer*>(m_server);
    if (sslServer) {
        sslServer->setSslConfiguration(config);
    }
}

void HttpServer::setTlsEnabled(bool enabled)
{
    m_useTls = enabled;
    auto* sslServer = qobject_cast<SslAwareServer*>(m_server);
    if (sslServer) {
        sslServer->setUseTls(enabled);
    }
}

bool HttpServer::isTlsEnabled() const
{
    return m_useTls;
}

HttpServer::~HttpServer()
{
    stop();
}

bool HttpServer::start(quint16 port, const QString& address)
{
    m_port = port;
    m_address = address;

    if (!m_server->listen(QHostAddress(address), port)) {
        LOG_ERROR("Failed to start HTTP server on %s:%d: %s",
                  qPrintable(address), port,
                  qPrintable(m_server->errorString()));
        return false;
    }

    m_running = true;
    LOG_INFO("HTTP server started on %s:%d", qPrintable(address), port);
    emit serverStarted(port);
    return true;
}

void HttpServer::stop()
{
    if (m_running) {
        m_server->close();
        m_running = false;
        LOG_INFO("HTTP server stopped");
        emit serverStopped();
    }
}

bool HttpServer::isRunning() const
{
    return m_running;
}

quint16 HttpServer::port() const
{
    return m_port;
}

void HttpServer::setRootPath(const QString& path)
{
    m_rootPath = path;
}

QString HttpServer::rootPath() const
{
    return m_rootPath;
}

void HttpServer::addRoute(const QString& method, const QString& pathPattern, RouteHandler handler)
{
    Route route;
    route.method = method.toUpper();
    route.pathPattern = pathPattern;
    route.handler = handler;
    m_routes.append(route);
}

void HttpServer::addStreamingRoute(const QString& method, const QString& pathPattern, StreamingBodyHandler handler)
{
    StreamingRoute route;
    route.method = method.toUpper();
    route.pathPattern = pathPattern;
    route.handler = handler;
    m_streamingRoutes.append(route);
}

void HttpServer::setDefaultHandler(RouteHandler handler)
{
    m_defaultHandler = handler;
}

void HttpServer::cancelStreamingRequest(QTcpSocket* socket)
{
    if (m_streamingContexts.contains(socket)) {
        delete m_streamingContexts[socket];
        m_streamingContexts.remove(socket);
    }
}

void HttpServer::handleClientSocket(QTcpSocket* socket, const QByteArray& buffer)
{
    HttpRequest request;
    parseRequest(buffer, request);
    request.remoteAddress = socket->peerAddress().toString();
    request.remotePort = socket->peerPort();

    if (request.method.isEmpty()) {
        HttpResponse response = HttpResponse::badRequest();
        sendResponse(socket, response);
        return;
    }

    // Handle CORS preflight
    if (request.method == "OPTIONS") {
        HttpResponse response;
        response.statusCode = 204;
        response.statusMessage = "No Content";
        response.headers["Access-Control-Allow-Origin"] = "*";
        response.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS";
         response.headers["Access-Control-Allow-Headers"] = "Content-Type, X-Upload-Session, X-File-Path, X-Chunk-Index";
        response.headers["Access-Control-Max-Age"] = "86400";
        response.headers["Connection"] = "close";
        sendResponse(socket, response);
        return;
    }

    emit requestReceived(request);

    HttpResponse response;
    handleRequest(request, response);
    sendResponse(socket, response);
}

void HttpServer::parseRequest(const QByteArray& data, HttpRequest& request)
{
    int headerEnd = data.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        headerEnd = data.indexOf("\n\n");
        if (headerEnd < 0) return;
    }

    QByteArray headerData = data.left(headerEnd);
    parseRequestHeaders(headerData, request);

    int bodyStart = headerEnd + 4;
    if (bodyStart < data.size()) {
        request.body = data.mid(bodyStart);
    }
}

void HttpServer::parseRequestHeaders(const QByteArray& headerData, HttpRequest& request)
{
    QList<QByteArray> lines = headerData.split('\n');
    if (lines.isEmpty()) return;

    parseFirstLine(lines[0], request.method, request.path, request.version);

    for (int i = 1; i < lines.size(); ++i) {
        QString line = QString::fromUtf8(lines[i]).trimmed();
        if (line.isEmpty()) break;

        int colonIndex = line.indexOf(':');
        if (colonIndex > 0) {
            QString key = line.left(colonIndex).trimmed();
            QString value = line.mid(colonIndex + 1).trimmed();
            request.headers[key] = value;
        }
    }

    // Parse query string
    QString pathAndQuery = request.path;
    int queryIndex = pathAndQuery.indexOf('?');
    if (queryIndex >= 0) {
        QString queryString = pathAndQuery.mid(queryIndex + 1);
        request.path = pathAndQuery.left(queryIndex);

        for (const QString& pair : queryString.split('&')) {
            int eqIndex = pair.indexOf('=');
            if (eqIndex >= 0) {
                QString key = QString::fromUtf8(QByteArray::fromPercentEncoding(pair.left(eqIndex).toUtf8()));
                QString value = QString::fromUtf8(QByteArray::fromPercentEncoding(pair.mid(eqIndex + 1).toUtf8()));
                request.queryParams[key] = value;
            }
        }
    }
}

void HttpServer::sendResponse(QTcpSocket* socket, const HttpResponse& response)
{
    // Handle streaming file responses
    if (response.isStreaming) {
        sendStreamingResponse(socket, response);
        return;
    }

    QByteArray statusLine = QString("HTTP/1.1 %1 %2\r\n").arg(response.statusCode).arg(response.statusMessage).toUtf8();

    QByteArray headerLines;
    for (auto it = response.headers.begin(); it != response.headers.end(); ++it) {
        headerLines.append(QString("%1: %2\r\n").arg(it.key()).arg(it.value()).toUtf8());
    }
    if (!response.headers.contains("Content-Length")) {
        headerLines.append(QString("Content-Length: %1\r\n").arg(response.body.size()).toUtf8());
    }
    headerLines.append("Access-Control-Allow-Origin: *\r\n");
    headerLines.append("\r\n");

    socket->write(statusLine);
    socket->write(headerLines);
    if (!response.body.isEmpty()) {
        socket->write(response.body);
    }
    socket->flush();
    socket->disconnectFromHost();
}

void HttpServer::sendStreamingResponse(QTcpSocket* socket, const HttpResponse& response)
{
    QFile* file = new QFile(response.filePath);
    if (!file->open(QIODevice::ReadOnly)) {
        LOG_ERROR("sendStreamingResponse: cannot open file %s", qPrintable(response.filePath));
        delete file;
        // Fall back to error response
        HttpResponse errResp = HttpResponse::internalError("无法读取文件");
        // Recursive call but not streaming
        QByteArray statusLine = QString("HTTP/1.1 %1 %2\r\n").arg(errResp.statusCode).arg(errResp.statusMessage).toUtf8();
        QByteArray headerLines;
        for (auto it = errResp.headers.begin(); it != errResp.headers.end(); ++it) {
            headerLines.append(QString("%1: %2\r\n").arg(it.key()).arg(it.value()).toUtf8());
        }
        headerLines.append(QString("Content-Length: %1\r\n").arg(errResp.body.size()).toUtf8());
        headerLines.append("Access-Control-Allow-Origin: *\r\n\r\n");
        socket->write(statusLine);
        socket->write(headerLines);
        socket->write(errResp.body);
        socket->flush();
        socket->disconnectFromHost();
        return;
    }

    // Determine file size and range
    qint64 fileSize = file->size();
    qint64 offset = response.streamOffset;
    qint64 length = response.streamLength;

    if (length < 0) {
        length = fileSize - offset;
    }

    // Seek to start position
    if (offset > 0) {
        file->seek(offset);
    }

    // Set up download state for tracking and cleanup
    StreamingDownloadState dlState;
    dlState.file = file;
    dlState.bytesSent = 0;
    dlState.totalBytes = length;
    dlState.progressCallback = nullptr;
    dlState.deleteAfterSend = response.deleteAfterSend;
    m_downloadContexts[socket] = dlState;

    // Send HTTP headers
    QByteArray statusLine = QString("HTTP/1.1 %1 %2\r\n").arg(response.statusCode).arg(response.statusMessage).toUtf8();

    QByteArray headerLines;
    for (auto it = response.headers.begin(); it != response.headers.end(); ++it) {
        headerLines.append(QString("%1: %2\r\n").arg(it.key()).arg(it.value()).toUtf8());
    }
    if (!response.headers.contains("Content-Length")) {
        headerLines.append(QString("Content-Length: %1\r\n").arg(length).toUtf8());
    }
    headerLines.append("Access-Control-Allow-Origin: *\r\n");
    headerLines.append("\r\n");

    socket->write(statusLine);
    socket->write(headerLines);
    socket->flush();

    // Stream file data in chunks
    const int chunkSize = 65536; // 64KB chunks
    char buf[chunkSize];
    qint64 totalWritten = 0;

    while (totalWritten < length) {
        qint64 toRead = qMin((qint64)chunkSize, length - totalWritten);
        qint64 bytesRead = file->read(buf, toRead);
        if (bytesRead <= 0) break;

        qint64 bytesWritten = socket->write(buf, bytesRead);
        if (bytesWritten < 0) break; // Socket error

        totalWritten += bytesWritten;
        m_downloadContexts[socket].bytesSent = totalWritten;

        // Flush periodically and wait for buffer to drain
        if (totalWritten % (chunkSize * 16) == 0 || totalWritten >= length) {
            socket->flush();
            if (totalWritten < length) {
                socket->waitForBytesWritten(5000);
            }
        }
    }

    // Cleanup
    file->close();
    delete file;
    m_downloadContexts.remove(socket);

    // Delete temp file if requested (e.g. zip)
    if (!response.deleteAfterSend.isEmpty()) {
        QFile::remove(response.deleteAfterSend);
    }

    LOG_INFO("Streaming response complete: %lld bytes sent for %s",
             totalWritten, qPrintable(response.filePath));

    socket->flush();
    socket->disconnectFromHost();
}

bool HttpServer::parseFirstLine(const QByteArray& line, QString& method, QString& path, QString& version)
{
    QList<QByteArray> parts = line.split(' ');
    if (parts.size() < 3) {
        return false;
    }

    method = QString::fromUtf8(parts[0]).trimmed();
    path = QString::fromUtf8(parts[1]).trimmed();
    version = QString::fromUtf8(parts[2]).trimmed();
    return true;
}

bool HttpServer::matchRoute(const QString& method, const QString& path, RouteHandler& handler)
{
    QString upperMethod = method.toUpper();

    for (const Route& route : m_routes) {
        if (route.method != upperMethod && route.method != "*") continue;

        if (route.pathPattern == path) {
            handler = route.handler;
            return true;
        }

        if (route.pathPattern.endsWith("*")) {
            QString prefix = route.pathPattern.left(route.pathPattern.size() - 1);
            if (path.startsWith(prefix)) {
                handler = route.handler;
                return true;
            }
        }
    }

    return false;
}

bool HttpServer::matchStreamingRoute(const QString& method, const QString& path, StreamingBodyHandler& handler)
{
    QString upperMethod = method.toUpper();

    for (const StreamingRoute& route : m_streamingRoutes) {
        if (route.method != upperMethod) continue;

        if (route.pathPattern == path) {
            handler = route.handler;
            return true;
        }

        if (route.pathPattern.endsWith("*")) {
            QString prefix = route.pathPattern.left(route.pathPattern.size() - 1);
            if (path.startsWith(prefix)) {
                handler = route.handler;
                return true;
            }
        }
    }

    return false;
}

void HttpServer::handleRequest(const HttpRequest& request, HttpResponse& response)
{
    RouteHandler handler;
    if (matchRoute(request.method, request.path, handler)) {
        handler(request, response);
        return;
    }

    if (m_defaultHandler) {
        m_defaultHandler(request, response);
        return;
    }

    response = HttpResponse::ok(
        "<html><head><meta charset='utf-8'><title>NetShare</title></head>"
        "<body style='font-family:sans-serif;text-align:center;padding:50px'>"
        "<h1>NetShare Server</h1><p>局域网文件分享服务运行中</p></body></html>",
        "text/html");
}

HttpResponse HttpResponse::ok(const QByteArray& body, const QString& contentType)
{
    HttpResponse r;
    r.statusCode = 200;
    r.statusMessage = "OK";
    r.body = body;
    r.headers["Content-Type"] = contentType;
    r.headers["Connection"] = "close";
    return r;
}

HttpResponse HttpResponse::badRequest(const QString& message)
{
    HttpResponse r;
    r.statusCode = 400;
    r.statusMessage = "Bad Request";
    r.body = message.toUtf8();
    r.headers["Content-Type"] = "text/plain";
    r.headers["Connection"] = "close";
    return r;
}

HttpResponse HttpResponse::notFound(const QString& message)
{
    HttpResponse r;
    r.statusCode = 404;
    r.statusMessage = "Not Found";
    r.body = message.toUtf8();
    r.headers["Content-Type"] = "text/plain";
    r.headers["Connection"] = "close";
    return r;
}

HttpResponse HttpResponse::internalError(const QString& message)
{
    HttpResponse r;
    r.statusCode = 500;
    r.statusMessage = "Internal Server Error";
    r.body = message.toUtf8();
    r.headers["Content-Type"] = "text/plain";
    r.headers["Connection"] = "close";
    return r;
}

HttpResponse HttpResponse::redirect(const QString& url)
{
    HttpResponse r;
    r.statusCode = 302;
    r.statusMessage = "Found";
    r.headers["Location"] = url;
    r.headers["Connection"] = "close";
    return r;
}

HttpResponse HttpResponse::fileResponse(const QByteArray& data, const QString& fileName,
                                         const QString& contentType, const QString& rangeHeader)
{
    HttpResponse r;

    if (!rangeHeader.isEmpty()) {
        qint64 fileSize = data.size();
        QRegularExpression re("bytes=(\\d+)-(\\d*)");
        QRegularExpressionMatch match = re.match(rangeHeader);

        if (match.hasMatch()) {
            qint64 start = match.captured(1).toLongLong();
            qint64 end = match.captured(2).isEmpty() ? fileSize - 1 : match.captured(2).toLongLong();

            if (start >= fileSize || end >= fileSize || start > end) {
                r.statusCode = 416;
                r.statusMessage = "Range Not Satisfiable";
                r.headers["Content-Range"] = QString("bytes */%1").arg(fileSize);
                r.headers["Connection"] = "close";
                return r;
            }

            r.statusCode = 206;
            r.statusMessage = "Partial Content";
            r.body = data.mid(start, end - start + 1);
            r.headers["Content-Range"] = QString("bytes %1-%2/%3").arg(start).arg(end).arg(fileSize);
        } else {
            r.statusCode = 200;
            r.statusMessage = "OK";
            r.body = data;
        }
    } else {
        r.statusCode = 200;
        r.statusMessage = "OK";
        r.body = data;
    }

    r.headers["Content-Type"] = contentType;
    r.headers["Content-Disposition"] = QString("attachment; filename=\"%1\"").arg(fileName);
    r.headers["Accept-Ranges"] = "bytes";
    r.headers["Connection"] = "close";
    return r;
}

HttpResponse HttpResponse::streamingFileResponse(const QString& filePath, const QString& fileName,
                                                   const QString& contentType, const QString& rangeHeader,
                                                   const QString& deleteAfter)
{
    HttpResponse r;
    r.isStreaming = true;
    r.filePath = filePath;
    r.deleteAfterSend = deleteAfter;

    qint64 fileSize = QFileInfo(filePath).size();

    if (!rangeHeader.isEmpty()) {
        QRegularExpression re("bytes=(\\d+)-(\\d*)");
        QRegularExpressionMatch match = re.match(rangeHeader);

        if (match.hasMatch()) {
            qint64 start = match.captured(1).toLongLong();
            qint64 end = match.captured(2).isEmpty() ? fileSize - 1 : match.captured(2).toLongLong();

            if (start >= fileSize || end >= fileSize || start > end) {
                r.statusCode = 416;
                r.statusMessage = "Range Not Satisfiable";
                r.headers["Content-Range"] = QString("bytes */%1").arg(fileSize);
                r.headers["Connection"] = "close";
                r.isStreaming = false; // Error response, not streaming
                return r;
            }

            r.statusCode = 206;
            r.statusMessage = "Partial Content";
            r.streamOffset = start;
            r.streamLength = end - start + 1;
            r.headers["Content-Range"] = QString("bytes %1-%2/%3").arg(start).arg(end).arg(fileSize);
        } else {
            r.statusCode = 200;
            r.statusMessage = "OK";
            r.streamOffset = 0;
            r.streamLength = fileSize;
        }
    } else {
        r.statusCode = 200;
        r.statusMessage = "OK";
        r.streamOffset = 0;
        r.streamLength = fileSize;
    }

    r.headers["Content-Type"] = contentType;
    r.headers["Content-Disposition"] = QString("attachment; filename=\"%1\"").arg(fileName);
    r.headers["Accept-Ranges"] = "bytes";
    r.headers["Connection"] = "close";
    return r;
}

#include "HttpServer.moc"
