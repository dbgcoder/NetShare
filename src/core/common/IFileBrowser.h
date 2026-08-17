#ifndef IFILEBROWSER_H
#define IFILEBROWSER_H

#include <QObject>
#include <QString>
#include <QVariantList>

// Forward declaration — FileEntry is defined in FileBrowser.h
class FileEntry;

class IFileBrowser
{
public:
    virtual ~IFileBrowser() = default;

    virtual QVariantList listDirectory(const QString& dirPath) const = 0;
    virtual QVariantList searchFiles(const QString& dirPath, const QString& pattern) const = 0;
    virtual FileEntry getFileInfo(const QString& filePath) const = 0;
    virtual bool exists(const QString& path) const = 0;
    virtual bool isDirectory(const QString& path) const = 0;
    virtual QString parentDirectory(const QString& path) const = 0;
    virtual QString absolutePath(const QString& path) const = 0;
};

#endif
