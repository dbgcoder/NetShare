#include "CivetWebServer.h"
#include "Logger.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>

CivetWebServer::CivetWebServer(QObject* parent)
    : QObject(parent)
{
}

CivetWebServer::~CivetWebServer()
{
    stop();
}

bool CivetWebServer::start(quint16 port, const QString& bindAddress)
{
    if (m_running) {
        LOG_WARN("CivetWebServer already running on port %d", m_port);
        return true;
    }

    m_port = port;
    m_bindAddress = bindAddress;

    QString portSpec = QStringLiteral("%1").arg(port);
    if (m_tlsEnabled && !m_certPath.isEmpty() && !m_keyPath.isEmpty()) {
        portSpec = QStringLiteral("%1s").arg(port);
    }

    QStringList optionList = {
        QStringLiteral("listening_ports"), portSpec,
        QStringLiteral("num_threads"), QStringLiteral("10"),
        QStringLiteral("enable_keep_alive"), QStringLiteral("yes"),
        QStringLiteral("request_timeout_ms"), QStringLiteral("30000"),
        QStringLiteral("decode_url"), QStringLiteral("yes"),
        QStringLiteral("max_request_size"), QStringLiteral("16384"),
        QStringLiteral("access_control_allow_origin"), QStringLiteral("*"),
        QStringLiteral("document_root"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/web"),
    };

    if (m_tlsEnabled && !m_certPath.isEmpty() && !m_keyPath.isEmpty()) {
        optionList << QStringLiteral("ssl_certificate") << m_certPath
                   << QStringLiteral("ssl_private_key") << m_keyPath;
    }

    QVector<QByteArray> optionData;
    optionData.reserve(optionList.size());
    for (const auto& opt : optionList) {
        optionData.append(opt.toUtf8());
    }

    QVector<char*> options;
    options.reserve(optionData.size() + 1);
    for (auto& ba : optionData) {
        options.append(ba.data());
    }
    options.append(nullptr);

    mg_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.log_message = &CivetWebServer::staticLogMessage;

    m_ctx = mg_start(&callbacks, this, const_cast<const char**>(options.data()));
    if (!m_ctx) {
        LOG_ERROR("CivetWebServer failed to start on %s:%d",
                  qPrintable(bindAddress), port);
        emit errorOccurred(QStringLiteral("Failed to start server on port %1").arg(port));
        return false;
    }

    for (const auto& route : m_routes) {
        mg_set_request_handler(m_ctx, route.uri.toUtf8().constData(),
                               &CivetWebServer::staticBeginRequestHandler, this);
    }
    for (const auto& route : m_streamingRoutes) {
        mg_set_request_handler(m_ctx, route.uri.toUtf8().constData(),
                               &CivetWebServer::staticBeginRequestHandler, this);
    }
    if (m_wsConnectHandler) {
        mg_set_websocket_handler(m_ctx, m_wsPath.toUtf8().constData(),
                                 &CivetWebServer::staticWsConnectHandler,
                                 &CivetWebServer::staticWsReadyHandler,
                                 &CivetWebServer::staticWsDataHandler,
                                 &CivetWebServer::staticWsCloseHandler,
                                 this);
    }

    m_running = true;
    LOG_INFO("CivetWebServer started on %s:%d (TLS: %s)",
             qPrintable(bindAddress), port,
             m_tlsEnabled ? "enabled" : "disabled");
    emit serverStarted(port);
    return true;
}

void CivetWebServer::stop()
{
    if (!m_running || !m_ctx) {
        return;
    }

    mg_stop(m_ctx);
    m_ctx = nullptr;
    m_running = false;

    m_wsClients.clear();
    m_wsSubscriptions.clear();
    m_wsLastPong.clear();

    if (m_heartbeatTimer) {
        m_heartbeatTimer->stop();
        delete m_heartbeatTimer;
        m_heartbeatTimer = nullptr;
    }

    LOG_INFO("CivetWebServer stopped");
    emit serverStopped();
}

bool CivetWebServer::isRunning() const
{
    return m_running;
}

quint16 CivetWebServer::port() const
{
    return m_port;
}

void CivetWebServer::addRoute(const QString& method, const QString& uri, RouteHandler handler)
{
    Route route;
    route.method = method;
    route.uri = uri;
    route.handler = std::move(handler);
    m_routes.append(route);

    if (m_ctx) {
        mg_set_request_handler(m_ctx, uri.toUtf8().constData(),
                               &CivetWebServer::staticBeginRequestHandler, this);
    }
}

void CivetWebServer::addStreamingRoute(const QString& method, const QString& uri, StreamingHandler handler)
{
    StreamingRoute route;
    route.method = method;
    route.uri = uri;
    route.handler = std::move(handler);
    m_streamingRoutes.append(route);

    if (m_ctx) {
        mg_set_request_handler(m_ctx, uri.toUtf8().constData(),
                               &CivetWebServer::staticBeginRequestHandler, this);
    }
}

void CivetWebServer::setDefaultHandler(RouteHandler handler)
{
    m_defaultHandler = std::move(handler);
}

void CivetWebServer::enableWebSocket(const QString& path,
                                      WsConnectHandler onConnect,
                                      WsReadyHandler   onReady,
                                      WsDataHandler    onData,
                                      WsCloseHandler   onClose)
{
    m_wsConnectHandler = std::move(onConnect);
    m_wsReadyHandler   = std::move(onReady);
    m_wsDataHandler    = std::move(onData);
    m_wsCloseHandler   = std::move(onClose);
    m_wsPath           = path;

    if (m_ctx) {
        mg_set_websocket_handler(m_ctx, path.toUtf8().constData(),
                                 &CivetWebServer::staticWsConnectHandler,
                                 &CivetWebServer::staticWsReadyHandler,
                                 &CivetWebServer::staticWsDataHandler,
                                 &CivetWebServer::staticWsCloseHandler,
                                 this);
    }

    setupHeartbeat();
}

void CivetWebServer::broadcastToSubscribers(const QString& token, const QString& type,
                                             const QJsonObject& data)
{
    QJsonObject msg;
    msg[QStringLiteral("type")] = type;
    msg[QStringLiteral("data")] = data;

    QByteArray payload = QJsonDocument(msg).toJson(QJsonDocument::Compact);

    auto it = m_wsClients.find(token);
    if (it == m_wsClients.end()) {
        LOG_WARN("broadcastToSubscribers: no clients for token=%s", qPrintable(token));
        return;
    }

    LOG_INFO("broadcastToSubscribers: token=%s type=%s client_count=%d payload_size=%d",
             qPrintable(token), qPrintable(type),
             static_cast<int>(it.value().size()), payload.size());

    QList<mg_connection*> deadConns;
    for (mg_connection* client : it.value()) {
        int ret = mg_websocket_write(client, MG_WEBSOCKET_OPCODE_TEXT,
                           payload.constData(), payload.size());
        LOG_INFO("broadcastToSubscribers: write to conn=%p returned %d",
                 static_cast<void*>(client), ret);
        if (ret <= 0) {
            deadConns.append(client);
        }
    }
    for (auto* conn : deadConns) {
        unsubscribeClientFromAll(conn);
        m_connToIp.remove(conn);
    }
}

bool CivetWebServer::sendToIp(const QString& ip, const QString& type, const QJsonObject& data)
{
    QJsonObject msg;
    msg[QStringLiteral("type")] = type;
    msg[QStringLiteral("data")] = data;

    QByteArray payload = QJsonDocument(msg).toJson(QJsonDocument::Compact);

    LOG_INFO("sendToIp target=%s type=%s payload_size=%d m_connToIp size=%d",
             qPrintable(ip), qPrintable(type), payload.size(), static_cast<int>(m_connToIp.size()));

    bool sent = false;
    QList<mg_connection*> deadConns;
    for (auto it = m_connToIp.constBegin(); it != m_connToIp.constEnd(); ++it) {
        if (it.value() == ip) {
            int ret = mg_websocket_write(it.key(), MG_WEBSOCKET_OPCODE_TEXT,
                               payload.constData(), payload.size());
            LOG_INFO("sendToIp mg_websocket_write conn=%p returned %d",
                     static_cast<void*>(it.key()), ret);
            if (ret > 0) {
                sent = true;
            } else {
                LOG_WARN("sendToIp: connection %p appears dead (write returned %d), will cleanup",
                         static_cast<void*>(it.key()), ret);
                deadConns.append(it.key());
            }
        }
    }

    for (auto* conn : deadConns) {
        unsubscribeClientFromAll(conn);
        m_connToIp.remove(conn);
    }

    if (!sent && deadConns.isEmpty()) {
        LOG_WARN("sendToIp: no connection found for IP %s", qPrintable(ip));
    }

    return sent;
}

void CivetWebServer::subscribeClient(mg_connection* conn, const QString& token)
{
    m_wsClients[token].insert(conn);
    m_wsSubscriptions[conn].insert(token);
    LOG_INFO("subscribeClient: conn=%p token=%s total_clients_for_token=%d",
             static_cast<void*>(conn), qPrintable(token),
             static_cast<int>(m_wsClients.value(token).size()));
}

void CivetWebServer::unsubscribeClient(mg_connection* conn, const QString& token)
{
    m_wsClients[token].remove(conn);
    if (m_wsClients[token].isEmpty()) {
        m_wsClients.remove(token);
    }
    m_wsSubscriptions[conn].remove(token);
}

void CivetWebServer::unsubscribeClientFromAll(mg_connection* conn)
{
    auto it = m_wsSubscriptions.find(conn);
    if (it == m_wsSubscriptions.end()) {
        return;
    }

    LOG_INFO("unsubscribeClientFromAll: conn=%p tokens=%d",
             static_cast<void*>(conn), static_cast<int>(it.value().size()));

    for (const QString& token : it.value()) {
        m_wsClients[token].remove(conn);
        if (m_wsClients[token].isEmpty()) {
            m_wsClients.remove(token);
        }
    }
    m_wsSubscriptions.erase(it);
    m_wsLastPong.remove(conn);
    m_connToIp.remove(conn);
}

int CivetWebServer::connectedClientCount() const
{
    return m_wsSubscriptions.size();
}

QVariantList CivetWebServer::getConnectedClientsList() const
{
    QVariantList result;
    QSet<QString> seenIps;
    for (auto it = m_connToIp.constBegin(); it != m_connToIp.constEnd(); ++it) {
        QString ip = it.value();
        if (ip.isEmpty() || seenIps.contains(ip)) continue;
        seenIps.insert(ip);
        QVariantMap entry;
        entry[QStringLiteral("address")] = ip;
        entry[QStringLiteral("port")] = m_port;
        result.append(entry);
    }
    return result;
}

void CivetWebServer::setSslCertificate(const QString& certPath, const QString& keyPath)
{
    m_certPath = certPath;
    m_keyPath = keyPath;
}

void CivetWebServer::setTlsEnabled(bool enabled)
{
    m_tlsEnabled = enabled;
}

void CivetWebServer::sendJsonResponse(mg_connection* conn, int status, const QByteArray& json)
{
    mg_printf(conn,
              "HTTP/1.1 %d %s\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: %d\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "\r\n",
              status, mg_get_response_code_text(conn, status),
              json.size());
    mg_write(conn, json.constData(), json.size());
}

void CivetWebServer::sendHtmlResponse(mg_connection* conn, int status, const QByteArray& html)
{
    mg_printf(conn,
              "HTTP/1.1 %d %s\r\n"
              "Content-Type: text/html; charset=utf-8\r\n"
              "Content-Length: %d\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "\r\n",
              status, mg_get_response_code_text(conn, status),
              html.size());
    mg_write(conn, html.constData(), html.size());
}

void CivetWebServer::sendFileResponse(mg_connection* conn, const QString& filePath,
                                       const QString& mimeType, const QString& fileName)
{
    Q_UNUSED(mimeType)
    Q_UNUSED(fileName)
    mg_send_file(conn, filePath.toUtf8().constData());
}

qint64 CivetWebServer::sendStreamingFileResponse(mg_connection* conn, const QString& filePath,
                                                const QString& mimeType, const QString& fileName,
                                                const QString& rangeHeader,
                                                std::function<void(qint64 totalSent, qint64 fileSize)> progressCallback)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        sendHtmlResponse(conn, 404, QByteArrayLiteral("File not found"));
        return -1;
    }

    qint64 fileSize = file.size();
    qint64 startByte = 0;
    qint64 endByte = fileSize - 1;
    bool hasRange = false;

    if (!rangeHeader.isEmpty()) {
        QRegularExpression rangeRe("bytes=(\\d+)-(\\d*)");
        auto match = rangeRe.match(rangeHeader);
        if (match.hasMatch()) {
            hasRange = true;
            startByte = match.captured(1).toLongLong();
            if (!match.captured(2).isEmpty())
                endByte = match.captured(2).toLongLong();
            if (startByte >= fileSize) {
                mg_printf(conn,
                    "HTTP/1.1 416 Range Not Satisfiable\r\n"
                    "Content-Range: bytes */%lld\r\n"
                    "Content-Length: 0\r\n\r\n", fileSize);
                return -1;
            }
            if (endByte >= fileSize) endByte = fileSize - 1;
        }
    }

    qint64 contentLength = endByte - startByte + 1;
    QByteArray contentType = mimeType.toUtf8();
    if (contentType.isEmpty()) contentType = "application/octet-stream";
    QByteArray utfFileName = fileName.toUtf8();

    if (hasRange) {
        mg_printf(conn,
            "HTTP/1.1 206 Partial Content\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %lld\r\n"
            "Content-Range: bytes %lld-%lld/%lld\r\n"
            "Content-Disposition: attachment; filename=\"%s\"\r\n"
            "Accept-Ranges: bytes\r\n"
            "Cache-Control: no-cache\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Expose-Headers: Content-Length, Accept-Ranges, Content-Range\r\n"
            "\r\n",
            contentType.constData(), contentLength,
            startByte, endByte, fileSize,
            utfFileName.constData());
    } else {
        mg_printf(conn,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %lld\r\n"
            "Content-Disposition: attachment; filename=\"%s\"\r\n"
            "Accept-Ranges: bytes\r\n"
            "Cache-Control: no-cache\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Expose-Headers: Content-Length, Accept-Ranges, Content-Range\r\n"
            "\r\n",
            contentType.constData(), contentLength,
            utfFileName.constData());
    }

    file.seek(startByte);
    char buf[65536];
    qint64 remaining = contentLength;
    qint64 totalSent = 0;
    while (remaining > 0) {
        int toRead = static_cast<int>(qMin(remaining, static_cast<qint64>(sizeof(buf))));
        int bytesRead = static_cast<int>(file.read(buf, toRead));
        if (bytesRead <= 0) break;
        int written = mg_write(conn, buf, bytesRead);
        if (written <= 0) return -1;
        totalSent += written;
        remaining -= written;
        if (progressCallback) {
            progressCallback(startByte + totalSent, fileSize);
        }
    }
    return totalSent;
}

