#include "ShareManager.h"
#include "Logger.h"
#include "DatabaseManager.h"
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QDateTime>
#include <QNetworkInterface>
#include <QGuiApplication>
#include <QClipboard>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDesktopServices>
#include <QFileInfo>

ShareManager& ShareManager::instance()
{
    static ShareManager instance;
    return instance;
}

void ShareManager::shutdown()
{
    // Meyers singleton — no manual cleanup needed
}

ShareManager::ShareManager(QObject* parent)
    : QObject(parent)
{
}

ShareManager::~ShareManager() = default;

void ShareManager::setDatabase(DatabaseManager* db)
{
    m_database = db;
    if (m_database) {
        loadSharesFromDb();
    }
}

QString ShareManager::localIp() const
{
    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress& address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol &&
            !address.isLoopback() &&
            address.isInSubnet(QHostAddress("192.168.0.0"), 16) ||
            (address.protocol() == QAbstractSocket::IPv4Protocol &&
             !address.isLoopback() &&
             address.isInSubnet(QHostAddress("10.0.0.0"), 8)) ||
            (address.protocol() == QAbstractSocket::IPv4Protocol &&
             !address.isLoopback() &&
             address.isInSubnet(QHostAddress("172.16.0.0"), 12))) {
            return address.toString();
        }
    }
    return QHostAddress(QHostAddress::LocalHost).toString();
}

QString ShareManager::createShare(const QString& filePath, bool isFolder,
                                  int expireHours, int maxDownloads,
                                  const QString& password, int source)
{
    ShareInfo info;
    info.token = generateToken();
    info.filePath = filePath;
    info.fileSize = QFileInfo(filePath).size();
    if (expireHours > 0) {
        info.expiresAt = QDateTime::currentDateTime().addSecs(expireHours * 3600);
    }
    info.maxDownloads = maxDownloads;
    info.downloadCount = 0;
    info.passwordRequired = !password.isEmpty();
    info.passwordHash = hashPassword(password);
    info.isFolder = isFolder;
    info.source = source;
    info.createdAt = QDateTime::currentDateTime();

    // Persist to database
    if (!saveShareToDb(info)) {
        LOG_ERROR("Failed to persist share %s to database", qPrintable(info.token));
        // Still add to memory so the share works in this session
    }

    m_shares[info.token] = info;
    m_activeTokens.insert(info.token);

    LOG_INFO("Share created: %s for %s", qPrintable(info.token), qPrintable(filePath));
    emit shareCreated(info.token);
    LOG_INFO("Share created signal emitted: %s", qPrintable(info.token));

    return info.token;
}

ShareInfo ShareManager::getShareInfo(const QString& token) const
{
    return m_shares.value(token);
}

QString ShareManager::createShareAuto(const QString& filePath, int expireHours,
                                      int maxDownloads, const QString& password,
                                      int source)
{
    QFileInfo fi(filePath);
    bool isFolder = fi.isDir();
    return createShare(filePath, isFolder, expireHours, maxDownloads, password, source);
}

bool ShareManager::validateShare(const QString& token, const QString& password) const
{
    ShareInfo info = m_shares.value(token);
    if (!info.isValid() || info.isExpired()) {
        return false;
    }
    // Received files (source=1) are not publicly shareable
    if (info.source == 1) {
        return false;
    }

    if (info.passwordRequired) {
        return info.passwordHash == hashPassword(password);
    }

    return true;
}

bool ShareManager::cancelShare(const QString& token)
{
    if (m_shares.contains(token)) {
        deleteShareFromDb(token);
        m_shares.remove(token);
        m_activeTokens.remove(token);
        LOG_INFO("Share cancelled: %s", qPrintable(token));
        emit shareCancelled(token);
        return true;
    }
    return false;
}

QVariantList ShareManager::getActiveShares() const
{
    QVariantList result;
    for (const ShareInfo& info : m_shares) {
        if (info.isValid() && !info.isExpired() && info.source == 0) {
            result.append(QVariant::fromValue(info));
        }
    }
    return result;
}

