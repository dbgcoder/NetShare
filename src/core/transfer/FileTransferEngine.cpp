#include "FileTransferEngine.h"
#include "Logger.h"
#include "ShareManager.h"
#include "ChunkManager.h"
#include "ResumeManager.h"
#include "BandwidthManager.h"
#include "TransferLogService.h"
#include <QDir>
#include <QStandardPaths>
#include <QAtomicInt>
#include <QtConcurrent>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>

// TransferWorker Implementation
TransferWorker::TransferWorker(const QString& url, qint64 offset, qint64 length,
                               const QString& chunkPath, QObject* parent)
    : QObject(parent), m_url(url), m_offset(offset), m_length(length), 
      m_chunkPath(chunkPath), m_file(nullptr), m_networkManager(new QNetworkAccessManager(this))
{
}

void TransferWorker::start()
{
    m_file = new QFile(m_chunkPath);
    if (!m_file->open(QIODevice::WriteOnly)) {
        emit chunkFinished(-1, false);
        deleteLater();
        return;
    }

    QNetworkRequest request(m_url);
    QString rangeHeader = QString("bytes=%1-%2").arg(m_offset).arg(m_offset + m_length - 1);
    request.setRawHeader("Range", rangeHeader.toUtf8());

    // Note: BandwidthManager integration would happen here by monitoring the reply's speed
    // and calling QNetworkReply::setReadBufferSize or pausing/resuming.

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, &TransferWorker::onDownloadFinished);
    connect(reply, &QNetworkReply::readyRead, this, &TransferWorker::onReadyRead);
}

void TransferWorker::onReadyRead()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply) {
        QByteArray data = reply->readAll();
        m_file->write(data);
        emit chunkProgress(data.size());
    }
}

void TransferWorker::onDownloadFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    bool success = true;
    if (reply->error() != QNetworkReply::NoError) {
        LOG_ERROR("TransferWorker error: %s", qPrintable(reply->errorString()));
        success = false;
    }
    
    if (m_file) {
        m_file->close();
        delete m_file;
    }
    reply->deleteLater();
    emit chunkFinished(0, success); // Index is handled by the engine
    deleteLater();
}

FileTransferEngine::FileTransferEngine(QObject* parent)
    : QObject(parent), m_mergeWatcher(new QFutureWatcher<void>(this)),
      m_shareManager(nullptr), m_chunkManager(nullptr), 
      m_resumeManager(nullptr), m_bandwidthManager(nullptr)
{
    m_tempDirectory = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/NetShare";
    QDir().mkpath(m_tempDirectory);
}

void FileTransferEngine::setManagers(ShareManager* sm, ChunkManager* cm, ResumeManager* rm, BandwidthManager* bm)
{
    m_shareManager = sm;
    m_chunkManager = cm;
    m_resumeManager = rm;
    m_bandwidthManager = bm;

    if (m_bandwidthManager) {
        connect(m_bandwidthManager, &BandwidthManager::bandwidthUpdated, this, [this](const QString& taskId, int speed) {
            if (m_tasks.contains(taskId)) {
                m_tasks[taskId].speed = speed;
            }
        });
    }
}

void FileTransferEngine::setTransferLogService(TransferLogService* tls)
{
    m_transferLogService = tls;
}

FileTransferEngine::~FileTransferEngine() = default;

bool FileTransferEngine::initialize()
{
    // Restore paused download tasks from database
    if (m_transferLogService) {
        QVariantList pausedLogs = m_transferLogService->pausedLogsForRestore();
        for (const QVariant& v : pausedLogs) {
            TransferLogEntry entry = v.value<TransferLogEntry>();
            TransferTask task;
            task.taskId = entry.taskId;
            task.type = TransferTask::Download; // Only download tasks support pause persistence
            task.status = TransferTask::Paused;
            task.fileName = entry.fileName;
            task.filePath = entry.filePath;
            task.fileSize = entry.fileSize;
            task.transferredSize = entry.transferredSize;
            task.progress = task.fileSize > 0 ? static_cast<int>((task.transferredSize * 100) / task.fileSize) : 0;
            task.startedAt = QDateTime::currentDateTime();

            if (!task.taskId.isEmpty()) {
                m_tasks[task.taskId] = task;
                LOG_INFO("Restored paused download task: %s (%s)", qPrintable(task.taskId), qPrintable(task.fileName));
            }
        }
    }

    LOG_INFO("FileTransferEngine initialized");
    return true;
}

