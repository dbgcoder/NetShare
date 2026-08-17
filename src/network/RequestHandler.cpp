#include "RequestHandler.h"
#include "ShareManager.h"
#include "FileBrowser.h"
#include "FolderPacker.h"
#include "FileTransferEngine.h"
#include "StreamingMultipartParser.h"
#include "ChunkManager.h"
#include "ChunkStateManager.h"
#include "TransferLogService.h"
#include "SettingsManager.h"
#include "chat/ChatService.h"
#include "Logger.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QMimeDatabase>
#include <QUrl>
#include <QUuid>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThreadPool>

static QMap<QString, QString> parseQueryString(const QString& qs)
{
    QMap<QString, QString> result;
    for (const auto& pair : qs.split('&')) {
        int eq = pair.indexOf('=');
        if (eq > 0) {
            result[QUrl::fromPercentEncoding(pair.left(eq).toUtf8())] =
                QUrl::fromPercentEncoding(pair.mid(eq + 1).toUtf8());
        }
    }
    return result;
}

RequestHandler::RequestHandler(IShareManager* shareManager,
                               IFileBrowser* fileBrowser,
                               IFolderPacker* folderPacker,
                               QObject* parent)
    : QObject(parent)
    , m_shareManager(shareManager)
    , m_fileBrowser(fileBrowser)
    , m_folderPacker(folderPacker)
    , m_transferEngine(nullptr)
    , m_transferLogService(nullptr)
    , m_settingsManager(nullptr)
{
    m_sessionCleanupTimer = new QTimer(this);
    connect(m_sessionCleanupTimer, &QTimer::timeout, this, &RequestHandler::cleanupExpiredSessions);
    m_sessionCleanupTimer->start(5 * 60 * 1000);

    qRegisterMetaType<QList<SavedFileInfo>>("QList<SavedFileInfo>");
    connect(this, &RequestHandler::uploadFinalizeReady,
            this, &RequestHandler::onUploadFinalizeReady);
}

RequestHandler::~RequestHandler()
{
    for (auto it = m_streamingStates.begin(); it != m_streamingStates.end(); ++it) {
        delete it.value()->parser;
        delete it.value();
    }
    m_streamingStates.clear();
}

void RequestHandler::setUploadDir(const QString& dir)
{
    m_uploadDir = dir;
}

QString RequestHandler::uploadDir() const
{
    return m_uploadDir;
}

void RequestHandler::setSettingsManager(SettingsManager* sm)
{
    m_settingsManager = sm;
}

void RequestHandler::setTransferEngine(FileTransferEngine* engine)
{
    m_transferEngine = engine;
}

void RequestHandler::setTransferLogService(TransferLogService* service)
{
    m_transferLogService = service;
}

void RequestHandler::setChatService(ChatService* chatService)
{
    m_chatService = chatService;
}

QString RequestHandler::tokenForTask(const QString& taskId) const
{
    return m_taskToToken.value(taskId, QString());
}

QString RequestHandler::shareTokenForTask(const QString& taskId) const
{
    return m_taskToShareToken.value(taskId, QString());
}

void RequestHandler::pauseUploadForTask(const QString& taskId)
{
    QString sessionId = m_taskToToken.value(taskId);
    if (!sessionId.isEmpty() && m_uploadSessions.contains(sessionId)) {
        auto& session = m_uploadSessions[sessionId];
        session.paused = true;
        session.pausedAt = QDateTime::currentDateTime();
        LOG_INFO("Server-side pause for task %s, session=%s", qPrintable(taskId), qPrintable(sessionId));
    }
}

void RequestHandler::resumeUploadForTask(const QString& taskId)
{
    QString sessionId = m_taskToToken.value(taskId);
    if (!sessionId.isEmpty() && m_uploadSessions.contains(sessionId)) {
        auto& session = m_uploadSessions[sessionId];
        session.paused = false;
        session.pausedAt = QDateTime();
        LOG_INFO("Server-side resume for task %s, session=%s", qPrintable(taskId), qPrintable(sessionId));
    }
}

void RequestHandler::registerRoutes(CivetWebServer* server)
{
    m_civetServer = server;

    server->addRoute("GET", "/", [this](mg_connection* conn, const HttpRequestInfo& info) {
        QString lang = langParam(info);
        QMap<QString, QString> vars;
        vars["LANG"] = lang;
        QString html = loadWebFile("home.html", vars);
        if (html.isEmpty()) {
            CivetWebServer::sendHtmlResponse(conn, 500, QByteArrayLiteral("Failed to load home page"));
            return 500;
        }
        CivetWebServer::sendHtmlResponse(conn, 200, html.toUtf8());
        return 200;
    });

    server->addRoute("GET", "/s/*", [this](mg_connection* conn, const HttpRequestInfo& info) {
        return handleSharePage(conn, info);
    });

    server->addRoute("GET", "/download/*", [this](mg_connection* conn, const HttpRequestInfo& info) {
        return handleFileDownload(conn, info);
    });

    server->addRoute("HEAD", "/download/*", [this](mg_connection* conn, const HttpRequestInfo& info) {
        return handleFileDownload(conn, info);
    });

    server->addRoute("GET", "/folder/*", [this](mg_connection* conn, const HttpRequestInfo& info) {
        return handleFolderDownload(conn, info);
    });

    server->addRoute("GET", "/api/shares", [this](mg_connection* conn, const HttpRequestInfo& info) {
        return handleApiShares(conn, info);
    });

    server->addRoute("GET", "/api/files/*", [this](mg_connection* conn, const HttpRequestInfo& info) {
        return handleApiFiles(conn, info);
    });

    server->addRoute("GET", "/receive", [this](mg_connection* conn, const HttpRequestInfo& info) {
        Q_UNUSED(info)
        QString html = loadWebFile("receive.html");
        if (html.isEmpty()) {
            CivetWebServer::sendHtmlResponse(conn, 500, QByteArrayLiteral("\xe6\x97\xa0\xe6\xb3\x95\xe5\x8a\xa0\xe8\xbd\xbd\xe6\x8e\xa5\xe6\x94\xb6\xe9\xa1\xb5\xe9\x9d\xa2"));
            return 500;
        }
        CivetWebServer::sendHtmlResponse(conn, 200, html.toUtf8());
        return 200;
    });

    server->addStreamingRoute("POST", "/receive", [this](mg_connection* conn, const HttpRequestInfo& info,
                                                         const QByteArray& chunk, bool isLast) {
        return handleStreamingUpload(conn, info, chunk, isLast);
    });

    server->addRoute("POST", "/api/upload/check", [this](mg_connection* conn, const HttpRequestInfo& info) {
        return handleUploadCheck(conn, info);
    });

    server->addStreamingRoute("POST", "/api/upload/file", [this](mg_connection* conn, const HttpRequestInfo& info,
                                                                 const QByteArray& chunk, bool isLast) {
        return handleStreamingFileUpload(conn, info, chunk, isLast);
    });

    server->addRoute("POST", "/api/upload/finalize", [this](mg_connection* conn, const HttpRequestInfo& info) {
        return handleUploadFinalize(conn, info);
    });

    server->addRoute("POST", "/api/upload/abort", [this](mg_connection* conn, const HttpRequestInfo& info) {
        return handleUploadAbort(conn, info);
    });

    server->addRoute("POST", "/api/upload/pause", [this](mg_connection* conn, const HttpRequestInfo& info) {
        return handleUploadPause(conn, info);
    });

    server->addRoute("POST", "/api/upload/resume", [this](mg_connection* conn, const HttpRequestInfo& info) {
        return handleUploadResume(conn, info);
    });

    server->addRoute("POST", "/api/upload/single", [this](mg_connection* conn, const HttpRequestInfo& info) {
        return handleUploadSingleFile(conn, info);
    });

    server->addRoute("GET", "/api/upload/pending", [this](mg_connection* conn, const HttpRequestInfo& info) {
        return handleUploadPending(conn, info);
    });

    server->addRoute("POST", "/api/chat/message", [this](mg_connection* conn, const HttpRequestInfo& info) {
        return handleChatMessage(conn, info);
    });

    server->addRoute("GET", "/api/language", [this](mg_connection* conn, const HttpRequestInfo& info) {
        Q_UNUSED(info)
        int langIndex = 0;
        if (m_settingsManager) {
            langIndex = m_settingsManager->value("General/Language", 0).toInt();
        }
        QString langCode = (langIndex == 1) ? "en" : "zh";
        QJsonObject obj;
        obj["language"] = langCode;
        CivetWebServer::sendJsonResponse(conn, 200, QJsonDocument(obj).toJson(QJsonDocument::Compact));
        return 200;
    });

    server->addStreamingRoute("POST", "/upload/*", [this](mg_connection* conn, const HttpRequestInfo& info,
                                                          const QByteArray& chunk, bool isLast) {
        return handleStreamingUpload(conn, info, chunk, isLast);
    });

    connect(server, &CivetWebServer::streamingConnDisconnected,
            this, [this](mg_connection* conn) {
        if (m_streamingStates.contains(conn)) {
            auto* state = m_streamingStates[conn];
            if (m_transferEngine && !state->uploadTaskId.isEmpty())
                m_transferEngine->failTask(state->uploadTaskId, "Upload interrupted: connection lost");
            if (!state->uploadTaskId.isEmpty()) {
                m_taskToToken.remove(state->uploadTaskId);
                m_taskToShareToken.remove(state->uploadTaskId);
            }
            delete state->parser;
            delete state;
            m_streamingStates.remove(conn);
            LOG_INFO("Cleaned up streaming upload state on connection close");
        }
        if (m_streamingFileStates.contains(conn)) {
            auto* state = m_streamingFileStates[conn];
            if (state->isChunked && !state->sessionId.isEmpty() && m_uploadSessions.contains(state->sessionId)) {
                auto& session = m_uploadSessions[state->sessionId];
                if (session.fileChunkStates.contains(state->filePath)) {
                    auto& csi = session.fileChunkStates[state->filePath];
                    if (state->chunkIndex >= 0 && state->chunkIndex < csi.chunks.size()) {
                        csi.chunks[state->chunkIndex].status = QStringLiteral("partial");
                        csi.chunks[state->chunkIndex].downloaded = state->bytesReceived;
                    }
                }
                ChunkStateManager* csm = m_transferEngine ? m_transferEngine->chunkStateManager() : nullptr;
                if (csm && state->chunkIndex >= 0) {
                    QString stateFilePath = uploadDir() + "/.chunks/" + state->filePath + ".netshare";
                    csm->updateChunkStatus(stateFilePath, state->chunkIndex, QStringLiteral("partial"), state->bytesReceived);
                }
            }
            if (state->chunkFile) { state->chunkFile->close(); delete state->chunkFile; }
            if (state->parser) delete state->parser;
            delete state;
            m_streamingFileStates.remove(conn);
            LOG_INFO("Cleaned up streaming file upload state on connection close");
        }
    });
}

