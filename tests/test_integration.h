#ifndef TEST_INTEGRATION_H
#define TEST_INTEGRATION_H

#include <QObject>
#include <QtTest/QtTest>
#include <QTcpSocket>

class CivetWebServer;
class RequestHandler;
class ShareManager;

class TestIntegration : public QObject
{
    Q_OBJECT

public:
    TestIntegration();
    ~TestIntegration();

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testHttpServerStartStop();
    void testHttpGetRoot();
    void testHttpGetSharePage();
    void testHttpGetNotFound();
    void testHttpPostUpload();
    void testHttpCorsHeaders();

    void testShareCreationAndAccess();
    void testShareWithPassword();
    void testShareExpiry();

private:
    CivetWebServer* m_server = nullptr;
    RequestHandler* m_requestHandler = nullptr;
    ShareManager* m_shareManager = nullptr;
    quint16 m_port = 18080;

    QByteArray httpGet(const QString& path);
    QByteArray httpPost(const QString& path, const QByteArray& body, const QString& contentType);
    int extractStatusCode(const QByteArray& response);
};

#endif
