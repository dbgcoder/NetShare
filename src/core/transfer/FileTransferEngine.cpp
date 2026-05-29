#include "FileTransferEngine.h"
#include "Logger.h"
#include "ShareManager.h"
#include "ChunkManager.h"
#include "ChunkStateManager.h"
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
                               const QString& chunkPath, int chunkIndex,
                               QObject* parent)
    : QObject(parent), m_url(url), m_offset(offset), m_length(length),
      m_chunkPath(chunkPath), m_chunkIndex(chunkIndex), m_file(nullptr),
      m_networkManager(new QNetworkAccessManager(this))
{
}

void TransferWorker::setResumeOffset(qint64 offset)
{
    m_resumeOffset = offset;
}

void TransferWorker::start()
{
    emit chunkStarted(m_chunkIndex);

    qint64 startOffset = m_offset + m_resumeOffset;
    qint64 endOffset = m_offset + m_length - 1;

    if (m_resumeOffset > 0) {
        m_file = new QFile(m_chunkPath);
        if (!m_file->open(QIODevice::WriteOnly | QIODevice::Append)) {
            emit chunkFinished(m_chunkIndex, false);
            deleteLater();
            return;
        }
    } else {
        m_file = new QFile(m_chunkPath);
        if (!m_file->open(QIODevice::WriteOnly)) {
            emit chunkFinished(m_chunkIndex, false);
            deleteLater();
            return;
        }
    }

    QNetworkRequest request(m_url);
    QString rangeHeader = QString("bytes=%1-%2").arg(startOffset).arg(endOffset);
    request.setRawHeader("Range", rangeHeader.toUtf8());

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
        emit chunkProgress(m_chunkIndex, data.size());
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
    emit chunkFinished(m_chunkIndex, success);
    deleteLater();
}

FileTransferEngine::FileTransferEngine(QObject* parent)
    : QObject(parent), m_mergeWatcher(new QFutureWatcher<void>(this)),
      m_shareManager(nullptr), m_chunkManager(nullptr), 
      m_chunkStateManager(nullptr), m_bandwidthManager(nullptr)
{
    m_tempDirectory = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/NetShare";
    QDir().mkpath(m_tempDirectory);

    m_speedCheckTimer = new QTimer(this);
    connect(m_speedCheckTimer, &QTimer::timeout, this, [this]() {
        qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
            auto& task = it.value();
            if (task.status != TransferTask::Uploading && task.status != TransferTask::Downloading)
                continue;

            auto lastIt = m_lastProgressTime.find(it.key());
            if (lastIt == m_lastProgressTime.end()) continue;

            qint64 elapsed = nowMs - lastIt.value();

            if (elapsed > 180000) {
                task.status = TransferTask::Paused;
                task.speed = 0;
                m_speedHistory.remove(it.key());
                if (m_bandwidthManager) m_bandwidthManager->removeRecord(it.key());

                if (m_transferLogService) {
                    int logType = (task.type == TransferTask::Upload) ? TransferLogEntry::UploadLog : TransferLogEntry::DownloadLog;
                    m_transferLogService->logTransfer(logType, task.fileName, task.filePath,
                        task.fileSize, QString(), TransferLogEntry::Paused,
                        QString(), task.transferredSize, task.taskId);
                }

                emit taskPaused(it.key());
                LOG_INFO("Task %s auto-paused after 3min inactivity", qPrintable(it.key()));
            } else if (elapsed > 3000 && task.speed > 0) {
                task.speed = 0;
                m_speedHistory.remove(it.key());
                emit taskProgress(it.key(), task.progress, 0);
            }
        }
    });
    m_speedCheckTimer->start(30000);
}

void FileTransferEngine::setManagers(ShareManager* sm, ChunkManager* cm, ChunkStateManager* csm, BandwidthManager* bm)
{
    m_shareManager = sm;
    m_chunkManager = cm;
    m_chunkStateManager = csm;
    m_bandwidthManager = bm;

    if (m_bandwidthManager) {
        connect(m_bandwidthManager, &BandwidthManager::bandwidthUpdated, this, [this](const QString& taskId, int speed) {
            if (m_tasks.contains(taskId)) {
                m_tasks[taskId].speed = speed;
            }
        });
    }
}