QVariantList ShareManager::getAllShares() const
{
    QVariantList result;
    for (const ShareInfo& info : m_shares) {
        if (info.isValid() && info.source == 0) {
            result.append(QVariant::fromValue(info));
        }
    }
    return result;
}

void ShareManager::cleanupExpiredShares()
{
    QStringList expiredTokens;
    for (auto it = m_shares.begin(); it != m_shares.end(); ++it) {
        if (it.value().isExpired()) {
            expiredTokens.append(it.key());
        }
    }

    for (const QString& token : expiredTokens) {
        deleteShareFromDb(token);
        m_shares.remove(token);
        m_activeTokens.remove(token);
        LOG_INFO("Share expired and cleaned up: %s", qPrintable(token));
        emit shareExpired(token);
    }
}

int ShareManager::getActiveShareCount() const
{
    int count = 0;
    for (const ShareInfo& info : m_shares) {
        if (info.source == 0) count++;
    }
    return count;
}

void ShareManager::copyToClipboard(const QString& text)
{
    QClipboard* clip = QGuiApplication::clipboard();
    if (clip) {
        clip->setText(text);
    }
}

void ShareManager::shareAccessed(const QString& token)
{
    if (!m_shares.contains(token)) return;

    ShareInfo& info = m_shares[token];
    info.downloadCount++;

    // Check if download limit reached
    if (info.maxDownloads > 0 && info.downloadCount >= info.maxDownloads) {
        LOG_INFO("Share %s reached max downloads (%d), auto-cancelling", qPrintable(token), info.maxDownloads);
        cancelShare(token);
        return;
    }

    updateShareInDb(info);
    emit shareAccessedSignal(token);
}

QString ShareManager::generateToken() const
{
    const QString chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    QString token;
    QRandomGenerator* gen = QRandomGenerator::global();

    for (int i = 0; i < 12; ++i) {
        int index = gen->bounded(chars.length());
        token.append(chars[index]);
    }

    return token;
}

QString ShareManager::hashPassword(const QString& password) const
{
    if (password.isEmpty()) {
        return QString();
    }
    QByteArray hash = QCryptographicHash::hash(
        password.toUtf8(), QCryptographicHash::Sha256);
    return hash.toHex();
}

// ---- Database persistence methods ----

bool ShareManager::saveShareToDb(const ShareInfo& info)
{
    if (!m_database) return false;

    QString sql = "INSERT INTO shares (token, file_path, file_size, is_folder, source, expires_at, "
                  "max_downloads, download_count, password_hash, description, created_at) "
                  "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    QVariantList args;
    args << info.token
         << info.filePath
         << info.fileSize
         << (info.isFolder ? 1 : 0)
         << info.source
         << info.expiresAt.toString(Qt::ISODate)
         << info.maxDownloads
         << info.downloadCount
         << info.passwordHash
         << info.description
         << info.createdAt.toString(Qt::ISODate);

    if (!m_database->execute(sql, args)) {
        LOG_ERROR("Failed to save share to DB: %s", qPrintable(m_database->lastError()));
        return false;
    }
    return true;
}

bool ShareManager::deleteShareFromDb(const QString& token)
{
    if (!m_database) return false;

    QString sql = "DELETE FROM shares WHERE token = ?";
    QVariantList args;
    args << token;

    if (!m_database->execute(sql, args)) {
        LOG_ERROR("Failed to delete share from DB: %s", qPrintable(m_database->lastError()));
        return false;
    }
    return true;
}

bool ShareManager::updateShareInDb(const ShareInfo& info)
{
    if (!m_database) return false;

    QString sql = "UPDATE shares SET download_count = ?, updated_at = ? WHERE token = ?";
    QVariantList args;
    args << info.downloadCount
         << QDateTime::currentDateTime().toString(Qt::ISODate)
         << info.token;

    if (!m_database->execute(sql, args)) {
        LOG_ERROR("Failed to update share in DB: %s", qPrintable(m_database->lastError()));
        return false;
    }
    return true;
}

