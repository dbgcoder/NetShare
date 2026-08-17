#include "ChunkStateManager.h"
#include "Logger.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

ChunkStateManager::ChunkStateManager(QObject* parent)
    : QObject(parent)
{
}

bool ChunkStateManager::createStateFile(const QString& stateFilePath, const ChunkStateInfo& info)
{
    QMutexLocker locker(&m_mutex);
    Q_UNUSED(locker)

    QFileInfo fi(stateFilePath);
    QDir dir = fi.absoluteDir();
    if (!dir.exists()) {
        if (!dir.mkpath(QStringLiteral("."))) {
            LOG_ERROR("ChunkStateManager: cannot create directory: %s", qPrintable(dir.absolutePath()));
            return false;
        }
    }

    ChunkStateInfo writable = info;
    writable.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    writable.lastUpdated = writable.createdAt;

    return saveStateFileInternal(stateFilePath, writable);
}

bool ChunkStateManager::loadStateFile(const QString& stateFilePath, ChunkStateInfo& info)
{
    QMutexLocker locker(&m_mutex);
    Q_UNUSED(locker)
    return loadStateFileInternal(stateFilePath, info);
}

bool ChunkStateManager::saveStateFile(const QString& stateFilePath, const ChunkStateInfo& info)
{
    QMutexLocker locker(&m_mutex);
    Q_UNUSED(locker)
    return saveStateFileInternal(stateFilePath, info);
}

bool ChunkStateManager::deleteStateFile(const QString& stateFilePath)
{
    QMutexLocker locker(&m_mutex);
    Q_UNUSED(locker)
    return deleteStateFileInternal(stateFilePath);
}

bool ChunkStateManager::updateChunkStatus(const QString& stateFilePath, int chunkIndex,
                                           const QString& status, qint64 downloaded)
{
    QMutexLocker locker(&m_mutex);
    Q_UNUSED(locker)

    ChunkStateInfo info;
    if (!loadStateFileInternal(stateFilePath, info)) {
        return false;
    }

    if (chunkIndex < 0 || chunkIndex >= info.chunks.size()) {
        LOG_ERROR("ChunkStateManager: chunk index %d out of range (0-%d) in %s",
                  chunkIndex, info.chunks.size() - 1, qPrintable(stateFilePath));
        return false;
    }

    info.chunks[chunkIndex].status = status;
    info.chunks[chunkIndex].downloaded = downloaded;
    info.lastUpdated = QDateTime::currentDateTime().toString(Qt::ISODate);

    return saveStateFileInternal(stateFilePath, info);
}

bool ChunkStateManager::updateTaskStatus(const QString& stateFilePath, const QString& status)
{
    QMutexLocker locker(&m_mutex);
    Q_UNUSED(locker)

    ChunkStateInfo info;
    if (!loadStateFileInternal(stateFilePath, info)) {
        return false;
    }

    info.status = status;
    info.lastUpdated = QDateTime::currentDateTime().toString(Qt::ISODate);

    return saveStateFileInternal(stateFilePath, info);
}

bool ChunkStateManager::updateTransferredSize(const QString& stateFilePath, qint64 transferredSize)
{
    QMutexLocker locker(&m_mutex);
    Q_UNUSED(locker)

    ChunkStateInfo info;
    if (!loadStateFileInternal(stateFilePath, info)) {
        return false;
    }

    info.transferredSize = transferredSize;
    info.lastUpdated = QDateTime::currentDateTime().toString(Qt::ISODate);

    return saveStateFileInternal(stateFilePath, info);
}

bool ChunkStateManager::markDownloadingChunksPartial(const QString& stateFilePath)
{
    QMutexLocker locker(&m_mutex);
    Q_UNUSED(locker)

    ChunkStateInfo info;
    if (!loadStateFileInternal(stateFilePath, info)) {
        return false;
    }

    bool anyChanged = false;
    for (ChunkState& chunk : info.chunks) {
        if (chunk.status == QStringLiteral("downloading") || chunk.status == QStringLiteral("uploading")) {
            chunk.status = QStringLiteral("partial");
            anyChanged = true;
        }
    }

    if (anyChanged) {
        info.status = QStringLiteral("paused");
        info.lastUpdated = QDateTime::currentDateTime().toString(Qt::ISODate);
        return saveStateFileInternal(stateFilePath, info);
    }

    return true;
}