void FileTransferEngine::stopAllTasks()
{
    // Persist paused download tasks to database before clearing
    if (m_transferLogService) {
        for (auto it = m_tasks.constBegin(); it != m_tasks.constEnd(); ++it) {
            const TransferTask& task = it.value();
            if (task.status == TransferTask::Paused && task.type == TransferTask::Download) {
                m_transferLogService->logTransfer(
                    TransferLogEntry::DownloadLog, task.fileName, task.filePath,
                    task.fileSize, QString(), TransferLogEntry::Paused,
                    QString(), task.transferredSize, task.taskId);
                LOG_INFO("Persisted paused download task: %s", qPrintable(task.taskId));
            }
        }
    }

    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        if (it.value().status == TransferTask::Downloading || it.value().status == TransferTask::Uploading) {
            it.value().status = TransferTask::Cancelled;
            it.value().speed = 0;
            if (m_bandwidthManager) m_bandwidthManager->removeRecord(it.key());
            emit taskCancelled(it.key());
        }
    }
    m_tasks.clear();
    LOG_INFO("All transfer tasks stopped");
}

QString FileTransferEngine::startDownload(const QString& shareToken, const QString& savePath, int threads)
{
    if (!m_shareManager) return QString();

    ShareInfo info = m_shareManager->getShareInfo(shareToken);
    if (info.filePath.isEmpty()) {
        LOG_ERROR("Share token not found: %s", qPrintable(shareToken));
        return QString();
    }

    QString taskId = QUuid::createUuid().toString();
    TransferTask task;
    task.taskId = taskId;
    task.type = TransferTask::Download;
    task.status = TransferTask::Preparing;
    task.savePath = savePath;
    task.threads = threads;
    task.startedAt = QDateTime::currentDateTime();
    task.fileName = QFileInfo(info.filePath).fileName();
    task.fileSize = QFileInfo(info.filePath).size();

    m_tasks[taskId] = task;
    emit taskStarted(taskId);

    // Start the actual download in a background thread
    (void)QtConcurrent::run([this, taskId, info, savePath, threads]() {
        this->performDownload(taskId, info, savePath, threads);
    });

    LOG_INFO("Download task started: %s for %s", qPrintable(taskId), qPrintable(info.filePath));
    return taskId;
}

void FileTransferEngine::performDownload(const QString& taskId, const ShareInfo& info, const QString& savePath, int threads)
{
    if (!m_chunkManager) return;

    // Check for resume info
    bool hasResume = m_resumeManager && m_resumeManager->hasResumeInfo(taskId);

    qint64 fileSize = QFileInfo(info.filePath).size();
    QString chunkDir = m_tempDirectory + "/" + taskId;
    QDir().mkpath(chunkDir);

    QVariantList chunks = m_chunkManager->splitFileForThreads(fileSize, threads);
    int totalChunks = chunks.size();

    // Track chunk success/failure
    QAtomicInt failedChunks(0);
    QList<QFuture<void>> futures;
    for (const QVariant& v : chunks) {
        ChunkInfo chunk = v.value<ChunkInfo>();
        QString chunkPath = chunkDir + QString("/chunk_%1").arg(chunk.index, 6, 10, QChar('0'));

        // If resuming and chunk already exists, skip it
        if (hasResume && QFileInfo::exists(chunkPath)) {
            LOG_INFO("Skipping existing chunk %d for task %s", chunk.index, qPrintable(taskId));
            continue;
        }

        // Construct URL - assuming local server is running on port 8080
        QString url = QString("http://%1:8080/download/%2/").arg(m_shareManager->localIp(), info.token);

        TransferWorker* worker = new TransferWorker(url, chunk.offset, chunk.size, chunkPath);
        connect(worker, &TransferWorker::chunkProgress, this, [this, taskId](qint64 bytes) {
            QMetaObject::invokeMethod(this, [this, taskId, bytes]() {
                if (m_tasks.contains(taskId)) {
                    if (m_bandwidthManager) m_bandwidthManager->recordTransfer(taskId, static_cast<int>(bytes));
                    m_tasks[taskId].transferredSize += bytes;
                    int progress = static_cast<int>((m_tasks[taskId].transferredSize * 100) / m_tasks[taskId].fileSize);
                    int speed = m_bandwidthManager ? m_bandwidthManager->currentSpeed(taskId) : 0;
                    m_tasks[taskId].speed = speed;
                    emit taskProgress(taskId, progress, speed);
                }
            }, Qt::QueuedConnection);
        });

        QFuture<void> future = QtConcurrent::run([worker, &failedChunks]() {
            QEventLoop loop;
            QObject::connect(worker, &TransferWorker::chunkFinished, &loop, [&loop, &failedChunks](int, bool success) {
                if (success) {
                    loop.quit();
                } else {
                    failedChunks.fetchAndAddRelaxed(1);
                    loop.exit(1); // Error
                }
            });
            worker->start();
            loop.exec();
        });
        futures.append(future);
    }

    // Wait for all chunks to finish
    for (auto& f : futures) {
        f.waitForFinished();
    }

    // Check if any chunks failed
    if (failedChunks.loadAcquire() > 0) {
        LOG_ERROR("Download failed for task %s: %d chunk(s) failed", qPrintable(taskId), failedChunks.loadAcquire());

        // Clean up chunk files
        QDir(chunkDir).removeRecursively();

        // Clean up resume info
        if (m_resumeManager) {
            m_resumeManager->removeResumeInfo(taskId);
        }

        QMetaObject::invokeMethod(this, [this, taskId]() {
            if (m_tasks.contains(taskId)) {
                m_tasks[taskId].status = TransferTask::Failed;
                m_tasks[taskId].error = "Chunk download failed";
                emit taskFailed(taskId, m_tasks[taskId].error);

                // Log download failure to database
                if (m_transferLogService) {
                    const TransferTask& task = m_tasks[taskId];
                    m_transferLogService->logTransfer(
                        TransferLogEntry::DownloadLog, task.fileName, task.filePath,
                        task.fileSize, QString(), TransferLogEntry::Failed,
                        task.error, task.transferredSize, task.taskId);
                }
            }
        }, Qt::QueuedConnection);
        return;
    }

    // Merge chunks
    QString finalPath = savePath + "/" + QFileInfo(info.filePath).fileName();
    m_chunkManager->mergeChunks(chunkDir, finalPath, totalChunks);

    // Clean up resume info
    if (m_resumeManager) {
        m_resumeManager->removeResumeInfo(taskId);
    }

    QMetaObject::invokeMethod(this, [this, taskId]() {
        if (m_tasks.contains(taskId)) {
            m_tasks[taskId].status = TransferTask::Completed;
            m_tasks[taskId].completedAt = QDateTime::currentDateTime();
            emit taskCompleted(taskId);
        }
    }, Qt::QueuedConnection);
}