int RequestHandler::handleSharePage(mg_connection* conn, const HttpRequestInfo& info)
{
    QString lang = langParam(info);
    QString token = info.uri.mid(3);
    if (token.isEmpty()) {
        CivetWebServer::sendHtmlResponse(conn, 404, QByteArrayLiteral("\xe5\x88\x86\xe4\xba\xab\xe4\xb8\x8d\xe5\xad\x98\xe5\x9c\xa8"));
        return 404;
    }

    ShareInfo shareInfo = m_shareManager->getShareInfo(token);
    if (!shareInfo.isValid() || shareInfo.isExpired()) {
        LOG_WARN("Share access rejected: token=%s", qPrintable(token));
        QMap<QString, QString> vars;
        vars["LANG"] = lang;
        CivetWebServer::sendHtmlResponse(conn, 200, loadWebFile("error.html", vars).toUtf8());
        return 200;
    }

    if (shareInfo.passwordRequired) {
        auto qp = parseQueryString(info.queryString);
        QString pw = qp.value("pw");
        if (pw.isEmpty() || !m_shareManager->validateShare(token, pw)) {
            QMap<QString, QString> vars;
            vars["TOKEN"] = token;
            vars["LANG"] = lang;
            CivetWebServer::sendHtmlResponse(conn, 200, loadWebFile("password.html", vars).toUtf8());
            return 200;
        }
    }

    m_shareManager->shareAccessed(token);
    QFileInfo fi(shareInfo.filePath);
    QMap<QString, QString> vars;
    vars["TOKEN"] = token;
    vars["LANG"] = lang;
    vars["IS_FOLDER"] = shareInfo.isFolder ? "true" : "false";
    vars["FOLDER_NAME"] = fi.fileName();
    CivetWebServer::sendHtmlResponse(conn, 200, loadWebFile("share.html", vars).toUtf8());
    return 200;
}

