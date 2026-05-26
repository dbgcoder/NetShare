#include "ChunkManager.h"
#include "Logger.h"
#include <QDir>
#include <QFile>

ChunkManager::ChunkManager(QObject* parent)
    : QObject(parent)
{
}

ChunkManager::~ChunkManager() = default;

QVariantList ChunkManager::splitFile(qint64 fileSize, int chunkSize) const
{
    QVariantList result;
    if (fileSize <= 0 || chunkSize <= 0) return result;

    qint64 offset = 0;
    int index = 0;

    while (offset < fileSize) {
        ChunkInfo chunk;
        chunk.index = index;
        chunk.offset = offset;
        chunk.size = qMin(static_cast<qint64>(chunkSize), fileSize - offset);
        chunk.status = ChunkInfo::Pending;
        result.append(QVariant::fromValue(chunk));

        offset += chunk.size;
        ++index;
    }

    return result;
}

QVariantList ChunkManager::splitFileForThreads(qint64 fileSize, int threadCount) const
{
    if (fileSize <= 0 || threadCount <= 0) return QVariantList();

    qint64 chunkSize = calculateChunkSize(fileSize, threadCount);
    return splitFile(fileSize, static_cast<int>(chunkSize));
}

bool ChunkManager::mergeChunks(const QString& chunkDir, const QString& outputPath, int totalChunks)
{
    QFile outFile(outputPath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        LOG_ERROR("ChunkManager: cannot open output file: %s", qPrintable(outputPath));
        emit mergeFailed("Cannot open output file");
        return false;
    }

    // Use streaming copy to avoid loading entire chunk into memory at once
    static const int COPY_BUF_SIZE = 1024 * 1024; // 1MB buffer
    QByteArray buf(COPY_BUF_SIZE, Qt::Uninitialized);

    for (int i = 0; i < totalChunks; ++i) {
        QString chunkPath = chunkDir + QString("/chunk_%1").arg(i, 6, 10, QChar('0'));
        QFile chunkFile(chunkPath);
        if (!chunkFile.open(QIODevice::ReadOnly)) {
            LOG_ERROR("ChunkManager: missing chunk %d", i);
            outFile.close();
            emit mergeFailed(QString("Missing chunk %1").arg(i));
            return false;
        }

        while (true) {
            qint64 bytesRead = chunkFile.read(buf.data(), COPY_BUF_SIZE);
            if (bytesRead <= 0) break;
            outFile.write(buf.constData(), bytesRead);
        }
        chunkFile.close();

        int percent = static_cast<int>((i + 1) * 100 / totalChunks);
        emit mergeProgress(percent);
    }

    outFile.close();
    LOG_INFO("ChunkManager: merged %d chunks -> %s", totalChunks, qPrintable(outputPath));
    emit mergeCompleted(outputPath);
    return true;
}

bool ChunkManager::writeChunk(const QString& chunkDir, int chunkIndex, const QByteArray& data)
{
    QDir dir(chunkDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QString chunkPath = chunkDir + QString("/chunk_%1").arg(chunkIndex, 6, 10, QChar('0'));
    QFile file(chunkPath);
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_ERROR("ChunkManager: cannot write chunk %d", chunkIndex);
        return false;
    }

    file.write(data);
    file.close();
    emit chunkWritten(chunkIndex);
    return true;
}

QByteArray ChunkManager::readChunk(const QString& chunkDir, int chunkIndex) const
{
    QString chunkPath = chunkDir + QString("/chunk_%1").arg(chunkIndex, 6, 10, QChar('0'));
    QFile file(chunkPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    QByteArray data = file.readAll();
    file.close();
    return data;
}

qint64 ChunkManager::calculateChunkSize(qint64 fileSize, int threadCount) const
{
    Q_UNUSED(threadCount)

    if (fileSize <= 0) return DEFAULT_CHUNK_SIZE;

    // Tiered chunk size strategy per design doc (PLAN.md §3.2):
    //   | File size     | Chunk size | Threads |
    //   |---------------|------------|---------|
    //   | < 100MB       | 1MB        | 3       |
    //   | 100MB - 1GB   | 4MB        | 4       |
    //   | 1GB - 5GB     | 8MB        | 5       |
    //   | > 5GB         | 16MB       | 6       |
    //
    // Key constraint: each chunk is sent as a single HTTP request body,
    // so chunk size must not exceed reasonable memory limits (~tens of MB).

    static const qint64 MB = 1024 * 1024;
    static const qint64 GB = 1024 * MB;
    static const qint64 TIER_1 = 100 * MB;  // 100MB
    static const qint64 TIER_2 = 1 * GB;    // 1GB
    static const qint64 TIER_3 = 5 * GB;    // 5GB

    if (fileSize < TIER_1)  return 1 * MB;
    if (fileSize < TIER_2)  return 4 * MB;
    if (fileSize < TIER_3)  return 8 * MB;
    return 16 * MB;
}

int ChunkManager::calculateChunkCount(qint64 fileSize, qint64 chunkSize) const
{
    if (chunkSize <= 0) return 0;
    return static_cast<int>((fileSize + chunkSize - 1) / chunkSize);
}

bool ChunkManager::verifyChunk(const QString& chunkDir, int chunkIndex, qint64 expectedSize) const
{
    QString chunkPath = chunkDir + QString("/chunk_%1").arg(chunkIndex, 6, 10, QChar('0'));
    QFileInfo fi(chunkPath);
    return fi.exists() && fi.size() == expectedSize;
}

bool ChunkManager::verifyMergedFile(const QString& filePath, qint64 expectedSize) const
{
    QFileInfo fi(filePath);
    return fi.exists() && fi.size() == expectedSize;
}

void ChunkManager::cleanupChunks(const QString& chunkDir)
{
    QDir dir(chunkDir);
    if (dir.exists()) {
        QStringList chunks = dir.entryList(QStringList() << "chunk_*", QDir::Files);
        for (const QString& chunk : chunks) {
            dir.remove(chunk);
        }
        dir.rmdir(chunkDir);
        LOG_INFO("ChunkManager: cleaned up chunks in %s", qPrintable(chunkDir));
    }
}
