#ifndef STREAMINGMULTIPARTPARSER_H
#define STREAMINGMULTIPARTPARSER_H

#include <QString>
#include <QByteArray>
#include <QList>
#include <QFile>
#include <functional>

class StreamingMultipartParser
{
public:
    struct SavedFile {
        QString fileName;    // relative path (may include directories)
        QString savePath;    // absolute path where saved
        qint64 fileSize;     // file size in bytes
    };

    using ProgressCallback = std::function<void(qint64 bytesWritten)>;

    explicit StreamingMultipartParser(const QString& saveDir);
    ~StreamingMultipartParser();

    // Initialize with Content-Type header value (extracts boundary)
    bool init(const QString& contentType);

    // Feed a data chunk. Data is streamed to disk incrementally.
    void feed(const QByteArray& data);

    // Finalize: call when all data has been fed. Returns all saved files.
    QList<SavedFile> finish();

    bool isFolderUpload() const { return m_isFolderUpload; }
    QString folderRoot() const { return m_folderRoot; }
    QString saveDir() const { return m_saveDir; }
    qint64 totalSize() const { return m_totalSize; }
    int fileCount() const { return m_savedFiles.size(); }
    const QList<SavedFile>& savedFiles() const { return m_savedFiles; }

    // Set progress callback - called each time file data is written to disk
    void setProgressCallback(ProgressCallback callback) { m_progressCallback = callback; }

    // Resume support: set offset and file path for appending to a partially uploaded file
    void setResumeOffset(qint64 offset) { m_resumeOffset = offset; }
    void setResumeFilePath(const QString& path) { m_resumeFilePath = path; }

private:
    enum class ParseState {
        FindBoundary,    // Looking for the next boundary marker
        ParseHeaders,    // Parsing part headers (Content-Disposition etc.)
        StreamFileData,  // Streaming file data to disk
        BufferFormData   // Buffering non-file form field value
    };

    void processBuffer();
    void handleFindBoundary();
    void handleParseHeaders();
    void handleStreamFileData();
    void handleBufferFormData();
    void closeCurrentFile();
    void saveCurrentFile();

    QByteArray m_boundaryBytes;  // "--boundary"
    QByteArray m_endBoundary;    // "--boundary--"
    QByteArray m_buffer;
    int m_bufferOffset;          // Start of unprocessed data in m_buffer
    QString m_saveDir;
    QList<SavedFile> m_savedFiles;
    bool m_isFolderUpload = false;
    QString m_folderRoot;
    qint64 m_totalSize = 0;

    // State machine fields
    ParseState m_state = ParseState::FindBoundary;
    QFile* m_currentFile = nullptr;
    QString m_currentFileName;   // Relative path for current file
    QString m_currentSavePath;   // Absolute save path for current file
    qint64 m_currentFileBytesWritten = 0; // Bytes written so far for current file

    // Threshold: how many bytes of m_buffer to keep for boundary detection overlap
    int m_overlapSize = 0;

    // Resume support
    qint64 m_resumeOffset = 0;       // Bytes already on disk for the resumed file
    QString m_resumeFilePath;         // Absolute path of the partial file to append to

    ProgressCallback m_progressCallback;
};

#endif