HttpRequestInfo CivetWebServer::fromCivetWeb(mg_connection* conn, const mg_request_info* ri)
{
    HttpRequestInfo info;
    info.method = QString::fromUtf8(ri->request_method);
    info.uri = QString::fromUtf8(ri->request_uri);
    info.queryString = QString::fromUtf8(ri->query_string ? ri->query_string : "");
    info.remoteAddress = QString::fromUtf8(ri->remote_addr);

    for (int i = 0; i < ri->num_headers; ++i) {
        info.headers[QString::fromUtf8(ri->http_headers[i].name)] =
            QString::fromUtf8(ri->http_headers[i].value);
    }

    if (ri->content_length > 0) {
        info.body.resize(static_cast<int>(ri->content_length));
        mg_read(conn, info.body.data(), info.body.size());
    }

    return info;
}

void CivetWebServer::setupHeartbeat()
{
    if (m_heartbeatTimer) {
        return;
    }
    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &CivetWebServer::sendHeartbeat);
    m_heartbeatTimer->start(30000);
}

void CivetWebServer::sendHeartbeat()
{
    qint64 now = QDateTime::currentSecsSinceEpoch();

    QList<mg_connection*> timedOut;
    for (auto it = m_wsLastPong.begin(); it != m_wsLastPong.end(); ++it) {
        if (now - it.value() > 60) {
            timedOut.append(it.key());
        }
    }

    for (mg_connection* conn : timedOut) {
        LOG_WARN("Heartbeat: closing timed-out connection %p (no pong for 60s)",
                 static_cast<void*>(conn));
        unsubscribeClientFromAll(conn);
        mg_close_connection(conn);
    }

    for (auto it = m_wsClients.begin(); it != m_wsClients.end(); ++it) {
        for (mg_connection* client : it.value()) {
            mg_websocket_write(client, MG_WEBSOCKET_OPCODE_PING, "", 0);
        }
    }
}

