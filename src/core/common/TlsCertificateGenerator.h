#ifndef TLSCERTIFICATEGENERATOR_H
#define TLSCERTIFICATEGENERATOR_H

#include <QString>
#include <QPair>

class TlsCertificateGenerator
{
public:
    struct Result {
        bool success = false;
        QString certPath;
        QString keyPath;
        QString errorMessage;
    };

    static Result generateSelfSignedCert(const QString& certDir,
                                          const QString& commonName = QStringLiteral("NetShare"));

    static bool certificatesExist(const QString& certDir);
    static QString defaultCertDir();
    static QString defaultCertPath();
    static QString defaultKeyPath();

private:
    static bool runOpenSSL(const QStringList& args);
};

#endif
