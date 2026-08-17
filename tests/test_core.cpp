#include "test_core.h"
#include "core/transfer/BandwidthManager.h"
#include "core/transfer/TransferLogService.h"
#include "core/share/ShareManager.h"

void TestCore::initTestCase() {}
void TestCore::cleanupTestCase() {}

// ---- BandwidthManager tests ----

void TestCore::testBandwidthDefaultLimit()
{
    BandwidthManager bm;
    QCOMPARE(bm.globalLimit(), 0);
}

void TestCore::testBandwidthSetGlobalLimit()
{
    BandwidthManager bm;
    bm.setGlobalLimit(1024000);
    QCOMPARE(bm.globalLimit(), 1024000);
}

void TestCore::testBandwidthSetTaskLimit()
{
    BandwidthManager bm;
    bm.setTaskLimit("task1", 500000);
    QCOMPARE(bm.taskLimit("task1"), 500000);
    QCOMPARE(bm.taskLimit("nonexistent"), 0);
}

void TestCore::testBandwidthAvailableWithGlobalOnly()
{
    BandwidthManager bm;
    bm.setGlobalLimit(1000);
    QCOMPARE(bm.availableBandwidth("task1"), 1000);
}

void TestCore::testBandwidthAvailableWithTaskOnly()
{
    BandwidthManager bm;
    bm.setTaskLimit("task1", 500);
    QCOMPARE(bm.availableBandwidth("task1"), 500);
}

void TestCore::testBandwidthAvailableMinOfBoth()
{
    BandwidthManager bm;
    bm.setGlobalLimit(1000);
    bm.setTaskLimit("task1", 500);
    QCOMPARE(bm.availableBandwidth("task1"), 500);

    bm.setTaskLimit("task1", 2000);
    QCOMPARE(bm.availableBandwidth("task1"), 1000); // min(2000, 1000)
}

void TestCore::testBandwidthRecordTransfer()
{
    BandwidthManager bm;
    bm.recordTransfer("task1", 1024);
    bm.recordTransfer("task1", 2048);
    // Speed is calculated on timer tick; before tick, currentSpeed is 0
    QCOMPARE(bm.currentSpeed("task1"), 0);
}

void TestCore::testBandwidthRemoveTaskLimit()
{
    BandwidthManager bm;
    bm.setTaskLimit("task1", 500);
    QCOMPARE(bm.taskLimit("task1"), 500);
    bm.removeTaskLimit("task1");
    QCOMPARE(bm.taskLimit("task1"), 0);
}

void TestCore::testBandwidthGlobalSpeed()
{
    BandwidthManager bm;
    QCOMPARE(bm.globalCurrentSpeed(), 0);
}

// ---- TransferLogService tests ----

void TestCore::testLogTransfer()
{
    TransferLogService svc;
    QString id = svc.logTransfer(0, "photo.jpg", "/photos/photo.jpg", 1024000, "192.168.1.10", 1, "completed");
    QVERIFY(!id.isEmpty());
    QCOMPARE(svc.totalCount(), 1);
}

void TestCore::testLogTransferUpload()
{
    TransferLogService svc;
    svc.logTransfer(1, "doc.pdf", "/docs/doc.pdf", 2048000, "192.168.1.20", 1, "completed");
    QCOMPARE(svc.countByType(1), 1);
    QCOMPARE(svc.countByType(0), 0);
}

void TestCore::testUpdateLogEntry()
{
    TransferLogService svc;
    QString id = svc.logTransfer(0, "file.txt", "/file.txt", 100, "10.0.0.1", 0, "");
    QVERIFY(svc.updateLogEntry(id, 1, "completed"));

    QVariantList logs = svc.queryLogs(10);
    QCOMPARE(logs.size(), 1);
    TransferLogEntry entry = logs.first().value<TransferLogEntry>();
    QCOMPARE(entry.status, 1);
    QCOMPARE(entry.detail, QString("completed"));
}

void TestCore::testUpdateLogEntryNotFound()
{
    TransferLogService svc;
    QVERIFY(!svc.updateLogEntry("nonexistent-id", 1, ""));
}