void CivetWebServer::checkHeartbeatTimeout()
{
}

int CivetWebServer::staticBeginRequestHandler(mg_connection* conn, void* cbdata)
{
    auto* self = static_cast<CivetWebServer*>(cbdata);
    if (!self) {
        return 0;
    }
    return self->beginRequestHandler(conn);
}

int CivetWebServer::beginRequestHandler(mg_connection* conn)
{
    const mg_request_info* ri = mg_get_request_info(conn);
    if (!ri) {
        return 0;
    }

    QString method = QString::fromUtf8(ri->request_method);
    QString uri = QString::fromUtf8(ri->request_uri);

    if (method == QStringLiteral("OPTIONS")) {
        mg_printf(conn,
                  "HTTP/1.1 204 No Content\r\n"
                  "Access-Control-Allow-Origin: *\r\n"
                  "Access-Control-Allow-Methods: GET, HEAD, POST, OPTIONS\r\n"
                  "Access-Control-Allow-Headers: Content-Type, Range\r\n"
                  "Access-Control-Max-Age: 86400\r\n"
                  "\r\n");
        return 1;
    }

    auto matchRoute = [](const QString& requestUri, const QString& routeUri) -> bool {
        if (routeUri.endsWith(QLatin1Char('*'))) {
            QString prefix = routeUri.left(routeUri.size() - 1);
            return requestUri.startsWith(prefix);
        }
        if (requestUri == routeUri) return true;
        if (requestUri.startsWith(routeUri + QLatin1Char('?'))) return true;
        if (requestUri.startsWith(routeUri + QLatin1Char('/'))) return true;
        return false;
    };

    for (const auto& route : m_routes) {
        if (route.method == method && matchRoute(uri, route.uri)) {
            HttpRequestInfo info = fromCivetWeb(conn, ri);
            return route.handler(conn, info);
        }
    }

    for (const auto& route : m_streamingRoutes) {
        if (route.method == method && matchRoute(uri, route.uri)) {
            HttpRequestInfo info;
            info.method = method;
            info.uri = uri;
            info.queryString = QString::fromUtf8(ri->query_string ? ri->query_string : "");
            info.remoteAddress = QString::fromUtf8(ri->remote_addr);
            for (int i = 0; i < ri->num_headers; ++i) {
                info.headers[QString::fromUtf8(ri->http_headers[i].name)] =
                    QString::fromUtf8(ri->http_headers[i].value);
            }
            QByteArray buf;
            buf.resize(65536);
            while (true) {
                int bytesRead = mg_read(conn, buf.data(), buf.size());
                if (bytesRead <= 0) {
                    route.handler(conn, info, QByteArray(), true);
                    return 1;
                }
                QByteArray chunk = buf.left(bytesRead);
                int ret = route.handler(conn, info, chunk, false);
                if (ret != 0) return 1;
            }
        }
    }

    if (m_defaultHandler) {
        HttpRequestInfo info = fromCivetWeb(conn, ri);
        return m_defaultHandler(conn, info);
    }

    return 0;
}