ChunkStateManager* FileTransferEngine::chunkStateManager() const
{
    return m_chunkStateManager;
}

void FileTransferEngine::setTransferLogService(TransferLogService* tls)
{
    m_transferLogService = tls;
}

void FileTransferEngine::setUploadDir(const QString& dir)
{
    m_uploadDir = dir;

    if (m_chunkStateManager && !m_uploadDir.isEmpty()) {
        QString uploadChunksDir = m_uploadDir + "/.chunks";
        m_chunkStateManager->cleanupExpired(uploadChunksDir, 7);

        QList<ChunkStateInfo> resumable = m_chunkStateManager->scanResumableTasks(uploadChunksDir);
        for (const ChunkStateInfo& csi : resumable) {
            bool alreadyRestored = false;
            for (auto it = m_tasks.constBegin(); it != m_tasks.constEnd(); ++it) {
                if (it.value().fileName == csi.fileName) {
                    alreadyRestored = true;
                    break;
                }
            }
            if (alreadyRestored) continue;

            TransferTask task;
            task.taskId = csi.taskId.isEmpty() ? QUuid::createUuid().toString() : csi.taskId;
            task.type = (csi.type == QStringLiteral("upload")) ? TransferTask::Upload : TransferTask::Download;
            task.fileName = csi.fileName;
            task.filePath = csi.savePath;
            task.fileSize = csi.fileSize;
            task.transferredSize = csi.transferredSize;
            task.progress = task.fileSize > 0 ? static_cast<int>((task.transferredSize * 100) / task.fileSize) : 0;
            task.status = TransferTask::Paused;
            task.startedAt = !csi.createdAt.isEmpty() ? QDateTime::fromString(csi.createdAt, Qt::ISODate) : QDateTime::currentDateTime();

            m_tasks[task.taskId] = task;
            m_lastProgressTime[task.taskId] = QDateTime::currentMSecsSinceEpoch();
            LOG_INFO("Restored upload resumable task from state file: %s (%s)",
                     qPrintable(task.taskId), qPrintable(task.fileName));
        }
    }
}

QString FileTransferEngine::stateFilePathForTask(const QString& fileName, int taskType) const
{
    if (taskType == TransferTask::Upload && !m_uploadDir.isEmpty()) {
        return m_uploadDir + "/.chunks/" + fileName + ".netshare";
    }
    return m_tempDirectory + "/" + fileName + ".netshare";
}

void FileTransferEngine::setUploadPauseCallback(std::function<void(const QString&)> cb)
{
    m_uploadPauseCallback = cb;
}

void FileTransferEngine::setUploadResumeCallback(std::function<void(const QString&)> cb)
{
    m_uploadResumeCallback = cb;
}

FileTransferEngine::~FileTransferEngine() = default;

