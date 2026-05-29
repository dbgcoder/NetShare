#ifndef REQUESTHANDLER_H
#define REQUESTHANDLER_H

#include <QObject>
#include <QTimer>
#include "CivetWebServer.h"
#include "IShareManager.h"
#include "IFileBrowser.h"
#include "IFolderPacker.h"
#include "StreamingMultipartParser.h"
#include "ChunkState.h"

struct UploadedFile {
    QString fileName;
    QByteArray data;
};

class FileTransferEngine;
class TransferLogService;
class ChatService;

struct StreamingUploadState {
    StreamingMultipartParser* parser;
    QString token;
    QString remoteAddress;
    bool isReceive;
    bool finished = false;
    qint64 currentFileTransferred = 0;
    qint64 bytesReceived = 0;
    QString uploadTaskId;

    StreamingUploadState() : parser(nullptr), isReceive(false) {}
};

struct StreamingFileUploadState {
    QString sessionId;
    QString filePath;
    int chunkIndex = -1;
    bool isChunked = false;
    bool finished = false;
    QFile* chunkFile = nullptr;
    qint64 bytesReceived = 0;
    qint64 expectedSize = 0;
    StreamingMultipartParser* parser;

    StreamingFileUploadState() : parser(nullptr) {}
};

struct SavedFileInfo {
    QString fileName;
    QString savePath;
    qint64 fileSize;
};

struct UploadSession {
    QString sessionId;
    QString taskId;
    QString folder;
    qint64 totalSize;
    qint64 transferredSize;
    int fileCount;
    QString remoteAddress;
    QList<SavedFileInfo> savedFiles;
    QDateTime createdAt;
    QMap<QString, ChunkStateInfo> fileChunkStates;
    QString chunkTempDir;
    bool paused = false;
    QDateTime pausedAt;
};

Q_DECLARE_METATYPE(SavedFileInfo)

class RequestHandler : public QObject
{
    Q_OBJECT

public:
    explicit RequestHandler(IShareManager* shareManager,
                            IFileBrowser* fileBrowser,
                            IFolderPacker* folderPacker,
                            QObject* parent = nullptr);
    ~RequestHandler() override;

    void registerRoutes(CivetWebServer* server);

    void setUploadDir(const QString& dir);
    QString uploadDir() const;
    void setSettingsManager(class SettingsManager* sm);

    void setTransferEngine(FileTransferEngine* engine);
    void setTransferLogService(TransferLogService* service);
    void setChatService(ChatService* chatService);

    QString tokenForTask(const QString& taskId) const;
    QString shareTokenForTask(const QString& taskId) const;

    void pauseUploadForTask(const QString& taskId);
    void resumeUploadForTask(const QString& taskId);

signals:
    void fileDownloaded(const QString& fileName, qint64 fileSize, const QString& remoteAddress);
    void fileUploaded(const QString& fileName, qint64 fileSize, const QString& remoteAddress);

    void uploadFinalizeReady(const QList<SavedFileInfo>& savedFiles, const QString& taskId,
                             const QString& folder, bool isFolder, qint64 totalSize,
                             const QString& remoteAddr);

private slots:
    void onUploadFinalizeReady(const QList<SavedFileInfo>& savedFiles, const QString& taskId,
                               const QString& folder, bool isFolder, qint64 totalSize,
                               const QString& remoteAddr);

private:
    int handleSharePage(mg_connection* conn, const HttpRequestInfo& info);
    int handleFileDownload(mg_connection* conn, const HttpRequestInfo& info);
    int handleFolderDownload(mg_connection* conn, const HttpRequestInfo& info);
    int handleApiShares(mg_connection* conn, const HttpRequestInfo& info);
    int handleApiFiles(mg_connection* conn, const HttpRequestInfo& info);
    int handleUploadPage(mg_connection* conn, const HttpRequestInfo& info);

    int handleStreamingUpload(mg_connection* conn, const HttpRequestInfo& info,
                              const QByteArray& chunk, bool isLast);

    int handleStreamingFileUpload(mg_connection* conn, const HttpRequestInfo& info,
                                  const QByteArray& chunk, bool isLast);

    int handleUploadCheck(mg_connection* conn, const HttpRequestInfo& info);
    int handleUploadSingleFile(mg_connection* conn, const HttpRequestInfo& info);
    int handleUploadFinalize(mg_connection* conn, const HttpRequestInfo& info);
    int handleUploadAbort(mg_connection* conn, const HttpRequestInfo& info);
    int handleUploadPause(mg_connection* conn, const HttpRequestInfo& info);
    int handleUploadResume(mg_connection* conn, const HttpRequestInfo& info);
    int handleUploadPending(mg_connection* conn, const HttpRequestInfo& info);
    int handleChatMessage(mg_connection* conn, const HttpRequestInfo& info);
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
    ChatService* m_chatService = nullptr;
    SettingsManager* m_settingsManager;
    QString m_uploadDir;
    CivetWebServer* m_civetServer = nullptr;

    QHash<mg_connection*, StreamingUploadState*> m_streamingStates;
    QHash<mg_connection*, StreamingFileUploadState*> m_streamingFileStates;
    QHash<QString, UploadSession> m_uploadSessions;
    QTimer* m_sessionCleanupTimer = nullptr;
    QMap<QString, QString> m_taskToToken;
    QMap<QString, QString> m_taskToShareToken;
};

#endif
