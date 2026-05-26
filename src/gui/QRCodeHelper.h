#ifndef QRCODEHELPER_H
#define QRCODEHELPER_H

#include <QObject>
#include <QString>

class QRCodeHelper : public QObject
{
    Q_OBJECT

public:
    explicit QRCodeHelper(QObject* parent = nullptr) : QObject(parent) {}

    /**
     * Generate a QR code image and return a file:// URL.
     * Uses qrcodegen library to produce a PNG, saves to temp file.
     *
     * @param text   Content to encode (e.g. a URL)
     * @param size   Target image size in pixels (default 500)
     * @return       file:/// URL string, or empty on error
     */
    Q_INVOKABLE QString generateDataUrl(const QString& text, int size = 500);
};

#endif
