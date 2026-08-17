#include "test_qrgenerator.h"
#include "qrcode/qrcodegen.hpp"

QTEST_MAIN(TestQRGenerator)

void TestQRGenerator::initTestCase()
{
}

void TestQRGenerator::cleanupTestCase()
{
}

void TestQRGenerator::init()
{
}

void TestQRGenerator::cleanup()
{
}

void TestQRGenerator::testGenerateQRCode()
{
    qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText("test content", qrcodegen::QrCode::Ecc::MEDIUM);
    QVERIFY(qr.getSize() > 0);
}

void TestQRGenerator::testEmptyContent()
{
    // qrcodegen handles empty content gracefully (returns small QR), doesn't throw
    qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText("", qrcodegen::QrCode::Ecc::MEDIUM);
    QVERIFY(qr.getSize() > 0);
}

void TestQRGenerator::testLargeContent()
{
    QString largeContent = QString("a").repeated(1000);
    qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(largeContent.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
    QVERIFY(qr.getSize() > 0);
}

void TestQRGenerator::testSpecialCharacters()
{
    qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText("中文内容!@#$%^&*()", qrcodegen::QrCode::Ecc::MEDIUM);
    QVERIFY(qr.getSize() > 0);
}

void TestQRGenerator::testErrorCorrectionLevel()
{
    qrcodegen::QrCode qrL = qrcodegen::QrCode::encodeText("test", qrcodegen::QrCode::Ecc::LOW);
    qrcodegen::QrCode qrM = qrcodegen::QrCode::encodeText("test", qrcodegen::QrCode::Ecc::MEDIUM);
    qrcodegen::QrCode qrH = qrcodegen::QrCode::encodeText("test", qrcodegen::QrCode::Ecc::HIGH);
    QVERIFY(qrL.getSize() > 0);
    QVERIFY(qrM.getSize() > 0);
    QVERIFY(qrH.getSize() > 0);
}

void TestQRGenerator::testQRCodeSize()
{
    qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText("test", qrcodegen::QrCode::Ecc::MEDIUM);
    int size = qr.getSize();
    QVERIFY(size > 0);
    // Module count should be consistent
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            // Just verify getModule doesn't crash
            qr.getModule(x, y);
        }
    }
}
