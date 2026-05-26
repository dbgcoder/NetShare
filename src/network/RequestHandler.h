#ifndef REQUESTHANDLER_H
#define REQUESTHANDLER_H

#include <QObject>
#include <QTimer>
#include "HttpServer.h"
#include "IShareManager.h"
#include "IFileBrowser.h"
#include "IFolderPacker.h"

struct UploadedFile {
    QString fileName;
    QByteArray data;
};

class FileTransferEngine;
class StreamingMultipartParser;
class TransferLogService;

struct StreamingUploadState {
    StreamingMultipartParser* parser;
    QString token;
    QString remoteAddress;
    bool isReceive;
    bool finished = false;
    qint64 currentFileTransferred = 0; // Bytes transferred for current file being parsed
    QString uploadTaskId;              // FileTransferEngine task ID for progress tracking
};

// Streaming state for /api/upload/file route (chunked or small file upload)
struct StreamingFileUploadState {
    QString sessionId;
    QString filePath;
    int chunkIndex = -1;       // -1 means small file mode (multipart)
    bool isChunked = false;
    bool finished = false;
    // Chunked mode: file handle for streaming write
    QFile* chunkFile = nullptr;
    qint64 bytesReceived = 0;
    qint64 expectedSize = 0;
    // Small file mode: streaming multipart parser
    StreamingMultipartParser* parser = nullptr;
};

struct SavedFileInfo {
    QString fileName;
    QString savePath;
    qint64 fileSize;
};

struct ChunkUploadInfo {
    int chunkIndex = 0;
    qint64 offset = 0;
    qint64 size = 0;
    bool completed = false;
};

struct FileChunkState {
    QString relativePath;       // File relative path
    qint64 fileSize = 0;        // Total file size
    int chunkSize = 0;          // Chunk size in bytes
    int totalChunks = 0;        // Total number of chunks
    QList<ChunkUploadInfo> chunks; // Per-chunk state
    int completedChunks = 0;    // Number of completed chunks
    bool useChunking = false;   // Whether to use chunked upload
};

struct UploadSession {
    QString sessionId;
    QString taskId;         // FileTransferEngine task ID
    QString folder;         // Folder root name
    qint64 totalSize;       // Total upload size
    qint64 transferredSize; // Bytes transferred so far
    int fileCount;          // Total file count
    QString remoteAddress;
    QList<SavedFileInfo> savedFiles; // Files saved by /api/upload/file (small file mode) or merged files
    QDateTime createdAt;    // Session creation time for timeout cleanup
    // Chunked upload state
    QMap<QString, FileChunkState> fileChunkStates; // key = relativePath
    QString chunkTempDir;   // Temporary chunk directory: <uploadDir>/.chunks/<sessionId>
};

class RequestHandler : public QObject
{
    Q_OBJECT

public:
    explicit RequestHandler(IShareManager* shareManager,
                            IFileBrowser* fileBrowser,
                            IFolderPacker* folderPacker,
                            QObject* parent = nullptr);
    ~RequestHandler() override;

    void registerRoutes(HttpServer* server);

    void setUploadDir(const QString& dir);
    QString uploadDir() const;
    void setSettingsManager(class SettingsManager* sm);

    void setTransferEngine(FileTransferEngine* engine);
    void setTransferLogService(TransferLogService* service);

    // Get token for a given task ID (for WebSocket progress routing)
    QString tokenForTask(const QString& taskId) const;

    // Get share token for a given task ID (for WebSocket progress routing to share page subscribers)
    QString shareTokenForTask(const QString& taskId) const;

signals:
    void fileDownloaded(const QString& fileName, qint64 fileSize, const QString& remoteAddress);
    void fileUploaded(const QString& fileName, qint64 fileSize, const QString& remoteAddress);

private:
    void handleSharePage(const HttpRequest& request, HttpResponse& response);
    void handleFileDownload(const HttpRequest& request, HttpResponse& response);
    void handleFolderDownload(const HttpRequest& request, HttpResponse& response);
    void handleApiShares(const HttpRequest& request, HttpResponse& response);
    void handleApiFiles(const HttpRequest& request, HttpResponse& response);
    void handleUploadPage(const HttpRequest& request, HttpResponse& response);

    // Streaming upload handler - called with each body chunk (legacy /receive route)
    void handleStreamingUpload(QTcpSocket* socket, const HttpRequest& request, const QByteArray& chunk, bool isLast);

    // Streaming file upload handler for /api/upload/file route
    void handleStreamingFileUpload(QTcpSocket* socket, const HttpRequest& request, const QByteArray& chunk, bool isLast);

    // Resume-capable upload handlers (chunked upload for large files, direct upload for small files)
    void handleUploadCheck(const HttpRequest& request, HttpResponse& response);
    void handleUploadSingleFile(const HttpRequest& request, HttpResponse& response);
    void handleUploadFinalize(const HttpRequest& request, HttpResponse& response);
    void handleUploadAbort(const HttpRequest& request, HttpResponse& response);
    void cleanupExpiredSessions();

    QByteArray generateSharePage(const QString& token, const QString& filePath, bool isFolder) const;
    QByteArray generatePasswordPage(const QString& token) const;
    QByteArray generateErrorPage(const QString& title, const QString& message) const;
    QByteArray generateUploadPage(const QString& token) const;

    QString mimeTypeForFile(const QString& fileName) const;
    QList<UploadedFile> parseMultipartFormData(const QByteArray& body, const QString& contentType) const;

    void recordCompletedTransfer(int type, const QString& fileName, qint64 fileSize,
                                 const QString& remoteAddress, const QString& filePath = QString());

    IShareManager* m_shareManager;
    IFileBrowser* m_fileBrowser;
    IFolderPacker* m_folderPacker;
    FileTransferEngine* m_transferEngine;
    TransferLogService* m_transferLogService;
    SettingsManager* m_settingsManager;
    QString m_uploadDir;
    HttpServer* m_httpServer = nullptr;

    // Per-socket streaming upload state (legacy /receive route only)
    QHash<QTcpSocket*, StreamingUploadState*> m_streamingStates;

    // Per-socket streaming file upload state (/api/upload/file route)
    QHash<QTcpSocket*, StreamingFileUploadState*> m_streamingFileStates;

    // Upload sessions for tracking folder upload progress
    QHash<QString, UploadSession> m_uploadSessions;

    // Timer for cleaning up expired upload sessions
    QTimer* m_sessionCleanupTimer = nullptr;

    // Task ID to token mapping for WebSocket progress routing
    QMap<QString, QString> m_taskToToken;
    QMap<QString, QString> m_taskToShareToken;
};

#endif
