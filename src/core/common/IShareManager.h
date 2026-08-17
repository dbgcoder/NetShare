#ifndef ISHAREMANAGER_H
#define ISHAREMANAGER_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QDateTime>

// Forward declaration — ShareInfo is defined in ShareManager.h
class ShareInfo;

class IShareManager
{
public:
    virtual ~IShareManager() = default;

    virtual QString localIp() const = 0;

    virtual QString createShare(const QString& filePath, bool isFolder = false,
                                int expireHours = 24, int maxDownloads = 0,
                                const QString& password = QString(),
                                int source = 0) = 0;
    virtual ShareInfo getShareInfo(const QString& token) const = 0;
    virtual bool validateShare(const QString& token, const QString& password = QString()) const = 0;
    virtual bool cancelShare(const QString& token) = 0;
    virtual QVariantList getActiveShares() const = 0;
    virtual void cleanupExpiredShares() = 0;

    virtual int getActiveShareCount() const = 0;
    virtual void copyToClipboard(const QString& text) = 0;
    virtual void shareAccessed(const QString& token) = 0;

    virtual QVariantList getReceivedFiles() const = 0;
    virtual int getReceivedFileCount() const = 0;
    virtual bool deleteReceivedFile(const QString& token) = 0;
    virtual bool openReceivedFile(const QString& token) = 0;
    virtual bool openReceivedFileFolder(const QString& token) = 0;
    virtual QString shareReceivedFile(const QString& token, int expireHours = 24, const QString& password = QString()) = 0;
};

#endif
