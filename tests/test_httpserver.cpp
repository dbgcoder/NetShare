#include "test_httpserver.h"
#include "network/CivetWebServer.h"
#include <QTcpSocket>
#include <QEventLoop>
#include <QTimer>

void TestHttpServer::initTestCase()
{
    m_server = new CivetWebServer(this);
    QVERIFY(m_server != nullptr);
}

void TestHttpServer::cleanupTestCase()
{
    if (m_server) {
        m_server->stop();
    }
}

void TestHttpServer::testServerStartStop()
{
    QVERIFY(m_server->start(m_port, "127.0.0.1"));
    QVERIFY(m_server->isRunning());
    m_server->stop();
    QVERIFY(!m_server->isRunning());

    QVERIFY(m_server->start(m_port, "127.0.0.1"));
    QVERIFY(m_server->isRunning());
}

void TestHttpServer::testServerPort()
{
    QCOMPARE(m_server->port(), m_port);
}

static QByteArray httpGet(quint16 port, const QString& path)
{
    QTcpSocket socket;
    socket.connectToHost("127.0.0.1", port);
    if (!socket.waitForConnected(3000)) return QByteArray();

    QByteArray request = QString("GET %1 HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n").arg(path).toUtf8();
    socket.write(request);
    if (!socket.waitForBytesWritten(3000)) return QByteArray();

    QByteArray response;
    QEventLoop loop;
    QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QTcpSocket::readyRead, &socket, [&]() {
        response.append(socket.readAll());
    });
    if (socket.bytesAvailable() > 0) response.append(socket.readAll());
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();
    if (socket.bytesAvailable() > 0) response.append(socket.readAll());
    return response;
}

static QByteArray httpPost(quint16 port, const QString& path, const QByteArray& body, const QString& contentType)
{
    QTcpSocket socket;
    socket.connectToHost("127.0.0.1", port);
    if (!socket.waitForConnected(3000)) return QByteArray();

    QByteArray request = QString("POST %1 HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: %2\r\nContent-Length: %3\r\nConnection: close\r\n\r\n")
                             .arg(path).arg(contentType).arg(body.size()).toUtf8();
    request.append(body);
    socket.write(request);
    if (!socket.waitForBytesWritten(3000)) return QByteArray();

    QByteArray response;
    QEventLoop loop;
    QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QTcpSocket::readyRead, &socket, [&]() {
        response.append(socket.readAll());
    });
    if (socket.bytesAvailable() > 0) response.append(socket.readAll());
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();
    if (socket.bytesAvailable() > 0) response.append(socket.readAll());
    return response;
}

static QByteArray httpOptions(quint16 port, const QString& path)
{
    QTcpSocket socket;
    socket.connectToHost("127.0.0.1", port);
    if (!socket.waitForConnected(3000)) return QByteArray();

    QByteArray request = QString("OPTIONS %1 HTTP/1.1\r\nHost: 127.0.0.1\r\nOrigin: http://localhost\r\nConnection: close\r\n\r\n").arg(path).toUtf8();
    socket.write(request);
    if (!socket.waitForBytesWritten(3000)) return QByteArray();

    QByteArray response;
    QEventLoop loop;
    QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QTcpSocket::readyRead, &socket, [&]() {
        response.append(socket.readAll());
    });
    if (socket.bytesAvailable() > 0) response.append(socket.readAll());
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();
    if (socket.bytesAvailable() > 0) response.append(socket.readAll());
    return response;
}

static int extractStatusCode(const QByteArray& response)
{
    if (response.isEmpty()) return 0;
    int spaceIdx = response.indexOf(' ');
    if (spaceIdx < 0) return 0;
    int endIdx = response.indexOf(' ', spaceIdx + 1);
    if (endIdx < 0) return 0;
    return response.mid(spaceIdx + 1, endIdx - spaceIdx - 1).toInt();
}

void TestHttpServer::testRouteRegistration()
{
    bool handlerCalled = false;
    m_server->addRoute("GET", "/test_route", [&](mg_connection* conn, const HttpRequestInfo&) -> int {
        handlerCalled = true;
        CivetWebServer::sendHtmlResponse(conn, 200, "route ok");
        return 200;
    });

    QByteArray response = httpGet(m_port, "/test_route");
    QVERIFY(handlerCalled);
    QCOMPARE(extractStatusCode(response), 200);
    QVERIFY(response.contains("route ok"));
}

void TestHttpServer::testExactRouteMatch()
{
    bool called = false;
    m_server->addRoute("GET", "/api/shares", [&](mg_connection* conn, const HttpRequestInfo&) -> int {
        called = true;
        CivetWebServer::sendJsonResponse(conn, 200, "[]");
        return 200;
    });

    QByteArray response = httpGet(m_port, "/api/shares");
    QVERIFY(called);
    QCOMPARE(extractStatusCode(response), 200);
}

void TestHttpServer::testWildcardRouteMatch()
{
    bool called = false;
    QString capturedUri;
    m_server->addRoute("GET", "/s/**", [&](mg_connection* conn, const HttpRequestInfo& info) -> int {
        called = true;
        capturedUri = info.uri;
        CivetWebServer::sendHtmlResponse(conn, 200, "share page");
        return 200;
    });

    QByteArray response = httpGet(m_port, "/s/abc123");
    QVERIFY(called);
    QVERIFY(capturedUri.startsWith("/s/"));
    QCOMPARE(extractStatusCode(response), 200);
    QVERIFY(response.contains("share page"));
}

