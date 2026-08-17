#ifndef TEST_HTTPSERVER_H
#define TEST_HTTPSERVER_H

#include <QObject>
#include <QtTest/QtTest>

class CivetWebServer;

class TestHttpServer : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testServerStartStop();
    void testServerPort();

    void testRouteRegistration();
    void testExactRouteMatch();
    void testWildcardRouteMatch();
    void testNoRouteMatch();
    void testDefaultHandler();

    void testSendJsonResponse();
    void testSendHtmlResponse();

    void testQueryParams();
    void testHeaders();

    void testCorsPreflight();
    void testMultipleRoutes();

private:
    CivetWebServer* m_server = nullptr;
    quint16 m_port = 19080;
};

#endif
