#ifndef TEST_HTTPSERVER_H
#define TEST_HTTPSERVER_H

#include <QObject>
#include <QtTest/QtTest>

class TestHttpServer : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // HttpResponse factory tests
    void testResponseOk();
    void testResponseBadRequest();
    void testResponseNotFound();
    void testResponseInternalError();
    void testResponseRedirect();
    void testFileResponseNoRange();
    void testFileResponsePartialContent();
    void testFileResponseRangeNotSatisfiable();

    // Request parsing tests
    void testParseGetRequest();
    void testParsePostRequest();
    void testParseQueryParams();
    void testParsePercentEncodedQuery();
    void testParseHeaders();

    // Route matching tests
    void testExactRouteMatch();
    void testWildcardRouteMatch();
    void testRouteMethodCaseInsensitive();
    void testNoRouteMatch();
    void testDefaultHandler();

    // handleRequest integration
    void testHandleRequestWithRoute();
    void testCorsPreflight();
};

#endif
