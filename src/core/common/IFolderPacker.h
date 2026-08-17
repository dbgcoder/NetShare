#ifndef IFOLDERPACKER_H
#define IFOLDERPACKER_H

#include <QObject>
#include <QString>

class IFolderPacker
{
public:
    virtual ~IFolderPacker() = default;

    virtual bool packFolder(const QString& folderPath, const QString& outputPath) = 0;
    virtual bool unpackArchive(const QString& archivePath, const QString& outputDir) = 0;
    virtual qint64 estimatePackedSize(const QString& folderPath) const = 0;
    virtual QString defaultOutputPath(const QString& folderPath) const = 0;
};

#endif
