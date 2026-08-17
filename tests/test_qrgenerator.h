#ifndef TEST_QRGENERATOR_H
#define TEST_QRGENERATOR_H

#include <QObject>
#include <QtTest/QtTest>

class TestQRGenerator : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void testGenerateQRCode();
    void testEmptyContent();
    void testLargeContent();
    void testSpecialCharacters();
    void testErrorCorrectionLevel();
    void testQRCodeSize();
};

#endif