bool FileTransferEngine::initialize()
{
    if (m_transferLogService) {
        QVariantList restorable = m_transferLogService->restorableLogs();
        for (const QVariant& v : restorable) {
            TransferLogEntry entry = v.value<TransferLogEntry>();
            if (entry.fileName.isEmpty() && entry.fileSize <= 0) continue;

            bool alreadyRestored = false;
            for (auto it = m_tasks.constBegin(); it != m_tasks.constEnd(); ++it) {
                if (it.value().fileName == entry.fileName && it.value().type == entry.type) {
                    alreadyRestored = true;
                    break;
                }
            }
            if (alreadyRestored) continue;

            TransferTask task;
            task.taskId = entry.taskId;
            task.type = (entry.type == TransferLogEntry::UploadLog) ? TransferTask::Upload : TransferTask::Download;
            task.fileName = entry.fileName;
            task.filePath = entry.filePath;
            task.fileSize = entry.fileSize;
            task.transferredSize = entry.transferredSize;
            task.progress = task.fileSize > 0 ? static_cast<int>((task.transferredSize * 100) / task.fileSize) : 0;
            task.startedAt = entry.timestamp.isValid() ? entry.timestamp : QDateTime::currentDateTime();

            if (entry.status == TransferLogEntry::Paused) {
                task.status = TransferTask::Paused;
            } else {
                task.status = (task.type == TransferTask::Upload) ? TransferTask::Uploading : TransferTask::Downloading;
            }

            if (!task.taskId.isEmpty()) {
                m_tasks[task.taskId] = task;
                m_lastProgressTime[task.taskId] = QDateTime::currentMSecsSinceEpoch();
                LOG_INFO("Restored task: %s (%s) type=%d status=%d",
                         qPrintable(task.taskId), qPrintable(task.fileName), task.type, task.status);
            }
        }
    }

    if (m_chunkStateManager && !m_tempDirectory.isEmpty()) {
        m_chunkStateManager->cleanupExpired(m_tempDirectory, 7);

        QList<ChunkStateInfo> resumable = m_chunkStateManager->scanResumableTasks(m_tempDirectory);

        if (!m_uploadDir.isEmpty()) {
            QString uploadChunksDir = m_uploadDir + "/.chunks";
            m_chunkStateManager->cleanupExpired(uploadChunksDir, 7);
            QList<ChunkStateInfo> uploadResumable = m_chunkStateManager->scanResumableTasks(uploadChunksDir);
            resumable.append(uploadResumable);
        }

        for (const ChunkStateInfo& csi : resumable) {
            bool alreadyRestored = false;
            for (auto it = m_tasks.constBegin(); it != m_tasks.constEnd(); ++it) {
                if (it.value().fileName == csi.fileName) {
                    alreadyRestored = true;
                    break;
                }
            }
            if (alreadyRestored) continue;

            TransferTask task;
            task.taskId = csi.taskId.isEmpty() ? QUuid::createUuid().toString() : csi.taskId;
            task.type = (csi.type == QStringLiteral("upload")) ? TransferTask::Upload : TransferTask::Download;
            task.fileName = csi.fileName;
            task.filePath = csi.savePath;
            task.fileSize = csi.fileSize;
            task.transferredSize = csi.transferredSize;
            task.progress = task.fileSize > 0 ? static_cast<int>((task.transferredSize * 100) / task.fileSize) : 0;
            task.status = TransferTask::Paused;
            task.startedAt = !csi.createdAt.isEmpty() ? QDateTime::fromString(csi.createdAt, Qt::ISODate) : QDateTime::currentDateTime();

            m_tasks[task.taskId] = task;
            m_lastProgressTime[task.taskId] = QDateTime::currentMSecsSinceEpoch();
            LOG_INFO("Restored resumable task from state file: %s (%s) type=%d",
                     qPrintable(task.taskId), qPrintable(task.fileName), task.type);
        }
    }

    LOG_INFO("FileTransferEngine initialized");
    return true;
}

