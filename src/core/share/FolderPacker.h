#ifndef FOLDERPACKER_H
#define FOLDERPACKER_H

#include <QObject>
#include <QString>

#include "IFolderPacker.h"

class FolderPacker : public QObject, public IFolderPacker
{
    Q_OBJECT

public:
    explicit FolderPacker(QObject* parent = nullptr);
    ~FolderPacker() override;

    Q_INVOKABLE bool packFolder(const QString& folderPath, const QString& outputPath);
    Q_INVOKABLE bool unpackArchive(const QString& archivePath, const QString& outputDir);
    Q_INVOKABLE qint64 estimatePackedSize(const QString& folderPath) const;
    Q_INVOKABLE QString defaultOutputPath(const QString& folderPath) const;

signals:
    void packProgress(const QString& folderPath, int percent);
    void packCompleted(const QString& folderPath, const QString& outputPath);
    void packFailed(const QString& folderPath, const QString& error);

private:
    bool packFolderInternal(const QString& folderPath, const QString& outputPath);
};

#endif