bool ShareManager::loadSharesFromDb()
{
    if (!m_database) return false;

    QString sql = "SELECT token, file_path, file_size, is_folder, source, expires_at, "
                  "max_downloads, download_count, password_hash, description, created_at FROM shares";
    QSqlQuery q = m_database->query(sql);

    int loadedCount = 0;
    while (q.next()) {
        ShareInfo info;
        info.token = q.value(0).toString();
        info.filePath = q.value(1).toString();
        info.fileSize = q.value(2).toLongLong();
        info.isFolder = q.value(3).toInt() == 1;
        info.source = q.value(4).toInt();
        QString expiresAtStr = q.value(5).toString();
        if (expiresAtStr.isEmpty()) {
            info.expiresAt = QDateTime(); // null = never expires
        } else {
            info.expiresAt = QDateTime::fromString(expiresAtStr, Qt::ISODate);
        }
        info.maxDownloads = q.value(6).toInt();
        info.downloadCount = q.value(7).toInt();
        info.passwordHash = q.value(8).toString();
        info.description = q.value(9).toString();
        info.passwordRequired = !info.passwordHash.isEmpty();
        QString createdAtStr = q.value(10).toString();
        if (!createdAtStr.isEmpty()) {
            info.createdAt = QDateTime::fromString(createdAtStr, Qt::ISODate);
        } else {
            info.createdAt = QDateTime::currentDateTime();
        }

        // Load expired shares into memory but don't add to active tokens
        if (info.isExpired()) {
            m_shares[info.token] = info;
            loadedCount++;
            continue;
        }

        m_shares[info.token] = info;
        m_activeTokens.insert(info.token);
        loadedCount++;
    }

    LOG_INFO("Loaded %d shares from database", loadedCount);
    return true;
}

QVariantList ShareManager::getReceivedFiles() const
{
    QVariantList result;
    for (const ShareInfo& info : m_shares) {
        if (info.source == 1 && info.isValid()) {
            result.append(QVariant::fromValue(info));
        }
    }
    return result;
}

int ShareManager::getReceivedFileCount() const
{
    int count = 0;
    for (const ShareInfo& info : m_shares) {
        if (info.source == 1 && info.isValid()) {
            count++;
        }
    }
    return count;
}

bool ShareManager::deleteReceivedFile(const QString& token)
{
    if (!m_shares.contains(token)) return false;
    ShareInfo info = m_shares[token];
    if (info.source != 1) return false;

    // Delete the actual file from disk
    if (QFile::exists(info.filePath)) {
        QFile::remove(info.filePath);
    }

    deleteShareFromDb(token);
    m_shares.remove(token);
    m_activeTokens.remove(token);

    LOG_INFO("Received file deleted: %s (%s)", qPrintable(info.filePath), qPrintable(token));
    emit receivedFileDeleted(token);
    return true;
}

bool ShareManager::openReceivedFile(const QString& token)
{
    if (!m_shares.contains(token)) return false;
    ShareInfo info = m_shares[token];
    if (info.source != 1) return false;

    if (!QFile::exists(info.filePath)) return false;

    return QDesktopServices::openUrl(QUrl::fromLocalFile(info.filePath));
}

bool ShareManager::openReceivedFileFolder(const QString& token)
{
    if (!m_shares.contains(token)) return false;
    ShareInfo info = m_shares[token];
    if (info.source != 1) return false;

    QFileInfo fi(info.filePath);
    QString dir = fi.absolutePath();
    if (!QDir(dir).exists()) return false;

    return QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

QString ShareManager::shareReceivedFile(const QString& token, int expireHours, const QString& password)
{
    if (!m_shares.contains(token)) return QString();
    ShareInfo info = m_shares[token];
    if (info.source != 1) return QString();

    if (!QFile::exists(info.filePath)) return QString();

    // Check if already shared (source=0 for same file path)
    for (const ShareInfo& s : m_shares) {
        if (s.source == 0 && s.filePath == info.filePath && s.isValid() && !s.isExpired()) {
            return s.token;
        }
    }

    return createShare(info.filePath, false, expireHours, 0, password, 0);
}

bool ShareInfo::isValid() const
{
    return !token.isEmpty() && !filePath.isEmpty();
}

bool ShareInfo::isExpired() const
{
    if (expiresAt.isNull()) {
        return false;
    }
    return QDateTime::currentDateTime() > expiresAt;
}