void FileTransferEngine::stopAllTasks()
{
    if (m_transferLogService) {
        for (auto it = m_tasks.constBegin(); it != m_tasks.constEnd(); ++it) {
            const TransferTask& task = it.value();
            int logType = (task.type == TransferTask::Upload) ? TransferLogEntry::UploadLog : TransferLogEntry::DownloadLog;

            if (task.status == TransferTask::Paused) {
                m_transferLogService->logTransfer(logType, task.fileName, task.filePath,
                    task.fileSize, QString(), TransferLogEntry::Paused,
                    QString(), task.transferredSize, task.taskId);
                LOG_INFO("Persisted paused task: %s", qPrintable(task.taskId));
            } else if (task.status == TransferTask::Uploading || task.status == TransferTask::Downloading) {
                m_transferLogService->logTransfer(logType, task.fileName, task.filePath,
                    task.fileSize, QString(), TransferLogEntry::Started,
                    QString(), task.transferredSize, task.taskId);
                LOG_INFO("Persisted in-progress task: %s", qPrintable(task.taskId));
            }
        }
    }

    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        if (it.value().status == TransferTask::Downloading || it.value().status == TransferTask::Uploading) {
            if (m_chunkStateManager && !it.value().fileName.isEmpty()) {
                QString stateFilePath = stateFilePathForTask(it.value().fileName, it.value().type);
                if (QFile::exists(stateFilePath)) {
                    m_chunkStateManager->markDownloadingChunksPartial(stateFilePath);
                }
            }
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

    qint64 fileSize = QFileInfo(info.filePath).size();
    QString chunkDir = m_tempDirectory + "/" + taskId;
    QDir().mkpath(chunkDir);

    QVariantList chunks = m_chunkManager->splitFileForThreads(fileSize, threads);
    int totalChunks = chunks.size();

    QString fileName;
    if (m_tasks.contains(taskId)) {
        fileName = m_tasks[taskId].fileName;
    }
    if (fileName.isEmpty()) {
        fileName = QFileInfo(info.filePath).fileName();
    }

    QString stateFilePath = m_tempDirectory + "/" + fileName + ".netshare";
    ChunkStateInfo stateInfo;
    bool hasResume = false;

    if (m_chunkStateManager) {
        ChunkStateInfo existingInfo;
        if (m_chunkStateManager->loadStateFile(stateFilePath, existingInfo)) {
            if (existingInfo.fileSize == fileSize) {
                hasResume = true;
                m_chunkStateManager->validateCompletedChunks(stateFilePath);

                QString oldChunkDir = existingInfo.chunkDir;
                existingInfo.taskId = taskId;
                existingInfo.status = QStringLiteral("downloading");
                existingInfo.chunkDir = chunkDir;

                if (!oldChunkDir.isEmpty() && oldChunkDir != chunkDir) {
                    QDir().mkpath(chunkDir);
                    for (ChunkState& cs : existingInfo.chunks) {
                        if (cs.status != QStringLiteral("completed")) continue;
                        QString srcPath = oldChunkDir + QString("/chunk_%1").arg(cs.index, 6, 10, QChar('0'));
                        QString dstPath = chunkDir + QString("/chunk_%1").arg(cs.index, 6, 10, QChar('0'));
                        if (QFile::exists(srcPath) && !QFile::exists(dstPath)) {
                            QFile::rename(srcPath, dstPath);
                        }
                    }
                    if (QDir(oldChunkDir).exists()) {
                        bool dirEmpty = true;
                        for (const auto& entry : QDir(oldChunkDir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
                            Q_UNUSED(entry)
                            dirEmpty = false;
                            break;
                        }
                        if (dirEmpty) {
                            QDir(oldChunkDir).removeRecursively();
                        }
                    }
                }

                qint64 recalculatedTransferred = 0;
                for (const ChunkState& cs : existingInfo.chunks) {
                    if (cs.status == QStringLiteral("completed")) {
                        recalculatedTransferred += cs.size;
                    } else if (cs.status == QStringLiteral("partial")) {
                        recalculatedTransferred += cs.downloaded;
                    }
                }
                existingInfo.transferredSize = recalculatedTransferred;

                stateInfo = existingInfo;
                m_chunkStateManager->saveStateFile(stateFilePath, stateInfo);
            } else {
                m_chunkStateManager->deleteStateFile(stateFilePath);
            }
        }

        if (!hasResume) {
            stateInfo.version = 1;
            stateInfo.taskId = taskId;
            stateInfo.type = QStringLiteral("download");
            stateInfo.fileName = fileName;
            stateInfo.fileSize = fileSize;
            stateInfo.chunkSize = m_chunkManager->calculateChunkSize(fileSize, threads);
            stateInfo.totalChunks = totalChunks;
            stateInfo.transferredSize = 0;
            stateInfo.status = QStringLiteral("downloading");
            stateInfo.url = QString("http://%1:8080/download/%2/").arg(m_shareManager->localIp(), info.token);
            stateInfo.savePath = savePath;
            stateInfo.chunkDir = chunkDir;

            stateInfo.chunks.clear();
            for (const QVariant& v : chunks) {
                ChunkState cs = v.value<ChunkState>();
                stateInfo.chunks.append(cs);
            }

            m_chunkStateManager->createStateFile(stateFilePath, stateInfo);
        }
    }

    QAtomicInt failedChunks(0);
    QList<QFuture<void>> futures;
    for (const QVariant& v : chunks) {
        ChunkState chunk = v.value<ChunkState>();
        QString chunkPath = chunkDir + QString("/chunk_%1").arg(chunk.index, 6, 10, QChar('0'));

        if (hasResume && m_chunkStateManager) {
            ChunkStateInfo currentInfo;
            if (m_chunkStateManager->loadStateFile(stateFilePath, currentInfo)) {
                if (chunk.index < currentInfo.chunks.size()) {
                    const ChunkState& cs = currentInfo.chunks[chunk.index];

                    if (cs.status == QStringLiteral("completed")) {
                        QFileInfo fi(chunkPath);
                        if (fi.exists() && fi.size() == chunk.size) {
                            LOG_INFO("Skipping complete chunk %d for task %s", chunk.index, qPrintable(taskId));
                            continue;
                        }
                    }

                    if (cs.status == QStringLiteral("failed")) {
                        QString failedPath = chunkPath;
                        if (QFile::exists(failedPath)) {
                            QFile::remove(failedPath);
                            LOG_INFO("Removing failed chunk %d for task %s", chunk.index, qPrintable(taskId));
                        }
                    }

                    if (cs.status == QStringLiteral("downloading")) {
                        QFileInfo fi(chunkPath);
                        if (fi.exists() && fi.size() > 0 && fi.size() < chunk.size) {
                            LOG_INFO("Downloading chunk %d treated as partial (%lld/%lld bytes) for task %s",
                                     chunk.index, fi.size(), chunk.size, qPrintable(taskId));
                        } else if (fi.exists()) {
                            QFile::remove(chunkPath);
                            LOG_INFO("Removing invalid downloading chunk %d for task %s", chunk.index, qPrintable(taskId));
                        }
                    }
                }
            }
        }

        QString url = QString("http://%1:8080/download/%2/").arg(m_shareManager->localIp(), info.token);

        TransferWorker* worker = new TransferWorker(url, chunk.offset, chunk.size, chunkPath, chunk.index);

        if (hasResume && m_chunkStateManager) {
            qint64 resumeOffset = 0;
            ChunkStateInfo currentInfo;
            if (m_chunkStateManager->loadStateFile(stateFilePath, currentInfo)) {
                if (chunk.index < currentInfo.chunks.size()) {
                    const ChunkState& cs = currentInfo.chunks[chunk.index];
                    if (cs.status == QStringLiteral("partial") || cs.status == QStringLiteral("downloading")) {
                        resumeOffset = cs.downloaded;
                    }
                }
            }
            if (resumeOffset <= 0) {
                QFileInfo fi(chunkPath);
                if (fi.exists() && fi.size() > 0 && fi.size() < chunk.size) {
                    resumeOffset = fi.size();
                }
            }
            if (resumeOffset > 0 && resumeOffset < chunk.size) {
                worker->setResumeOffset(resumeOffset);
            }
        }

        QString capturedStateFilePath = stateFilePath;
        connect(worker, &TransferWorker::chunkStarted, this, [this, capturedStateFilePath](int chunkIndex) {
            if (m_chunkStateManager) {
                m_chunkStateManager->updateChunkStatus(capturedStateFilePath, chunkIndex, QStringLiteral("downloading"), 0);
            }
        });
        connect(worker, &TransferWorker::chunkProgress, this, [this, taskId](int, qint64 bytes) {
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

        qint64 capturedChunkSize = chunk.size;
        QFuture<void> future = QtConcurrent::run([worker, &failedChunks, this, capturedStateFilePath, capturedChunkSize]() {
            QEventLoop loop;
            QObject::connect(worker, &TransferWorker::chunkFinished, &loop,
                [&loop, &failedChunks, this, capturedStateFilePath, capturedChunkSize](int chunkIndex, bool success) {
                    if (success) {
                        if (m_chunkStateManager && !capturedStateFilePath.isEmpty()) {
                            m_chunkStateManager->updateChunkStatus(capturedStateFilePath, chunkIndex,
                                QStringLiteral("completed"), capturedChunkSize);
                        }
                        loop.quit();
                    } else {
                        failedChunks.fetchAndAddRelaxed(1);
                        if (m_chunkStateManager && !capturedStateFilePath.isEmpty()) {
                            m_chunkStateManager->updateChunkStatus(capturedStateFilePath, chunkIndex,
                                QStringLiteral("failed"), 0);
                        }
                        loop.exit(1);
                    }
                });
            worker->start();
            loop.exec();
        });
        futures.append(future);
    }

    for (auto& f : futures) {
        f.waitForFinished();
    }

    if (failedChunks.loadAcquire() > 0) {
        LOG_ERROR("Download failed for task %s: %d chunk(s) failed", qPrintable(taskId), failedChunks.loadAcquire());

        QDir(chunkDir).removeRecursively();

        if (m_chunkStateManager && !stateFilePath.isEmpty()) {
            m_chunkStateManager->deleteStateFile(stateFilePath);
        }

        QMetaObject::invokeMethod(this, [this, taskId]() {
            if (m_tasks.contains(taskId)) {
                m_tasks[taskId].status = TransferTask::Failed;
                m_tasks[taskId].error = "Chunk download failed";
                emit taskFailed(taskId, m_tasks[taskId].error);

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

    if (m_chunkStateManager && !stateFilePath.isEmpty()) {
        m_chunkStateManager->deleteStateFile(stateFilePath);
    }
    QDir(chunkDir).removeRecursively();

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

        if (m_chunkStateManager) {
            QString stateFilePath = stateFilePathForTask(m_tasks[taskId].fileName, m_tasks[taskId].type);
            if (QFile::exists(stateFilePath)) {
                m_chunkStateManager->markDownloadingChunksPartial(stateFilePath);
            }
        }

        if (m_uploadPauseCallback && m_tasks[taskId].type == TransferTask::Upload)
            m_uploadPauseCallback(taskId);

        if (m_transferLogService) {
            const TransferTask& task = m_tasks[taskId];
            int logType = (task.type == TransferTask::Upload) ? TransferLogEntry::UploadLog : TransferLogEntry::DownloadLog;
            m_transferLogService->logTransfer(logType, task.fileName, task.filePath,
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
        m_tasks[taskId].status = (m_tasks[taskId].type == TransferTask::Upload) ? TransferTask::Uploading : TransferTask::Downloading;
        m_tasks[taskId].error.clear();
        m_lastProgressTime[taskId] = QDateTime::currentMSecsSinceEpoch();

        if (m_chunkStateManager) {
            QString stateFilePath = stateFilePathForTask(m_tasks[taskId].fileName, m_tasks[taskId].type);
            if (QFile::exists(stateFilePath)) {
                QString resumeStatus = (m_tasks[taskId].type == TransferTask::Upload)
                    ? QStringLiteral("uploading") : QStringLiteral("downloading");
                m_chunkStateManager->updateTaskStatus(stateFilePath, resumeStatus);
            }
        }

        if (m_uploadResumeCallback && m_tasks[taskId].type == TransferTask::Upload)
            m_uploadResumeCallback(taskId);

        if (m_transferLogService) {
            const TransferTask& task = m_tasks[taskId];
            int logType = (task.type == TransferTask::Upload) ? TransferLogEntry::UploadLog : TransferLogEntry::DownloadLog;
            m_transferLogService->logTransfer(logType, task.fileName, task.filePath,
                task.fileSize, QString(), TransferLogEntry::Started,
                QString(), task.transferredSize, task.taskId);
        }

        emit taskResumed(taskId);
        return true;
    }
    return false;
}

bool FileTransferEngine::cancelTask(const QString& taskId)
{
    if (m_tasks.contains(taskId)) {
        if (m_transferLogService) {
            const TransferTask& task = m_tasks[taskId];
            int logType = (task.type == TransferTask::Upload) ? TransferLogEntry::UploadLog : TransferLogEntry::DownloadLog;
            m_transferLogService->logTransfer(logType, task.fileName, task.filePath,
                task.fileSize, QString(), TransferLogEntry::Cancelled,
                QString(), task.transferredSize, task.taskId);
        }

        m_tasks[taskId].status = TransferTask::Cancelled;
        m_tasks[taskId].speed = 0;
        m_speedHistory.remove(taskId);
        m_lastProgressTime.remove(taskId);
        if (m_bandwidthManager) m_bandwidthManager->removeRecord(taskId);

        if (m_chunkStateManager) {
            QString stateFilePath = stateFilePathForTask(m_tasks[taskId].fileName, m_tasks[taskId].type);
            if (QFile::exists(stateFilePath)) {
                m_chunkStateManager->deleteStateFile(stateFilePath);
            }
        }

        QString chunkDir = m_tempDirectory + "/" + taskId;
        if (QDir(chunkDir).exists()) {
            QDir(chunkDir).removeRecursively();
        }

        emit taskCancelled(taskId);
        m_tasks.remove(taskId);
        return true;
    }
    return false;
}

bool FileTransferEngine::deleteTask(const QString& taskId)
{
    QString fileName;
    int taskType = -1;

    if (m_tasks.contains(taskId)) {
        fileName = m_tasks[taskId].fileName;
        taskType = m_tasks[taskId].type;
    }

    if (m_transferLogService && (fileName.isEmpty() || taskType < 0)) {
        QVariantList logs = m_transferLogService->queryLogs(500, 0);
        for (const QVariant& v : logs) {
            TransferLogEntry entry = v.value<TransferLogEntry>();
            QString logTaskId = entry.taskId.isEmpty() ? entry.id : entry.taskId;
            if (logTaskId == taskId) {
                fileName = entry.fileName;
                taskType = (entry.type == TransferLogEntry::UploadLog) ? TransferTask::Upload : TransferTask::Download;
                break;
            }
        }
    }

    if (!fileName.isEmpty() && taskType >= 0) {
        QStringList toRemove;
        for (auto it = m_tasks.constBegin(); it != m_tasks.constEnd(); ++it) {
            if (it.value().fileName == fileName && it.value().type == taskType) {
                toRemove.append(it.key());
            }
        }
        for (const QString& tid : toRemove) {
            m_tasks.remove(tid);
            m_speedHistory.remove(tid);
            m_lastProgressTime.remove(tid);
            if (m_bandwidthManager) m_bandwidthManager->removeRecord(tid);
        }
    } else {
        m_tasks.remove(taskId);
        m_speedHistory.remove(taskId);
        m_lastProgressTime.remove(taskId);
        if (m_bandwidthManager) m_bandwidthManager->removeRecord(taskId);
    }

    if (m_chunkStateManager && !fileName.isEmpty()) {
        int effectiveType = taskType >= 0 ? taskType : TransferTask::Download;
        QString stateFilePath = stateFilePathForTask(fileName, effectiveType);
        if (QFile::exists(stateFilePath)) {
            m_chunkStateManager->deleteStateFile(stateFilePath);
        }
    }

    if (!fileName.isEmpty()) {
        QString chunkDir = m_tempDirectory + "/" + taskId;
        if (QDir(chunkDir).exists()) {
            QDir(chunkDir).removeRecursively();
        }
    }

    if (m_transferLogService) {
        if (!fileName.isEmpty() && taskType >= 0) {
            int logType = (taskType == TransferTask::Upload) ? TransferLogEntry::UploadLog : TransferLogEntry::DownloadLog;
            m_transferLogService->deleteLogsByFileName(fileName, logType);
        } else {
            m_transferLogService->deleteLogByTaskId(taskId);
        }
    }

    emit taskDeleted(taskId);
    return true;
}

bool FileTransferEngine::failTask(const QString& taskId, const QString& error)
{
    if (m_tasks.contains(taskId)) {
        m_tasks[taskId].status = TransferTask::Failed;
        m_tasks[taskId].error = error;
        m_tasks[taskId].speed = 0;
        m_speedHistory.remove(taskId);
        m_lastProgressTime.remove(taskId);
        if (m_bandwidthManager) m_bandwidthManager->removeRecord(taskId);

        if (m_transferLogService) {
            const TransferTask& task = m_tasks[taskId];
            int logType = (task.type == TransferTask::Upload) ? TransferLogEntry::UploadLog : TransferLogEntry::DownloadLog;
            m_transferLogService->logTransfer(logType, task.fileName, task.filePath,
                task.fileSize, QString(), TransferLogEntry::Failed,
                error, task.transferredSize, task.taskId);
        }

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
    m_lastProgressTime[task.taskId] = QDateTime::currentMSecsSinceEpoch();
    emit taskStarted(task.taskId);
}

void FileTransferEngine::addDownloadTask(const TransferTask& task)
{
    m_tasks[task.taskId] = task;
    m_lastProgressTime[task.taskId] = QDateTime::currentMSecsSinceEpoch();
    emit taskStarted(task.taskId);
}

QString FileTransferEngine::resumeOrCreateDownloadTask(const QString& fileName, const QString& filePath, qint64 fileSize, qint64 startByte)
{
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        if (it.value().fileName == fileName && it.value().type == TransferTask::Download) {
            QString existingTaskId = it.key();
            int status = it.value().status;

            if (status == TransferTask::Downloading || status == TransferTask::Uploading) {
                return existingTaskId;
            }

            if (status == TransferTask::Failed || status == TransferTask::Paused || status == TransferTask::Cancelled) {
                auto& t = it.value();
                t.status = TransferTask::Downloading;
                t.transferredSize = startByte;
                t.progress = fileSize > 0 ? static_cast<int>((startByte * 100) / fileSize) : 0;
                t.speed = 0;
                t.error.clear();
                t.startedAt = QDateTime::currentDateTime();
                m_speedHistory.remove(existingTaskId);
                m_lastProgressTime[existingTaskId] = QDateTime::currentMSecsSinceEpoch();
                if (m_bandwidthManager) m_bandwidthManager->removeRecord(existingTaskId);

                if (m_transferLogService) {
                    m_transferLogService->deleteLogsByFileName(fileName, TransferLogEntry::DownloadLog);
                }

                emit taskResumed(existingTaskId);
                return existingTaskId;
            }

            if (status == TransferTask::Completed) {
                m_tasks.remove(existingTaskId);
                m_speedHistory.remove(existingTaskId);
                m_lastProgressTime.remove(existingTaskId);
                if (m_bandwidthManager) m_bandwidthManager->removeRecord(existingTaskId);

                if (m_transferLogService) {
                    m_transferLogService->deleteLogsByFileName(fileName, TransferLogEntry::DownloadLog);
                }

                emit taskDeleted(existingTaskId);
                break;
            }

            break;
        }
    }

    TransferTask task;
    task.taskId = QUuid::createUuid().toString();
    task.type = TransferTask::Download;
    task.status = TransferTask::Downloading;
    task.fileName = fileName;
    task.filePath = filePath;
    task.fileSize = fileSize;
    task.transferredSize = startByte;
    task.progress = fileSize > 0 ? static_cast<int>((startByte * 100) / fileSize) : 0;
    task.speed = 0;
    task.startedAt = QDateTime::currentDateTime();
    m_tasks[task.taskId] = task;
    m_lastProgressTime[task.taskId] = QDateTime::currentMSecsSinceEpoch();
    emit taskStarted(task.taskId);
    return task.taskId;
}

void FileTransferEngine::updateTaskProgress(const QString& taskId, qint64 transferredSize)
{
    if (!m_tasks.contains(taskId)) return;
    auto& t = m_tasks[taskId];
    if (t.status != TransferTask::Uploading && t.status != TransferTask::Downloading
        && t.status != TransferTask::Pending && t.status != TransferTask::Preparing) {
        return;
    }

    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    m_lastProgressTime[taskId] = nowMs;
    auto& history = m_speedHistory[taskId];
    history.append({transferredSize, nowMs});
    while (history.size() > 10) history.removeFirst();

    int speed = 0;
    if (history.size() >= 2) {
        auto& oldest = history.first();
        qint64 dt = nowMs - oldest.timeMs;
        if (dt > 200) {
            speed = static_cast<int>((transferredSize - oldest.bytes) * 1000 / dt);
        }
    }

    t.transferredSize = transferredSize;
    t.progress = t.fileSize > 0 ? static_cast<int>((transferredSize * 100) / t.fileSize) : 0;
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
    m_speedHistory.remove(taskId);
    m_lastProgressTime.remove(taskId);
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
    QStringList toRemove;
    for (auto it = m_tasks.constBegin(); it != m_tasks.constEnd(); ++it) {
        if (it.value().type == TransferTask::Upload
            && it.value().fileName == fileName
            && (it.value().status == TransferTask::Failed
                || it.value().status == TransferTask::Uploading
                || it.value().status == TransferTask::Paused)) {
            toRemove.append(it.key());
        }
    }
    for (const QString& taskId : toRemove) {
        m_tasks.remove(taskId);
        if (m_bandwidthManager) m_bandwidthManager->removeRecord(taskId);
    }
    if (!toRemove.isEmpty()) {
        LOG_INFO("[TaskManager][remove.paused] file=%s, removed=%s",
                 qPrintable(fileName), qPrintable(toRemove.join(", ")));
    }
}

QString FileTransferEngine::findUploadTaskByName(const QString& fileName) const
{
    for (auto it = m_tasks.constBegin(); it != m_tasks.constEnd(); ++it) {
        if (it.value().type == TransferTask::Upload
            && it.value().fileName == fileName
            && it.value().status == TransferTask::Paused) {
            return it.key();
        }
    }
    return QString();
}