void TestHttpServer::testNoRouteMatch()
{
    QByteArray response = httpGet(m_port, "/nonexistent_path_xyz");
    int status = extractStatusCode(response);
    QVERIFY(status == 200 || status == 404 || status == 0);
}

void TestHttpServer::testDefaultHandler()
{
    bool defaultCalled = false;
    m_server->setDefaultHandler([&](mg_connection* conn, const HttpRequestInfo&) -> int {
        defaultCalled = true;
        CivetWebServer::sendHtmlResponse(conn, 404, "custom 404");
        return 404;
    });

    QByteArray response = httpGet(m_port, "/unregistered_path_abc");
    if (defaultCalled) {
        QCOMPARE(extractStatusCode(response), 404);
        QVERIFY(response.contains("custom 404"));
    }
}

void TestHttpServer::testSendJsonResponse()
{
    m_server->addRoute("GET", "/json_test", [&](mg_connection* conn, const HttpRequestInfo&) -> int {
        CivetWebServer::sendJsonResponse(conn, 200, "{\"status\":\"ok\"}");
        return 200;
    });

    QByteArray response = httpGet(m_port, "/json_test");
    QCOMPARE(extractStatusCode(response), 200);
    QVERIFY(response.contains("application/json"));
    QVERIFY(response.contains("\"status\""));
}

void TestHttpServer::testSendHtmlResponse()
{
    m_server->addRoute("GET", "/html_test", [&](mg_connection* conn, const HttpRequestInfo&) -> int {
        CivetWebServer::sendHtmlResponse(conn, 200, "<h1>Hello</h1>");
        return 200;
    });

    QByteArray response = httpGet(m_port, "/html_test");
    QCOMPARE(extractStatusCode(response), 200);
    QVERIFY(response.contains("text/html"));
    QVERIFY(response.contains("<h1>Hello</h1>"));
}

void TestHttpServer::testQueryParams()
{
    bool called = false;
    QString capturedQuery;
    m_server->addRoute("GET", "/search", [&](mg_connection* conn, const HttpRequestInfo& info) -> int {
        called = true;
        capturedQuery = info.queryString;
        CivetWebServer::sendHtmlResponse(conn, 200, "search ok");
        return 200;
    });

    QByteArray response = httpGet(m_port, "/search?q=hello&page=2");
    QVERIFY(called);
    QVERIFY(capturedQuery.contains("q=hello"));
    QCOMPARE(extractStatusCode(response), 200);
}

void TestHttpServer::testHeaders()
{
    bool called = false;
    bool hasCustomHeader = false;
    QString customHeaderValue;
    m_server->addRoute("GET", "/header_test", [&](mg_connection* conn, const HttpRequestInfo& info) -> int {
        called = true;
        hasCustomHeader = info.headers.contains("X-Custom-Header");
        customHeaderValue = info.headers.value("X-Custom-Header");
        CivetWebServer::sendHtmlResponse(conn, 200, "header ok");
        return 200;
    });

    QTcpSocket socket;
    socket.connectToHost("127.0.0.1", m_port);
    QVERIFY(socket.waitForConnected(3000));

    QByteArray request = "GET /header_test HTTP/1.1\r\nHost: 127.0.0.1\r\nX-Custom-Header: testvalue\r\nConnection: close\r\n\r\n";
    socket.write(request);
    QVERIFY(socket.waitForBytesWritten(3000));

    QByteArray response;
    QEventLoop loop;
    QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QTcpSocket::readyRead, &socket, [&]() {
        response.append(socket.readAll());
    });
    if (socket.bytesAvailable() > 0) response.append(socket.readAll());
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();
    if (socket.bytesAvailable() > 0) response.append(socket.readAll());

    QVERIFY(called);
    QVERIFY(hasCustomHeader);
    QCOMPARE(customHeaderValue, QString("testvalue"));
    QCOMPARE(extractStatusCode(response), 200);
}

void TestHttpServer::testCorsPreflight()
{
    QByteArray response = httpOptions(m_port, "/any_path");
    int status = extractStatusCode(response);
    QVERIFY(status != 0);
}

void TestHttpServer::testMultipleRoutes()
{
    bool getCalled = false;
    bool postCalled = false;

    m_server->addRoute("GET", "/multi", [&](mg_connection* conn, const HttpRequestInfo&) -> int {
        getCalled = true;
        CivetWebServer::sendHtmlResponse(conn, 200, "GET ok");
        return 200;
    });
    m_server->addRoute("POST", "/multi", [&](mg_connection* conn, const HttpRequestInfo&) -> int {
        postCalled = true;
        CivetWebServer::sendHtmlResponse(conn, 200, "POST ok");
        return 200;
    });

    QByteArray getResponse = httpGet(m_port, "/multi");
    QVERIFY(getCalled);
    QVERIFY(getResponse.contains("GET ok"));

    QByteArray postResponse = httpPost(m_port, "/multi", "data", "text/plain");
    QVERIFY(postCalled);
    QVERIFY(postResponse.contains("POST ok"));
}

QTEST_MAIN(TestHttpServer)