int RequestHandler::handleFileDownload(mg_connection* conn, const HttpRequestInfo& info)
{
    QString tokenAndPath = info.uri.mid(10);
    int slashIndex = tokenAndPath.indexOf('/');
    if (slashIndex < 0) {
        CivetWebServer::sendHtmlResponse(conn, 400, QByteArrayLiteral("Invalid download URL"));
        return 400;
    }

    QString token = tokenAndPath.left(slashIndex);
    QString subPath = QUrl::fromPercentEncoding(tokenAndPath.mid(slashIndex + 1).toUtf8());

    ShareInfo shareInfo = m_shareManager->getShareInfo(token);
    if (!shareInfo.isValid() || shareInfo.isExpired()) {
        CivetWebServer::sendHtmlResponse(conn, 404, QByteArrayLiteral("\xe5\x88\x86\xe4\xba\xab\xe4\xb8\x8d\xe5\xad\x98\xe5\x9c\xa8\xe6\x88\x96\xe5\xb7\xb2\xe8\xbf\x87\xe6\x9c\x9f"));
        return 404;
    }

    if (shareInfo.passwordRequired) {
        auto qp = parseQueryString(info.queryString);
        QString pw = qp.value("pw");
        if (!m_shareManager->validateShare(token, pw)) {
            CivetWebServer::sendHtmlResponse(conn, 400, QByteArrayLiteral("\xe5\xaf\x86\xe7\xa0\x81\xe9\x94\x99\xe8\xaf\xaf"));
            return 400;
        }
    }

    QString filePath = subPath.isEmpty() ? shareInfo.filePath : shareInfo.filePath + "/" + subPath;

    QFileInfo fi(filePath);
    if (!fi.exists() || fi.isDir()) {
        CivetWebServer::sendHtmlResponse(conn, 404, QByteArrayLiteral("\xe6\x96\x87\xe4\xbb\xb6\xe4\xb8\x8d\xe5\xad\x98\xe5\x9c\xa8"));
        return 404;
    }

    QString contentType = mimeTypeForFile(fi.fileName());
    QString rangeHeader = info.headers.value("Range");

    if (info.method == QStringLiteral("HEAD")) {
        QByteArray ct = contentType.toUtf8();
        if (ct.isEmpty()) ct = "application/octet-stream";
        mg_printf(conn,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %lld\r\n"
            "Accept-Ranges: bytes\r\n"
            "Cache-Control: no-cache\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Expose-Headers: Content-Length, Accept-Ranges, Content-Range\r\n"
            "\r\n",
            ct.constData(), fi.size());
        return 200;
    }

    qint64 startByte = 0;
    if (!rangeHeader.isEmpty()) {
        QRegularExpression rangeRe("bytes=(\\d+)-");
        auto match = rangeRe.match(rangeHeader);
        if (match.hasMatch()) {
            startByte = match.captured(1).toLongLong();
        }
    }

    QString downloadTaskId;
    if (m_transferEngine) {
        QString capturedFileName = fi.fileName();
        QString capturedFilePath = filePath;
        qint64 capturedFileSize = fi.size();
        QMetaObject::invokeMethod(m_transferEngine, [this, &downloadTaskId, capturedFileName, capturedFilePath, capturedFileSize, startByte]() {
            downloadTaskId = m_transferEngine->resumeOrCreateDownloadTask(capturedFileName, capturedFilePath, capturedFileSize, startByte);
        }, Qt::BlockingQueuedConnection);
    }

    QString capturedTaskId = downloadTaskId;
    qint64 sent = CivetWebServer::sendStreamingFileResponse(conn, filePath, contentType, fi.fileName(), rangeHeader,
        [this, capturedTaskId](qint64 totalSent, qint64 fileSize) {
            Q_UNUSED(fileSize)
            if (m_transferEngine && !capturedTaskId.isEmpty()) {
                QMetaObject::invokeMethod(m_transferEngine, [this, capturedTaskId, totalSent]() {
                    m_transferEngine->updateTaskProgress(capturedTaskId, totalSent);
                }, Qt::QueuedConnection);
            }
        });

    if (sent > 0 && (rangeHeader.isEmpty() ? sent == fi.size() : true)) {
        LOG_INFO("File download: %s (%lld bytes) from %s",
                 qPrintable(fi.fileName()), fi.size(), qPrintable(info.remoteAddress));
        if (m_transferEngine && !downloadTaskId.isEmpty()) {
            QMetaObject::invokeMethod(m_transferEngine, [this, downloadTaskId]() {
                m_transferEngine->completeTask(downloadTaskId);
            }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(this, [this, fi, info, filePath]() {
            recordCompletedTransfer(0, fi.fileName(), fi.size(), info.remoteAddress, filePath);
        }, Qt::QueuedConnection);
    } else {
        LOG_WARN("File download interrupted: %s from %s",
                 qPrintable(fi.fileName()), qPrintable(info.remoteAddress));
        if (m_transferEngine && !downloadTaskId.isEmpty()) {
            QMetaObject::invokeMethod(m_transferEngine, [this, downloadTaskId]() {
                m_transferEngine->failTask(downloadTaskId, "Download interrupted");
            }, Qt::QueuedConnection);
        }
    }

    return 0;
}

int RequestHandler::handleFolderDownload(mg_connection* conn, const HttpRequestInfo& info)
{
    QString token = info.uri.mid(9);
    if (token.isEmpty()) {
        CivetWebServer::sendHtmlResponse(conn, 400, QByteArrayLiteral("Invalid folder download URL"));
        return 400;
    }

    ShareInfo shareInfo = m_shareManager->getShareInfo(token);
    if (!shareInfo.isValid() || shareInfo.isExpired()) {
        CivetWebServer::sendHtmlResponse(conn, 404, QByteArrayLiteral("\xe5\x88\x86\xe4\xba\xab\xe4\xb8\x8d\xe5\xad\x98\xe5\x9c\xa8\xe6\x88\x96\xe5\xb7\xb2\xe8\xbf\x87\xe6\x9c\x9f"));
        return 404;
    }

    if (!shareInfo.isFolder) {
        CivetWebServer::sendHtmlResponse(conn, 400, QByteArrayLiteral("Not a folder share"));
        return 400;
    }

    if (shareInfo.passwordRequired) {
        auto qp = parseQueryString(info.queryString);
        QString pw = qp.value("pw");
        if (!m_shareManager->validateShare(token, pw)) {
            CivetWebServer::sendHtmlResponse(conn, 400, QByteArrayLiteral("\xe5\xaf\x86\xe7\xa0\x81\xe9\x94\x99\xe8\xaf\xaf"));
            return 400;
        }
    }

    QFileInfo fi(shareInfo.filePath);
    QString outputPath = m_folderPacker->defaultOutputPath(shareInfo.filePath);

    if (!m_folderPacker->packFolder(shareInfo.filePath, outputPath)) {
        CivetWebServer::sendHtmlResponse(conn, 500, QByteArrayLiteral("\xe6\x96\x87\xe4\xbb\xb6\xe5\xa4\xb9\xe6\x89\x93\xe5\x8c\x85\xe5\xa4\xb1\xe8\xb4\xa5"));
        return 500;
    }

    QFileInfo zipFi(outputPath);
    QString zipName = fi.fileName() + ".zip";

    QString rangeHeader = info.headers.value("Range");
    qint64 startByte = 0;
    if (!rangeHeader.isEmpty()) {
        QRegularExpression rangeRe("bytes=(\\d+)-");
        auto match = rangeRe.match(rangeHeader);
        if (match.hasMatch()) {
            startByte = match.captured(1).toLongLong();
        }
    }

    QString downloadTaskId;
    if (m_transferEngine) {
        QString capturedZipName = zipName;
        QString capturedOutputPath = outputPath;
        qint64 capturedZipSize = zipFi.size();
        QMetaObject::invokeMethod(m_transferEngine, [this, &downloadTaskId, capturedZipName, capturedOutputPath, capturedZipSize, startByte]() {
            downloadTaskId = m_transferEngine->resumeOrCreateDownloadTask(capturedZipName, capturedOutputPath, capturedZipSize, startByte);
        }, Qt::BlockingQueuedConnection);
    }
    QString capturedTaskId = downloadTaskId;
    qint64 sent = CivetWebServer::sendStreamingFileResponse(conn, outputPath, QStringLiteral("application/zip"),
                                               zipName, rangeHeader,
        [this, capturedTaskId](qint64 totalSent, qint64 fileSize) {
            Q_UNUSED(fileSize)
            if (m_transferEngine && !capturedTaskId.isEmpty()) {
                QMetaObject::invokeMethod(m_transferEngine, [this, capturedTaskId, totalSent]() {
                    m_transferEngine->updateTaskProgress(capturedTaskId, totalSent);
                }, Qt::QueuedConnection);
            }
        });

    if (sent > 0 && (rangeHeader.isEmpty() ? sent == zipFi.size() : true)) {
        LOG_INFO("Folder download: %s from %s", qPrintable(fi.fileName()), qPrintable(info.remoteAddress));
        if (m_transferEngine && !downloadTaskId.isEmpty()) {
            QMetaObject::invokeMethod(m_transferEngine, [this, downloadTaskId]() {
                m_transferEngine->completeTask(downloadTaskId);
            }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(this, [this, zipName, zipFi, info, outputPath]() {
            recordCompletedTransfer(0, zipName, zipFi.size(), info.remoteAddress, outputPath);
        }, Qt::QueuedConnection);
    } else {
        LOG_WARN("Folder download interrupted: %s from %s", qPrintable(fi.fileName()), qPrintable(info.remoteAddress));
        if (m_transferEngine && !downloadTaskId.isEmpty()) {
            QMetaObject::invokeMethod(m_transferEngine, [this, downloadTaskId]() {
                m_transferEngine->failTask(downloadTaskId, "Download interrupted");
            }, Qt::QueuedConnection);
        }
    }

    return 0;
}

int RequestHandler::handleApiShares(mg_connection* conn, const HttpRequestInfo& info)
{
    Q_UNUSED(info)
    QVariantList shares = m_shareManager->getActiveShares();

    QByteArray json = "[";
    for (int i = 0; i < shares.size(); ++i) {
        ShareInfo si = shares[i].value<ShareInfo>();
        if (i > 0) json.append(",");
        json.append(QString("{\"token\":\"%1\",\"filePath\":\"%2\",\"isFolder\":%3,\"fileSize\":%4,"
                           "\"downloadCount\":%5,\"passwordRequired\":%6}")
                    .arg(si.token)
                    .arg(QString(si.filePath).replace("\\", "/").replace("\"", "\\\""))
                    .arg(si.isFolder ? "true" : "false")
                    .arg(si.fileSize).arg(si.downloadCount)
                    .arg(si.passwordRequired ? "true" : "false").toUtf8());
    }
    json.append("]");
    CivetWebServer::sendJsonResponse(conn, 200, json);
    return 200;
}

int RequestHandler::handleApiFiles(mg_connection* conn, const HttpRequestInfo& info)
{
    QString tokenAndPath = info.uri.mid(11);
    int slashIndex = tokenAndPath.indexOf('/');
    QString token = slashIndex >= 0 ? tokenAndPath.left(slashIndex) : tokenAndPath;
    QString subPath = slashIndex >= 0 ? QUrl::fromPercentEncoding(tokenAndPath.mid(slashIndex + 1).toUtf8()) : "";

    ShareInfo si = m_shareManager->getShareInfo(token);
    if (!si.isValid() || si.isExpired()) {
        CivetWebServer::sendJsonResponse(conn, 404, "{\"error\":\"Share not found\"}");
        return 404;
    }

    QString dirPath = si.filePath;
    if (!subPath.isEmpty()) dirPath = si.filePath + "/" + subPath;

    if (!m_fileBrowser->isDirectory(dirPath)) {
        CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"Not a directory\"}");
        return 400;
    }

    QVariantList entries = m_fileBrowser->listDirectory(dirPath);

    QByteArray json = "[";
    for (int i = 0; i < entries.size(); ++i) {
        FileEntry entry = entries[i].value<FileEntry>();
        if (i > 0) json.append(",");
        json.append(QString("{\"name\":\"%1\",\"isFolder\":%2,\"fileSize\":%3,\"displaySize\":\"%4\","
                           "\"extension\":\"%5\"}")
                    .arg(QString(entry.name).replace("\"", "\\\""))
                    .arg(entry.isFolder ? "true" : "false")
                    .arg(entry.fileSize).arg(entry.displaySize).arg(entry.extension).toUtf8());
    }
    json.append("]");
    CivetWebServer::sendJsonResponse(conn, 200, json);
    return 200;
}

int RequestHandler::handleStreamingUpload(mg_connection* conn, const HttpRequestInfo& info,
                                           const QByteArray& chunk, bool isLast)
{
    if (!m_streamingStates.contains(conn)) {
        QString contentType = info.headers.value("Content-Type");
        QString dir = uploadDir();
        if (dir.isEmpty()) dir = QDir::tempPath() + "/netshare_uploads";

        auto* state = new StreamingUploadState;
        state->parser = new StreamingMultipartParser(dir);
        if (!state->parser->init(contentType)) {
            delete state->parser; delete state;
            CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"Invalid multipart boundary\"}");
            return 400;
        }
        bool isReceive = (info.uri == "/receive" || info.uri.startsWith("/receive?"));
        state->isReceive = isReceive;
        state->remoteAddress = info.remoteAddress;

        if (m_transferEngine) {
            TransferTask task;
            task.taskId = QUuid::createUuid().toString();
            task.type = TransferTask::Upload;
            task.status = TransferTask::Uploading;
            task.fileName = "upload";
            task.fileSize = -1;
            task.transferredSize = 0;
            task.progress = 0;
            task.startedAt = QDateTime::currentDateTime();
            m_transferEngine->addUploadingTask(task);
            state->uploadTaskId = task.taskId;
            m_taskToToken[task.taskId] = task.taskId;
        }

        m_streamingStates.insert(conn, state);
    }

    auto* state = m_streamingStates[conn];
    if (state->finished) return 0;

    if (!chunk.isEmpty() && state->parser) {
        state->parser->feed(chunk);
        state->bytesReceived += chunk.size();
    }

    if (isLast) {
        state->finished = true;
        state->parser->finish();

        const auto& savedFiles = state->parser->savedFiles();
        bool isFolder = state->parser->isFolderUpload();
        QString folderRoot = state->parser->folderRoot();
        qint64 totalSize = state->parser->totalSize();

        LOG_INFO("Streaming upload complete: files=%d, isFolder=%d, totalSize=%lld",
                 savedFiles.size(), isFolder, totalSize);

        if (isFolder && !folderRoot.isEmpty()) {
            QString folderPath = state->parser->saveDir() + "/" + folderRoot;
            m_shareManager->createShare(folderPath, true, 0, 0, QString(), 1);
            recordCompletedTransfer(1, folderRoot, totalSize, state->remoteAddress, folderPath);
        } else {
            for (const auto& sf : savedFiles) {
                m_shareManager->createShare(sf.savePath, false, 0, 0, QString(), 1);
                recordCompletedTransfer(1, QFileInfo(sf.savePath).fileName(), sf.fileSize, state->remoteAddress, sf.savePath);
            }
        }

        if (m_transferEngine && !state->uploadTaskId.isEmpty())
            m_transferEngine->completeTask(state->uploadTaskId);

        if (state->isReceive) {
            CivetWebServer::sendJsonResponse(conn, 200,
                QString("{\"success\":true,\"count\":%1}").arg(savedFiles.size()).toUtf8());
        } else {
            QString host = info.headers.value("Host");
            QString url = !host.isEmpty()
                ? QString("http://%1/upload/success").arg(host)
                : QString("http://%1:8080/upload/success").arg(m_shareManager->localIp());
            CivetWebServer::sendJsonResponse(conn, 200,
                QString("{\"url\":\"%1\"}").arg(url).toUtf8());
        }

        delete state->parser; delete state;
        m_streamingStates.remove(conn);
    }

    return 0;
}

int RequestHandler::handleStreamingFileUpload(mg_connection* conn, const HttpRequestInfo& info,
                                                const QByteArray& chunk, bool isLast)
{
    if (!m_streamingFileStates.contains(conn)) {
        QString sessionId = info.headers.value("X-Upload-Session");
        QString filePath = QUrl::fromPercentEncoding(info.headers.value("X-File-Path").toUtf8());
        QString chunkIndexStr = info.headers.value("X-Chunk-Index");

        if (sessionId.isEmpty() || !m_uploadSessions.contains(sessionId)) {
            CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"Invalid or expired upload session\"}");
            return 400;
        }

        auto& uploadSession = m_uploadSessions[sessionId];
        if (uploadSession.paused) {
            CivetWebServer::sendJsonResponse(conn, 409, "{\"error\":\"Upload session is paused\"}");
            return 409;
        }

        auto* state = new StreamingFileUploadState;
        state->sessionId = sessionId;
        state->filePath = filePath;
        state->isChunked = !chunkIndexStr.isEmpty();

        if (state->isChunked) {
            int chunkIndex = chunkIndexStr.toInt();
            state->chunkIndex = chunkIndex;
            auto& session = m_uploadSessions[sessionId];

            if (!session.fileChunkStates.contains(filePath)) {
                delete state;
                CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"File not found in upload session\"}");
                return 400;
            }
            auto& csi = session.fileChunkStates[filePath];
            if (chunkIndex < 0 || chunkIndex >= csi.totalChunks) {
                delete state;
                CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"Invalid chunk index\"}");
                return 400;
            }
            if (csi.chunks[chunkIndex].status == QStringLiteral("completed")) {
                delete state;
                auto& s = m_uploadSessions[sessionId];
                auto& fc = s.fileChunkStates[filePath];
                int completedCount = 0;
                for (const auto& c : fc.chunks) if (c.status == QStringLiteral("completed")) completedCount++;
                CivetWebServer::sendJsonResponse(conn, 200,
                    QString("{\"success\":true,\"chunkIndex\":%1,\"completedChunks\":%2,\"skipped\":true}")
                        .arg(chunkIndex).arg(completedCount).toUtf8());
                return 200;
            }

            QString chunkDir = session.chunkTempDir + "/" + filePath;
            QDir().mkpath(chunkDir);
            QString chunkPath = chunkDir + QString("/chunk_%1").arg(chunkIndex, 6, 10, QChar('0'));

            state->chunkFile = new QFile(chunkPath);
            if (!state->chunkFile->open(QIODevice::WriteOnly)) {
                LOG_ERROR("Failed to open chunk file: %s", qPrintable(chunkPath));
                delete state->chunkFile; state->chunkFile = nullptr;
                delete state;
                CivetWebServer::sendJsonResponse(conn, 500, "{\"error\":\"Failed to write chunk\"}");
                return 500;
            }
            state->expectedSize = csi.chunks[chunkIndex].size;
        } else {
            auto& session = m_uploadSessions[sessionId];
            QString dir = uploadDir();
            if (dir.isEmpty()) dir = QDir::tempPath() + "/netshare_uploads";
            QString contentType = info.headers.value("Content-Type");
            state->parser = new StreamingMultipartParser(dir);
            if (!state->parser->init(contentType)) {
                delete state->parser; state->parser = nullptr;
                delete state;
                CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"Invalid multipart boundary\"}");
                return 400;
            }
            if (!filePath.isEmpty() && session.fileChunkStates.contains(filePath)) {
                auto& csi = session.fileChunkStates[filePath];
                QFileInfo fi(dir + "/" + filePath);
                if (fi.exists() && fi.isFile() && fi.size() > 0 && fi.size() < csi.fileSize) {
                    state->parser->setResumeOffset(fi.size());
                    state->parser->setResumeFilePath(fi.absoluteFilePath());
                }
            }
        }

        m_streamingFileStates.insert(conn, state);
    }

    auto* state = m_streamingFileStates[conn];
    if (state->finished) return 0;

    if (state->isChunked) {
        if (!chunk.isEmpty() && state->chunkFile) {
            qint64 written = state->chunkFile->write(chunk);
            if (written < 0) {
                LOG_ERROR("Failed to write chunk data: session=%s", qPrintable(state->sessionId));
                state->finished = true;
                m_streamingFileStates.remove(conn);
                state->chunkFile->close(); delete state->chunkFile;
                CivetWebServer::sendJsonResponse(conn, 500, "{\"error\":\"Disk write error\"}");
                delete state;
                return 500;
            }
            state->bytesReceived += written;
        }
    } else {
        if (!chunk.isEmpty() && state->parser)
            state->parser->feed(chunk);
    }

    if (isLast) {
        state->finished = true;
        m_streamingFileStates.remove(conn);

        if (state->isChunked) {
            if (state->chunkFile) { state->chunkFile->close(); delete state->chunkFile; state->chunkFile = nullptr; }

            auto& session = m_uploadSessions[state->sessionId];
            auto& csi = session.fileChunkStates[state->filePath];
            int chunkIndex = state->chunkIndex;

            if (state->bytesReceived != state->expectedSize && chunkIndex < csi.totalChunks - 1) {
                CivetWebServer::sendJsonResponse(conn, 400,
                    QString("{\"error\":\"Chunk size mismatch: expected %1, got %2\"}")
                        .arg(state->expectedSize).arg(state->bytesReceived).toUtf8());
                delete state; return 400;
            }

            ChunkManager chunkMgr;
            QString chunkDir = session.chunkTempDir + "/" + state->filePath;
            if (!chunkMgr.verifyChunk(chunkDir, chunkIndex, state->bytesReceived)) {
                CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"Chunk verification failed\"}");
                delete state; return 400;
            }

            csi.chunks[chunkIndex].status = QStringLiteral("completed");
            session.transferredSize += state->bytesReceived;

            if (m_transferEngine)
                m_transferEngine->updateTaskProgress(session.taskId, session.transferredSize);

            ChunkStateManager* csm = m_transferEngine ? m_transferEngine->chunkStateManager() : nullptr;
            if (csm) {
                QString stateFilePath = uploadDir() + "/.chunks/" + state->filePath + ".netshare";
                qint64 chunkSize = (chunkIndex < csi.chunks.size()) ? csi.chunks[chunkIndex].size : 0;
                csm->updateChunkStatus(stateFilePath, chunkIndex, QStringLiteral("completed"), chunkSize);
            }

            int completedCount = 0;
            for (const auto& c : csi.chunks) if (c.status == QStringLiteral("completed")) completedCount++;

            LOG_INFO("Chunk upload complete: session=%s chunk=%d/%d",
                     qPrintable(state->sessionId), chunkIndex, csi.totalChunks);

            CivetWebServer::sendJsonResponse(conn, 200,
                QString("{\"success\":true,\"chunkIndex\":%1,\"completedChunks\":%2,\"totalChunks\":%3}")
                    .arg(chunkIndex).arg(completedCount).arg(csi.totalChunks).toUtf8());

        } else {
            auto& session = m_uploadSessions[state->sessionId];
            state->parser->finish();
            const auto& savedFiles = state->parser->savedFiles();

            if (savedFiles.isEmpty()) {
                CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"No file found in upload\"}");
                delete state->parser; state->parser = nullptr;
                delete state; return 400;
            }

            const auto& sf = savedFiles.first();
            QString saveFileName = state->filePath.isEmpty() ? sf.fileName : state->filePath;
            session.transferredSize += sf.fileSize;

            SavedFileInfo sfi;
            sfi.fileName = saveFileName; sfi.savePath = sf.savePath; sfi.fileSize = sf.fileSize;
            session.savedFiles.append(sfi);

            if (session.fileChunkStates.contains(state->filePath)) {
                auto& csi = session.fileChunkStates[state->filePath];
                for (auto& c : csi.chunks) c.status = QStringLiteral("completed");
            }

            if (m_transferEngine)
                m_transferEngine->updateTaskProgress(session.taskId, session.transferredSize);

            LOG_INFO("Small file upload complete: session=%s file=%s size=%lld",
                     qPrintable(state->sessionId), qPrintable(saveFileName), sf.fileSize);

            CivetWebServer::sendJsonResponse(conn, 200,
                QString("{\"success\":true,\"path\":\"%1\"}").arg(saveFileName).toUtf8());

            delete state->parser; state->parser = nullptr;
        }
        delete state;
    }
    return 0;
}

