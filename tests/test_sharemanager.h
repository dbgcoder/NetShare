#ifndef TEST_SHAREMANAGER_H
#define TEST_SHAREMANAGER_H

#include <QObject>
#include <QtTest/QtTest>

class TestShareManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testLocalIp();
    void testCreateShare();
    void testCreateShareWithPassword();
    void testCancelShare();
    void testGetActiveShares();
    void testShareAccessed();
    void testErrorCodeToString();
    void testResultType();
    void testResultVoid();
};

#endif
