#include "ResumeManager.h"
#include "Logger.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>

ResumeManager::ResumeManager(QObject* parent)
    : QObject(parent)
{
    m_resumeDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/resume";
    QDir().mkpath(m_resumeDir);
}

ResumeManager::~ResumeManager() = default;

QString ResumeManager::resumeFilePath(const QString& taskId) const
{
    return m_resumeDir + "/" + taskId + ".json";
}

bool ResumeManager::saveResumeInfo(const ResumeInfo& info)
{
    QJsonObject obj;
    obj["taskId"] = info.taskId;
    obj["url"] = info.url;
    obj["savePath"] = info.savePath;
    obj["fileSize"] = info.fileSize;
    obj["downloadedSize"] = info.downloadedSize;
    obj["chunkCount"] = info.chunkCount;
    obj["lastUpdated"] = info.lastUpdated.toString(Qt::ISODate);

    QJsonArray chunks;
    for (const QVariant& v : info.completedChunks) {
        chunks.append(v.toInt());
    }
    obj["completedChunks"] = chunks;

    QJsonDocument doc(obj);

    QFile file(resumeFilePath(info.taskId));
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_ERROR("ResumeManager: cannot save resume info for %s", qPrintable(info.taskId));
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Compact));
    file.close();
    return true;
}

ResumeInfo ResumeManager::loadResumeInfo(const QString& taskId) const
{
    ResumeInfo info;
    info.taskId = taskId;

    QFile file(resumeFilePath(taskId));
    if (!file.open(QIODevice::ReadOnly)) {
        return info;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return info;

    QJsonObject obj = doc.object();
    info.url = obj["url"].toString();
    info.savePath = obj["savePath"].toString();
    info.fileSize = obj["fileSize"].toVariant().toLongLong();
    info.downloadedSize = obj["downloadedSize"].toVariant().toLongLong();
    info.chunkCount = obj["chunkCount"].toInt();
    info.lastUpdated = QDateTime::fromString(obj["lastUpdated"].toString(), Qt::ISODate);

    QJsonArray chunks = obj["completedChunks"].toArray();
    for (const QJsonValue& v : chunks) {
        info.completedChunks.append(v.toInt());
    }

    return info;
}

bool ResumeManager::hasResumeInfo(const QString& taskId) const
{
    return QFile::exists(resumeFilePath(taskId));
}

bool ResumeManager::removeResumeInfo(const QString& taskId)
{
    QString path = resumeFilePath(taskId);
    if (QFile::exists(path)) {
        QFile::remove(path);
        LOG_INFO("ResumeManager: removed resume info for %s", qPrintable(taskId));
        return true;
    }
    return false;
}

QVariantList ResumeManager::getAllResumeInfo() const
{
    QVariantList result;
    QDir dir(m_resumeDir);
    QStringList files = dir.entryList(QStringList() << "*.json", QDir::Files);

    for (const QString& file : files) {
        QString taskId = file.left(file.size() - 5);
        result.append(QVariant::fromValue(loadResumeInfo(taskId)));
    }

    return result;
}

void ResumeManager::cleanupExpired(int maxAgeDays)
{
    QDir dir(m_resumeDir);
    QStringList files = dir.entryList(QStringList() << "*.json", QDir::Files);
    QDateTime threshold = QDateTime::currentDateTime().addDays(-maxAgeDays);

    int removed = 0;
    for (const QString& file : files) {
        QFileInfo fi(dir.absoluteFilePath(file));
        if (fi.lastModified() < threshold) {
            dir.remove(file);
            ++removed;
        }
    }

    if (removed > 0) {
        LOG_INFO("ResumeManager: cleaned up %d expired resume files", removed);
    }
}

QString ResumeManager::resumeDirectory() const
{
    return m_resumeDir;
}