int RequestHandler::handleUploadCheck(mg_connection* conn, const HttpRequestInfo& info)
{
    LOG_INFO("[ReceivePage][upload/check] from=%s bodyLen=%d",
             qPrintable(info.remoteAddress), info.body.size());

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(info.body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"Invalid JSON\"}");
        return 400;
    }

    QJsonObject root = doc.object();
    QJsonArray filesArr = root.value("files").toArray();
    if (filesArr.isEmpty()) {
        CivetWebServer::sendJsonResponse(conn, 200, "{\"existing\":[],\"partial\":[]}");
        return 200;
    }

    QString dir = uploadDir();
    if (dir.isEmpty()) dir = QDir::tempPath() + "/netshare_uploads";

    QString folderRoot;
    qint64 totalSize = 0;
    bool isFolder = false;
    for (const QJsonValue& val : filesArr) {
        QJsonObject obj = val.toObject();
        QString relativePath = obj.value("path").toString();
        totalSize += obj.value("size").toVariant().toLongLong();
        if (relativePath.contains('/') && folderRoot.isEmpty()) {
            folderRoot = relativePath.section('/', 0, 0);
            isFolder = true;
        }
    }

    static const qint64 CHUNK_THRESHOLD = 10 * 1024 * 1024;

    QJsonArray existingArr, partialArr;
    qint64 existingSize = 0, partialSize = 0;
    ChunkManager chunkMgr;
    QMap<QString, ChunkStateInfo> fileChunkStates;
    ChunkStateManager* chunkStateMgr = m_transferEngine ? m_transferEngine->chunkStateManager() : nullptr;

    for (const QJsonValue& val : filesArr) {
        QJsonObject obj = val.toObject();
        QString relativePath = obj.value("path").toString();
        qint64 expectedSize = obj.value("size").toVariant().toLongLong();
        QString fullPath = dir + "/" + relativePath;
        QFileInfo fi(fullPath);

        if (fi.exists() && fi.isFile() && fi.size() == expectedSize) {
            QJsonObject ex; ex["path"] = relativePath; ex["size"] = expectedSize;
            existingArr.append(ex); existingSize += expectedSize; continue;
        }

        bool useChunking = expectedSize >= CHUNK_THRESHOLD;
        int chunkSize = 0, chunkCount = 0;

        if (useChunking) {
            chunkSize = static_cast<int>(chunkMgr.calculateChunkSize(expectedSize, 3));
            QVariantList chunks = chunkMgr.splitFile(expectedSize, chunkSize);
            chunkCount = chunks.size();
        }

        ChunkStateInfo csi;
        csi.version = 1;
        csi.fileName = relativePath;
        csi.fileSize = expectedSize;
        csi.chunkSize = chunkSize;
        csi.totalChunks = chunkCount;
        csi.status = QStringLiteral("uploading");
        csi.type = QStringLiteral("upload");

        if (useChunking) {
            QVariantList chunks = chunkMgr.splitFile(expectedSize, chunkSize);
            for (const QVariant& v : chunks) {
                ChunkState ci = v.value<ChunkState>();
                ci.status = QStringLiteral("pending");
                csi.chunks.append(ci);
            }
        }

        if (!useChunking && fi.exists() && fi.isFile() && fi.size() > 0 && fi.size() < expectedSize) {
            QJsonObject pa; pa["path"] = relativePath; pa["size"] = fi.size();
            pa["useChunking"] = false; partialArr.append(pa); partialSize += fi.size();
        }

        fileChunkStates[relativePath] = csi;
    }

    if (!m_transferEngine) {
        LOG_ERROR("handleUploadCheck: m_transferEngine is null");
        CivetWebServer::sendJsonResponse(conn, 500, "{\"error\":\"Upload service not available\"}");
        return 500;
    }

    QString sessionId = QUuid::createUuid().toString();
    QString taskId = QUuid::createUuid().toString();
    QString chunkTempDir = dir + "/.chunks/" + sessionId;
    QString chunksMetaDir = dir + "/.chunks";
    bool sessionReused = false;

    if (chunkStateMgr) {
        for (auto it = fileChunkStates.begin(); it != fileChunkStates.end(); ++it) {
            const QString& relativePath = it.key();
            ChunkStateInfo& csi = it.value();
            if (csi.totalChunks <= 0) continue;

            QString stateFilePath = chunksMetaDir + "/" + relativePath + ".netshare";
            ChunkStateInfo existingInfo;
            if (chunkStateMgr->loadStateFile(stateFilePath, existingInfo)) {
                if (existingInfo.fileSize == csi.fileSize && existingInfo.totalChunks == csi.totalChunks) {
                    chunkStateMgr->validateCompletedChunks(stateFilePath);

                    for (ChunkState& cs : existingInfo.chunks) {
                        if (cs.status == QStringLiteral("partial") || cs.status == QStringLiteral("uploading")) {
                            QString ckPath = existingInfo.chunkDir + QString("/chunk_%1").arg(cs.index, 6, 10, QChar('0'));
                            QFileInfo fi(ckPath);
                            if (fi.exists()) {
                                QFile::remove(ckPath);
                                LOG_INFO("[UploadCheck][chunk.discard] file=%s, chunk=%d",
                                         qPrintable(relativePath), cs.index);
                            }
                            cs.status = QStringLiteral("pending");
                            cs.downloaded = 0;
                        }
                    }

                    QString oldChunkDir = existingInfo.chunkDir;
                    if (QDir(oldChunkDir).exists() && !sessionReused) {
                        chunkTempDir = QFileInfo(oldChunkDir).absolutePath();
                        sessionId = QFileInfo(chunkTempDir).fileName();
                        sessionReused = true;
                        LOG_INFO("[UploadCheck][chunkdir.reuse] file=%s, chunkDir=%s, sessionId=%s",
                                 qPrintable(relativePath), qPrintable(oldChunkDir), qPrintable(sessionId));
                    }

                    existingInfo.taskId = taskId;
                    existingInfo.status = QStringLiteral("uploading");
                    existingInfo.lastUpdated = QDateTime::currentDateTime().toString(Qt::ISODate);
                    if (!QDir(oldChunkDir).exists()) {
                        existingInfo.chunkDir = chunkTempDir + "/" + relativePath;
                    }
                    chunkStateMgr->saveStateFile(stateFilePath, existingInfo);

                    csi = existingInfo;
                    csi.fileName = relativePath;

                    QJsonObject pa;
                    pa["path"] = relativePath; pa["size"] = csi.fileSize;
                    pa["chunkSize"] = csi.chunkSize; pa["chunkCount"] = csi.totalChunks;
                    pa["useChunking"] = true;
                    QJsonArray completedArr; qint64 completedBytes = 0;
                    for (const auto& chunk : csi.chunks) {
                        if (chunk.status == QStringLiteral("completed")) {
                            completedArr.append(chunk.index);
                            completedBytes += chunk.size;
                        }
                    }
                    pa["completedChunks"] = completedArr; pa["completedBytes"] = completedBytes;
                    partialArr.append(pa); partialSize += completedBytes;
                } else {
                    chunkStateMgr->deleteStateFile(stateFilePath);
                    if (!existingInfo.chunkDir.isEmpty() && QDir(existingInfo.chunkDir).exists()) {
                        QDir(existingInfo.chunkDir).removeRecursively();
                    }

                    csi.taskId = taskId;
                    csi.chunkDir = chunkTempDir + "/" + relativePath;
                    csi.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
                    csi.lastUpdated = csi.createdAt;
                    chunkStateMgr->createStateFile(stateFilePath, csi);

                    QJsonObject pa;
                    pa["path"] = relativePath; pa["size"] = csi.fileSize;
                    pa["chunkSize"] = csi.chunkSize; pa["chunkCount"] = csi.totalChunks;
                    pa["useChunking"] = true; pa["completedChunks"] = QJsonArray(); pa["completedBytes"] = 0;
                    partialArr.append(pa);
                }
            } else {
                csi.taskId = taskId;
                csi.chunkDir = chunkTempDir + "/" + relativePath;
                csi.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
                csi.lastUpdated = csi.createdAt;
                chunkStateMgr->createStateFile(stateFilePath, csi);

                QJsonObject pa;
                pa["path"] = relativePath; pa["size"] = csi.fileSize;
                pa["chunkSize"] = csi.chunkSize; pa["chunkCount"] = csi.totalChunks;
                pa["useChunking"] = true; pa["completedChunks"] = QJsonArray(); pa["completedBytes"] = 0;
                partialArr.append(pa);
            }
        }
    } else {
        for (auto it = fileChunkStates.begin(); it != fileChunkStates.end(); ++it) {
            const QString& relativePath = it.key();
            ChunkStateInfo& csi = it.value();
            if (csi.totalChunks <= 0) continue;
            QJsonObject pa;
            pa["path"] = relativePath; pa["size"] = csi.fileSize;
            pa["chunkSize"] = csi.chunkSize; pa["chunkCount"] = csi.totalChunks;
            pa["useChunking"] = true; pa["completedChunks"] = QJsonArray(); pa["completedBytes"] = 0;
            partialArr.append(pa);
        }
    }

    if (sessionReused) {
        QDir chunksDir(chunksMetaDir);
        if (chunksDir.exists()) {
            QStringList cleanedDirs;
            for (const QString& dirName : chunksDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                if (!dirName.startsWith('{')) continue;
                QString dirPath = chunksMetaDir + "/" + dirName;
                if (dirPath == chunkTempDir) continue;
                bool inUse = false;
                for (auto it = m_uploadSessions.constBegin(); it != m_uploadSessions.constEnd(); ++it) {
                    if (it.value().chunkTempDir == dirPath) {
                        inUse = true;
                        break;
                    }
                }
                if (!inUse) {
                    QDir(dirPath).removeRecursively();
                    cleanedDirs.append(dirName);
                }
            }
            if (!cleanedDirs.isEmpty()) {
                LOG_INFO("[UploadCheck][chunkdir.cleanup] removed=%s", qPrintable(cleanedDirs.join(", ")));
            }
        }
    }

    qint64 initialTransferredSize = existingSize + partialSize;

    QString taskFileName;
    if (isFolder && !folderRoot.isEmpty()) taskFileName = folderRoot;
    else taskFileName = filesArr.size() == 1
        ? filesArr[0].toObject().value("path").toString()
        : QString(tr("%1 files")).arg(filesArr.size());

    QString pausedTaskId = m_transferEngine->findUploadTaskByName(taskFileName);
    if (!pausedTaskId.isEmpty()) {
        m_transferEngine->resumeTask(pausedTaskId);
        m_transferEngine->updateTaskProgress(pausedTaskId, initialTransferredSize);
        taskId = pausedTaskId;
        LOG_INFO("[UploadCheck][task.resume] file=%s, taskId=%s, transferredSize=%lld",
                 qPrintable(taskFileName), qPrintable(pausedTaskId), initialTransferredSize);
    } else {
        m_transferEngine->removeFailedUploadTasksByName(taskFileName);
        for (const QJsonValue& val : filesArr) {
            QString relPath = val.toObject().value("path").toString();
            m_transferEngine->removeFailedUploadTasksByName(relPath);
        }

        TransferTask task;
        task.taskId = taskId; task.type = TransferTask::Upload; task.status = TransferTask::Uploading;
        if (isFolder && !folderRoot.isEmpty()) {
            task.fileName = folderRoot; task.filePath = dir + "/" + folderRoot;
        } else {
            task.fileName = taskFileName; task.filePath = dir;
        }
        task.fileSize = totalSize; task.transferredSize = initialTransferredSize;
        task.progress = totalSize > 0 ? static_cast<int>((initialTransferredSize * 100) / totalSize) : 0;
        task.startedAt = QDateTime::currentDateTime();
        m_transferEngine->addUploadingTask(task);
        LOG_INFO("[UploadCheck][task.create] file=%s, taskId=%s", qPrintable(taskFileName), qPrintable(taskId));
    }

    UploadSession session;
    session.sessionId = sessionId; session.taskId = taskId; session.folder = folderRoot;
    session.totalSize = totalSize; session.transferredSize = initialTransferredSize;
    session.fileCount = filesArr.size(); session.remoteAddress = info.remoteAddress;
    session.createdAt = QDateTime::currentDateTime();
    session.fileChunkStates = fileChunkStates; session.chunkTempDir = chunkTempDir;
    session.paused = false;
    m_uploadSessions[sessionId] = session;
    m_taskToToken[taskId] = sessionId;

    QString shareToken = root.value("token").toString();
    if (!shareToken.isEmpty()) m_taskToShareToken[taskId] = shareToken;

    QJsonObject result;
    result["existing"] = existingArr;
    result["sessionId"] = sessionId;
    result["partial"] = partialArr;
    CivetWebServer::sendJsonResponse(conn, 200, QJsonDocument(result).toJson(QJsonDocument::Compact));
    return 200;
}

