#ifndef TEST_CORE_H
#define TEST_CORE_H

#include <QObject>
#include <QtTest/QtTest>

class TestCore : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // BandwidthManager tests
    void testBandwidthDefaultLimit();
    void testBandwidthSetGlobalLimit();
    void testBandwidthSetTaskLimit();
    void testBandwidthAvailableWithGlobalOnly();
    void testBandwidthAvailableWithTaskOnly();
    void testBandwidthAvailableMinOfBoth();
    void testBandwidthRecordTransfer();
    void testBandwidthRemoveTaskLimit();
    void testBandwidthGlobalSpeed();

    // TransferLogService tests
    void testLogTransfer();
    void testLogTransferUpload();
    void testUpdateLogEntry();
    void testUpdateLogEntryNotFound();
    void testQueryLogs();
    void testQueryByType();
    void testSearchLogs();
    void testTotalCount();
    void testCountByType();
    void testTotalBytesTransferred();
    void testClearLogs();

    // ShareInfo tests
    void testShareInfoExpired();
    void testShareInfoNeverExpires();
};

#endif