void TestCore::testQueryLogs()
{
    TransferLogService svc;
    svc.logTransfer(0, "a.txt", "/a.txt", 100, "10.0.0.1", 1, "");
    svc.logTransfer(0, "b.txt", "/b.txt", 200, "10.0.0.2", 1, "");
    svc.logTransfer(0, "c.txt", "/c.txt", 300, "10.0.0.3", 1, "");

    QVariantList logs = svc.queryLogs(2, 0);
    QCOMPARE(logs.size(), 2);

    logs = svc.queryLogs(2, 2);
    QCOMPARE(logs.size(), 1);
}

void TestCore::testQueryByType()
{
    TransferLogService svc;
    svc.logTransfer(0, "download.txt", "/d.txt", 100, "10.0.0.1", 1, "");
    svc.logTransfer(1, "upload.txt", "/u.txt", 200, "10.0.0.2", 1, "");

    QVariantList downloads = svc.queryByType(0, 100);
    QCOMPARE(downloads.size(), 1);

    QVariantList uploads = svc.queryByType(1, 100);
    QCOMPARE(uploads.size(), 1);
}

void TestCore::testSearchLogs()
{
    TransferLogService svc;
    svc.logTransfer(0, "photo.jpg", "/photos/photo.jpg", 1024, "192.168.1.10", 1, "");
    svc.logTransfer(0, "document.pdf", "/docs/document.pdf", 2048, "192.168.1.20", 1, "");

    QVariantList results = svc.searchLogs("photo");
    QCOMPARE(results.size(), 1);

    results = svc.searchLogs("192.168.1.20");
    QCOMPARE(results.size(), 1);

    results = svc.searchLogs("nonexistent");
    QCOMPARE(results.size(), 0);
}

void TestCore::testTotalCount()
{
    TransferLogService svc;
    QCOMPARE(svc.totalCount(), 0);
    svc.logTransfer(0, "a.txt", "/a.txt", 100, "10.0.0.1", 1, "");
    svc.logTransfer(1, "b.txt", "/b.txt", 200, "10.0.0.2", 1, "");
    QCOMPARE(svc.totalCount(), 2);
}

void TestCore::testCountByType()
{
    TransferLogService svc;
    svc.logTransfer(0, "a.txt", "/a.txt", 100, "10.0.0.1", 1, "");
    svc.logTransfer(1, "b.txt", "/b.txt", 200, "10.0.0.2", 1, "");
    svc.logTransfer(0, "c.txt", "/c.txt", 300, "10.0.0.3", 1, "");
    QCOMPARE(svc.countByType(0), 2);
    QCOMPARE(svc.countByType(1), 1);
}

void TestCore::testTotalBytesTransferred()
{
    TransferLogService svc;
    svc.logTransfer(0, "a.txt", "/a.txt", 1000, "10.0.0.1", 1, ""); // completed
    svc.logTransfer(0, "b.txt", "/b.txt", 2000, "10.0.0.2", 2, ""); // failed - not counted
    QCOMPARE(svc.totalBytesTransferred(), 1000LL);
}

void TestCore::testClearLogs()
{
    TransferLogService svc;
    svc.logTransfer(0, "a.txt", "/a.txt", 100, "10.0.0.1", 1, "");
    svc.logTransfer(0, "b.txt", "/b.txt", 200, "10.0.0.2", 1, "");
    QCOMPARE(svc.totalCount(), 2);

    svc.clearLogs(0);
    QCOMPARE(svc.totalCount(), 0);
}

// ---- ShareInfo tests ----

void TestCore::testShareInfoExpired()
{
    ShareInfo info;
    info.token = "test";
    info.filePath = "/test";
    info.expiresAt = QDateTime::currentDateTime().addSecs(-3600); // 1 hour ago
    QVERIFY(info.isExpired());
}

void TestCore::testShareInfoNeverExpires()
{
    ShareInfo info;
    info.token = "test";
    info.filePath = "/test";
    // expiresAt is null (default)
    QVERIFY(info.expiresAt.isNull());
    QVERIFY(!info.isExpired());
}

QTEST_MAIN(TestCore)