int RequestHandler::handleUploadSingleFile(mg_connection* conn, const HttpRequestInfo& info)
{
    QString sessionId = info.headers.value("X-Upload-Session");
    QString filePath = QUrl::fromPercentEncoding(info.headers.value("X-File-Path").toUtf8());
    QString chunkIndexStr = info.headers.value("X-Chunk-Index");

    if (sessionId.isEmpty() || !m_uploadSessions.contains(sessionId)) {
        if (!filePath.isEmpty()) {
            QString stateFilePath = m_uploadDir + "/.chunks/" + filePath + ".netshare";
            ChunkStateManager* csm = m_transferEngine ? m_transferEngine->chunkStateManager() : nullptr;
            bool stateFileExists = false;
            if (csm) {
                ChunkStateInfo dummy;
                stateFileExists = csm->loadStateFile(stateFilePath, dummy);
            } else {
                stateFileExists = QFile::exists(stateFilePath);
            }
            if (stateFileExists) {
                LOG_INFO("[UploadSingle][410] sessionId=%s, filePath=%s, stateFile=%s, recheck_required",
                         qPrintable(sessionId), qPrintable(filePath), qPrintable(stateFilePath));
                CivetWebServer::sendJsonResponse(conn, 410,
                    QString("{\"error\":\"session_expired\",\"hint\":\"recheck_required\"}").toUtf8());
                return 410;
            } else {
                LOG_INFO("[UploadSingle][400] sessionId=%s, filePath=%s, no state file",
                         qPrintable(sessionId), qPrintable(filePath));
                CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"Invalid or expired upload session\"}");
                return 400;
            }
        }
        CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"Invalid or expired upload session\"}");
        return 400;
    }

    auto& session = m_uploadSessions[sessionId];

    if (session.paused) {
        CivetWebServer::sendJsonResponse(conn, 409, "{\"error\":\"Upload session is paused\"}");
        return 409;
    }

    if (!chunkIndexStr.isEmpty()) {
        int chunkIndex = chunkIndexStr.toInt();
        if (!session.fileChunkStates.contains(filePath)) {
            CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"File not found in upload session\"}");
            return 400;
        }
        auto& csi = session.fileChunkStates[filePath];
        if (chunkIndex < 0 || chunkIndex >= csi.totalChunks) {
            CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"Invalid chunk index\"}");
            return 400;
        }
        if (csi.chunks[chunkIndex].status == QStringLiteral("completed")) {
            int completedCount = 0;
            for (const auto& c : csi.chunks) if (c.status == QStringLiteral("completed")) completedCount++;
            CivetWebServer::sendJsonResponse(conn, 200,
                QString("{\"success\":true,\"chunkIndex\":%1,\"completedChunks\":%2,\"skipped\":true}")
                    .arg(chunkIndex).arg(completedCount).toUtf8());
            return 200;
        }

        QString chunkDir = session.chunkTempDir + "/" + filePath;
        QDir().mkpath(chunkDir);
        ChunkManager chunkMgr;
        qint64 expectedChunkSize = csi.chunks[chunkIndex].size;

        if (info.body.size() != expectedChunkSize && chunkIndex < csi.totalChunks - 1) {
            CivetWebServer::sendJsonResponse(conn, 400,
                QString("{\"error\":\"Chunk size mismatch: expected %1, got %2\"}")
                    .arg(expectedChunkSize).arg(info.body.size()).toUtf8());
            return 400;
        }

        if (!chunkMgr.writeChunk(chunkDir, chunkIndex, info.body)) {
            CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"Failed to write chunk\"}");
            return 400;
        }
        if (!chunkMgr.verifyChunk(chunkDir, chunkIndex, info.body.size())) {
            CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"Chunk verification failed\"}");
            return 400;
        }

        csi.chunks[chunkIndex].status = QStringLiteral("completed");
        session.transferredSize += info.body.size();

        if (m_transferEngine)
            m_transferEngine->updateTaskProgress(session.taskId, session.transferredSize);

        ChunkStateManager* csm = m_transferEngine ? m_transferEngine->chunkStateManager() : nullptr;
        if (csm) {
            QString stateFilePath = uploadDir() + "/.chunks/" + filePath + ".netshare";
            qint64 chunkSize = (chunkIndex < csi.chunks.size()) ? csi.chunks[chunkIndex].size : 0;
            csm->updateChunkStatus(stateFilePath, chunkIndex, QStringLiteral("completed"), chunkSize);
        }

        int completedCount = 0;
        for (const auto& c : csi.chunks) if (c.status == QStringLiteral("completed")) completedCount++;

        LOG_INFO("Chunk upload: session=%s, file=%s, chunk=%d/%d, size=%lld",
                 qPrintable(sessionId), qPrintable(filePath), chunkIndex, csi.totalChunks,
                 static_cast<qint64>(info.body.size()));

        CivetWebServer::sendJsonResponse(conn, 200,
            QString("{\"success\":true,\"chunkIndex\":%1,\"completedChunks\":%2,\"totalChunks\":%3}")
                .arg(chunkIndex).arg(completedCount).arg(csi.totalChunks).toUtf8());
    } else {
        QString contentType = info.headers.value("Content-Type");
        QList<UploadedFile> files = parseMultipartFormData(info.body, contentType);
        if (files.isEmpty()) {
            CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"No file found in upload\"}");
            return 400;
        }
        const UploadedFile& uf = files.first();
        QString dir = uploadDir();
        if (dir.isEmpty()) dir = QDir::tempPath() + "/netshare_uploads";

        QString saveFileName = filePath.isEmpty() ? uf.fileName : filePath;
        QString fullPath = dir + "/" + saveFileName;
        QDir().mkpath(QFileInfo(fullPath).absolutePath());

        QIODevice::OpenMode openMode = QIODevice::WriteOnly;
        qint64 existingSize = 0;
        if (session.fileChunkStates.contains(saveFileName)) {
            auto& csi = session.fileChunkStates[saveFileName];
            QFileInfo fi(fullPath);
            if (fi.exists() && fi.isFile() && fi.size() > 0 && fi.size() < csi.fileSize) {
                existingSize = fi.size(); openMode = QIODevice::WriteOnly | QIODevice::Append;
            }
        }

        QFile file(fullPath);
        if (!file.open(openMode)) {
            CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"Failed to save file\"}");
            return 400;
        }
        file.write(uf.data); file.close();

        qint64 addedSize = uf.data.size();
        if (filePath.isEmpty()) filePath = uf.fileName;
        session.transferredSize += addedSize;

        SavedFileInfo sfi;
        sfi.fileName = saveFileName; sfi.savePath = fullPath; sfi.fileSize = addedSize;
        session.savedFiles.append(sfi);

        if (session.fileChunkStates.contains(filePath)) {
            auto& csi = session.fileChunkStates[filePath];
            for (auto& c : csi.chunks) c.status = QStringLiteral("completed");
        }

        if (m_transferEngine)
            m_transferEngine->updateTaskProgress(session.taskId, session.transferredSize);

        LOG_INFO("Small file upload: session=%s, file=%s, size=%lld",
                 qPrintable(sessionId), qPrintable(saveFileName), addedSize);

        CivetWebServer::sendJsonResponse(conn, 200,
            QString("{\"success\":true,\"path\":\"%1\"}").arg(saveFileName).toUtf8());
    }
    return 200;
}

