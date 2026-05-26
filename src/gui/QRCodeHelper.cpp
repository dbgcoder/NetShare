#include "QRCodeHelper.h"
#include "qrcode/qrcodegen.hpp"
#include "Logger.h"

#include <QDir>
#include <QUrl>
#include <QImage>
#include <QDateTime>

QString QRCodeHelper::generateDataUrl(const QString& text, int size)
{
    if (text.isEmpty()) {
        LOG_WARN("QRCodeHelper: text is empty");
        return QString();
    }

    if (size <= 0)
        size = 500;

    try {
        QByteArray utf8 = text.toUtf8();
        qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(utf8.constData(), qrcodegen::QrCode::Ecc::MEDIUM);

        int moduleCount = qr.getSize();
        int margin = 4; // QR spec: >= 4 modules quiet zone
        int totalModules = moduleCount + margin * 2;
        int moduleSize = size / totalModules;
        if (moduleSize < 1) moduleSize = 1;
        int actualSize = moduleSize * totalModules;

        // Create image — every module is exactly moduleSize x moduleSize pixels
        QImage image(actualSize, actualSize, QImage::Format_RGB32);
        if (image.isNull()) {
            LOG_ERROR("QRCodeHelper: failed to create image");
            return QString();
        }

        // Fill white
        QRgb white = 0xFFFFFFFF;
        QRgb black = 0xFF000000;
        image.fill(white);

        // Paint black modules pixel by pixel — no QPainter, no interpolation
        for (int y = 0; y < moduleCount; y++) {
            for (int x = 0; x < moduleCount; x++) {
                if (qr.getModule(x, y)) {
                    int px = (margin + x) * moduleSize;
                    int py = (margin + y) * moduleSize;
                    for (int dy = 0; dy < moduleSize; dy++) {
                        for (int dx = 0; dx < moduleSize; dx++) {
                            image.setPixel(px + dx, py + dy, black);
                        }
                    }
                }
            }
        }

        // Save to temp file, return file:// URL with cache-busting timestamp
        QString tempPath = QDir::tempPath() + "/netshare_qr.png";
        if (!image.save(tempPath, "PNG")) {
            LOG_ERROR("QRCodeHelper: failed to save QR image");
            return QString();
        }

        // Append timestamp to bust QML Image cache
        QString fileUrl = QUrl::fromLocalFile(tempPath).toString()
                          + "?t=" + QString::number(QDateTime::currentMSecsSinceEpoch());

        LOG_INFO("QRCodeHelper: generated QR %dx%d (modules=%d, moduleSize=%d, margin=%d modules) for '%s'",
                 actualSize, actualSize, moduleCount, moduleSize, margin, qPrintable(text));

        return fileUrl;

    } catch (const std::exception& e) {
        LOG_ERROR("QRCodeHelper: encode failed: %s", e.what());
        return QString();
    } catch (...) {
        LOG_ERROR("QRCodeHelper: encode failed (unknown)");
        return QString();
    }
}
