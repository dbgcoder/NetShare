#ifndef SHAREMANAGER_H
#define SHAREMANAGER_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QMap>
#include <QSet>
#include <QVariantList>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QtQml/qqml.h>

#include "IShareManager.h"

class DatabaseManager;

class ShareInfo
{
    Q_GADGET
    QML_VALUE_TYPE(shareInfo)
    Q_PROPERTY(QString token MEMBER token)
    Q_PROPERTY(QString filePath MEMBER filePath)
    Q_PROPERTY(qint64 fileSize MEMBER fileSize)
    Q_PROPERTY(QDateTime expiresAt MEMBER expiresAt)
    Q_PROPERTY(int maxDownloads MEMBER maxDownloads)
    Q_PROPERTY(int downloadCount MEMBER downloadCount)
    Q_PROPERTY(bool passwordRequired MEMBER passwordRequired)
    Q_PROPERTY(QString passwordHash MEMBER passwordHash)
    Q_PROPERTY(bool isFolder MEMBER isFolder)
    Q_PROPERTY(QString description MEMBER description)
    Q_PROPERTY(int source MEMBER source)
    Q_PROPERTY(QDateTime createdAt MEMBER createdAt)

public:
    QString token;
    QString filePath;
    qint64 fileSize = 0;
    QDateTime expiresAt;
    int maxDownloads = 0;
    int downloadCount = 0;
    bool passwordRequired = false;
    QString passwordHash;
    bool isFolder = false;
    QString description;
    int source = 0;  // 0=shared out, 1=received/upload
    QDateTime createdAt;

    Q_INVOKABLE bool isValid() const;
    Q_INVOKABLE bool isExpired() const;
};

class ShareManager : public QObject, public IShareManager
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString localIp READ localIp CONSTANT)

public:
    static ShareManager& instance();
    static void shutdown();

    void setDatabase(DatabaseManager* db);

    QString localIp() const;

    Q_INVOKABLE QString createShare(const QString& filePath, bool isFolder = false,
                       int expireHours = 24, int maxDownloads = 0,
                       const QString& password = QString(),
                       int source = 0);
    Q_INVOKABLE ShareInfo getShareInfo(const QString& token) const;
    Q_INVOKABLE bool validateShare(const QString& token, const QString& password = QString()) const;
    Q_INVOKABLE bool cancelShare(const QString& token);
    Q_INVOKABLE QVariantList getActiveShares() const;
    Q_INVOKABLE QVariantList getAllShares() const;
    Q_INVOKABLE void cleanupExpiredShares();

    Q_INVOKABLE int getActiveShareCount() const;
    Q_INVOKABLE void copyToClipboard(const QString& text);

    // Received file management
    Q_INVOKABLE QVariantList getReceivedFiles() const;
    Q_INVOKABLE int getReceivedFileCount() const;
    Q_INVOKABLE bool deleteReceivedFile(const QString& token);
    Q_INVOKABLE bool openReceivedFile(const QString& token);
    Q_INVOKABLE bool openReceivedFileFolder(const QString& token);
    Q_INVOKABLE QString shareReceivedFile(const QString& token, int expireHours = 24, const QString& password = QString());

    // Called by RequestHandler when a share is accessed for download
    void shareAccessed(const QString& token);

signals:
    void shareCreated(const QString& token);
    void shareCancelled(const QString& token);
    void shareAccessedSignal(const QString& token);
    void shareExpired(const QString& token);
    void receivedFileDeleted(const QString& token);

private:
    ShareManager(QObject* parent = nullptr);
    ~ShareManager() override;

    ShareManager(const ShareManager&) = delete;
    ShareManager& operator=(const ShareManager&) = delete;

    QString generateToken() const;
    QString hashPassword(const QString& password) const;

    bool saveShareToDb(const ShareInfo& info);
    bool deleteShareFromDb(const QString& token);
    bool updateShareInDb(const ShareInfo& info);
    bool loadSharesFromDb();

    QMap<QString, ShareInfo> m_shares;
    QSet<QString> m_activeTokens;
    DatabaseManager* m_database = nullptr;
};

#endif