int CivetWebServer::staticLogMessage(const mg_connection* conn, const char* message)
{
    Q_UNUSED(conn)
    if (message) {
        LOG_INFO("CivetWeb: %s", message);
    }
    return 0;
}

int CivetWebServer::staticWsConnectHandler(const mg_connection* conn, void* cbdata)
{
    auto* self = static_cast<CivetWebServer*>(cbdata);
    if (!self || !self->m_wsConnectHandler) {
        return 0;
    }
    return self->m_wsConnectHandler(conn);
}

void CivetWebServer::staticWsReadyHandler(mg_connection* conn, void* cbdata)
{
    auto* self = static_cast<CivetWebServer*>(cbdata);
    if (!self) {
        return;
    }

    const mg_request_info* ri = mg_get_request_info(conn);
    if (ri && ri->remote_addr) {
        QString remoteAddr = QString::fromUtf8(ri->remote_addr);
        QHostAddress addr(remoteAddr);
        QString pureIp = addr.isNull() ? remoteAddr : addr.toString();
        self->m_connToIp[conn] = pureIp;
        LOG_INFO("WebSocket ready: stored IP mapping %s (raw=%s)",
                 qPrintable(pureIp), qPrintable(remoteAddr));
    } else {
        LOG_WARN("WebSocket ready: ri=%p ri->remote_addr=%p",
                 static_cast<const void*>(ri), ri ? static_cast<const void*>(ri->remote_addr) : nullptr);
    }

    self->m_wsLastPong[conn] = QDateTime::currentSecsSinceEpoch();

    if (self->m_wsReadyHandler) {
        self->m_wsReadyHandler(conn);
    }
}

