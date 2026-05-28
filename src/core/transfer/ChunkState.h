#ifndef CHUNKSTATE_H
#define CHUNKSTATE_H

#include <QObject>
#include <QString>
#include <QList>
#include <QDateTime>
#include <QVariantList>

struct ChunkState
{
    Q_GADGET
    Q_PROPERTY(int index MEMBER index)
    Q_PROPERTY(qint64 offset MEMBER offset)
    Q_PROPERTY(qint64 size MEMBER size)
    Q_PROPERTY(QString status MEMBER status)
    Q_PROPERTY(qint64 downloaded MEMBER downloaded)

public:
    int index = 0;
    qint64 offset = 0;
    qint64 size = 0;
    QString status = QStringLiteral("pending");
    qint64 downloaded = 0;
};

struct ChunkStateInfo
{
    Q_GADGET
    Q_PROPERTY(int version MEMBER version)
    Q_PROPERTY(QString taskId MEMBER taskId)
    Q_PROPERTY(QString type MEMBER type)
    Q_PROPERTY(QString fileName MEMBER fileName)
    Q_PROPERTY(qint64 fileSize MEMBER fileSize)
    Q_PROPERTY(int chunkSize MEMBER chunkSize)
    Q_PROPERTY(int totalChunks MEMBER totalChunks)
    Q_PROPERTY(qint64 transferredSize MEMBER transferredSize)
    Q_PROPERTY(QString status MEMBER status)
    Q_PROPERTY(QString url MEMBER url)
    Q_PROPERTY(QString savePath MEMBER savePath)
    Q_PROPERTY(QString remoteAddress MEMBER remoteAddress)
    Q_PROPERTY(QString chunkDir MEMBER chunkDir)
    Q_PROPERTY(QString createdAt MEMBER createdAt)
    Q_PROPERTY(QString lastUpdated MEMBER lastUpdated)

public:
    int version = 1;
    QString taskId;
    QString type;
    QString fileName;
    qint64 fileSize = 0;
    int chunkSize = 0;
    int totalChunks = 0;
    qint64 transferredSize = 0;
    QString status = QStringLiteral("pending");
    QString url;
    QString savePath;
    QString remoteAddress;
    QString chunkDir;
    QString createdAt;
    QString lastUpdated;
    QList<ChunkState> chunks;
};

Q_DECLARE_METATYPE(ChunkState)
Q_DECLARE_METATYPE(ChunkStateInfo)

#endif // CHUNKSTATE_H
