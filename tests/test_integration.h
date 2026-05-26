#ifndef TEST_INTEGRATION_H
#define TEST_INTEGRATION_H

#include <QObject>
#include <QtTest/QtTest>
#include <QTcpSocket>

class HttpServer;
class RequestHandler;
class ShareManager;
class TransferLogService;

class TestIntegration : public QObject
{
    Q_OBJECT

public:
    TestIntegration();
    ~TestIntegration();

private slots:
    void initTestCase();
    void cleanupTestCase();

    // End-to-end HTTP server tests
    void testHttpServerStartStop();
    void testHttpGetRoot();
    void testHttpGetSharePage();
    void testHttpGetNotFound();
    void testHttpPostUpload();
    void testHttpCorsHeaders();

    // Share + HTTP integration
    void testShareCreationAndAccess();
    void testShareWithPassword();
    void testShareExpiry();

private:
    HttpServer* m_server;
    ShareManager* m_shareManager;
    quint16 m_port;

    QByteArray httpGet(const QString& path);
    QByteArray httpPost(const QString& path, const QByteArray& body, const QString& contentType);
    int extractStatusCode(const QByteArray& response);
};

#endif
