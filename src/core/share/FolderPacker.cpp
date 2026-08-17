#include "FolderPacker.h"
#include "Logger.h"
#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QProcess>

FolderPacker::FolderPacker(QObject* parent)
    : QObject(parent)
{
}

FolderPacker::~FolderPacker() = default;

bool FolderPacker::packFolder(const QString& folderPath, const QString& outputPath)
{
    QFileInfo fi(folderPath);
    if (!fi.exists() || !fi.isDir()) {
        LOG_ERROR("FolderPacker: path is not a valid directory: %s", qPrintable(folderPath));
        emit packFailed(folderPath, "Invalid directory");
        return false;
    }

    bool ok = packFolderInternal(folderPath, outputPath);
    if (ok) {
        emit packCompleted(folderPath, outputPath);
    } else {
        emit packFailed(folderPath, "Pack failed");
    }
    return ok;
}

bool FolderPacker::unpackArchive(const QString& archivePath, const QString& outputDir)
{
    QFileInfo fi(archivePath);
    if (!fi.exists()) {
        LOG_ERROR("FolderPacker: archive does not exist: %s", qPrintable(archivePath));
        return false;
    }

    QDir dir(outputDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

#ifdef Q_OS_WIN
    QProcess proc;
    proc.start("powershell", {
        "-Command",
        QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
            .arg(archivePath, outputDir)
    });
    if (!proc.waitForFinished(60000)) {
        LOG_ERROR("FolderPacker: unpack timeout for %s", qPrintable(archivePath));
        return false;
    }
    return proc.exitCode() == 0;
#else
    QProcess proc;
    proc.start("unzip", {"-o", archivePath, "-d", outputDir});
    if (!proc.waitForFinished(60000)) {
        LOG_ERROR("FolderPacker: unpack timeout for %s", qPrintable(archivePath));
        return false;
    }
    return proc.exitCode() == 0;
#endif
}

qint64 FolderPacker::estimatePackedSize(const QString& folderPath) const
{
    qint64 totalSize = 0;
    QDir dir(folderPath);

    QFileInfoList entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);

    for (const QFileInfo& fi : entries) {
        if (fi.isDir()) {
            totalSize += estimatePackedSize(fi.absoluteFilePath());
        } else {
            totalSize += fi.size();
        }
    }

    return totalSize;
}

QString FolderPacker::defaultOutputPath(const QString& folderPath) const
{
    QFileInfo fi(folderPath);
    return fi.absolutePath() + "/" + fi.fileName() + ".zip";
}

bool FolderPacker::packFolderInternal(const QString& folderPath, const QString& outputPath)
{
    QFileInfo folderInfo(folderPath);
    QString folderName = folderInfo.fileName();
    QString parentDir = folderInfo.absolutePath();

#ifdef Q_OS_WIN
    QProcess proc;
    proc.start("powershell", {
        "-Command",
        QString("Compress-Archive -Path '%1\\*' -DestinationPath '%2' -Force")
            .arg(folderPath, outputPath)
    });

    if (!proc.waitForFinished(120000)) {
        LOG_ERROR("FolderPacker: pack timeout for %s", qPrintable(folderPath));
        return false;
    }

    if (proc.exitCode() != 0) {
        QString stderrOutput = proc.readAllStandardError();
        LOG_ERROR("FolderPacker: pack failed: %s", qPrintable(stderrOutput));
        return false;
    }
#else
    QProcess proc;
    proc.setWorkingDirectory(parentDir);
    proc.start("zip", {"-r", outputPath, folderName});

    if (!proc.waitForFinished(120000)) {
        LOG_ERROR("FolderPacker: pack timeout for %s", qPrintable(folderPath));
        return false;
    }

    if (proc.exitCode() != 0) {
        LOG_ERROR("FolderPacker: zip failed with exit code %d", proc.exitCode());
        return false;
    }
#endif

    LOG_INFO("FolderPacker: packed %s -> %s", qPrintable(folderPath), qPrintable(outputPath));
    return true;
}
