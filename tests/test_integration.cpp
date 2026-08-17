#include "test_integration.h"
#include "network/CivetWebServer.h"
#include "network/RequestHandler.h"
#include "core/share/ShareManager.h"
#include "core/common/Logger.h"
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>

TestIntegration::TestIntegration() = default;
TestIntegration::~TestIntegration() = default;

void TestIntegration::initTestCase()
{
    Logger::initialize(QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/netshare_test_int/logs");

    m_shareManager = &ShareManager::instance();
    m_port = 18080;

    m_server = new CivetWebServer(this);
    m_requestHandler = new RequestHandler(m_shareManager, nullptr, nullptr, this);
    m_requestHandler->setUploadDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/netshare_test_int/uploads");
    m_requestHandler->registerRoutes(m_server);

    QVERIFY(m_server->start(m_port, "127.0.0.1"));
}

void TestIntegration::cleanupTestCase()
{
    if (m_server) {
        m_server->stop();
    }
    Logger::shutdown();
}

QByteArray TestIntegration::httpGet(const QString& path)
{
    QTcpSocket socket;
    socket.connectToHost("127.0.0.1", m_port);
    if (!socket.waitForConnected(5000)) return QByteArray();

    QByteArray request = QString("GET %1 HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n").arg(path).toUtf8();
    socket.write(request);
    if (!socket.waitForBytesWritten(5000)) return QByteArray();

    QByteArray response;
    QEventLoop loop;
    QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QTcpSocket::readyRead, &socket, [&]() {
        response.append(socket.readAll());
    });
    if (socket.bytesAvailable() > 0) response.append(socket.readAll());
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    if (socket.bytesAvailable() > 0) response.append(socket.readAll());
    return response;
}

QByteArray TestIntegration::httpPost(const QString& path, const QByteArray& body, const QString& contentType)
{
    QTcpSocket socket;
    socket.connectToHost("127.0.0.1", m_port);
    if (!socket.waitForConnected(5000)) return QByteArray();

    QByteArray request = QString("POST %1 HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: %2\r\nContent-Length: %3\r\nConnection: close\r\n\r\n")
                             .arg(path).arg(contentType).arg(body.size()).toUtf8();
    request.append(body);
    socket.write(request);
    if (!socket.waitForBytesWritten(5000)) return QByteArray();

    QByteArray response;
    QEventLoop loop;
    QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QTcpSocket::readyRead, &socket, [&]() {
        response.append(socket.readAll());
    });
    if (socket.bytesAvailable() > 0) response.append(socket.readAll());
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    if (socket.bytesAvailable() > 0) response.append(socket.readAll());
    return response;
}

int TestIntegration::extractStatusCode(const QByteArray& response)
{
    if (response.isEmpty()) return 0;
    int spaceIdx = response.indexOf(' ');
    if (spaceIdx < 0) return 0;
    int endIdx = response.indexOf(' ', spaceIdx + 1);
    if (endIdx < 0) return 0;
    return response.mid(spaceIdx + 1, endIdx - spaceIdx - 1).toInt();
}

void TestIntegration::testHttpServerStartStop()
{
    QVERIFY(m_server->isRunning());
    QCOMPARE(m_server->port(), m_port);
}

void TestIntegration::testHttpGetRoot()
{
    QByteArray response = httpGet("/");
    QVERIFY(!response.isEmpty());
    int status = extractStatusCode(response);
    QCOMPARE(status, 200);
    QVERIFY(response.contains("NetShare"));
}

void TestIntegration::testHttpGetSharePage()
{
    QString token = m_shareManager->createShare("C:/integration_test.txt", false, 24, 0, "");
    QVERIFY(!token.isEmpty());

    QByteArray response = httpGet("/s/" + token.toUtf8());
    int status = extractStatusCode(response);
    QCOMPARE(status, 200);
    QVERIFY(response.contains("integration_test"));

    m_shareManager->cancelShare(token);
}

void TestIntegration::testHttpGetNotFound()
{
    QByteArray response = httpGet("/s/nonexistent_token");
    int status = extractStatusCode(response);
    QVERIFY(status == 200 || status == 404);
}

void TestIntegration::testHttpPostUpload()
{
    QString token = m_shareManager->createShare("C:/upload_test.txt", false, 24, 0, "");
    QVERIFY(!token.isEmpty());

    QString boundary = "----TestBoundary12345";
    QByteArray body;
    body.append("--" + boundary.toUtf8() + "\r\n");
    body.append("Content-Disposition: form-data; name=\"files\"; filename=\"test_upload.txt\"\r\n");
    body.append("Content-Type: text/plain\r\n\r\n");
    body.append("Hello, integration test!\r\n");
    body.append("--" + boundary.toUtf8() + "--\r\n");

    QByteArray response = httpPost("/upload/" + token.toUtf8(), body,
                                   "multipart/form-data; boundary=" + boundary);
    int status = extractStatusCode(response);
    QCOMPARE(status, 200);

    m_shareManager->cancelShare(token);
}

void TestIntegration::testHttpCorsHeaders()
{
    QByteArray response = httpGet("/");
    QVERIFY2(response.contains("Access-Control-Allow-Origin"),
             "CORS header not found in response");
}

void TestIntegration::testShareCreationAndAccess()
{
    QString token = m_shareManager->createShare("C:/e2e_test.txt", false, 0, 0, "");
    QVERIFY(!token.isEmpty());

    ShareInfo info = m_shareManager->getShareInfo(token);
    QVERIFY(info.isValid());
    QVERIFY(!info.isExpired());
    QCOMPARE(info.filePath, QString("C:/e2e_test.txt"));

    m_shareManager->cancelShare(token);
    info = m_shareManager->getShareInfo(token);
    QVERIFY(!info.isValid());
}

void TestIntegration::testShareWithPassword()
{
    QString token = m_shareManager->createShare("C:/secret.txt", false, 24, 0, "pass123");
    QVERIFY(!token.isEmpty());

    ShareInfo info = m_shareManager->getShareInfo(token);
    QVERIFY(info.passwordRequired);
    QVERIFY(m_shareManager->validateShare(token, "pass123"));
    QVERIFY(!m_shareManager->validateShare(token, "wrong"));

    m_shareManager->cancelShare(token);
}

void TestIntegration::testShareExpiry()
{
    QString token = m_shareManager->createShare("C:/expiring.txt", false, 1, 0, "");
    ShareInfo info = m_shareManager->getShareInfo(token);
    QVERIFY(!info.isExpired());
    QVERIFY(!info.expiresAt.isNull());

    m_shareManager->cancelShare(token);

    token = m_shareManager->createShare("C:/permanent.txt", false, 0, 0, "");
    info = m_shareManager->getShareInfo(token);
    QVERIFY(!info.isExpired());
    QVERIFY(info.expiresAt.isNull());

    m_shareManager->cancelShare(token);
}

QTEST_MAIN(TestIntegration)