QString FileTransferEngine::startUpload(const QString& remotePath, const QString& localPath, int threads)
{
    QString taskId = QUuid::createUuid().toString();

    TransferTask task;
    task.taskId = taskId;
    task.type = TransferTask::Upload;
    task.status = TransferTask::Pending;
    task.filePath = localPath;
    task.savePath = remotePath;
    task.threads = threads;
    task.startedAt = QDateTime::currentDateTime();

    m_tasks[taskId] = task;

    LOG_INFO("Upload task started: %s", qPrintable(taskId));
    emit taskStarted(taskId);

    return taskId;
}

bool FileTransferEngine::pauseTask(const QString& taskId)
{
    if (m_tasks.contains(taskId)) {
        m_tasks[taskId].status = TransferTask::Paused;
        m_tasks[taskId].speed = 0;
        if (m_bandwidthManager) m_bandwidthManager->removeRecord(taskId);

        // Persist paused download tasks to database
        if (m_transferLogService && m_tasks[taskId].type == TransferTask::Download) {
            const TransferTask& task = m_tasks[taskId];
            m_transferLogService->logTransfer(
                TransferLogEntry::DownloadLog, task.fileName, task.filePath,
                task.fileSize, QString(), TransferLogEntry::Paused,
                QString(), task.transferredSize, task.taskId);
        }

        emit taskPaused(taskId);
        return true;
    }
    return false;
}

bool FileTransferEngine::resumeTask(const QString& taskId)
{
    if (m_tasks.contains(taskId)) {
        m_tasks[taskId].status = TransferTask::Downloading;
        emit taskResumed(taskId);
        return true;
    }
    return false;
}

bool FileTransferEngine::cancelTask(const QString& taskId)
{
    if (m_tasks.contains(taskId)) {
        m_tasks[taskId].status = TransferTask::Cancelled;
        m_tasks[taskId].speed = 0;
        if (m_bandwidthManager) m_bandwidthManager->removeRecord(taskId);
        emit taskCancelled(taskId);
        m_tasks.remove(taskId);
        return true;
    }
    return false;
}

