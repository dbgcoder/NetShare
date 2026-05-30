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
        Q_UNUSED(info)
        QString lang = langParam(info);
        QString service = (lang == "en") ? "LAN File Sharing Service" : "局域网文件分享服务";
        QString useLink = (lang == "en") ? "Please use a share link to access specific content" : "请使用分享链接访问具体分享内容";
        CivetWebServer::sendHtmlResponse(conn, 200,
            QString("<html><head><meta charset='utf-8'><title>NetShare</title></head>"
            "<body style='font-family:system-ui,sans-serif;text-align:center;padding:80px 20px;background:#1a1a2e;color:#eee'>"
            "<h1 style='font-size:3em;color:#e94560'>NetShare</h1>"
            "<p style='font-size:1.2em;color:#aaa'>%1</p>"
            "<p style='color:#666;margin-top:40px'>%2</p></body></html>")
            .arg(service, useLink).toUtf8());
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

    server->addRoute("GET", "/receive", [](mg_connection* conn, const HttpRequestInfo& info) {
        Q_UNUSED(info)
        QStringList paths = { "web/receive.html",
            QCoreApplication::applicationDirPath() + "/web/receive.html" };
        QByteArray page;
        for (const auto& p : paths) {
            QFile f(p);
            if (f.open(QIODevice::ReadOnly)) {
                page = f.readAll();
                f.close();
                LOG_INFO("Serving receive page from %s, size=%d bytes", qPrintable(p), page.size());
                break;
            }
        }
        if (page.isEmpty()) {
            CivetWebServer::sendHtmlResponse(conn, 500, QByteArrayLiteral("\xe6\x97\xa0\xe6\xb3\x95\xe5\x8a\xa0\xe8\xbd\xbd\xe6\x8e\xa5\xe6\x94\xb6\xe9\xa1\xb5\xe9\x9d\xa2"));
            return 500;
        }
        mg_printf(conn,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Cache-Control: no-cache, no-store, must-revalidate\r\n"
            "Pragma: no-cache\r\n"
            "Expires: 0\r\n"
            "Content-Length: %d\r\n\r\n", page.size());
        mg_write(conn, page.constData(), page.size());
        return 200;
    });

    server->addRoute("GET", "/upload/*", [this](mg_connection* conn, const HttpRequestInfo& info) {
        return handleUploadPage(conn, info);
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
        CivetWebServer::sendHtmlResponse(conn, 200, generateErrorPage(lang));
        return 200;
    }

    if (shareInfo.passwordRequired) {
        auto qp = parseQueryString(info.queryString);
        QString pw = qp.value("pw");
        if (pw.isEmpty() || !m_shareManager->validateShare(token, pw)) {
            CivetWebServer::sendHtmlResponse(conn, 200, generatePasswordPage(token, lang));
            return 200;
        }
    }

    m_shareManager->shareAccessed(token);
    CivetWebServer::sendHtmlResponse(conn, 200, generateSharePage(token, shareInfo.filePath, shareInfo.isFolder, lang));
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

int RequestHandler::handleUploadPage(mg_connection* conn, const HttpRequestInfo& info)
{
    QString lang = langParam(info);
    QString token = info.uri.mid(8);
    CivetWebServer::sendHtmlResponse(conn, 200, generateUploadPage(token, lang));
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
    auto qp = parseQueryString(info.queryString);
    if (qp.value("lang") == "en") return "en";
    return "zh";
}

QString RequestHandler::htmlI18nDict(const QString& lang) const
{
    QString zh = "{"
        "'lan_service':'局域网文件分享服务',"
        "'lan_use_link':'请使用分享链接访问具体分享内容',"
        "'lan_download':'下载',"
        "'lan_download_zip':'打包下载 (ZIP)',"
        "'lan_download_file':'下载文件',"
        "'lan_direct_download':'直接下载',"
        "'lan_folder_share':'文件夹分享',"
        "'lan_file_share':'文件分享',"
        "'lan_file_list':'文件列表',"
        "'lan_name':'名称',"
        "'lan_size':'大小',"
        "'lan_action':'操作',"
        "'lan_upload_file':'上传文件',"
        "'lan_powered_by':'由 NetShare 提供',"
        "'lan_preparing':'准备中...',"
        "'lan_cancel':'取消',"
        "'lan_fetching_info':'获取文件信息...',"
        "'lan_fetch_failed':'获取文件信息失败',"
        "'lan_no_resume':'服务器不支持断点续传，使用直接下载',"
        "'lan_large_file':'文件较大',"
        "'lan_suggest_manager':'建议使用下载管理器',"
        "'lan_file_size':'文件大小: ',"
        "'lan_error':'错误: ',"
        "'lan_resuming':'续传中... ',"
        "'lan_downloaded':' 已下载',"
        "'lan_downloading':'下载中...',"
        "'lan_dl_failed':'下载失败: HTTP ',"
        "'lan_interrupted':'下载中断，点击下载按钮继续',"
        "'lan_cancelled_resume':'已取消，点击下载按钮继续',"
        "'lan_interrupted_msg':'下载中断: ',"
        "'lan_saving':'保存中...',"
        "'lan_complete':'下载完成!',"
        "'lan_pwd_title':'密码验证',"
        "'lan_pwd_required':'该分享需要密码',"
        "'lan_enter_pwd':'请输入访问密码',"
        "'lan_pwd_placeholder':'输入密码',"
        "'lan_verify':'验证',"
        "'lan_upload_title':'上传文件',"
        "'lan_upload_subtitle':'选择文件或文件夹上传到局域网分享',"
        "'lan_drag_here':'将文件或文件夹拖拽到此处',"
        "'lan_select_file':'📄 选择文件',"
        "'lan_select_folder':'📁 选择文件夹',"
        "'lan_uploading':'上传中...',"
        "'lan_start_upload':'开始上传',"
        "'lan_upload_success':'上传成功！',"
        "'lan_upload_files':'上传 ',"
        "'lan_files_unit':' 个文件 (',"
        "'lan_prepare_upload':'准备上传...',"
        "'lan_upload_complete':'上传完成',"
        "'lan_view_share':'查看分享页面',"
        "'lan_uploading_n':'正在上传 ',"
        "'lan_upload_failed':'上传失败: ',"
        "'lan_network_error':'网络错误',"
        "'lan_n_files':'%1个文件',"
        "'lan_error_share_not_exist':'分享不存在',"
        "'lan_error_share_invalid':'该分享链接无效或已过期'"
    "}";

    QString en = "{"
        "'lan_service':'LAN File Sharing Service',"
        "'lan_use_link':'Please use a share link to access specific content',"
        "'lan_download':'Download',"
        "'lan_download_zip':'Download (ZIP)',"
        "'lan_download_file':'Download File',"
        "'lan_direct_download':'Direct Download',"
        "'lan_folder_share':'Folder Share',"
        "'lan_file_share':'File Share',"
        "'lan_file_list':'File List',"
        "'lan_name':'Name',"
        "'lan_size':'Size',"
        "'lan_action':'Action',"
        "'lan_upload_file':'Upload File',"
        "'lan_powered_by':'Powered by NetShare',"
        "'lan_preparing':'Preparing...',"
        "'lan_cancel':'Cancel',"
        "'lan_fetching_info':'Fetching file info...',"
        "'lan_fetch_failed':'Failed to fetch file info',"
        "'lan_no_resume':'Server does not support resume, using direct download',"
        "'lan_large_file':'Large file',"
        "'lan_suggest_manager':'a download manager is recommended',"
        "'lan_file_size':'File size: ',"
        "'lan_error':'Error: ',"
        "'lan_resuming':'Resuming... ',"
        "'lan_downloaded':' downloaded',"
        "'lan_downloading':'Downloading...',"
        "'lan_dl_failed':'Download failed: HTTP ',"
        "'lan_interrupted':'Download interrupted, click download to continue',"
        "'lan_cancelled_resume':'Cancelled, click download to continue',"
        "'lan_interrupted_msg':'Download interrupted: ',"
        "'lan_saving':'Saving...',"
        "'lan_complete':'Download complete!',"
        "'lan_pwd_title':'Password Verification',"
        "'lan_pwd_required':'This share requires a password',"
        "'lan_enter_pwd':'Please enter the access password',"
        "'lan_pwd_placeholder':'Enter password',"
        "'lan_verify':'Verify',"
        "'lan_upload_title':'Upload File',"
        "'lan_upload_subtitle':'Select files or folders to upload to LAN share',"
        "'lan_drag_here':'Drag files or folders here',"
        "'lan_select_file':'📄 Select Files',"
        "'lan_select_folder':'📁 Select Folder',"
        "'lan_uploading':'Uploading...',"
        "'lan_start_upload':'Start Upload',"
        "'lan_upload_success':'Upload successful!',"
        "'lan_upload_files':'Upload ',"
        "'lan_files_unit':' files (',"
        "'lan_prepare_upload':'Preparing upload...',"
        "'lan_upload_complete':'Upload complete',"
        "'lan_view_share':'View share page',"
        "'lan_uploading_n':'Uploading ',"
        "'lan_upload_failed':'Upload failed: ',"
        "'lan_network_error':'Network error',"
        "'lan_n_files':'%1 files',"
        "'lan_error_share_not_exist':'Share Not Found',"
        "'lan_error_share_invalid':'This share link is invalid or has expired'"
    "}";

    return (lang == "en") ? en : zh;
}

QByteArray RequestHandler::generateSharePage(const QString& token, const QString& filePath, bool isFolder, const QString& lang) const
{
    QFileInfo fi(filePath);
    QString folderName = fi.fileName();
    QString i18nDict = htmlI18nDict(lang);

    QString fileListHtml;
    if (isFolder) {
        QVariantList entries = m_fileBrowser->listDirectory(filePath);
        for (const QVariant& v : entries) {
            FileEntry entry = v.value<FileEntry>();
            QString icon = entry.isFolder ? "\xf0\x9f\x93\x81" : "\xf0\x9f\x93\x84";
            QString sizeStr = entry.isFolder ? "" : entry.displaySize;
            QString downloadLink;
            if (entry.isFolder) {
                downloadLink = QString("/s/%1?sub=%2&lang=%3").arg(token, QUrl::toPercentEncoding(entry.name), lang);
            } else {
                downloadLink = QString("/download/%1/%2").arg(token, QUrl::toPercentEncoding(entry.name));
            }
            fileListHtml += QString(
                "<tr>"
                "<td style='padding:10px 16px;border-bottom:1px solid #2a2a3e'>%1 %2</td>"
                "<td style='padding:10px 16px;border-bottom:1px solid #2a2a3e;color:#888'>%3</td>"
                "<td style='padding:10px 16px;border-bottom:1px solid #2a2a3e'>"
                "<a href='%4' style='color:#e94560;text-decoration:none'>"+QString("'+t('lan_download')+'")+"</a></td>"
                "</tr>").arg(icon, entry.name, sizeStr, downloadLink);
        }
    }

    QString downloadBtn;
    if (isFolder) {
        downloadBtn = QString("<a href='/folder/%1' style='display:inline-block;padding:14px 40px;background:#e94560;"
                              "color:#fff;text-decoration:none;border-radius:8px;font-size:16px;font-weight:bold'>"
                              "'+t('lan_download_zip')+'</a>").arg(token);
    } else {
        downloadBtn = QString(
            "<button onclick='startDownload(\"/download/%1/\")' "
            "style='display:inline-block;padding:14px 40px;background:#e94560;"
            "color:#fff;border:none;border-radius:8px;font-size:16px;font-weight:bold;cursor:pointer'>"
            "'+t('lan_download_file')+'</button>"
            "<a href='/download/%1/' style='display:inline-block;margin-left:10px;padding:14px 20px;background:#333;"
            "color:#aaa;text-decoration:none;border-radius:8px;font-size:14px'>'+t('lan_direct_download')+'</a>").arg(token);
    }

    QString fileListSection;
    if (isFolder) {
        fileListSection = QString(
            "<div style='margin-top:30px'>"
            "<h3 style='color:#ccc;font-size:16px;margin-bottom:12px'>'+t('lan_file_list')+'</h3>"
            "<table style='width:100%;border-collapse:collapse;background:#1a1a2e;border-radius:8px;overflow:hidden'>"
            "<thead><tr style='background:#16213e'>"
            "<th style='padding:12px 16px;text-align:left;color:#aaa;font-size:13px'>'+t('lan_name')+'</th>"
            "<th style='padding:12px 16px;text-align:left;color:#aaa;font-size:13px'>'+t('lan_size')+'</th>"
            "<th style='padding:12px 16px;text-align:left;color:#aaa;font-size:13px'>'+t('lan_action')+'</th>"
            "</tr></thead>"
            "<tbody>%1</tbody></table></div>").arg(fileListHtml);
    }

    QString progressSection =
        "<div id='dl-progress' style='display:none;margin-top:20px;text-align:left'>"
        "<div style='background:#1a1a2e;border-radius:8px;padding:16px'>"
        "<div style='display:flex;justify-content:space-between;margin-bottom:8px'>"
        "<span id='dl-status' style='color:#e94560;font-size:14px'>'+t('lan_preparing')+'</span>"
        "<span id='dl-speed' style='color:#888;font-size:14px'></span></div>"
        "<div style='background:#0f0f23;border-radius:4px;height:8px;overflow:hidden'>"
        "<div id='dl-bar' style='background:#e94560;height:100%;width:0%;transition:width 0.3s'></div></div>"
        "<div style='display:flex;justify-content:space-between;margin-top:8px'>"
        "<span id='dl-size' style='color:#888;font-size:12px'></span>"
        "<span id='dl-percent' style='color:#888;font-size:12px'>0%</span></div>"
        "<button id='dl-cancel-btn' onclick='cancelDownload()' "
        "style='margin-top:12px;padding:8px 20px;background:#555;color:#fff;border:none;"
        "border-radius:6px;font-size:13px;cursor:pointer;display:none'>'+t('lan_cancel')+'</button></div></div>";

    QString langSuffix = (lang == "en") ? "&lang=en" : "";

    return QString(
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>NetShare - %1</title></head>"
        "<body style='font-family:system-ui,-apple-system,sans-serif;background:#0f0f23;color:#eee;"
        "margin:0;padding:0;min-height:100vh;display:flex;align-items:center;justify-content:center'>"
        "<div style='max-width:640px;width:100%;padding:20px'>"
        "<div style='background:#16213e;border-radius:16px;padding:40px;text-align:center'>"
        "<div style='font-size:48px;margin-bottom:16px'>%2</div>"
        "<h1 style='font-size:24px;margin:0 0 8px'>%1</h1>"
        "<p style='color:#888;margin:0 0 24px'>%3</p>"
        "%4"
        "%5"
        "%6"
        "<a href='/upload/%7?lang=%8' style='display:inline-block;margin-top:20px;padding:10px 28px;"
        "background:#4caf50;color:#fff;text-decoration:none;border-radius:8px;font-size:14px'>'+t('lan_upload_file')+'</a>"
        "<p style='color:#444;font-size:12px;margin-top:30px'>'+t('lan_powered_by')+'</p>"
        "</div></div>"
        "<script>"
        "var i18n=%9;"
        "function t(k){return i18n[k]||k};"
        "var dlChunks=[];var dlFileSize=0;var dlUrl='';var dlCancelled=false;var dlController=null;"
        "function formatSize(b){if(b<1024)return b+' B';if(b<1048576)return(b/1024).toFixed(1)+' KB';return(b/1048576).toFixed(1)+' MB';}"
        "function updateProgress(downloaded,total){"
        "var pct=total>0?Math.round(downloaded/total*100):0;"
        "document.getElementById('dl-bar').style.width=pct+'%%';"
        "document.getElementById('dl-percent').textContent=pct+'%%';"
        "document.getElementById('dl-size').textContent=formatSize(downloaded)+' / '+formatSize(total);}"
        "function updateSpeed(bytesPerSec){"
        "document.getElementById('dl-speed').textContent=bytesPerSec>0?formatSize(bytesPerSec)+'/s':'';}"
        "async function startDownload(url){"
        "dlUrl=url;dlCancelled=false;"
        "document.getElementById('dl-progress').style.display='block';"
        "document.getElementById('dl-cancel-btn').style.display='inline-block';"
        "document.getElementById('dl-status').textContent=t('lan_fetching_info');"
        "try{"
        "var headResp=await fetch(url,{method:'HEAD'});"
        "if(!headResp.ok){document.getElementById('dl-status').textContent=t('lan_fetch_failed');return;}"
        "dlFileSize=parseInt(headResp.headers.get('Content-Length'))||0;"
        "var acceptRanges=headResp.headers.get('Accept-Ranges');"
        "if(acceptRanges!=='bytes'){document.getElementById('dl-status').textContent=t('lan_no_resume');window.location.href=url;return;}"
        "if(dlFileSize>524288000){document.getElementById('dl-status').textContent=t('lan_large_file')+'('+formatSize(dlFileSize)+'),'+t('lan_suggest_manager');}"
        "else{document.getElementById('dl-status').textContent=t('lan_file_size')+formatSize(dlFileSize);}"
        "await resumeDownload();"
        "}catch(e){document.getElementById('dl-status').textContent=t('lan_error')+e.message;}}"
        "async function resumeDownload(){"
        "var downloaded=0;for(var i=0;i<dlChunks.length;i++){var buf=await dlChunks[i].arrayBuffer();downloaded+=buf.byteLength;}"
        "if(downloaded>=dlFileSize&&dlFileSize>0){saveFile();return;}"
        "dlController=new AbortController();"
        "var headers={};if(downloaded>0){headers['Range']='bytes='+downloaded+'-';}"
        "document.getElementById('dl-status').textContent=downloaded>0?t('lan_resuming')+formatSize(downloaded)+t('lan_downloaded'):t('lan_downloading');"
        "updateProgress(downloaded,dlFileSize);"
        "var lastTime=Date.now();var lastDownloaded=downloaded;"
        "try{"
        "var resp=await fetch(dlUrl,{headers:headers,signal:dlController.signal});"
        "if(!resp.ok&&(resp.status!==206)){document.getElementById('dl-status').textContent=t('lan_dl_failed')+resp.status;return;}"
        "var reader=resp.body.getReader();"
        "while(true){"
        "if(dlCancelled)break;"
        "var result=await reader.read();"
        "if(result.done)break;"
        "dlChunks.push(result.value);"
        "downloaded=0;for(var i=0;i<dlChunks.length;i++){downloaded+=dlChunks[i].byteLength;}"
        "var now=Date.now();var dt=now-lastTime;"
        "if(dt>=500){var speed=(downloaded-lastDownloaded)*1000/dt;updateSpeed(speed);lastTime=now;lastDownloaded=downloaded;}"
        "updateProgress(downloaded,dlFileSize);}"
        "if(!dlCancelled){"
        "if(dlFileSize>0&&downloaded>=dlFileSize){saveFile();}"
        "else if(dlFileSize<=0&&result.done){dlFileSize=downloaded;saveFile();}"
        "else{document.getElementById('dl-status').textContent=t('lan_interrupted');}}"
        "else{document.getElementById('dl-status').textContent=t('lan_cancelled_resume');}"
        "}catch(e){"
        "if(e.name==='AbortError'){document.getElementById('dl-status').textContent=t('lan_cancelled_resume');}"
        "else{document.getElementById('dl-status').textContent=t('lan_interrupted_msg')+e.message;}}}"
        "function saveFile(){"
        "document.getElementById('dl-status').textContent=t('lan_saving');"
        "var blob=new Blob(dlChunks);var url=URL.createObjectURL(blob);"
        "var a=document.createElement('a');a.href=url;"
        "var name=dlUrl.split('/').filter(function(s){return s.length>0}).pop()||'download';"
        "a.download=decodeURIComponent(name);a.click();"
        "setTimeout(function(){URL.revokeObjectURL(url);},10000);"
        "document.getElementById('dl-status').textContent=t('lan_complete');"
        "updateProgress(dlFileSize,dlFileSize);dlChunks=[];}"
        "function cancelDownload(){dlCancelled=true;if(dlController)dlController.abort();}"
        "</script></body></html>")
        .arg(folderName)
        .arg(isFolder ? "\xf0\x9f\x93\x82" : "\xf0\x9f\x93\x84")
        .arg(isFolder ? (lang == "en" ? "Folder Share" : "文件夹分享") : (lang == "en" ? "File Share" : "文件分享"))
        .arg(downloadBtn)
        .arg(progressSection)
        .arg(fileListSection)
        .arg(token)
        .arg(lang)
        .arg(i18nDict)
        .toUtf8();
}

QByteArray RequestHandler::generatePasswordPage(const QString& token, const QString& lang) const
{
    QString i18nDict = htmlI18nDict(lang);
    QString langSuffix = (lang == "en") ? "&lang=en" : "";
    return QString(
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>NetShare - '+t('lan_pwd_title')+'</title></head>"
        "<body style='font-family:system-ui,sans-serif;background:#0f0f23;color:#eee;"
        "margin:0;padding:0;min-height:100vh;display:flex;align-items:center;justify-content:center'>"
        "<div style='max-width:400px;width:100%;padding:20px'>"
        "<div style='background:#16213e;border-radius:16px;padding:40px;text-align:center'>"
        "<div style='font-size:48px;margin-bottom:16px'>🔒</div>"
        "<h1 style='font-size:20px;margin:0 0 8px'>'+t('lan_pwd_required')+'</h1>"
        "<p style='color:#888;margin:0 0 24px'>'+t('lan_enter_pwd')+'</p>"
        "<form method='get' action='/s/%1%2' style='display:flex;gap:8px'>"
        "<input type='password' name='pw' placeholder=\"'+t('lan_pwd_placeholder')+'\" "
        "style='flex:1;padding:12px 16px;border:1px solid #2a2a3e;border-radius:8px;"
        "background:#1a1a2e;color:#eee;font-size:14px;outline:none'>"
        "<button type='submit' style='padding:12px 24px;background:#e94560;color:#fff;"
        "border:none;border-radius:8px;font-size:14px;cursor:pointer'>'+t('lan_verify')+'</button>"
        "</form></div></div>"
        "<script>var i18n=%3;function t(k){return i18n[k]||k};</script>"
        "</body></html>")
        .arg(token).arg(langSuffix).arg(i18nDict).toUtf8();
}

QByteArray RequestHandler::generateErrorPage(const QString& lang) const
{
    QString i18nDict = htmlI18nDict(lang);
    QString langAttr = (lang == "en") ? "en" : "zh";
    return QString(
        "<!DOCTYPE html><html lang='%1'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>NetShare - '+t('lan_error_share_not_exist')+'</title></head>"
        "<body style='font-family:system-ui,sans-serif;background:#0f0f23;color:#eee;"
        "margin:0;padding:0;min-height:100vh;display:flex;align-items:center;justify-content:center'>"
        "<div style='max-width:400px;width:100%;padding:20px'>"
        "<div style='background:#16213e;border-radius:16px;padding:40px;text-align:center'>"
        "<div style='font-size:48px;margin-bottom:16px'>😕</div>"
        "<h1 style='font-size:20px;margin:0 0 8px'>'+t('lan_error_share_not_exist')+'</h1>"
        "<p style='color:#888;margin:0 0 24px'>'+t('lan_error_share_invalid')+'</p>"
        "</div></div>"
        "<script>var i18n=%2;function t(k){return i18n[k]||k;}</script>"
        "</body></html>")
        .arg(langAttr).arg(i18nDict).toUtf8();
}

QByteArray RequestHandler::generateUploadPage(const QString& token, const QString& lang) const
{
    QString i18nDict = htmlI18nDict(lang);
    QString langSuffix = (lang == "en") ? "?lang=en" : "";
    return QString(
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>NetShare - '+t('lan_upload_title')+'</title></head>"
        "<body style='font-family:system-ui,sans-serif;background:#0f0f23;color:#eee;"
        "margin:0;padding:0;min-height:100vh;display:flex;align-items:center;justify-content:center'>"
        "<div style='max-width:540px;width:100%;padding:20px'>"
        "<div style='background:#16213e;border-radius:16px;padding:40px;text-align:center'>"
        "<div style='font-size:48px;margin-bottom:16px'>📤</div>"
        "<h1 style='font-size:24px;margin:0 0 8px'>'+t('lan_upload_title')+'</h1>"
        "<p style='color:#888;margin:0 0 24px'>'+t('lan_upload_subtitle')+'</p>"
        "<input type='file' multiple style='display:none' id='fileInput'>"
        "<input type='file' multiple webkitdirectory directory mozdirectory style='display:none' id='folderInput'>"
        "<div id='dropZone' style='border:2px dashed #2a2a3e;border-radius:12px;padding:40px;"
        "cursor:pointer;transition:border-color 0.2s'>"
        "<div style='font-size:48px;margin-bottom:12px'>📁</div>"
        "<p style='color:#888;font-size:14px'>'+t('lan_drag_here')+'</p>"
        "</div>"
        "<div style='display:flex;gap:8px;margin-top:12px'>"
        "<div id='selectFileBtn' "
        "style='flex:1;padding:10px;background:#1a1a3e;color:#4caf50;border:1px solid #2a2a3e;"
        "border-radius:8px;font-size:14px;cursor:pointer;text-align:center'>'+t('lan_select_file')+'</div>"
        "<div id='selectFolderBtn' "
        "style='flex:1;padding:10px;background:#1a1a3e;color:#2196f3;border:1px solid #2a2a3e;"
        "border-radius:8px;font-size:14px;cursor:pointer;text-align:center'>'+t('lan_select_folder')+'</div>"
        "</div>"
        "<div id='fileList' style='margin:16px 0;max-height:240px;overflow-y:auto'></div>"
        "<div id='progress' style='display:none;margin:16px 0'>"
        "<div style='height:6px;background:#2a2a3e;border-radius:3px;overflow:hidden'>"
        "<div id='progressBar' style='height:100%;background:#4caf50;width:0%;transition:width 0.3s'></div></div>"
        "<div style='display:flex;justify-content:space-between;margin-top:8px'>"
        "<span id='progressText' style='color:#888;font-size:12px'>0%</span>"
        "<span id='statusText' style='color:#888;font-size:12px'>'+t('lan_uploading')+'</span></div></div>"
        "<button type='button' id='uploadBtn' disabled style='width:100%;padding:14px;"
        "background:#4caf50;color:#fff;border:none;border-radius:8px;font-size:16px;"
        "font-weight:600;cursor:pointer'>'+t('lan_start_upload')+'</button>"
        "<div id='result' style='display:none;margin-top:20px'>"
        "<div style='font-size:48px;margin-bottom:12px'>✅</div>"
        "<p style='color:#888;font-size:14px;margin-bottom:16px'>'+t('lan_upload_success')+'</p>"
        "<a id='resultLink' href='#' style='color:#4caf50;word-break:break-all;font-size:13px'></a></div>"
        "</div>"
        "<script>"
        "var i18n=%2;"
        "function t(k){return i18n[k]||k};"
        "(function(){"
        "var token='%1';"
        "var allFiles=[];"
        "var fileInput=document.getElementById('fileInput');"
        "var folderInput=document.getElementById('folderInput');"
        "var dropZone=document.getElementById('dropZone');"
        "var fileList=document.getElementById('fileList');"
        "var uploadBtn=document.getElementById('uploadBtn');"
        "var progress=document.getElementById('progress');"
        "var progressBar=document.getElementById('progressBar');"
        "var progressText=document.getElementById('progressText');"
        "var statusText=document.getElementById('statusText');"
        "var result=document.getElementById('result');"
        "var resultLink=document.getElementById('resultLink');"
        "function formatSize(s){if(s<1024)return s+'B';if(s<1048576)return(s/1024).toFixed(1)+'KB';return(s/1048576).toFixed(1)+'MB'}"
        "function updateFileList(){"
        "fileList.innerHTML=allFiles.map(function(f,i){"
        "return '<div style=\"padding:6px 8px;background:#1a1a3e;border-radius:6px;margin:4px 0;"
        "display:flex;justify-content:space-between;align-items:center;font-size:13px\">"
        "<span style=\"overflow:hidden;text-overflow:ellipsis;white-space:nowrap;flex:1\">'+f.name+'</span>"
        "<span style=\"color:#888;margin-left:8px\">'+formatSize(f.size)+'</span>"
        "<span onclick=\"allFiles.splice('+i+',1);updateFileList()\" style=\"color:#e94560;cursor:pointer;margin-left:8px\">✕</span>"
        "</div>';}).join('');"
        "uploadBtn.disabled=allFiles.length===0;"
        "if(allFiles.length>0){uploadBtn.textContent=t('lan_upload_files')+allFiles.length+t('lan_files_unit')+formatSize(allFiles.reduce(function(a,b){return a+b.size},0))+')'}"
        "else{uploadBtn.textContent=t('lan_start_upload')}}"
        "function addFiles(fileList){for(var i=0;i<fileList.length;i++){allFiles.push(fileList[i])}updateFileList()}"
        "document.getElementById('selectFileBtn').onclick=function(){fileInput.click()};"
        "document.getElementById('selectFolderBtn').onclick=function(){folderInput.click()};"
        "fileInput.onchange=function(){addFiles(this.files);this.value=''};"
        "folderInput.onchange=function(){addFiles(this.files);this.value=''};"
        "dropZone.ondragover=function(e){e.preventDefault();this.style.borderColor='#4caf50'};"
        "dropZone.ondragleave=function(){this.style.borderColor='#2a2a3e'};"
        "dropZone.ondrop=function(e){e.preventDefault();this.style.borderColor='#2a2a3e';addFiles(e.dataTransfer.files)};"
        "uploadBtn.onclick=function(){"
        "if(allFiles.length===0)return;"
        "uploadBtn.disabled=true;progress.style.display='block';result.style.display='none';"
        "statusText.textContent=t('lan_prepare_upload');"
        "var idx=0;var total=allFiles.length;"
        "function uploadNext(){"
        "if(idx>=total){"
        "progressBar.style.width='100%';progressText.textContent='100%';statusText.textContent=t('lan_upload_complete');"
        "result.style.display='block';resultLink.href='/s/'+token+'%3';resultLink.textContent=t('lan_view_share');"
        "allFiles=[];updateFileList();uploadBtn.disabled=false;return"
        "}"
        "var file=allFiles[idx];statusText.textContent=t('lan_uploading_n')+(idx+1)+'/'+total+'): '+file.name;"
        "var fd=new FormData();fd.append('file',file);fd.append('token',token);"
        "var xhr=new XMLHttpRequest();"
        "xhr.upload.onprogress=function(e){if(e.lengthComputable){"
        "var pct=Math.round((idx+e.loaded/e.length)/total*100);"
        "progressBar.style.width=pct+'%';progressText.textContent=pct+'%'}};"
        "xhr.onload=function(){if(xhr.status===200){idx++;uploadNext()}"
        "else{statusText.textContent=t('lan_upload_failed')+xhr.statusText;uploadBtn.disabled=false}};"
        "xhr.onerror=function(){statusText.textContent=t('lan_network_error');uploadBtn.disabled=false};"
        "xhr.open('POST','/upload/'+token,true);xhr.send(fd)"
        "}"
        "uploadNext()"
        "}"
        "})()"
        "</script>"
        "</div></div></body></html>")
        .arg(token).arg(i18nDict).arg(langSuffix).toUtf8();
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
