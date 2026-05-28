#ifndef CHUNKMANAGER_H
#define CHUNKMANAGER_H

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QVariantList>
#include "ChunkState.h"

class ChunkManager : public QObject
{
    Q_OBJECT

public:
    explicit ChunkManager(QObject* parent = nullptr);
    ~ChunkManager() override;

    Q_INVOKABLE QVariantList splitFile(qint64 fileSize, int chunkSize = 1024 * 1024) const;
    Q_INVOKABLE QVariantList splitFileForThreads(qint64 fileSize, int threadCount = 3) const;

    Q_INVOKABLE bool mergeChunks(const QString& chunkDir, const QString& outputPath, int totalChunks);
    Q_INVOKABLE bool writeChunk(const QString& chunkDir, int chunkIndex, const QByteArray& data);
    Q_INVOKABLE QByteArray readChunk(const QString& chunkDir, int chunkIndex) const;

    Q_INVOKABLE qint64 calculateChunkSize(qint64 fileSize, int threadCount) const;
    Q_INVOKABLE int calculateChunkCount(qint64 fileSize, qint64 chunkSize) const;

    Q_INVOKABLE bool verifyChunk(const QString& chunkDir, int chunkIndex, qint64 expectedSize) const;
    Q_INVOKABLE bool verifyMergedFile(const QString& filePath, qint64 expectedSize) const;

    Q_INVOKABLE void cleanupChunks(const QString& chunkDir);

signals:
    void chunkWritten(int chunkIndex);
    void mergeProgress(int percent);
    void mergeCompleted(const QString& outputPath);
    void mergeFailed(const QString& error);

private:
    static constexpr qint64 DEFAULT_CHUNK_SIZE = 1024 * 1024;
};

#endif