int RequestHandler::handleUploadFinalize(mg_connection* conn, const HttpRequestInfo& info)
{
    LOG_INFO("[ReceivePage][upload/finalize] from=%s bodyLen=%d",
             qPrintable(info.remoteAddress), info.body.size());

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(info.body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"Invalid JSON\"}");
        return 400;
    }

    QJsonObject root = doc.object();
    QString folder = root.value("folder").toString();
    bool isFolder = root.value("isFolder").toBool();
    qint64 totalSize = root.value("totalSize").toVariant().toLongLong();
    int fileCount = root.value("fileCount").toInt();
    QString token = root.value("token").toString();
    bool isReceive = root.value("isReceive").toBool();
    QString sessionId = root.value("sessionId").toString();

    if (!isReceive && token.isEmpty()) {
        CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"Missing token\"}");
        return 400;
    }
    if (!isReceive) {
        ShareInfo si = m_shareManager->getShareInfo(token);
        if (!si.isValid() || si.isExpired()) {
            CivetWebServer::sendJsonResponse(conn, 404, "{\"error\":\"Share not found or expired\"}");
            return 404;
        }
    }

    QString dir = uploadDir();
    if (dir.isEmpty()) dir = QDir::tempPath() + "/netshare_uploads";

    if (isReceive) {
        CivetWebServer::sendJsonResponse(conn, 200,
            QString("{\"success\":true,\"count\":%1}").arg(fileCount).toUtf8());
    } else {
        QString host = info.headers.value("Host");
        QString url = !host.isEmpty()
            ? QString("http://%1/upload/success").arg(host)
            : QString("http://%1:8080/upload/success").arg(m_shareManager->localIp());
        CivetWebServer::sendJsonResponse(conn, 200,
            QString("{\"url\":\"%1\"}").arg(url).toUtf8());
    }

    if (!sessionId.isEmpty() && m_uploadSessions.contains(sessionId)) {
        auto session = m_uploadSessions[sessionId];
        m_uploadSessions.remove(sessionId);
        m_taskToToken.remove(session.taskId);
        m_taskToShareToken.remove(session.taskId);

        ChunkStateManager* csm = m_transferEngine ? m_transferEngine->chunkStateManager() : nullptr;
        if (csm) {
            QString uploadBaseDir = uploadDir();
            if (uploadBaseDir.isEmpty()) uploadBaseDir = QDir::tempPath() + "/netshare_uploads";
            for (auto it = session.fileChunkStates.constBegin(); it != session.fileChunkStates.constEnd(); ++it) {
                if (it.value().totalChunks <= 0) continue;
                QString stateFilePath = uploadBaseDir + "/.chunks/" + it.key() + ".netshare";
                csm->deleteStateFile(stateFilePath);
            }
        }

        QStringList incompleteFiles;
        for (auto it = session.fileChunkStates.begin(); it != session.fileChunkStates.end(); ++it) {
            const ChunkStateInfo& csi = it.value();
            int completedCount = 0;
            for (const auto& c : csi.chunks) if (c.status == QStringLiteral("completed")) completedCount++;
            if (csi.totalChunks > 0 && completedCount < csi.totalChunks)
                incompleteFiles << QString("%1 (%2/%3)").arg(csi.fileName)
                    .arg(completedCount).arg(csi.totalChunks);
        }
        if (!incompleteFiles.isEmpty()) {
            LOG_WARN("Finalize: incomplete chunks: %s", qPrintable(incompleteFiles.join(", ")));
            return 1;
        }

        QString chunkTempDir = session.chunkTempDir;
        QList<ChunkStateInfo> fileStates;
        for (auto it = session.fileChunkStates.begin(); it != session.fileChunkStates.end(); ++it) {
            fileStates.append(it.value());
        }
        QList<SavedFileInfo> sessionSavedFiles = session.savedFiles;
        QString taskId = session.taskId;

        QThreadPool::globalInstance()->start([this, dir, chunkTempDir, fileStates, sessionSavedFiles, taskId, folder, isFolder, totalSize, remoteAddr = info.remoteAddress, chunkTempDirCopy = chunkTempDir]() {
            ChunkManager chunkMgr;
            QList<SavedFileInfo> savedFiles;

            for (const auto& csi : fileStates) {
                if (csi.totalChunks > 0) {
                    QString chunkDir = chunkTempDir + "/" + csi.fileName;
                    QString outputPath = dir + "/" + csi.fileName;
                    QDir().mkpath(QFileInfo(outputPath).absolutePath());
                    if (!chunkMgr.mergeChunks(chunkDir, outputPath, csi.totalChunks)) {
                        LOG_ERROR("Finalize: merge failed for %s", qPrintable(csi.fileName)); continue;
                    }
                    if (!chunkMgr.verifyMergedFile(outputPath, csi.fileSize)) {
                        LOG_ERROR("Finalize: verify failed for %s", qPrintable(csi.fileName)); continue;
                    }
                    chunkMgr.cleanupChunks(chunkDir);
                    SavedFileInfo sfi;
                    sfi.fileName = QFileInfo(csi.fileName).fileName();
                    sfi.savePath = outputPath; sfi.fileSize = csi.fileSize;
                    savedFiles.append(sfi);
                } else {
                    QString existingPath = dir + "/" + csi.fileName;
                    QFileInfo fi(existingPath);
                    if (fi.exists() && fi.isFile() && fi.size() > 0) {
                        SavedFileInfo sfi;
                        sfi.fileName = QFileInfo(csi.fileName).fileName();
                        sfi.savePath = existingPath;
                        sfi.fileSize = fi.size();
                        savedFiles.append(sfi);
                    }
                }
            }

            for (const auto& sfi : sessionSavedFiles) {
                bool dup = false;
                for (const auto& existing : savedFiles) {
                    if (existing.savePath == sfi.savePath) { dup = true; break; }
                }
                if (!dup) savedFiles.append(sfi);
            }

            if (!chunkTempDirCopy.isEmpty())
                QDir(chunkTempDirCopy).removeRecursively();

            {
                QString searchDir = dir + "/.chunks";
                QDir searchDirObj(searchDir);
                if (searchDirObj.exists()) {
                    QStringList sessionDirs = searchDirObj.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                    for (const QString& prevSession : sessionDirs) {
                        if (m_uploadSessions.contains(prevSession)) continue;
                        QString prevSessionPath = searchDir + "/" + prevSession;
                        bool hasFiles = false;
                        QDir checkDir(prevSessionPath);
                        for (const QString& sub : checkDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                            QDir subDir(prevSessionPath + "/" + sub);
                            if (!subDir.entryList(QStringList() << "chunk_*", QDir::Files).isEmpty()) {
                                hasFiles = true;
                                break;
                            }
                        }
                        if (!hasFiles) QDir(prevSessionPath).removeRecursively();
                    }
                }
            }

            emit uploadFinalizeReady(savedFiles, taskId, folder, isFolder, totalSize, remoteAddr);
            LOG_INFO("Finalize: background merge completed for task=%s", qPrintable(taskId));
        });
    } else {
        if (isFolder && !folder.isEmpty()) {
            QString folderPath = dir + "/" + folder;
            m_shareManager->createShare(folderPath, true, 0, 0, QString(), 1);
            if (m_transferEngine)
                m_transferEngine->completeTaskByName(folder, TransferTask::Upload);
            if (m_transferLogService)
                m_transferLogService->logTransfer(1, folder, folderPath, totalSize,
                                                   info.remoteAddress, TransferLogEntry::Completed);
        }
    }

    return 1;
}

