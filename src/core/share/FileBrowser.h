#ifndef FILEBROWSER_H
#define FILEBROWSER_H

#include <QObject>
#include <QString>
#include <QList>
#include <QDateTime>
#include <QVariantList>

#include "IFileBrowser.h"

class FileEntry
{
    Q_GADGET
    Q_PROPERTY(QString name MEMBER name)
    Q_PROPERTY(QString path MEMBER path)
    Q_PROPERTY(bool isFolder MEMBER isFolder)
    Q_PROPERTY(qint64 fileSize MEMBER fileSize)
    Q_PROPERTY(QString displaySize MEMBER displaySize)
    Q_PROPERTY(QDateTime lastModified MEMBER lastModified)
    Q_PROPERTY(QString extension MEMBER extension)

public:
    QString name;
    QString path;
    bool isFolder = false;
    qint64 fileSize = 0;
    QString displaySize;
    QDateTime lastModified;
    QString extension;
};

class FileBrowser : public QObject, public IFileBrowser
{
    Q_OBJECT

public:
    explicit FileBrowser(QObject* parent = nullptr);
    ~FileBrowser() override;

    Q_INVOKABLE QVariantList listDirectory(const QString& dirPath) const;
    Q_INVOKABLE QVariantList searchFiles(const QString& dirPath, const QString& pattern) const;
    Q_INVOKABLE FileEntry getFileInfo(const QString& filePath) const;
    Q_INVOKABLE bool exists(const QString& path) const;
    Q_INVOKABLE bool isDirectory(const QString& path) const;
    Q_INVOKABLE QString parentDirectory(const QString& path) const;
    Q_INVOKABLE QString absolutePath(const QString& path) const;

    static QString formatFileSize(qint64 bytes);

private:
    FileEntry entryFromFileInfo(const QString& basePath, const class QFileInfo& fi) const;
};

#endif