bool FileTransferEngine::failTask(const QString& taskId, const QString& error)
{
    if (m_tasks.contains(taskId)) {
        m_tasks[taskId].status = TransferTask::Failed;
        m_tasks[taskId].error = error;
        m_tasks[taskId].speed = 0;
        if (m_bandwidthManager) m_bandwidthManager->removeRecord(taskId);
        emit taskFailed(taskId, error);
        return true;
    }
    return false;
}

TransferTask FileTransferEngine::getTaskInfo(const QString& taskId) const
{
    return m_tasks.value(taskId);
}

QVariantList FileTransferEngine::getAllTasks() const
{
    QVariantList result;
    for (const TransferTask& task : m_tasks) {
        result.append(QVariant::fromValue(task));
    }
    return result;
}

QVariantList FileTransferEngine::getActiveTasks() const
{
    QVariantList result;
    for (const TransferTask& task : m_tasks) {
        if (task.status == TransferTask::Downloading || task.status == TransferTask::Uploading || task.status == TransferTask::Pending) {
            result.append(QVariant::fromValue(task));
        }
    }
    return result;
}

int FileTransferEngine::getActiveTaskCount() const
{
    int count = 0;
    for (const TransferTask& task : m_tasks) {
        if (task.status == TransferTask::Downloading || task.status == TransferTask::Uploading) {
            ++count;
        }
    }
    return count;
}

qint64 FileTransferEngine::getTotalTransferredSize() const
{
    qint64 total = 0;
    for (const TransferTask& task : m_tasks) {
        total += task.transferredSize;
    }
    return total;
}

void FileTransferEngine::addCompletedTask(const TransferTask& task)
{
    m_tasks[task.taskId] = task;
    emit taskCompleted(task.taskId);
}

void FileTransferEngine::addUploadingTask(const TransferTask& task)
{
    m_tasks[task.taskId] = task;
    emit taskStarted(task.taskId);
}

void FileTransferEngine::updateTaskProgress(const QString& taskId, qint64 transferredSize)
{
    if (!m_tasks.contains(taskId)) return;
    auto& t = m_tasks[taskId];
    // Don't update progress on tasks that are no longer active
    if (t.status != TransferTask::Uploading && t.status != TransferTask::Downloading
        && t.status != TransferTask::Pending && t.status != TransferTask::Preparing) {
        return;
    }
    qint64 delta = transferredSize - t.transferredSize;
    if (delta > 0 && m_bandwidthManager) {
        m_bandwidthManager->recordTransfer(taskId, static_cast<int>(delta));
    }
    t.transferredSize = transferredSize;
    t.progress = t.fileSize > 0 ? static_cast<int>((transferredSize * 100) / t.fileSize) : 0;
    int speed = m_bandwidthManager ? m_bandwidthManager->currentSpeed(taskId) : 0;
    t.speed = speed;
    emit taskProgress(taskId, t.progress, speed);
}

void FileTransferEngine::completeTask(const QString& taskId)
{
    if (!m_tasks.contains(taskId)) return;
    auto& t = m_tasks[taskId];
    t.status = TransferTask::Completed;
    t.transferredSize = t.fileSize;
    t.progress = 100;
    t.speed = 0;
    t.completedAt = QDateTime::currentDateTime();
    if (m_bandwidthManager) m_bandwidthManager->removeRecord(taskId);
    emit taskCompleted(taskId);
}

void FileTransferEngine::completeTaskByName(const QString& fileName, int type)
{
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        if (it.value().fileName == fileName && it.value().type == type
            && (it.value().status == TransferTask::Uploading || it.value().status == TransferTask::Downloading)) {
            it.value().status = TransferTask::Completed;
            it.value().transferredSize = it.value().fileSize;
            it.value().progress = 100;
            it.value().speed = 0;
            it.value().completedAt = QDateTime::currentDateTime();
            if (m_bandwidthManager) m_bandwidthManager->removeRecord(it.key());
            emit taskCompleted(it.key());
            return;
        }
    }
}

void FileTransferEngine::removeFailedUploadTasksByName(const QString& fileName)
{
    // Remove failed upload tasks with the same fileName to avoid duplicates
    // when retry creates a new task via /api/upload/check
    QStringList toRemove;
    for (auto it = m_tasks.constBegin(); it != m_tasks.constEnd(); ++it) {
        if (it.value().type == TransferTask::Upload
            && it.value().status == TransferTask::Failed
            && it.value().fileName == fileName) {
            toRemove.append(it.key());
        }
    }
    for (const QString& taskId : toRemove) {
        m_tasks.remove(taskId);
        if (m_bandwidthManager) m_bandwidthManager->removeRecord(taskId);
    }
}