QList<ChunkStateInfo> ChunkStateManager::scanResumableTasks(const QString& directory)
{
    QMutexLocker locker(&m_mutex);
    Q_UNUSED(locker)

    QList<ChunkStateInfo> result;
    QDir dir(directory);
    if (!dir.exists()) return result;

    QStringList filters;
    filters << QStringLiteral("*.netshare");
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files);

    for (const QFileInfo& fi : files) {
        ChunkStateInfo info;
        if (loadStateFileInternal(fi.absoluteFilePath(), info)) {
            if (info.status == QStringLiteral("downloading") ||
                info.status == QStringLiteral("uploading") ||
                info.status == QStringLiteral("paused")) {
                result.append(info);
            } else if (info.status == QStringLiteral("completed")) {
                deleteStateFileInternal(fi.absoluteFilePath());
                if (!info.chunkDir.isEmpty()) {
                    QDir(info.chunkDir).removeRecursively();
                }
            }
        } else {
            QFile::remove(fi.absoluteFilePath());
        }
    }

    return result;
}

bool ChunkStateManager::validateCompletedChunks(const QString& stateFilePath)
{
    QMutexLocker locker(&m_mutex);
    Q_UNUSED(locker)

    ChunkStateInfo info;
    if (!loadStateFileInternal(stateFilePath, info)) {
        return false;
    }

    bool anyChanged = false;
    for (ChunkState& chunk : info.chunks) {
        if (chunk.status == QStringLiteral("pending")) continue;

        if (info.chunkDir.isEmpty()) {
            chunk.status = QStringLiteral("pending");
            chunk.downloaded = 0;
            anyChanged = true;
            continue;
        }

        QString chunkPath = info.chunkDir + QString("/chunk_%1").arg(chunk.index, 6, 10, QChar('0'));
        QFileInfo fi(chunkPath);

        if (chunk.status == QStringLiteral("completed")) {
            if (!fi.exists() || fi.size() != chunk.size) {
                LOG_WARN("ChunkStateManager: completed chunk %d validation failed (expected=%lld actual=%lld), degrading to pending",
                         chunk.index, chunk.size, fi.exists() ? fi.size() : 0);
                chunk.status = QStringLiteral("pending");
                chunk.downloaded = 0;
                if (fi.exists()) {
                    QFile::remove(chunkPath);
                }
                anyChanged = true;
            }
        } else if (chunk.status == QStringLiteral("partial") || chunk.status == QStringLiteral("uploading")) {
            if (!fi.exists() || fi.size() != chunk.size) {
                LOG_WARN("ChunkStateManager: %s chunk %d validation failed (expected=%lld actual=%lld), degrading to pending",
                         qPrintable(chunk.status), chunk.index, chunk.size, fi.exists() ? fi.size() : 0);
                chunk.status = QStringLiteral("pending");
                chunk.downloaded = 0;
                if (fi.exists()) {
                    QFile::remove(chunkPath);
                }
                anyChanged = true;
            } else {
                chunk.status = QStringLiteral("completed");
                chunk.downloaded = chunk.size;
                anyChanged = true;
            }
        }
    }

    if (anyChanged) {
        info.lastUpdated = QDateTime::currentDateTime().toString(Qt::ISODate);
        saveStateFileInternal(stateFilePath, info);
    }

    return !anyChanged;
}

void ChunkStateManager::cleanupExpired(const QString& directory, int maxAgeDays)
{
    QMutexLocker locker(&m_mutex);
    Q_UNUSED(locker)

    QDir dir(directory);
    if (!dir.exists()) return;

    QStringList filters;
    filters << QStringLiteral("*.netshare");
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files);

    int removed = 0;
    QDateTime threshold = QDateTime::currentDateTime().addDays(-maxAgeDays);

    for (const QFileInfo& fi : files) {
        if (fi.lastModified() < threshold) {
            ChunkStateInfo info;
            if (loadStateFileInternal(fi.absoluteFilePath(), info) && !info.chunkDir.isEmpty()) {
                QDir(info.chunkDir).removeRecursively();
            }
            QFile::remove(fi.absoluteFilePath());
            ++removed;
        }
    }

    if (removed > 0) {
        LOG_INFO("ChunkStateManager: cleaned up %d expired state files in %s", removed, qPrintable(directory));
    }
}