int CivetWebServer::staticWsDataHandler(mg_connection* conn, int op, char* data, size_t len, void* cbdata)
{
    auto* self = static_cast<CivetWebServer*>(cbdata);
    if (!self) {
        return 0;
    }

    int pureOp = op & 0x0F;

    if (pureOp == MG_WEBSOCKET_OPCODE_PONG) {
        self->m_wsLastPong[conn] = QDateTime::currentSecsSinceEpoch();
        return 1;
    }

    if (pureOp == MG_WEBSOCKET_OPCODE_TEXT && data && len > 0) {
        LOG_INFO("staticWsDataHandler: conn=%p op=%d pureOp=TEXT len=%zu data=%.100s",
                 static_cast<void*>(conn), op, len,
                 QByteArray(data, static_cast<int>(qMin(len, static_cast<size_t>(100)))).constData());
    } else if (pureOp != MG_WEBSOCKET_OPCODE_PONG) {
        LOG_INFO("staticWsDataHandler: conn=%p op=%d pureOp=%d len=%zu",
                 static_cast<void*>(conn), op, pureOp, len);
    }

    if (self->m_wsDataHandler) {
        return self->m_wsDataHandler(conn, pureOp, data, len);
    } else {
        LOG_WARN("staticWsDataHandler: m_wsDataHandler is NULL!");
    }
    return 1;
}

void CivetWebServer::staticWsCloseHandler(const mg_connection* conn, void* cbdata)
{
    auto* self = static_cast<CivetWebServer*>(cbdata);
    if (!self) {
        return;
    }

    auto* mutableConn = const_cast<mg_connection*>(conn);
    self->unsubscribeClientFromAll(mutableConn);

    if (self->m_wsCloseHandler) {
        self->m_wsCloseHandler(conn);
    }
}
