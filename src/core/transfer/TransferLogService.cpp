#include "TransferLogService.h"
#include "Logger.h"
#include "DatabaseManager.h"
#include <QUuid>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlQuery>

TransferLogService::TransferLogService(QObject* parent)
    : QObject(parent)
{
}

TransferLogService::~TransferLogService() = default;

void TransferLogService::setDatabase(DatabaseManager* db)
{
    m_database = db;
    if (m_database) {
        loadLogsFromDb();
    }
}

QString TransferLogService::generateId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString TransferLogService::logTransfer(int type, const QString& fileName, const QString& filePath,
                                          qint64 fileSize, const QString& peerAddress,
                                          int status, const QString& detail,
                                          qint64 transferredSize, const QString& engineTaskId)
{
    TransferLogEntry entry;
    entry.id = generateId();
    entry.taskId = engineTaskId.isEmpty() ? entry.id : engineTaskId;
    entry.type = type;
    entry.fileName = fileName;
    entry.filePath = filePath;
    entry.fileSize = fileSize;
    entry.transferredSize = transferredSize;
    entry.peerAddress = peerAddress;
    entry.status = status;
    entry.timestamp = QDateTime::currentDateTime();
    entry.detail = detail;

    m_logs.prepend(entry);

    // Persist to database
    if (!saveLogToDb(entry)) {
        LOG_WARN("TransferLogService: failed to persist log entry %s", qPrintable(entry.id));
    }

    emit logAdded(entry.id);

    LOG_INFO("TransferLog: %s %s (%lld bytes) from %s - status %d",
             type == TransferLogEntry::DownloadLog ? "Download" : "Upload",
             qPrintable(fileName), fileSize,
             qPrintable(peerAddress), status);

    return entry.id;
}

bool TransferLogService::updateLogEntry(const QString& id, int status, const QString& detail)
{
    for (auto& entry : m_logs) {
        if (entry.id == id) {
            entry.status = status;
            if (!detail.isEmpty()) {
                entry.detail = detail;
            }
            updateLogInDb(entry);
            emit logUpdated(id);
            return true;
        }
    }
    return false;
}

QVariantList TransferLogService::queryLogs(int limit, int offset) const
{
    QVariantList result;
    int start = qMin(offset, m_logs.size());
    int end = qMin(start + limit, m_logs.size());

    for (int i = start; i < end; ++i) {
        result.append(QVariant::fromValue(m_logs[i]));
    }
    return result;
}

QVariantList TransferLogService::queryByType(int type, int limit) const
{
    QVariantList result;
    for (const TransferLogEntry& entry : m_logs) {
        if (entry.type == type) {
            result.append(QVariant::fromValue(entry));
            if (result.size() >= limit) break;
        }
    }
    return result;
}

QVariantList TransferLogService::queryByDateRange(const QDateTime& from, const QDateTime& to) const
{
    QVariantList result;
    for (const TransferLogEntry& entry : m_logs) {
        if (entry.timestamp >= from && entry.timestamp <= to) {
            result.append(QVariant::fromValue(entry));
        }
    }
    return result;
}

QVariantList TransferLogService::searchLogs(const QString& keyword) const
{
    QVariantList result;
    for (const TransferLogEntry& entry : m_logs) {
        if (entry.fileName.contains(keyword, Qt::CaseInsensitive) ||
            entry.filePath.contains(keyword, Qt::CaseInsensitive) ||
            entry.peerAddress.contains(keyword, Qt::CaseInsensitive)) {
            result.append(QVariant::fromValue(entry));
        }
    }
    return result;
}

int TransferLogService::totalCount() const
{
    return m_logs.size();
}

int TransferLogService::countByType(int type) const
{
    int count = 0;
    for (const TransferLogEntry& entry : m_logs) {
        if (entry.type == type) ++count;
    }
    return count;
}

qint64 TransferLogService::totalBytesTransferred() const
{
    qint64 total = 0;
    for (const TransferLogEntry& entry : m_logs) {
        if (entry.status == TransferLogEntry::Completed) {
            total += entry.fileSize;
        }
    }
    return total;
}

