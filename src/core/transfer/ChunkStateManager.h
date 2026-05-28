#ifndef CHUNKSTATEMANAGER_H
#define CHUNKSTATEMANAGER_H

#include <QObject>
#include <QMutex>
#include "ChunkState.h"

class ChunkStateManager : public QObject
{
    Q_OBJECT

public:
    explicit ChunkStateManager(QObject* parent = nullptr);

    bool createStateFile(const QString& stateFilePath, const ChunkStateInfo& info);
    bool loadStateFile(const QString& stateFilePath, ChunkStateInfo& info);
    bool saveStateFile(const QString& stateFilePath, const ChunkStateInfo& info);
    bool deleteStateFile(const QString& stateFilePath);

    bool updateChunkStatus(const QString& stateFilePath, int chunkIndex,
                           const QString& status, qint64 downloaded);
    bool updateTaskStatus(const QString& stateFilePath, const QString& status);
    bool updateTransferredSize(const QString& stateFilePath, qint64 transferredSize);
    bool markDownloadingChunksPartial(const QString& stateFilePath);

    QList<ChunkStateInfo> scanResumableTasks(const QString& directory);

    bool validateCompletedChunks(const QString& stateFilePath);

    void cleanupExpired(const QString& directory, int maxAgeDays = 7);

private:
    bool loadStateFileInternal(const QString& stateFilePath, ChunkStateInfo& info);
    bool saveStateFileInternal(const QString& stateFilePath, const ChunkStateInfo& info);
    bool deleteStateFileInternal(const QString& stateFilePath);

    QMutex m_mutex;
};

#endif // CHUNKSTATEMANAGER_H