bool ChunkStateManager::loadStateFileInternal(const QString& stateFilePath, ChunkStateInfo& info)
{
    QFile file(stateFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_WARN("ChunkStateManager: cannot open state file: %s", qPrintable(stateFilePath));
        return false;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();

    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        LOG_ERROR("ChunkStateManager: invalid JSON in state file: %s - %s",
                  qPrintable(stateFilePath), qPrintable(err.errorString()));
        return false;
    }

    QJsonObject root = doc.object();

    info.version = root[QStringLiteral("version")].toInt(1);
    if (info.version != 1) {
        LOG_ERROR("ChunkStateManager: unsupported version %d in state file: %s",
                  info.version, qPrintable(stateFilePath));
        return false;
    }

    info.taskId = root[QStringLiteral("taskId")].toString();
    info.type = root[QStringLiteral("type")].toString();
    info.fileName = root[QStringLiteral("fileName")].toString();
    info.fileSize = root[QStringLiteral("fileSize")].toVariant().toLongLong();
    info.chunkSize = root[QStringLiteral("chunkSize")].toInt();
    info.totalChunks = root[QStringLiteral("totalChunks")].toInt();
    info.transferredSize = root[QStringLiteral("transferredSize")].toVariant().toLongLong();
    info.status = root[QStringLiteral("status")].toString();
    info.url = root[QStringLiteral("url")].toString();
    info.savePath = root[QStringLiteral("savePath")].toString();
    info.remoteAddress = root[QStringLiteral("remoteAddress")].toString();
    info.chunkDir = root[QStringLiteral("chunkDir")].toString();
    info.createdAt = root[QStringLiteral("createdAt")].toString();
    info.lastUpdated = root[QStringLiteral("lastUpdated")].toString();

    info.chunks.clear();
    QJsonArray chunksArr = root[QStringLiteral("chunks")].toArray();
    for (const QJsonValue& val : chunksArr) {
        QJsonObject obj = val.toObject();
        ChunkState chunk;
        chunk.index = obj[QStringLiteral("index")].toInt();
        chunk.offset = obj[QStringLiteral("offset")].toVariant().toLongLong();
        chunk.size = obj[QStringLiteral("size")].toVariant().toLongLong();
        chunk.status = obj[QStringLiteral("status")].toString(QStringLiteral("pending"));
        chunk.downloaded = obj[QStringLiteral("downloaded")].toVariant().toLongLong();
        info.chunks.append(chunk);
    }

    return true;
}

bool ChunkStateManager::saveStateFileInternal(const QString& stateFilePath, const ChunkStateInfo& info)
{
    QFileInfo fi(stateFilePath);
    QDir dir = fi.absoluteDir();
    if (!dir.exists()) {
        if (!dir.mkpath(QStringLiteral("."))) {
            LOG_ERROR("ChunkStateManager: cannot create directory for save: %s", qPrintable(dir.absolutePath()));
            return false;
        }
    }

    QJsonObject root;
    root[QStringLiteral("version")] = info.version;
    root[QStringLiteral("taskId")] = info.taskId;
    root[QStringLiteral("type")] = info.type;
    root[QStringLiteral("fileName")] = info.fileName;
    root[QStringLiteral("fileSize")] = info.fileSize;
    root[QStringLiteral("chunkSize")] = info.chunkSize;
    root[QStringLiteral("totalChunks")] = info.totalChunks;
    root[QStringLiteral("transferredSize")] = info.transferredSize;
    root[QStringLiteral("status")] = info.status;
    root[QStringLiteral("url")] = info.url;
    root[QStringLiteral("savePath")] = info.savePath;
    root[QStringLiteral("remoteAddress")] = info.remoteAddress;
    root[QStringLiteral("chunkDir")] = info.chunkDir;
    root[QStringLiteral("createdAt")] = info.createdAt;
    root[QStringLiteral("lastUpdated")] = info.lastUpdated;

    QJsonArray chunksArr;
    for (const ChunkState& chunk : info.chunks) {
        QJsonObject obj;
        obj[QStringLiteral("index")] = chunk.index;
        obj[QStringLiteral("offset")] = chunk.offset;
        obj[QStringLiteral("size")] = chunk.size;
        obj[QStringLiteral("status")] = chunk.status;
        obj[QStringLiteral("downloaded")] = chunk.downloaded;
        chunksArr.append(obj);
    }
    root[QStringLiteral("chunks")] = chunksArr;

    QJsonDocument doc(root);
    QFile file(stateFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOG_ERROR("ChunkStateManager: cannot save state file: %s", qPrintable(stateFilePath));
        return false;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool ChunkStateManager::deleteStateFileInternal(const QString& stateFilePath)
{
    if (!QFile::exists(stateFilePath)) {
        return true;
    }
    if (QFile::remove(stateFilePath)) {
        LOG_INFO("ChunkStateManager: deleted state file: %s", qPrintable(stateFilePath));
        return true;
    }
    LOG_ERROR("ChunkStateManager: failed to delete state file: %s", qPrintable(stateFilePath));
    return false;
}