int RequestHandler::handleUploadAbort(mg_connection* conn, const HttpRequestInfo& info)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(info.body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"Invalid JSON\"}");
        return 400;
    }

    QString sessionId = doc.object().value("sessionId").toString();
    if (sessionId.isEmpty() || !m_uploadSessions.contains(sessionId)) {
        CivetWebServer::sendJsonResponse(conn, 200, "{\"success\":true}");
        return 200;
    }

    auto& session = m_uploadSessions[sessionId];
    QString taskId = session.taskId;

    if (m_transferEngine && !taskId.isEmpty())
        m_transferEngine->failTask(taskId, "Upload aborted");

    ChunkStateManager* csm = m_transferEngine ? m_transferEngine->chunkStateManager() : nullptr;
    if (csm) {
        QString dir = uploadDir();
        if (dir.isEmpty()) dir = QDir::tempPath() + "/netshare_uploads";
        for (auto it = session.fileChunkStates.constBegin(); it != session.fileChunkStates.constEnd(); ++it) {
            if (it.value().totalChunks <= 0) continue;
            QString stateFilePath = dir + "/.chunks/" + it.key() + ".netshare";
            csm->deleteStateFile(stateFilePath);
        }
    }

    if (!session.chunkTempDir.isEmpty())
        QDir(session.chunkTempDir).removeRecursively();

    m_taskToToken.remove(taskId);
    m_taskToShareToken.remove(taskId);
    m_uploadSessions.remove(sessionId);

    LOG_INFO("Upload aborted: session=%s", qPrintable(sessionId));

    CivetWebServer::sendJsonResponse(conn, 200, "{\"success\":true}");
    return 200;
}

int RequestHandler::handleUploadPause(mg_connection* conn, const HttpRequestInfo& info)
{
    QJsonDocument doc = QJsonDocument::fromJson(info.body);
    QString sessionId = doc.object().value("sessionId").toString();
    if (sessionId.isEmpty() || !m_uploadSessions.contains(sessionId)) {
        CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"Invalid session\"}");
        return 400;
    }

    auto& session = m_uploadSessions[sessionId];
    session.paused = true;
    session.pausedAt = QDateTime::currentDateTime();

    if (m_transferEngine && !session.taskId.isEmpty())
        m_transferEngine->pauseTask(session.taskId);

    ChunkStateManager* csm = m_transferEngine ? m_transferEngine->chunkStateManager() : nullptr;
    if (csm) {
        QString dir = uploadDir();
        if (dir.isEmpty()) dir = QDir::tempPath() + "/netshare_uploads";
        for (auto it = session.fileChunkStates.constBegin(); it != session.fileChunkStates.constEnd(); ++it) {
            if (it.value().totalChunks <= 0) continue;
            QString stateFilePath = dir + "/.chunks/" + it.key() + ".netshare";
            if (QFile::exists(stateFilePath)) {
                csm->markDownloadingChunksPartial(stateFilePath);
            }
        }
    }

    if (m_civetServer) {
        QJsonObject data;
        data["taskId"] = session.taskId;
        data["sessionId"] = sessionId;

        if (!sessionId.isEmpty())
            m_civetServer->broadcastToSubscribers(sessionId, "pause_upload", data);

        QString shareToken = m_taskToShareToken.value(session.taskId);
        if (!shareToken.isEmpty() && shareToken != sessionId)
            m_civetServer->broadcastToSubscribers(shareToken, "pause_upload", data);
    }

    LOG_INFO("Upload paused: session=%s", qPrintable(sessionId));
    CivetWebServer::sendJsonResponse(conn, 200, "{\"success\":true}");
    return 200;
}

int RequestHandler::handleUploadResume(mg_connection* conn, const HttpRequestInfo& info)
{
    QJsonDocument doc = QJsonDocument::fromJson(info.body);
    QString sessionId = doc.object().value("sessionId").toString();
    if (sessionId.isEmpty() || !m_uploadSessions.contains(sessionId)) {
        CivetWebServer::sendJsonResponse(conn, 400, "{\"error\":\"Invalid session\"}");
        return 400;
    }

    auto& session = m_uploadSessions[sessionId];
    session.paused = false;
    session.pausedAt = QDateTime();

    if (m_transferEngine && !session.taskId.isEmpty())
        m_transferEngine->resumeTask(session.taskId);

    ChunkStateManager* csm = m_transferEngine ? m_transferEngine->chunkStateManager() : nullptr;
    if (csm) {
        QString dir = uploadDir();
        if (dir.isEmpty()) dir = QDir::tempPath() + "/netshare_uploads";
        for (auto it = session.fileChunkStates.constBegin(); it != session.fileChunkStates.constEnd(); ++it) {
            if (it.value().totalChunks <= 0) continue;
            QString stateFilePath = dir + "/.chunks/" + it.key() + ".netshare";
            if (QFile::exists(stateFilePath)) {
                csm->updateTaskStatus(stateFilePath, QStringLiteral("uploading"));
            }
        }
    }

    if (m_civetServer) {
        QJsonObject data;
        data["taskId"] = session.taskId;
        data["sessionId"] = sessionId;

        if (!sessionId.isEmpty())
            m_civetServer->broadcastToSubscribers(sessionId, "resume_upload", data);

        QString shareToken = m_taskToShareToken.value(session.taskId);
        if (!shareToken.isEmpty() && shareToken != sessionId)
            m_civetServer->broadcastToSubscribers(shareToken, "resume_upload", data);
    }

    LOG_INFO("Upload resumed: session=%s", qPrintable(sessionId));
    CivetWebServer::sendJsonResponse(conn, 200, "{\"success\":true}");
    return 200;
}