bool TransferLogService::exportLogs(const QString& filePath) const
{
    QJsonArray array;
    for (const TransferLogEntry& entry : m_logs) {
        QJsonObject obj;
        obj["id"] = entry.id;
        obj["type"] = entry.type;
        obj["fileName"] = entry.fileName;
        obj["filePath"] = entry.filePath;
        obj["fileSize"] = entry.fileSize;
        obj["peerAddress"] = entry.peerAddress;
        obj["status"] = entry.status;
        obj["timestamp"] = entry.timestamp.toString(Qt::ISODate);
        obj["detail"] = entry.detail;
        array.append(obj);
    }

    QJsonDocument doc(array);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_ERROR("TransferLogService: cannot export to %s", qPrintable(filePath));
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    LOG_INFO("TransferLogService: exported %d logs to %s", m_logs.size(), qPrintable(filePath));
    return true;
}

void TransferLogService::clearLogs(int olderThanDays)
{
    if (olderThanDays <= 0) {
        m_logs.clear();
        LOG_INFO("TransferLogService: all logs cleared");
        return;
    }

    QDateTime threshold = QDateTime::currentDateTime().addDays(-olderThanDays);
    int removed = 0;

    auto it = m_logs.begin();
    while (it != m_logs.end()) {
        if (it->timestamp < threshold) {
            it = m_logs.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }

    LOG_INFO("TransferLogService: cleared %d logs older than %d days", removed, olderThanDays);
}

QVariantList TransferLogService::pausedLogsForRestore() const
{
    QVariantList result;
    for (const TransferLogEntry& entry : m_logs) {
        if (entry.status == TransferLogEntry::Paused) {
            result.append(QVariant::fromValue(entry));
        }
    }
    return result;
}

bool TransferLogService::saveLogToDb(const TransferLogEntry& entry)
{
    if (!m_database) {
        LOG_WARN("TransferLogService: cannot persist - no database");
        return false;
    }

    QString sql = "INSERT INTO transfer_logs (task_id, type, action, file_name, file_size, transferred_size, error) "
                  "VALUES (?, ?, ?, ?, ?, ?, ?)";
    QVariantList args;
    args << entry.taskId
         << (entry.type == TransferLogEntry::DownloadLog ? "download" : "upload")
         << (entry.status == TransferLogEntry::Completed ? "completed" :
             entry.status == TransferLogEntry::Failed ? "failed" :
             entry.status == TransferLogEntry::Cancelled ? "cancelled" :
             entry.status == TransferLogEntry::Paused ? "paused" : "started")
         << entry.fileName
         << entry.fileSize
         << entry.transferredSize
         << entry.detail;

    bool ok = m_database->execute(sql, args);
    if (!ok) {
        LOG_WARN("TransferLogService: failed to persist log entry %s - DB error: %s",
                 qPrintable(entry.id), qPrintable(m_database->lastError()));
    }
    return ok;
}

bool TransferLogService::updateLogInDb(const TransferLogEntry& entry)
{
    if (!m_database) return false;

    QString sql = "UPDATE transfer_logs SET action = ?, transferred_size = ?, error = ? WHERE task_id = ?";
    QVariantList args;
    args << (entry.status == TransferLogEntry::Completed ? "completed" :
             entry.status == TransferLogEntry::Failed ? "failed" :
             entry.status == TransferLogEntry::Cancelled ? "cancelled" :
             entry.status == TransferLogEntry::Paused ? "paused" : "started")
         << entry.transferredSize
         << entry.detail
         << entry.taskId;

    return m_database->execute(sql, args);
}

bool TransferLogService::loadLogsFromDb()
{
    if (!m_database) return false;

    QString sql = "SELECT task_id, type, action, file_name, file_size, transferred_size, error, created_at "
                  "FROM transfer_logs ORDER BY created_at DESC LIMIT 500";
    QSqlQuery q = m_database->query(sql);

    while (q.next()) {
        TransferLogEntry entry;
        entry.id = generateId();
        entry.taskId = q.value(0).toString();
        entry.type = (q.value(1).toString() == "download") ?
                     TransferLogEntry::DownloadLog : TransferLogEntry::UploadLog;

        QString action = q.value(2).toString();
        entry.status = (action == "completed") ? TransferLogEntry::Completed :
                       (action == "failed") ? TransferLogEntry::Failed :
                       (action == "cancelled") ? TransferLogEntry::Cancelled :
                       (action == "paused") ? TransferLogEntry::Paused : TransferLogEntry::Started;

        entry.fileName = q.value(3).toString();
        entry.fileSize = q.value(4).toLongLong();
        entry.transferredSize = q.value(5).toLongLong();
        entry.detail = q.value(6).toString();
        entry.timestamp = QDateTime::fromString(q.value(7).toString(), Qt::ISODate);

        m_logs.append(entry);
    }

    LOG_INFO("TransferLogService: loaded %d logs from database", m_logs.size());
    return true;
}
