#include "test_sharemanager.h"
#include "core/common/NetShareError.h"
#include "core/share/ShareManager.h"
#include "core/common/Logger.h"
#include <QStandardPaths>

void TestShareManager::initTestCase()
{
    Logger::initialize(QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/netshare_test/logs");
}

void TestShareManager::cleanupTestCase()
{
    Logger::shutdown();
}

void TestShareManager::testLocalIp()
{
    ShareManager& sm = ShareManager::instance();
    QString ip = sm.localIp();
    QVERIFY(ip.contains('.') || ip == "127.0.0.1");
}

void TestShareManager::testCreateShare()
{
    ShareManager& sm = ShareManager::instance();
    QString token = sm.createShare("C:/testfile.txt", false, 24, 0, "");
    QVERIFY(!token.isEmpty());
    QCOMPARE(token.length(), 12);

    ShareInfo info = sm.getShareInfo(token);
    QVERIFY(info.isValid());
    QCOMPARE(info.filePath, QString("C:/testfile.txt"));
    QVERIFY(!info.isFolder);
    QVERIFY(!info.passwordRequired);

    sm.cancelShare(token);
}

void TestShareManager::testCreateShareWithPassword()
{
    ShareManager& sm = ShareManager::instance();
    QString token = sm.createShare("C:/secret.txt", false, 24, 0, "mypassword");
    QVERIFY(!token.isEmpty());

    ShareInfo info = sm.getShareInfo(token);
    QVERIFY(info.passwordRequired);
    QVERIFY(!info.passwordHash.isEmpty());

    QVERIFY(sm.validateShare(token, "mypassword"));
    QVERIFY(!sm.validateShare(token, "wrongpassword"));

    sm.cancelShare(token);
}

void TestShareManager::testCancelShare()
{
    ShareManager& sm = ShareManager::instance();
    QString token = sm.createShare("C:/cancel_test.txt", false, 24);
    QVERIFY(!token.isEmpty());

    QVERIFY(sm.cancelShare(token));

    ShareInfo info = sm.getShareInfo(token);
    QVERIFY(!info.isValid());

    QVERIFY(!sm.cancelShare(token));
}

void TestShareManager::testGetActiveShares()
{
    ShareManager& sm = ShareManager::instance();
    QString t1 = sm.createShare("C:/active1.txt", false, 24);
    QString t2 = sm.createShare("C:/active2.txt", false, 24);

    QVariantList shares = sm.getActiveShares();
    QVERIFY(shares.size() >= 2);

    sm.cancelShare(t1);
    sm.cancelShare(t2);
}

void TestShareManager::testShareAccessed()
{
    ShareManager& sm = ShareManager::instance();
    QString token = sm.createShare("C:/access_test.txt", false, 24, 2, "");

    ShareInfo info = sm.getShareInfo(token);
    QCOMPARE(info.downloadCount, 0);

    sm.shareAccessed(token);
    info = sm.getShareInfo(token);
    QCOMPARE(info.downloadCount, 1);

    sm.shareAccessed(token);
    info = sm.getShareInfo(token);
    QVERIFY(!info.isValid());
}

void TestShareManager::testErrorCodeToString()
{
    QCOMPARE(errorCodeToString(ErrorCode::NoError), QString("No error"));
    QCOMPARE(errorCodeToString(ErrorCode::ShareNotFound), QString("Share not found"));
    QCOMPARE(errorCodeToString(ErrorCode::TransferChunkFailed), QString("Chunk download failed"));
}

void TestShareManager::testResultType()
{
    Result<QString> okResult(QString("hello"));
    QVERIFY(okResult.isSuccess());
    QCOMPARE(okResult.value(), QString("hello"));
    QCOMPARE(okResult.valueOrDefault("default"), QString("hello"));

    Result<QString> errResult(ErrorCode::ShareNotFound, "Share XYZ not found");
    QVERIFY(errResult.isError());
    QCOMPARE(errResult.error(), ErrorCode::ShareNotFound);
    QVERIFY(errResult.errorMessage().contains("Share XYZ not found"));
    QCOMPARE(errResult.valueOrDefault("fallback"), QString("fallback"));
}

void TestShareManager::testResultVoid()
{
    auto ok = Result<void>::ok();
    QVERIFY(ok.isSuccess());

    auto err = Result<void>::fail(ErrorCode::DatabaseNotOpen, "DB not initialized");
    QVERIFY(err.isError());
    QCOMPARE(err.error(), ErrorCode::DatabaseNotOpen);
}

QTEST_MAIN(TestShareManager)
