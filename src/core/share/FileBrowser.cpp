#include "FileBrowser.h"
#include "Logger.h"
#include <QDir>
#include <QFileInfo>

FileBrowser::FileBrowser(QObject* parent)
    : QObject(parent)
{
}

FileBrowser::~FileBrowser() = default;

QVariantList FileBrowser::listDirectory(const QString& dirPath) const
{
    QVariantList result;
    QDir dir(dirPath);

    if (!dir.exists()) {
        LOG_WARN("Directory does not exist: %s", qPrintable(dirPath));
        return result;
    }

    QFileInfoList entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot,
        QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo& fi : entries) {
        result.append(QVariant::fromValue(entryFromFileInfo(dirPath, fi)));
    }

    return result;
}

QVariantList FileBrowser::searchFiles(const QString& dirPath, const QString& pattern) const
{
    QVariantList result;
    QDir dir(dirPath);

    if (!dir.exists()) {
        return result;
    }

    QStringList nameFilters;
    nameFilters << "*" + pattern + "*";

    QFileInfoList entries = dir.entryInfoList(
        nameFilters,
        QDir::AllEntries | QDir::NoDotAndDotDot,
        QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo& fi : entries) {
        result.append(QVariant::fromValue(entryFromFileInfo(dirPath, fi)));
    }

    return result;
}

FileEntry FileBrowser::getFileInfo(const QString& filePath) const
{
    QFileInfo fi(filePath);
    return entryFromFileInfo(fi.absolutePath(), fi);
}

bool FileBrowser::exists(const QString& path) const
{
    return QFileInfo::exists(path);
}

bool FileBrowser::isDirectory(const QString& path) const
{
    return QFileInfo(path).isDir();
}

QString FileBrowser::parentDirectory(const QString& path) const
{
    QFileInfo fi(path);
    if (fi.isDir()) {
        QDir dir(path);
        dir.cdUp();
        return dir.absolutePath();
    }
    return fi.absolutePath();
}

QString FileBrowser::absolutePath(const QString& path) const
{
    return QFileInfo(path).absoluteFilePath();
}

QString FileBrowser::formatFileSize(qint64 bytes)
{
    if (bytes < 0) return "0 B";
    if (bytes < 1024) return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024) return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024LL * 1024 * 1024) return QString::number(bytes / (1024.0 * 1024), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 1) + " GB";
}

FileEntry FileBrowser::entryFromFileInfo(const QString& /*basePath*/, const QFileInfo& fi) const
{
    FileEntry entry;
    entry.name = fi.fileName();
    entry.path = fi.absoluteFilePath();
    entry.isFolder = fi.isDir();
    entry.fileSize = fi.size();
    entry.displaySize = fi.isDir() ? QString() : formatFileSize(fi.size());
    entry.lastModified = fi.lastModified();
    entry.extension = fi.isDir() ? QString() : fi.suffix().toLower();
    return entry;
}
