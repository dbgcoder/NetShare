#ifndef RESUMEMANAGER_H
#define RESUMEMANAGER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QVariantList>
#include <QDateTime>

class ResumeInfo
{
    Q_GADGET
    Q_PROPERTY(QString taskId MEMBER taskId)
    Q_PROPERTY(QString url MEMBER url)
    Q_PROPERTY(QString savePath MEMBER savePath)
    Q_PROPERTY(qint64 fileSize MEMBER fileSize)
    Q_PROPERTY(qint64 downloadedSize MEMBER downloadedSize)
    Q_PROPERTY(int chunkCount MEMBER chunkCount)
    Q_PROPERTY(QVariantList completedChunks MEMBER completedChunks)
    Q_PROPERTY(QDateTime lastUpdated MEMBER lastUpdated)

public:
    QString taskId;
    QString url;
    QString savePath;
    qint64 fileSize = 0;
    qint64 downloadedSize = 0;
    int chunkCount = 0;
    QVariantList completedChunks;
    QDateTime lastUpdated;
};

class ResumeManager : public QObject
{
    Q_OBJECT

public:
    explicit ResumeManager(QObject* parent = nullptr);
    ~ResumeManager() override;

    Q_INVOKABLE bool saveResumeInfo(const ResumeInfo& info);
    Q_INVOKABLE ResumeInfo loadResumeInfo(const QString& taskId) const;
    Q_INVOKABLE bool hasResumeInfo(const QString& taskId) const;
    Q_INVOKABLE bool removeResumeInfo(const QString& taskId);

    Q_INVOKABLE QVariantList getAllResumeInfo() const;
    Q_INVOKABLE void cleanupExpired(int maxAgeDays = 7);

    Q_INVOKABLE QString resumeDirectory() const;

private:
    QString resumeFilePath(const QString& taskId) const;
    QString m_resumeDir;
};

#endif
