#ifndef TRANSFERLOGSERVICE_H
#define TRANSFERLOGSERVICE_H

#include <QObject>
#include <QString>
#include <QList>
#include <QDateTime>
#include <QVariantList>

class TransferLogEntry
{
    Q_GADGET
    Q_PROPERTY(QString id MEMBER id)
    Q_PROPERTY(QString taskId MEMBER taskId)
    Q_PROPERTY(int type MEMBER type)
    Q_PROPERTY(QString fileName MEMBER fileName)
    Q_PROPERTY(QString filePath MEMBER filePath)
    Q_PROPERTY(qint64 fileSize MEMBER fileSize)
    Q_PROPERTY(qint64 transferredSize MEMBER transferredSize)
    Q_PROPERTY(QString peerAddress MEMBER peerAddress)
    Q_PROPERTY(int status MEMBER status)
    Q_PROPERTY(QDateTime timestamp MEMBER timestamp)
    Q_PROPERTY(QString detail MEMBER detail)

public:
    enum Type { DownloadLog, UploadLog };
    Q_ENUM(Type)
    enum Status { Started, Completed, Failed, Cancelled, Paused };
    Q_ENUM(Status)

    QString id;
    QString taskId;
    int type = DownloadLog;
    QString fileName;
    QString filePath;
    qint64 fileSize = 0;
    qint64 transferredSize = 0;
    QString peerAddress;
    int status = Started;
    QDateTime timestamp;
    QString detail;
};

class DatabaseManager;

class TransferLogService : public QObject
{
    Q_OBJECT

public:
    explicit TransferLogService(QObject* parent = nullptr);
    ~TransferLogService() override;

    void setDatabase(DatabaseManager* db);

    Q_INVOKABLE QString logTransfer(int type, const QString& fileName, const QString& filePath,
                                     qint64 fileSize, const QString& peerAddress,
                                     int status, const QString& detail = QString(),
                                     qint64 transferredSize = 0, const QString& engineTaskId = QString());
    Q_INVOKABLE bool updateLogEntry(const QString& id, int status, const QString& detail = QString());

    Q_INVOKABLE QVariantList queryLogs(int limit = 100, int offset = 0) const;
    Q_INVOKABLE QVariantList queryByType(int type, int limit = 100) const;
    Q_INVOKABLE QVariantList queryByDateRange(const QDateTime& from, const QDateTime& to) const;
    Q_INVOKABLE QVariantList searchLogs(const QString& keyword) const;

    Q_INVOKABLE int totalCount() const;
    Q_INVOKABLE int countByType(int type) const;
    Q_INVOKABLE qint64 totalBytesTransferred() const;

    Q_INVOKABLE bool exportLogs(const QString& filePath) const;
    Q_INVOKABLE void clearLogs(int olderThanDays = 0);

    Q_INVOKABLE bool deleteLogByTaskId(const QString& taskId);
    Q_INVOKABLE int deleteLogsByFileName(const QString& fileName, int type);

    QVariantList pausedLogsForRestore() const;
    QVariantList restorableLogs() const;

signals:
    void logAdded(const QString& id);
    void logUpdated(const QString& id);

private:
    QList<TransferLogEntry> m_logs;
    DatabaseManager* m_database = nullptr;
    QString generateId() const;
    bool saveLogToDb(const TransferLogEntry& entry);
    bool updateLogInDb(const TransferLogEntry& entry);
    bool loadLogsFromDb();
};

#endif