int RequestHandler::handleUploadPending(mg_connection* conn, const HttpRequestInfo& info)
{
    Q_UNUSED(info)
    ChunkStateManager* csm = m_transferEngine ? m_transferEngine->chunkStateManager() : nullptr;
    if (!csm) {
        CivetWebServer::sendJsonResponse(conn, 200, "{\"partial\":[]}");
        return 200;
    }

    QString dir = uploadDir();
    if (dir.isEmpty()) dir = QDir::tempPath() + "/netshare_uploads";
    QString uploadChunksDir = dir + "/.chunks";

    QList<ChunkStateInfo> resumable = csm->scanResumableTasks(uploadChunksDir);

    QJsonArray partialArr;
    for (const ChunkStateInfo& csi : resumable) {
        if (csi.type != QStringLiteral("upload")) continue;
        QJsonObject pa;
        pa["path"] = csi.fileName;
        pa["size"] = csi.fileSize;
        pa["chunkSize"] = csi.chunkSize;
        pa["chunkCount"] = csi.totalChunks;
        pa["useChunking"] = true;
        QJsonArray completedArr;
        qint64 completedBytes = 0;
        for (const ChunkState& chunk : csi.chunks) {
            if (chunk.status == QStringLiteral("completed")) {
                completedArr.append(chunk.index);
                completedBytes += chunk.size;
            }
        }
        pa["completedChunks"] = completedArr;
        pa["completedBytes"] = completedBytes;
        partialArr.append(pa);
    }

    QJsonObject result;
    result["partial"] = partialArr;
    CivetWebServer::sendJsonResponse(conn, 200, QJsonDocument(result).toJson(QJsonDocument::Compact));
    return 200;
}

void RequestHandler::cleanupExpiredSessions()
{
    QDateTime now = QDateTime::currentDateTime();
    QStringList expiredSessions;

    for (auto it = m_uploadSessions.begin(); it != m_uploadSessions.end(); ++it) {
        if (it->createdAt.secsTo(now) > 7200)
            expiredSessions.append(it.key());
    }

    for (const QString& sessionId : expiredSessions) {
        auto& session = m_uploadSessions[sessionId];
        QString taskId = session.taskId;

        if (m_transferEngine && !taskId.isEmpty())
            m_transferEngine->failTask(taskId, "Session expired");

        ChunkStateManager* csm = m_transferEngine ? m_transferEngine->chunkStateManager() : nullptr;
        if (csm) {
            QString dir = uploadDir();
            if (dir.isEmpty()) dir = QDir::tempPath() + "/netshare_uploads";
            for (auto it = session.fileChunkStates.constBegin(); it != session.fileChunkStates.constEnd(); ++it) {
                if (it.value().totalChunks <= 0) continue;
                QString stateFilePath = dir + "/.chunks/" + it.key() + ".netshare";
                csm->deleteStateFile(stateFilePath);
            }
        }

        if (!session.chunkTempDir.isEmpty())
            QDir(session.chunkTempDir).removeRecursively();

        m_taskToToken.remove(taskId);
        m_taskToShareToken.remove(taskId);
        m_uploadSessions.remove(sessionId);
    }
}

QList<UploadedFile> RequestHandler::parseMultipartFormData(const QByteArray& body, const QString& contentType) const
{
    QList<UploadedFile> result;
    QString boundary;
    int boundaryPos = contentType.indexOf("boundary=");
    if (boundaryPos < 0) return result;

    boundary = contentType.mid(boundaryPos + 9);
    if (boundary.startsWith('"') && boundary.endsWith('"'))
        boundary = boundary.mid(1, boundary.size() - 2);
    boundary = boundary.trimmed();

    QByteArray boundaryBytes = "--" + boundary.toUtf8();
    QByteArray endBoundary = boundaryBytes + "--";

    int pos = 0;
    while (pos < body.size()) {
        int partStart = body.indexOf(boundaryBytes, pos);
        if (partStart < 0) break;
        int afterBoundary = partStart + boundaryBytes.size();
        while (afterBoundary < body.size() && (body[afterBoundary] == '\r' || body[afterBoundary] == '\n'))
            afterBoundary++;
        int headerEnd = body.indexOf("\r\n\r\n", afterBoundary);
        if (headerEnd < 0) break;

        QByteArray partHeaders = body.mid(afterBoundary, headerEnd - afterBoundary);
        QString headersStr = QString::fromUtf8(partHeaders);
        QString fileName;
        QRegularExpression nameRe("filename=\"([^\"]+)\"");
        QRegularExpressionMatch match = nameRe.match(headersStr);
        if (match.hasMatch()) fileName = match.captured(1);

        if (fileName.isEmpty()) { pos = headerEnd + 4; continue; }

        int dataStart = headerEnd + 4;
        int nextBoundary = body.indexOf(boundaryBytes, dataStart);
        if (nextBoundary < 0) break;

        int dataEnd = nextBoundary;
        if (dataEnd > dataStart && body[dataEnd - 1] == '\n') dataEnd--;
        if (dataEnd > dataStart && body[dataEnd - 1] == '\r') dataEnd--;

        UploadedFile uf;
        uf.fileName = fileName;
        uf.data = body.mid(dataStart, dataEnd - dataStart);
        result.append(uf);
        pos = nextBoundary;
    }
    return result;
}

QString RequestHandler::langParam(const HttpRequestInfo& info) const
{
    Q_UNUSED(info)
    // Always use software language setting, ignore URL/browser language
    if (m_settingsManager) {
        int langIndex = m_settingsManager->value("General/Language", 0).toInt();
        if (langIndex == 1) return "en";
    }
    return "zh";
}

QString RequestHandler::loadWebFile(const QString& fileName, const QMap<QString, QString>& vars) const
{
    QStringList paths = { "web/" + fileName,
        QCoreApplication::applicationDirPath() + "/web/" + fileName };
    QString html;
    for (const auto& p : paths) {
        QFile f(p);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            html = QString::fromUtf8(f.readAll());
            f.close();
            break;
        }
    }
    if (html.isEmpty()) return html;
    // Replace {{KEY}} placeholders
    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it) {
        html.replace("{{" + it.key() + "}}", it.value());
    }
    return html;
}

QString RequestHandler::mimeTypeForFile(const QString& fileName) const
{
    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(fileName);
    if (mime.name() == "application/octet-stream") {
        QString ext = QFileInfo(fileName).suffix().toLower();
        if (ext == "txt" || ext == "md" || ext == "csv") return "text/plain";
        if (ext == "json") return "application/json";
        if (ext == "xml") return "application/xml";
        if (ext == "htm" || ext == "html") return "text/html";
        if (ext == "css") return "text/css";
        if (ext == "js") return "application/javascript";
        if (ext == "png") return "image/png";
        if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
        if (ext == "gif") return "image/gif";
        if (ext == "svg") return "image/svg+xml";
        if (ext == "mp4") return "video/mp4";
        if (ext == "mp3") return "audio/mpeg";
        if (ext == "pdf") return "application/pdf";
        if (ext == "zip") return "application/zip";
        if (ext == "rar") return "application/x-rar-compressed";
        if (ext == "7z") return "application/x-7z-compressed";
    }
    return mime.name();
}

void RequestHandler::recordCompletedTransfer(int type, const QString& fileName, qint64 fileSize,
                                             const QString& remoteAddress, const QString& filePath)
{
    emit (type == 0 ? fileDownloaded(fileName, fileSize, remoteAddress)
                     : fileUploaded(fileName, fileSize, remoteAddress));
    if (m_transferLogService)
        m_transferLogService->logTransfer(type, fileName, filePath, fileSize, remoteAddress, TransferLogEntry::Completed);
}

void RequestHandler::onUploadFinalizeReady(const QList<SavedFileInfo>& savedFiles,
                                            const QString& taskId,
                                            const QString& folder, bool isFolder,
                                            qint64 totalSize, const QString& remoteAddr)
{
    QString dir = uploadDir();
    if (dir.isEmpty()) dir = QDir::tempPath() + "/netshare_uploads";

    if (isFolder && !folder.isEmpty()) {
        QString folderPath = savedFiles.isEmpty() ? QString() : savedFiles.first().savePath;
        if (folderPath.isEmpty()) folderPath = dir + "/" + folder;
        m_shareManager->createShare(folderPath, true, 0, 0, QString(), 1);
        if (m_transferEngine) m_transferEngine->completeTask(taskId);
        if (m_transferLogService) {
            m_transferLogService->deleteLogsByFileName(folder, TransferLogEntry::UploadLog);
            m_transferLogService->logTransfer(1, folder, folderPath, totalSize, remoteAddr, TransferLogEntry::Completed);
        }
    } else {
        for (const auto& sfi : savedFiles) {
            m_shareManager->createShare(sfi.savePath, false, 0, 0, QString(), 1);
            if (m_transferLogService) {
                m_transferLogService->deleteLogsByFileName(sfi.fileName, TransferLogEntry::UploadLog);
                m_transferLogService->logTransfer(1, sfi.fileName, sfi.savePath,
                                                   sfi.fileSize, remoteAddr, TransferLogEntry::Completed);
            }
        }
        if (m_transferEngine) m_transferEngine->completeTask(taskId);
    }
}

int RequestHandler::handleChatMessage(mg_connection* conn, const HttpRequestInfo& info)
{
    if (!m_chatService) {
        CivetWebServer::sendJsonResponse(conn, 503, "{\"status\":\"error\",\"message\":\"Chat service not available\"}");
        return 503;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(info.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        CivetWebServer::sendJsonResponse(conn, 400, "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return 400;
    }

    QJsonObject obj = doc.object();
    QString from = obj[QStringLiteral("from")].toString();
    QString fromIp = obj[QStringLiteral("fromIp")].toString();
    QString content = obj[QStringLiteral("content")].toString();
    QString timestamp = obj[QStringLiteral("timestamp")].toString();

    if (content.trimmed().isEmpty()) {
        CivetWebServer::sendJsonResponse(conn, 400, "{\"status\":\"error\",\"message\":\"Content is empty\"}");
        return 400;
    }

    m_chatService->onMessageReceived(from, fromIp, content, timestamp, info.remoteAddress);

    CivetWebServer::sendJsonResponse(conn, 200, "{\"status\":\"ok\"}");
    return 200;
}
