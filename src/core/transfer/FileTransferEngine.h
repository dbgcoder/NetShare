#ifndef FILETRANSFERENGINE_H
#define FILETRANSFERENGINE_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QList>
#include <QUuid>
#include <QDateTime>
#include <QVariantList>
#include <QFutureWatcher>
#include <QFile>
#include <QNetworkAccessManager>
#include <QTimer>
#include <functional>

#include "ShareManager.h"
#include <QtQml/qqml.h>

class ChunkManager;
class ResumeManager;
class BandwidthManager;
class TransferLogService;

class TransferWorker : public QObject
{
    Q_OBJECT
public:
    explicit TransferWorker(const QString& url, qint64 offset, qint64 length, 
                            const QString& chunkPath, QObject* parent = nullptr);
    void start();

signals:
    void chunkFinished(int index, bool success);
    void chunkProgress(qint64 bytesTransferred);

private slots:
    void onDownloadFinished();
    void onReadyRead();

private:
    QString m_url;
    qint64 m_offset;
    qint64 m_length;
    QString m_chunkPath;
    QFile* m_file = nullptr;
    QNetworkAccessManager* m_networkManager = nullptr;
};

class TransferTask
{
    Q_GADGET
    QML_VALUE_TYPE(transferTask)
    Q_PROPERTY(QString taskId MEMBER taskId)
    Q_PROPERTY(int type MEMBER type)
    Q_PROPERTY(int status MEMBER status)
    Q_PROPERTY(QString fileName MEMBER fileName)
    Q_PROPERTY(QString filePath MEMBER filePath)
    Q_PROPERTY(QString savePath MEMBER savePath)
    Q_PROPERTY(qint64 fileSize MEMBER fileSize)
    Q_PROPERTY(qint64 transferredSize MEMBER transferredSize)
    Q_PROPERTY(int progress MEMBER progress)
    Q_PROPERTY(int speed MEMBER speed)
    Q_PROPERTY(int threads MEMBER threads)
    Q_PROPERTY(QString error MEMBER error)
    Q_PROPERTY(QDateTime startedAt MEMBER startedAt)
    Q_PROPERTY(QDateTime completedAt MEMBER completedAt)

public:
    enum Type { Download, Upload };
    Q_ENUM(Type)
    enum Status { Pending, Preparing, Downloading, Uploading, Paused, Completed, Failed, Cancelled };
    Q_ENUM(Status)

    QString taskId;
    int type = Download;
    int status = Pending;
    QString fileName;
    QString filePath;
    QString savePath;
    qint64 fileSize = 0;
    qint64 transferredSize = 0;
    int progress = 0;
    int speed = 0;
    int threads = 1;
    QString error;
    QDateTime startedAt;
    QDateTime completedAt;
};

class FileTransferEngine : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit FileTransferEngine(QObject* parent = nullptr);
    ~FileTransferEngine() override;

    bool initialize();
    void stopAllTasks();

    Q_INVOKABLE QString startDownload(const QString& shareToken, const QString& savePath, int threads = 3);
    Q_INVOKABLE QString startUpload(const QString& remotePath, const QString& localPath, int threads = 3);

    Q_INVOKABLE bool pauseTask(const QString& taskId);
    Q_INVOKABLE bool resumeTask(const QString& taskId);
    Q_INVOKABLE bool cancelTask(const QString& taskId);
    Q_INVOKABLE bool deleteTask(const QString& taskId);
    bool failTask(const QString& taskId, const QString& error);

    Q_INVOKABLE TransferTask getTaskInfo(const QString& taskId) const;
    Q_INVOKABLE QVariantList getAllTasks() const;
    Q_INVOKABLE QVariantList getActiveTasks() const;

    Q_INVOKABLE int getActiveTaskCount() const;
    Q_INVOKABLE qint64 getTotalTransferredSize() const;

    void addCompletedTask(const TransferTask& task);
    void addUploadingTask(const TransferTask& task);
    void updateTaskProgress(const QString& taskId, qint64 transferredSize);
    void completeTask(const QString& taskId);
    void completeTaskByName(const QString& fileName, int type);
    void removeFailedUploadTasksByName(const QString& fileName);

    void setManagers(ShareManager* sm, ChunkManager* cm, ResumeManager* rm, BandwidthManager* bm);
    void setTransferLogService(TransferLogService* tls);
    void setUploadPauseCallback(std::function<void(const QString& taskId)> cb);
    void setUploadResumeCallback(std::function<void(const QString& taskId)> cb);

private:
    void performDownload(const QString& taskId, const ShareInfo& info, const QString& savePath, int threads);

signals:
    void taskStarted(const QString& taskId);
    void taskProgress(const QString& taskId, int progress, int speed);
    void taskCompleted(const QString& taskId);
    void taskFailed(const QString& taskId, const QString& error);
    void taskCancelled(const QString& taskId);
    void taskPaused(const QString& taskId);
    void taskResumed(const QString& taskId);
    void taskDeleted(const QString& taskId);

private:
    QMap<QString, TransferTask> m_tasks;
    QString m_tempDirectory;
    QFutureWatcher<void>* m_mergeWatcher;

    ShareManager* m_shareManager;
    ChunkManager* m_chunkManager;
    ResumeManager* m_resumeManager;
    BandwidthManager* m_bandwidthManager;
    TransferLogService* m_transferLogService = nullptr;
    std::function<void(const QString&)> m_uploadPauseCallback;
    std::function<void(const QString&)> m_uploadResumeCallback;

    struct SpeedSample {
        qint64 bytes = 0;
        qint64 timeMs = 0;
    };
    QMap<QString, QList<SpeedSample>> m_speedHistory;
    QMap<QString, qint64> m_lastProgressTime;
    QTimer* m_speedCheckTimer;

    void onChunkFinished(const QString& taskId, int index, bool success);
};

#endif
