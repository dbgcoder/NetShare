#include "RequestHandler.h"
#include "ShareManager.h"
#include "FileBrowser.h"
#include "FolderPacker.h"
#include "FileTransferEngine.h"
#include "StreamingMultipartParser.h"
#include "ChunkManager.h"
#include "TransferLogService.h"
#include "SettingsManager.h"
#include "Logger.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QMimeDatabase>
#include <QUrl>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

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
    // Setup session cleanup timer (check every 5 minutes, expire after 30 minutes)
    m_sessionCleanupTimer = new QTimer(this);
    connect(m_sessionCleanupTimer, &QTimer::timeout, this, &RequestHandler::cleanupExpiredSessions);
    m_sessionCleanupTimer->start(5 * 60 * 1000); // 5 minutes
}

RequestHandler::~RequestHandler()
{
    // Clean up any remaining streaming upload states (legacy /receive route)
    for (auto it = m_streamingStates.begin(); it != m_streamingStates.end(); ++it) {
        delete it.value()->parser;
        delete it.value();
    }
    m_streamingStates.clear();
}

QString RequestHandler::tokenForTask(const QString& taskId) const
{
    return m_taskToToken.value(taskId, QString());
}

QString RequestHandler::shareTokenForTask(const QString& taskId) const
{
    return m_taskToShareToken.value(taskId, QString());
}

void RequestHandler::registerRoutes(HttpServer* server)
{
    m_httpServer = server;

    server->addRoute("GET", "/", [this](const HttpRequest& req, HttpResponse& res) {
        Q_UNUSED(req)
        res = HttpResponse::ok(
            "<html><head><meta charset='utf-8'><title>NetShare</title></head>"
            "<body style='font-family:system-ui,sans-serif;text-align:center;padding:80px 20px;background:#1a1a2e;color:#eee'>"
            "<h1 style='font-size:3em;color:#e94560'>NetShare</h1>"
            "<p style='font-size:1.2em;color:#aaa'>局域网文件分享服务</p>"
            "<p style='color:#666;margin-top:40px'>请使用分享链接访问具体分享内容</p></body></html>",
            "text/html");
    });

    server->addRoute("GET", "/s/*", [this](const HttpRequest& req, HttpResponse& res) {
        handleSharePage(req, res);
    });

    server->addRoute("GET", "/download/*", [this](const HttpRequest& req, HttpResponse& res) {
        handleFileDownload(req, res);
    });

    server->addRoute("GET", "/folder/*", [this](const HttpRequest& req, HttpResponse& res) {
        handleFolderDownload(req, res);
    });

    server->addRoute("GET", "/api/shares", [this](const HttpRequest& req, HttpResponse& res) {
        handleApiShares(req, res);
    });

    server->addRoute("GET", "/api/files/*", [this](const HttpRequest& req, HttpResponse& res) {
        handleApiFiles(req, res);
    });

    server->addRoute("GET", "/receive", [](const HttpRequest& req, HttpResponse& res) {
        Q_UNUSED(req)
        // Try multiple paths: relative to executable, relative to working directory
        QStringList paths = {
            "web/receive.html",
            QCoreApplication::applicationDirPath() + "/web/receive.html",
        };
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
            res = HttpResponse::internalError("无法加载接收页面");
            return;
        }
        res = HttpResponse::ok(page, "text/html; charset=utf-8");
        res.headers["Cache-Control"] = "no-cache, no-store, must-revalidate";
        res.headers["Pragma"] = "no-cache";
        res.headers["Expires"] = "0";
    });

    server->addRoute("GET", "/upload/*", [this](const HttpRequest& req, HttpResponse& res) {
        handleUploadPage(req, res);
    });

    // Streaming upload routes - receive body data in real-time chunks
    server->addStreamingRoute("POST", "/receive", [this](QTcpSocket* socket, const HttpRequest& request, const QByteArray& chunk, bool isLast) {
        handleStreamingUpload(socket, request, chunk, isLast);
    });

    // Resume-capable upload API routes (per-file upload with breakpoint continuation)
    server->addRoute("POST", "/api/upload/check", [this](const HttpRequest& req, HttpResponse& res) {
        handleUploadCheck(req, res);
    });

    server->addStreamingRoute("POST", "/api/upload/file", [this](QTcpSocket* socket, const HttpRequest& request, const QByteArray& chunk, bool isLast) {
        handleStreamingFileUpload(socket, request, chunk, isLast);
    });

    server->addRoute("POST", "/api/upload/finalize", [this](const HttpRequest& req, HttpResponse& res) {
        handleUploadFinalize(req, res);
    });

    server->addRoute("POST", "/api/upload/abort", [this](const HttpRequest& req, HttpResponse& res) {
        handleUploadAbort(req, res);
    });

    server->addStreamingRoute("POST", "/upload/*", [this](QTcpSocket* socket, const HttpRequest& request, const QByteArray& chunk, bool isLast) {
        handleStreamingUpload(socket, request, chunk, isLast);
    });

    // Connect streaming socket disconnect signal for state cleanup
    connect(server, &HttpServer::streamingSocketDisconnected,
            this, [this](QTcpSocket* socket) {
        // Clean up streaming upload state (legacy /receive route)
        if (m_streamingStates.contains(socket)) {
            auto* state = m_streamingStates[socket];
            // Mark the task as failed in the transfer engine
            if (m_transferEngine && !state->uploadTaskId.isEmpty()) {
                m_transferEngine->failTask(state->uploadTaskId, "Upload interrupted: connection lost");
            }
            // Clean up token mappings
            if (!state->uploadTaskId.isEmpty()) {
                m_taskToToken.remove(state->uploadTaskId);
                m_taskToShareToken.remove(state->uploadTaskId);
            }
            delete state->parser;
            delete state;
            m_streamingStates.remove(socket);
            LOG_INFO("Cleaned up streaming upload state on socket disconnect, task marked as failed");
        }
        // Clean up streaming file upload state (/api/upload/file route)
        if (m_streamingFileStates.contains(socket)) {
            auto* state = m_streamingFileStates[socket];
            if (state->chunkFile) {
                state->chunkFile->close();
                delete state->chunkFile;
            }
            if (state->parser) {
                delete state->parser;
            }
            delete state;
            m_streamingFileStates.remove(socket);
            LOG_INFO("Cleaned up streaming file upload state on socket disconnect");
        }
    });
}

void RequestHandler::handleSharePage(const HttpRequest& request, HttpResponse& response)
{
    QString token = request.path.mid(3);
    if (token.isEmpty()) {
        response = HttpResponse::notFound("分享不存在");
        return;
    }

    ShareInfo info = m_shareManager->getShareInfo(token);
    if (!info.isValid() || info.isExpired()) {
        LOG_WARN("Share access rejected: token=%s, valid=%d, expired=%d, expiresAt=%s",
                 qPrintable(token), info.isValid(), info.isExpired(),
                 qPrintable(info.expiresAt.toString(Qt::ISODate)));
        response = HttpResponse::ok(generateErrorPage("分享不存在", "该分享链接无效或已过期"), "text/html");
        return;
    }

    if (info.passwordRequired) {
        QString pw = request.queryParams.value("pw");
        if (pw.isEmpty() || !m_shareManager->validateShare(token, pw)) {
            response = HttpResponse::ok(generatePasswordPage(token), "text/html");
            return;
        }
    }

    m_shareManager->shareAccessed(token);
    response = HttpResponse::ok(generateSharePage(token, info.filePath, info.isFolder), "text/html");
}

void RequestHandler::handleFileDownload(const HttpRequest& request, HttpResponse& response)
{
    QString tokenAndPath = request.path.mid(10);
    int slashIndex = tokenAndPath.indexOf('/');
    if (slashIndex < 0) {
        response = HttpResponse::badRequest("Invalid download URL");
        return;
    }

    QString token = tokenAndPath.left(slashIndex);
    QString subPath = QUrl::fromPercentEncoding(tokenAndPath.mid(slashIndex + 1).toUtf8());

    ShareInfo info = m_shareManager->getShareInfo(token);
    if (!info.isValid() || info.isExpired()) {
        response = HttpResponse::notFound("分享不存在或已过期");
        return;
    }

    if (info.passwordRequired) {
        QString pw = request.queryParams.value("pw");
        if (!m_shareManager->validateShare(token, pw)) {
            response = HttpResponse::badRequest("密码错误");
            return;
        }
    }

    QString filePath;
    if (subPath.isEmpty()) {
        filePath = info.filePath;
    } else {
        filePath = info.filePath + "/" + subPath;
    }

    QFileInfo fi(filePath);
    if (!fi.exists() || fi.isDir()) {
        response = HttpResponse::notFound("文件不存在");
        return;
    }

    QString contentType = mimeTypeForFile(fi.fileName());
    QString rangeHeader = request.headers.value("Range");
    response = HttpResponse::streamingFileResponse(filePath, fi.fileName(), contentType, rangeHeader);

    LOG_INFO("File download: %s (%lld bytes) from %s",
             qPrintable(fi.fileName()), fi.size(),
             qPrintable(request.remoteAddress));

    recordCompletedTransfer(0, fi.fileName(), fi.size(), request.remoteAddress, filePath);
}

void RequestHandler::handleFolderDownload(const HttpRequest& request, HttpResponse& response)
{
    QString token = request.path.mid(9);
    if (token.isEmpty()) {
        response = HttpResponse::badRequest("Invalid folder download URL");
        return;
    }

    ShareInfo info = m_shareManager->getShareInfo(token);
    if (!info.isValid() || info.isExpired()) {
        response = HttpResponse::notFound("分享不存在或已过期");
        return;
    }

    if (!info.isFolder) {
        response = HttpResponse::badRequest("Not a folder share");
        return;
    }

    if (info.passwordRequired) {
        QString pw = request.queryParams.value("pw");
        if (!m_shareManager->validateShare(token, pw)) {
            response = HttpResponse::badRequest("密码错误");
            return;
        }
    }

    QFileInfo fi(info.filePath);
    QString outputPath = m_folderPacker->defaultOutputPath(info.filePath);

    if (!m_folderPacker->packFolder(info.filePath, outputPath)) {
        response = HttpResponse::internalError("文件夹打包失败");
        return;
    }

    QString zipName = fi.fileName() + ".zip";
    response = HttpResponse::streamingFileResponse(outputPath, zipName, "application/zip",
                                                     request.headers.value("Range"), outputPath);

    LOG_INFO("Folder download: %s from %s",
             qPrintable(fi.fileName()),
             qPrintable(request.remoteAddress));
}

void RequestHandler::handleApiShares(const HttpRequest& request, HttpResponse& response)
{
    Q_UNUSED(request)
    QVariantList shares = m_shareManager->getActiveShares();

    QByteArray json = "[";
    for (int i = 0; i < shares.size(); ++i) {
        ShareInfo info = shares[i].value<ShareInfo>();
        if (i > 0) json.append(",");
        json.append(QString("{\"token\":\"%1\",\"filePath\":\"%2\",\"isFolder\":%3,\"fileSize\":%4,\"downloadCount\":%5,\"passwordRequired\":%6}")
                    .arg(info.token)
                    .arg(QString(info.filePath).replace("\\", "/").replace("\"", "\\\""))
                    .arg(info.isFolder ? "true" : "false")
                    .arg(info.fileSize)
                    .arg(info.downloadCount)
                    .arg(info.passwordRequired ? "true" : "false").toUtf8());
    }
    json.append("]");

    response = HttpResponse::ok(json, "application/json");
}

void RequestHandler::handleApiFiles(const HttpRequest& request, HttpResponse& response)
{
    QString tokenAndPath = request.path.mid(11);
    int slashIndex = tokenAndPath.indexOf('/');
    QString token = slashIndex >= 0 ? tokenAndPath.left(slashIndex) : tokenAndPath;
    QString subPath = slashIndex >= 0 ? QUrl::fromPercentEncoding(tokenAndPath.mid(slashIndex + 1).toUtf8()) : "";

    ShareInfo info = m_shareManager->getShareInfo(token);
    if (!info.isValid() || info.isExpired()) {
        response = HttpResponse::notFound("Share not found");
        return;
    }

    QString dirPath = info.filePath;
    if (!subPath.isEmpty()) {
        dirPath = info.filePath + "/" + subPath;
    }

    if (!m_fileBrowser->isDirectory(dirPath)) {
        response = HttpResponse::badRequest("Not a directory");
        return;
    }

    QVariantList entries = m_fileBrowser->listDirectory(dirPath);

    QByteArray json = "[";
    for (int i = 0; i < entries.size(); ++i) {
        FileEntry entry = entries[i].value<FileEntry>();
        if (i > 0) json.append(",");
        json.append(QString("{\"name\":\"%1\",\"isFolder\":%2,\"fileSize\":%3,\"displaySize\":\"%4\",\"extension\":\"%5\"}")
                    .arg(QString(entry.name).replace("\"", "\\\""))
                    .arg(entry.isFolder ? "true" : "false")
                    .arg(entry.fileSize)
                    .arg(entry.displaySize)
                    .arg(entry.extension).toUtf8());
    }
    json.append("]");

    response = HttpResponse::ok(json, "application/json");
}

QByteArray RequestHandler::generateSharePage(const QString& token, const QString& filePath, bool isFolder) const
{
    QFileInfo fi(filePath);
    QString folderName = fi.fileName();

    QString fileListHtml;
    if (isFolder) {
        QVariantList entries = m_fileBrowser->listDirectory(filePath);
        for (const QVariant& v : entries) {
            FileEntry entry = v.value<FileEntry>();
            QString icon = entry.isFolder ? "📁" : "📄";
            QString sizeStr = entry.isFolder ? "" : entry.displaySize;
            QString downloadLink;
            if (entry.isFolder) {
                downloadLink = QString("/s/%1?sub=%2").arg(token, QUrl::toPercentEncoding(entry.name));
            } else {
                downloadLink = QString("/download/%1/%2").arg(token, QUrl::toPercentEncoding(entry.name));
            }
            fileListHtml += QString(
                "<tr>"
                "<td style='padding:10px 16px;border-bottom:1px solid #2a2a3e'>%1 %2</td>"
                "<td style='padding:10px 16px;border-bottom:1px solid #2a2a3e;color:#888'>%3</td>"
                "<td style='padding:10px 16px;border-bottom:1px solid #2a2a3e'>"
                "<a href='%4' style='color:#e94560;text-decoration:none'>下载</a></td>"
                "</tr>").arg(icon, entry.name, sizeStr, downloadLink);
        }
    }

    QString downloadBtn;
    if (isFolder) {
        downloadBtn = QString("<a href='/folder/%1' style='display:inline-block;padding:14px 40px;background:#e94560;"
                              "color:#fff;text-decoration:none;border-radius:8px;font-size:16px;font-weight:bold'>"
                              "打包下载 (ZIP)</a>").arg(token);
    } else {
        downloadBtn = QString("<a href='/download/%1/' style='display:inline-block;padding:14px 40px;background:#e94560;"
                              "color:#fff;text-decoration:none;border-radius:8px;font-size:16px;font-weight:bold'>"
                              "下载文件</a>").arg(token);
    }

    QString fileListSection;
    if (isFolder) {
        fileListSection = QString(
            "<div style='margin-top:30px'>"
            "<h3 style='color:#ccc;font-size:16px;margin-bottom:12px'>文件列表</h3>"
            "<table style='width:100%;border-collapse:collapse;background:#1a1a2e;border-radius:8px;overflow:hidden'>"
            "<thead><tr style='background:#16213e'>"
            "<th style='padding:12px 16px;text-align:left;color:#aaa;font-size:13px'>名称</th>"
            "<th style='padding:12px 16px;text-align:left;color:#aaa;font-size:13px'>大小</th>"
            "<th style='padding:12px 16px;text-align:left;color:#aaa;font-size:13px'>操作</th>"
            "</tr></thead>"
            "<tbody>%1</tbody></table></div>").arg(fileListHtml);
    }

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
        "<a href='/upload/%6' style='display:inline-block;margin-top:20px;padding:10px 28px;"
        "background:#4caf50;color:#fff;text-decoration:none;border-radius:8px;font-size:14px'>📤 上传文件</a>"
        "<p style='color:#444;font-size:12px;margin-top:30px'>由 NetShare 提供</p>"
        "</div></div></body></html>")
        .arg(folderName)
        .arg(isFolder ? "📂" : "📄")
        .arg(isFolder ? "文件夹分享" : "文件分享")
        .arg(downloadBtn)
        .arg(fileListSection)
        .arg(token)
        .toUtf8();
}

QByteArray RequestHandler::generatePasswordPage(const QString& token) const
{
    return QString(
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>NetShare - 密码验证</title></head>"
        "<body style='font-family:system-ui,sans-serif;background:#0f0f23;color:#eee;"
        "margin:0;padding:0;min-height:100vh;display:flex;align-items:center;justify-content:center'>"
        "<div style='max-width:400px;width:100%;padding:20px'>"
        "<div style='background:#16213e;border-radius:16px;padding:40px;text-align:center'>"
        "<div style='font-size:48px;margin-bottom:16px'>🔒</div>"
        "<h1 style='font-size:20px;margin:0 0 8px'>该分享需要密码</h1>"
        "<p style='color:#888;margin:0 0 24px'>请输入访问密码</p>"
        "<form method='get' action='/s/%1' style='display:flex;gap:8px'>"
        "<input type='password' name='pw' placeholder='输入密码' "
        "style='flex:1;padding:12px 16px;border:1px solid #2a2a3e;border-radius:8px;"
        "background:#1a1a2e;color:#eee;font-size:14px;outline:none'>"
        "<button type='submit' style='padding:12px 24px;background:#e94560;color:#fff;"
        "border:none;border-radius:8px;font-size:14px;cursor:pointer'>验证</button>"
        "</form></div></div></body></html>")
        .arg(token).toUtf8();
}

QByteArray RequestHandler::generateErrorPage(const QString& title, const QString& message) const
{
    return QString(
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>NetShare - %1</title></head>"
        "<body style='font-family:system-ui,sans-serif;background:#0f0f23;color:#eee;"
        "margin:0;padding:0;min-height:100vh;display:flex;align-items:center;justify-content:center'>"
        "<div style='max-width:400px;width:100%;padding:20px'>"
        "<div style='background:#16213e;border-radius:16px;padding:40px;text-align:center'>"
        "<div style='font-size:48px;margin-bottom:16px'>😕</div>"
        "<h1 style='font-size:20px;margin:0 0 8px'>%1</h1>"
        "<p style='color:#888;margin:0'>%2</p>"
        "</div></div></body></html>")
        .arg(title, message).toUtf8();
}

void RequestHandler::setUploadDir(const QString& dir)
{
    m_uploadDir = dir;
    if (!m_uploadDir.isEmpty()) {
        QDir().mkpath(m_uploadDir);
    }
}

QString RequestHandler::uploadDir() const
{
    if (m_settingsManager) {
        QString configPath = m_settingsManager->getString("Receive/StoragePath", QString());
        if (!configPath.isEmpty()) return configPath;
    }
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

void RequestHandler::recordCompletedTransfer(int type, const QString& fileName, qint64 fileSize,
                                              const QString& remoteAddress, const QString& filePath)
{
    if (!m_transferEngine) return;

    QString taskId = QUuid::createUuid().toString();
    TransferTask task;
    task.taskId = taskId;
    task.type = type;  // 0=Download, 1=Upload
    task.status = TransferTask::Completed;
    task.fileName = fileName;
    task.fileSize = fileSize;
    task.transferredSize = fileSize;
    task.progress = 100;
    task.filePath = filePath;
    task.startedAt = QDateTime::currentDateTime();
    task.completedAt = QDateTime::currentDateTime();

    // Add directly to the engine's task map
    m_transferEngine->addCompletedTask(task);

    // Persist to transfer log database
    if (m_transferLogService) {
        int logType = (type == 0) ? 0 : 1; // TransferLogEntry::DownloadLog=0, UploadLog=1
        m_transferLogService->logTransfer(logType, fileName, filePath, fileSize, remoteAddress,
                                2); // TransferLogEntry::Completed=2
    }

    if (type == 0) {
        emit fileDownloaded(fileName, fileSize, remoteAddress);
    } else {
        emit fileUploaded(fileName, fileSize, remoteAddress);
    }
}

void RequestHandler::handleUploadPage(const HttpRequest& request, HttpResponse& response)
{
    QString token = request.path.mid(8);
    if (token.isEmpty()) {
        response = HttpResponse::notFound("Invalid upload URL");
        return;
    }

    ShareInfo info = m_shareManager->getShareInfo(token);
    if (!info.isValid() || info.isExpired()) {
        response = HttpResponse::ok(generateErrorPage("分享不存在", "该分享链接无效或已过期"), "text/html");
        return;
    }

    // Serve upload.html
    QString htmlPath = QDir::currentPath() + "/web/upload.html";
    QFile file(htmlPath);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray html = file.readAll();
        file.close();

        // Inject token into the page by replacing the token extraction logic
        html.replace("new URLSearchParams(location.search).get('token') || ''",
                     ("'" + token + "'").toUtf8());

        response = HttpResponse::ok(html, "text/html");
    } else {
        // Fallback: generate upload page inline
        response = HttpResponse::ok(generateUploadPage(token), "text/html");
    }
}

void RequestHandler::handleStreamingUpload(QTcpSocket* socket, const HttpRequest& request, const QByteArray& chunk, bool isLast)
{
    // First call: create parser and state
    if (!m_streamingStates.contains(socket)) {
        QString contentType = request.headers.value("Content-Type");
        if (!contentType.startsWith("multipart/form-data")) {
            HttpResponse response = HttpResponse::badRequest("Expected multipart/form-data");
            m_httpServer->sendResponse(socket, response);
            m_httpServer->cancelStreamingRequest(socket);
            return;
        }

        QString dir = uploadDir();
        if (dir.isEmpty()) {
            dir = QDir::tempPath() + "/netshare_uploads";
            QDir().mkpath(dir);
        }

        // For /upload/{token}, validate the token
        bool isReceive = request.path == "/receive";
        QString token;
        if (!isReceive) {
            token = request.path.mid(8);
            if (token.isEmpty()) {
                HttpResponse response = HttpResponse::badRequest("Invalid upload URL");
                m_httpServer->sendResponse(socket, response);
                m_httpServer->cancelStreamingRequest(socket);
                return;
            }
            ShareInfo info = m_shareManager->getShareInfo(token);
            if (!info.isValid() || info.isExpired()) {
                HttpResponse response = HttpResponse::notFound("分享不存在或已过期");
                m_httpServer->sendResponse(socket, response);
                m_httpServer->cancelStreamingRequest(socket);
                return;
            }
        }

        auto* state = new StreamingUploadState;
        state->parser = new StreamingMultipartParser(dir);
        if (!state->parser->init(contentType)) {
            delete state->parser;
            delete state;
            HttpResponse response = HttpResponse::badRequest("Invalid multipart boundary");
            m_httpServer->sendResponse(socket, response);
            m_httpServer->cancelStreamingRequest(socket);
            return;
        }
        state->token = token;
        state->remoteAddress = request.remoteAddress;
        state->isReceive = isReceive;
        state->currentFileTransferred = 0;

        // Create a transfer task for progress tracking
        if (m_transferEngine) {
            TransferTask task;
            task.taskId = QUuid::createUuid().toString();
            task.type = TransferTask::Upload;
            task.status = TransferTask::Uploading;
            task.fileName = isReceive ? "接收文件" : "上传文件";
            task.filePath = dir;
            task.startedAt = QDateTime::currentDateTime();
            m_transferEngine->addUploadingTask(task);
            state->uploadTaskId = task.taskId;

            // Map taskId to token for WebSocket progress routing
            m_taskToToken[task.taskId] = token;
            // Streaming uploads also map to share token for share page visibility
            if (!token.isEmpty()) {
                m_taskToShareToken[task.taskId] = token;
            }

            // Set progress callback on the parser
            QString taskId = task.taskId;
            state->parser->setProgressCallback([this, taskId](qint64 bytesWritten) {
                if (m_transferEngine) {
                    m_transferEngine->updateTaskProgress(taskId, bytesWritten);
                }
            });
        }

        m_streamingStates.insert(socket, state);

        LOG_INFO("Streaming upload started: path=%s, isReceive=%d, chunkSize=%lld, isLast=%d",
                 qPrintable(request.path), isReceive, (qint64)chunk.size(), isLast);
    }

    auto* state = m_streamingStates[socket];

    // Guard against double-call (e.g. from disconnected signal during sendResponse)
    if (state->finished) return;

    // Feed data to parser - files are saved to disk as they're parsed
    if (!chunk.isEmpty()) {
        state->parser->feed(chunk);
    }

    if (isLast) {
        state->finished = true;

        // Clear progress callback before finish() to avoid double-counting
        state->parser->setProgressCallback(nullptr);

        state->parser->finish();

        // Create share/transfer records
        const auto& savedFiles = state->parser->savedFiles();
        bool isFolder = state->parser->isFolderUpload();
        QString folderRoot = state->parser->folderRoot();
        qint64 totalSize = state->parser->totalSize();

        LOG_INFO("Streaming upload complete: files=%d, isFolder=%d, folderRoot=%s, totalSize=%lld",
                 savedFiles.size(), isFolder, qPrintable(folderRoot), totalSize);

        if (isFolder && !folderRoot.isEmpty()) {
            // Folder upload: create one share record for the folder root
            QString folderPath = state->parser->saveDir() + "/" + folderRoot;
            m_shareManager->createShare(folderPath, true, 0, 0, QString(), 1);
            LOG_INFO("Folder share created, recording transfer...");
            recordCompletedTransfer(1, folderRoot, totalSize, state->remoteAddress, folderPath);
            LOG_INFO("Transfer recorded");
        } else {
            // Single file upload: create a share record for each file
            for (const auto& sf : savedFiles) {
                m_shareManager->createShare(sf.savePath, false, 0, 0, QString(), 1);
                recordCompletedTransfer(1, QFileInfo(sf.savePath).fileName(), sf.fileSize, state->remoteAddress, sf.savePath);
            }
        }

        // Complete the transfer task
        if (m_transferEngine && !state->uploadTaskId.isEmpty()) {
            m_transferEngine->completeTask(state->uploadTaskId);
        }

        // Send response
        LOG_INFO("Preparing response...");
        HttpResponse response;
        if (state->isReceive) {
            response = HttpResponse::ok(
                QString("{\"success\":true,\"count\":%1}").arg(savedFiles.size()).toUtf8(),
                "application/json");
        } else {
            QString host = request.headers.value("Host");
            QString url;
            if (!host.isEmpty()) {
                url = QString("http://%1/upload/success").arg(host);
            } else {
                url = QString("http://%1:8080/upload/success").arg(m_shareManager->localIp());
            }
            response = HttpResponse::ok(
                QString("{\"url\":\"%1\"}").arg(url).toUtf8(),
                "application/json");
        }

        // Only send response if socket is still connected
        if (socket->state() == QAbstractSocket::ConnectedState) {
            LOG_INFO("Sending response...");
            m_httpServer->sendResponse(socket, response);
            LOG_INFO("Response sent");
        }

        // Cleanup
        delete state->parser;
        delete state;
        m_streamingStates.remove(socket);
        LOG_INFO("Streaming upload state cleaned up");
    }
}

void RequestHandler::handleUploadCheck(const HttpRequest& request, HttpResponse& response)
{
    LOG_INFO("[ReceivePage][upload/check] from=%s bodyLen=%d",
        request.remoteAddress.toUtf8().constData(), request.body.size());

    // Parse JSON body: {"files": [{"path":"folder/sub/file.txt","size":1024}, ...]}
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(request.body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        response = HttpResponse::badRequest("Invalid JSON");
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray filesArr = root.value("files").toArray();
    if (filesArr.isEmpty()) {
        response = HttpResponse::ok("{\"existing\":[]}", "application/json");
        return;
    }

    QString dir = uploadDir();
    if (dir.isEmpty()) {
        dir = QDir::tempPath() + "/netshare_uploads";
    }

    // Detect folder upload from file paths
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

    // Chunk size threshold: files >= 10MB use chunked upload
    static const qint64 CHUNK_THRESHOLD = 10 * 1024 * 1024;

    // Check existing files (fully uploaded) and partial files (partially uploaded)
    QJsonArray existingArr;
    QJsonArray partialArr;
    qint64 existingSize = 0;
    qint64 partialSize = 0;

    // Chunk manager for calculating chunk sizes
    ChunkManager chunkMgr;

    // Build file chunk state map
    QMap<QString, FileChunkState> fileChunkStates;

    for (const QJsonValue& val : filesArr) {
        QJsonObject obj = val.toObject();
        QString relativePath = obj.value("path").toString();
        qint64 expectedSize = obj.value("size").toVariant().toLongLong();

        QString fullPath = dir + "/" + relativePath;
        QFileInfo fi(fullPath);

        // Check if the file is already complete at the final path
        if (fi.exists() && fi.isFile() && fi.size() == expectedSize) {
            // Fully uploaded - skip this file
            QJsonObject ex;
            ex["path"] = relativePath;
            ex["size"] = expectedSize;
            existingArr.append(ex);
            existingSize += expectedSize;
            continue;
        }

        // Determine chunking strategy for this file
        bool useChunking = expectedSize >= CHUNK_THRESHOLD;
        int chunkSize = 0;
        int chunkCount = 0;
        QList<int> completedChunkIndices;

        if (useChunking) {
            // Calculate chunk size based on file size (PLAN.md tiered strategy)
            chunkSize = static_cast<int>(chunkMgr.calculateChunkSize(expectedSize, 3));
            QVariantList chunks = chunkMgr.splitFile(expectedSize, chunkSize);
            chunkCount = chunks.size();

            // Check for existing chunk files in .chunks directory
            // We need a session ID first to know the chunk dir, so we'll check
            // for ANY session's chunks for this file.
            // For now, we'll check after session creation (see below).
        }

        FileChunkState fcs;
        fcs.relativePath = relativePath;
        fcs.fileSize = expectedSize;
        fcs.chunkSize = chunkSize;
        fcs.totalChunks = chunkCount;
        fcs.useChunking = useChunking;

        if (useChunking) {
            // Initialize chunk states
            QVariantList chunks = chunkMgr.splitFile(expectedSize, chunkSize);
            for (const QVariant& v : chunks) {
                ChunkInfo ci = v.value<ChunkInfo>();
                ChunkUploadInfo cui;
                cui.chunkIndex = ci.index;
                cui.offset = ci.offset;
                cui.size = static_cast<qint64>(ci.size);
                cui.completed = false;
                fcs.chunks.append(cui);
            }
        }

        // For non-chunked partial files, check the final path
        if (!useChunking && fi.exists() && fi.isFile() && fi.size() > 0 && fi.size() < expectedSize) {
            QJsonObject pa;
            pa["path"] = relativePath;
            pa["size"] = fi.size();  // Current bytes on disk
            pa["useChunking"] = false;
            partialArr.append(pa);
            partialSize += fi.size();
        }

        fileChunkStates[relativePath] = fcs;
    }

    // Build response
    QJsonObject result;
    result["existing"] = existingArr;

    // Create an in-progress transfer task and session for both folder and single file uploads
    if (!m_transferEngine) {
        LOG_ERROR("handleUploadCheck: m_transferEngine is null, upload not available");
        result["error"] = "Upload service not available";
        response = HttpResponse::internalError("Upload service not available");
        return;
    }

    {
        QString sessionId = QUuid::createUuid().toString();
        QString taskId = QUuid::createUuid().toString();

        // Set up chunk temp directory
        QString chunkTempDir = dir + "/.chunks/" + sessionId;

        // Check for existing chunks from previous sessions for each chunked file
        // We scan the .chunks directory for any previous session that may have chunks for this file
        QString searchDir = dir + "/.chunks";
        QDir searchDirObj(searchDir);
        if (searchDirObj.exists()) {
            // Step 1: Clean up expired session directories (> 24 hours old)
            QStringList sessionDirs = searchDirObj.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString& prevSession : sessionDirs) {
                QString prevSessionPath = searchDir + "/" + prevSession;
                QFileInfo sessionInfo(prevSessionPath);
                if (sessionInfo.lastModified().secsTo(QDateTime::currentDateTime()) > 86400) {
                    // Session older than 24 hours - check if it's still active
                    bool isActive = false;
                    for (auto sit = m_uploadSessions.begin(); sit != m_uploadSessions.end(); ++sit) {
                        if (sit.value().chunkTempDir.startsWith(prevSessionPath)) {
                            isActive = true;
                            break;
                        }
                    }
                    if (!isActive) {
                        LOG_INFO("Cleaning up expired chunk session: %s", qPrintable(prevSessionPath));
                        QDir(prevSessionPath).removeRecursively();
                    }
                }
            }

            // Step 2: Cache directory scan results to avoid O(chunks × sessions) complexity
            // Re-scan after cleanup
            sessionDirs = searchDirObj.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

            // Build a map: relativePath -> list of (sessionDir, chunkIndex) for quick lookup
            // This scans each session dir once instead of once per chunk
            QMap<QString, QMap<int, QString>> chunkSourceMap; // relativePath -> (chunkIndex -> sourcePath)
            for (const QString& prevSession : sessionDirs) {
                QString prevSessionPath = searchDir + "/" + prevSession;
                // List subdirectories (one per file path) in this session
                QDir prevSessionDir(prevSessionPath);
                QStringList fileDirs = prevSessionDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                for (const QString& fileDir : fileDirs) {
                    QString relativeFilePath = fileDir;
                    QString prevChunkDir = prevSessionPath + "/" + relativeFilePath;
                    QDir chunkDirObj(prevChunkDir);
                    QStringList chunkFiles = chunkDirObj.entryList(QDir::Files);
                    for (const QString& chunkFile : chunkFiles) {
                        // Parse chunk index from filename: chunk_000001
                        if (chunkFile.startsWith("chunk_")) {
                            bool ok = false;
                            int idx = chunkFile.mid(6).toInt(&ok);
                            if (ok && !chunkSourceMap[relativeFilePath].contains(idx)) {
                                chunkSourceMap[relativeFilePath][idx] = prevChunkDir + "/" + chunkFile;
                            }
                        }
                    }
                }
            }

            // Step 3: Use cached scan results to check and copy chunks
            for (auto it = fileChunkStates.begin(); it != fileChunkStates.end(); ++it) {
                FileChunkState& fcs = it.value();
                if (!fcs.useChunking) continue;

                const QMap<int, QString>& availableChunks = chunkSourceMap[fcs.relativePath];

                // Check which chunks are available from previous sessions
                for (int i = 0; i < fcs.totalChunks; ++i) {
                    if (fcs.chunks[i].completed) continue;
                    if (!availableChunks.contains(i)) continue;

                    // Verify the chunk file size
                    QString srcPath = availableChunks[i];
                    QFileInfo fi(srcPath);
                    if (fi.size() == fcs.chunks[i].size ||
                        (i == fcs.totalChunks - 1 && fi.size() > 0 && fi.size() <= fcs.chunks[i].size)) {
                        fcs.chunks[i].completed = true;
                        fcs.completedChunks++;
                    }
                }

                // Copy completed chunks to new session directory
                if (fcs.completedChunks > 0) {
                    QString newChunkDir = chunkTempDir + "/" + fcs.relativePath;
                    QDir().mkpath(newChunkDir);

                    for (int i = 0; i < fcs.totalChunks; ++i) {
                        if (!fcs.chunks[i].completed) continue;
                        if (!availableChunks.contains(i)) continue;

                        QString srcPath = availableChunks[i];
                        QString dstPath = newChunkDir + QString("/chunk_%1").arg(i, 6, 10, QChar('0'));
                        QFile::copy(srcPath, dstPath);
                    }
                }

                // Add to partial response with chunk info
                QJsonObject pa;
                pa["path"] = fcs.relativePath;
                pa["size"] = fcs.fileSize;
                pa["chunkSize"] = fcs.chunkSize;
                pa["chunkCount"] = fcs.totalChunks;
                pa["useChunking"] = true;

                QJsonArray completedArr;
                qint64 completedBytes = 0;
                for (const auto& chunk : fcs.chunks) {
                    if (chunk.completed) {
                        completedArr.append(chunk.chunkIndex);
                        completedBytes += chunk.size;
                    }
                }
                pa["completedChunks"] = completedArr;
                pa["completedBytes"] = completedBytes;
                partialArr.append(pa);
                partialSize += completedBytes;
            }
        } else {
            // No .chunks directory - add partial info without chunk scanning
            for (auto it = fileChunkStates.begin(); it != fileChunkStates.end(); ++it) {
                FileChunkState& fcs = it.value();
                if (!fcs.useChunking) continue;

                QJsonObject pa;
                pa["path"] = fcs.relativePath;
                pa["size"] = fcs.fileSize;
                pa["chunkSize"] = fcs.chunkSize;
                pa["chunkCount"] = fcs.totalChunks;
                pa["useChunking"] = true;
                pa["completedChunks"] = QJsonArray();
                pa["completedBytes"] = 0;
                partialArr.append(pa);
            }
        }

        qint64 initialTransferredSize = existingSize + partialSize;

        // Determine the task fileName for matching against existing failed tasks
        QString taskFileName;
        if (isFolder && !folderRoot.isEmpty()) {
            taskFileName = folderRoot;
        } else {
            taskFileName = filesArr.size() == 1
                ? filesArr[0].toObject().value("path").toString()
                : QString("%1个文件").arg(filesArr.size());
        }

        // Remove any previous failed upload task with the same fileName
        m_transferEngine->removeFailedUploadTasksByName(taskFileName);

        TransferTask task;
        task.taskId = taskId;
        task.type = TransferTask::Upload;
        task.status = TransferTask::Uploading;
        if (isFolder && !folderRoot.isEmpty()) {
            task.fileName = folderRoot;
            task.filePath = dir + "/" + folderRoot;
        } else {
            task.fileName = taskFileName;
            task.filePath = dir;
        }
        task.fileSize = totalSize;
        task.transferredSize = initialTransferredSize;
        task.progress = totalSize > 0 ? static_cast<int>((initialTransferredSize * 100) / totalSize) : 0;
        task.startedAt = QDateTime::currentDateTime();

        m_transferEngine->addUploadingTask(task);

        // Store session info
        UploadSession session;
        session.sessionId = sessionId;
        session.taskId = taskId;
        session.folder = folderRoot;
        session.totalSize = totalSize;
        session.transferredSize = initialTransferredSize;
        session.fileCount = filesArr.size();
        session.remoteAddress = request.remoteAddress;
        session.createdAt = QDateTime::currentDateTime();
        session.fileChunkStates = fileChunkStates;
        session.chunkTempDir = chunkTempDir;
        m_uploadSessions[sessionId] = session;

        // Map taskId to sessionId for WebSocket progress routing
        m_taskToToken[taskId] = sessionId;

        // Also map taskId to share token if provided
        QString shareToken = root.value("token").toString();
        if (!shareToken.isEmpty()) {
            m_taskToShareToken[taskId] = shareToken;
        }

        result["sessionId"] = sessionId;
    }

    result["partial"] = partialArr;

    response = HttpResponse::ok(QJsonDocument(result).toJson(QJsonDocument::Compact), "application/json");
}

void RequestHandler::handleStreamingFileUpload(QTcpSocket* socket, const HttpRequest& request, const QByteArray& chunk, bool isLast)
{
    // First call: parse headers and set up streaming state
    if (!m_streamingFileStates.contains(socket)) {
        QString sessionId = request.headers.value("X-Upload-Session");
        QString filePath = QUrl::fromPercentEncoding(request.headers.value("X-File-Path").toUtf8());
        QString chunkIndexStr = request.headers.value("X-Chunk-Index");

        // Validate session
        if (sessionId.isEmpty() || !m_uploadSessions.contains(sessionId)) {
            HttpResponse response = HttpResponse::badRequest("Invalid or expired upload session");
            m_httpServer->sendResponse(socket, response);
            m_httpServer->cancelStreamingRequest(socket);
            return;
        }

        auto* state = new StreamingFileUploadState;
        state->sessionId = sessionId;
        state->filePath = filePath;
        state->isChunked = !chunkIndexStr.isEmpty();

        if (state->isChunked) {
            // ===== Chunked upload mode: stream chunk data directly to disk =====
            int chunkIndex = chunkIndexStr.toInt();
            state->chunkIndex = chunkIndex;

            auto& session = m_uploadSessions[sessionId];

            // Find the file chunk state
            if (!session.fileChunkStates.contains(filePath)) {
                delete state;
                HttpResponse response = HttpResponse::badRequest("File not found in upload session");
                m_httpServer->sendResponse(socket, response);
                m_httpServer->cancelStreamingRequest(socket);
                return;
            }

            auto& fcs = session.fileChunkStates[filePath];
            if (chunkIndex < 0 || chunkIndex >= fcs.totalChunks) {
                delete state;
                HttpResponse response = HttpResponse::badRequest("Invalid chunk index");
                m_httpServer->sendResponse(socket, response);
                m_httpServer->cancelStreamingRequest(socket);
                return;
            }

            // If chunk already completed, skip (idempotent) - respond immediately
            if (fcs.chunks[chunkIndex].completed) {
                delete state;
                QJsonObject result;
                result["success"] = true;
                result["chunkIndex"] = chunkIndex;
                result["completedChunks"] = fcs.completedChunks;
                result["skipped"] = true;
                HttpResponse response = HttpResponse::ok(
                    QJsonDocument(result).toJson(QJsonDocument::Compact), "application/json");
                m_httpServer->sendResponse(socket, response);
                m_httpServer->cancelStreamingRequest(socket);
                return;
            }

            // Open chunk file for streaming write
            QString chunkDir = session.chunkTempDir + "/" + filePath;
            QDir().mkpath(chunkDir);
            QString chunkPath = chunkDir + QString("/chunk_%1").arg(chunkIndex, 6, 10, QChar('0'));

            state->chunkFile = new QFile(chunkPath);
            if (!state->chunkFile->open(QIODevice::WriteOnly)) {
                LOG_ERROR("Failed to open chunk file for streaming write: %s", qPrintable(chunkPath));
                delete state->chunkFile;
                state->chunkFile = nullptr;
                delete state;
                HttpResponse response = HttpResponse::badRequest("Failed to write chunk");
                m_httpServer->sendResponse(socket, response);
                m_httpServer->cancelStreamingRequest(socket);
                return;
            }

            state->expectedSize = fcs.chunks[chunkIndex].size;
            state->bytesReceived = 0;

            LOG_INFO("[StreamingFileUpload] chunk start: session=%s file=%s chunk=%d expectedSize=%lld",
                     qPrintable(sessionId), qPrintable(filePath), chunkIndex, state->expectedSize);

        } else {
            // ===== Small file upload mode: use StreamingMultipartParser =====
            auto& session = m_uploadSessions[sessionId];
            QString dir = uploadDir();
            if (dir.isEmpty()) {
                dir = QDir::tempPath() + "/netshare_uploads";
            }

            QString contentType = request.headers.value("Content-Type");
            if (!contentType.startsWith("multipart/form-data")) {
                delete state;
                HttpResponse response = HttpResponse::badRequest("Expected multipart/form-data");
                m_httpServer->sendResponse(socket, response);
                m_httpServer->cancelStreamingRequest(socket);
                return;
            }

            state->parser = new StreamingMultipartParser(dir);
            if (!state->parser->init(contentType)) {
                delete state->parser;
                state->parser = nullptr;
                delete state;
                HttpResponse response = HttpResponse::badRequest("Invalid multipart boundary");
                m_httpServer->sendResponse(socket, response);
                m_httpServer->cancelStreamingRequest(socket);
                return;
            }

            // Handle resume for small files
            if (!filePath.isEmpty() && session.fileChunkStates.contains(filePath)) {
                auto& fcs = session.fileChunkStates[filePath];
                QFileInfo fi(dir + "/" + filePath);
                if (fi.exists() && fi.isFile() && fi.size() > 0 && fi.size() < fcs.fileSize) {
                    state->parser->setResumeOffset(fi.size());
                    state->parser->setResumeFilePath(fi.absoluteFilePath());
                }
            }

            LOG_INFO("[StreamingFileUpload] small file start: session=%s file=%s",
                     qPrintable(sessionId), qPrintable(filePath));
        }

        m_streamingFileStates.insert(socket, state);
    }

    auto* state = m_streamingFileStates[socket];
    if (state->finished) return;

    // Feed data to appropriate handler
    if (state->isChunked) {
        // Write chunk data directly to file
        if (!chunk.isEmpty() && state->chunkFile) {
            qint64 written = state->chunkFile->write(chunk);
            if (written < 0) {
                LOG_ERROR("Failed to write chunk data: session=%s file=%s chunk=%d",
                          qPrintable(state->sessionId), qPrintable(state->filePath), state->chunkIndex);
                state->finished = true;
                m_streamingFileStates.remove(socket);
                state->chunkFile->close();
                delete state->chunkFile;
                state->chunkFile = nullptr;
                HttpResponse response = HttpResponse::internalError("Disk write error");
                m_httpServer->sendResponse(socket, response);
                delete state;
                return;
            }
            state->bytesReceived += written;
        }
    } else {
        // Feed to streaming multipart parser
        if (!chunk.isEmpty() && state->parser) {
            state->parser->feed(chunk);
        }
    }

    // Handle completion
    if (isLast) {
        state->finished = true;

        // CRITICAL: Remove from map BEFORE sendResponse to prevent double-free.
        // sendResponse() calls disconnectFromHost() which may synchronously
        // trigger the disconnected signal, whose handler also tries to delete
        // the state from m_streamingFileStates.
        m_streamingFileStates.remove(socket);

        if (state->isChunked) {
            // ===== Finish chunked upload =====
            if (state->chunkFile) {
                state->chunkFile->close();
                delete state->chunkFile;
                state->chunkFile = nullptr;
            }

            auto& session = m_uploadSessions[state->sessionId];
            auto& fcs = session.fileChunkStates[state->filePath];
            int chunkIndex = state->chunkIndex;

            // Verify chunk size (only non-last chunks must match exactly)
            if (state->bytesReceived != state->expectedSize && chunkIndex < fcs.totalChunks - 1) {
                QJsonObject result;
                result["success"] = false;
                result["error"] = QString("Chunk size mismatch: expected %1, got %2")
                    .arg(state->expectedSize).arg(state->bytesReceived);
                HttpResponse response = HttpResponse::badRequest(
                    QJsonDocument(result).toJson(QJsonDocument::Compact));
                m_httpServer->sendResponse(socket, response);
                delete state;
                return;
            }

            // Verify the written chunk file
            ChunkManager chunkMgr;
            QString chunkDir = session.chunkTempDir + "/" + state->filePath;
            if (!chunkMgr.verifyChunk(chunkDir, chunkIndex, state->bytesReceived)) {
                HttpResponse response = HttpResponse::badRequest("Chunk verification failed");
                m_httpServer->sendResponse(socket, response);
                delete state;
                return;
            }

            // Update chunk state
            fcs.chunks[chunkIndex].completed = true;
            fcs.completedChunks++;
            session.transferredSize += state->bytesReceived;

            // Update transfer engine progress
            if (m_transferEngine) {
                m_transferEngine->updateTaskProgress(session.taskId, session.transferredSize);
            }

            LOG_INFO("Chunk upload complete: session=%s file=%s chunk=%d/%d size=%lld",
                     qPrintable(state->sessionId), qPrintable(state->filePath),
                     chunkIndex, fcs.totalChunks, state->bytesReceived);

            QJsonObject result;
            result["success"] = true;
            result["chunkIndex"] = chunkIndex;
            result["completedChunks"] = fcs.completedChunks;
            result["totalChunks"] = fcs.totalChunks;
            HttpResponse response = HttpResponse::ok(
                QJsonDocument(result).toJson(QJsonDocument::Compact), "application/json");
            m_httpServer->sendResponse(socket, response);

        } else {
            // ===== Finish small file upload =====
            auto& session = m_uploadSessions[state->sessionId];

            state->parser->finish();
            const auto& savedFiles = state->parser->savedFiles();

            if (savedFiles.isEmpty()) {
                HttpResponse response = HttpResponse::badRequest("No file found in upload");
                m_httpServer->sendResponse(socket, response);
                delete state->parser;
                state->parser = nullptr;
                delete state;
                return;
            }

            const auto& sf = savedFiles.first();
            QString saveFileName = state->filePath.isEmpty() ? sf.fileName : state->filePath;

            // Update session
            session.transferredSize += sf.fileSize;

            SavedFileInfo info;
            info.fileName = saveFileName;
            info.savePath = sf.savePath;
            info.fileSize = sf.fileSize;
            session.savedFiles.append(info);

            // Mark the file as complete in chunk states (if it was tracked)
            if (session.fileChunkStates.contains(state->filePath)) {
                auto& fcs = session.fileChunkStates[state->filePath];
                fcs.completedChunks = fcs.totalChunks;
                for (auto& chunkInfo : fcs.chunks) {
                    chunkInfo.completed = true;
                }
            }

            // Update transfer engine progress
            if (m_transferEngine) {
                m_transferEngine->updateTaskProgress(session.taskId, session.transferredSize);
            }

            LOG_INFO("Small file upload complete: session=%s file=%s size=%lld",
                     qPrintable(state->sessionId), qPrintable(saveFileName), sf.fileSize);

            QJsonObject result;
            result["success"] = true;
            result["path"] = saveFileName;
            HttpResponse response = HttpResponse::ok(
                QJsonDocument(result).toJson(QJsonDocument::Compact), "application/json");
            m_httpServer->sendResponse(socket, response);

            delete state->parser;
            state->parser = nullptr;
        }

        delete state;
    }
}

void RequestHandler::handleUploadSingleFile(const HttpRequest& request, HttpResponse& response)
{
    QString sessionId = request.headers.value("X-Upload-Session");
    QString filePath = QUrl::fromPercentEncoding(request.headers.value("X-File-Path").toUtf8());
    QString chunkIndexStr = request.headers.value("X-Chunk-Index");

    LOG_INFO("[ReceivePage][upload/file] from=%s session=%s path=%s chunk=%s bodyLen=%d",
        request.remoteAddress.toUtf8().constData(),
        sessionId.left(8).toUtf8().constData(),
        filePath.toUtf8().constData(),
        chunkIndexStr.toUtf8().constData(),
        request.body.size());

    // Validate session
    if (sessionId.isEmpty() || !m_uploadSessions.contains(sessionId)) {
        response = HttpResponse::badRequest("Invalid or expired upload session");
        return;
    }

    if (!chunkIndexStr.isEmpty()) {
        // ===== Chunked upload mode =====
        int chunkIndex = chunkIndexStr.toInt();
        auto& session = m_uploadSessions[sessionId];

        // Find the file chunk state
        if (!session.fileChunkStates.contains(filePath)) {
            response = HttpResponse::badRequest("File not found in upload session");
            return;
        }

        auto& fcs = session.fileChunkStates[filePath];
        if (chunkIndex < 0 || chunkIndex >= fcs.totalChunks) {
            response = HttpResponse::badRequest("Invalid chunk index");
            return;
        }

        // If chunk already completed, skip (idempotent)
        if (fcs.chunks[chunkIndex].completed) {
            QJsonObject result;
            result["success"] = true;
            result["chunkIndex"] = chunkIndex;
            result["completedChunks"] = fcs.completedChunks;
            result["skipped"] = true;
            response = HttpResponse::ok(QJsonDocument(result).toJson(QJsonDocument::Compact), "application/json");
            return;
        }

        // Write chunk data to temporary directory
        QString chunkDir = session.chunkTempDir + "/" + filePath;
        QDir().mkpath(chunkDir);

        // The request body is the raw chunk data
        ChunkManager chunkMgr;
        qint64 expectedChunkSize = fcs.chunks[chunkIndex].size;

        // Verify chunk size matches expected
        if (request.body.size() != expectedChunkSize && chunkIndex < fcs.totalChunks - 1) {
            // Only the last chunk can be smaller than chunkSize
            response = HttpResponse::badRequest(
                QString("Chunk size mismatch: expected %1, got %2")
                    .arg(expectedChunkSize).arg(request.body.size()).toUtf8());
            return;
        }

        if (!chunkMgr.writeChunk(chunkDir, chunkIndex, request.body)) {
            response = HttpResponse::badRequest("Failed to write chunk");
            return;
        }

        // Verify the written chunk
        if (!chunkMgr.verifyChunk(chunkDir, chunkIndex, request.body.size())) {
            response = HttpResponse::badRequest("Chunk verification failed");
            return;
        }

        // Update chunk state
        fcs.chunks[chunkIndex].completed = true;
        fcs.completedChunks++;
        session.transferredSize += request.body.size();

        // Update transfer engine progress
        if (m_transferEngine) {
            m_transferEngine->updateTaskProgress(session.taskId, session.transferredSize);
        }

        LOG_INFO("Chunk upload: session=%s, file=%s, chunk=%d/%d, size=%lld",
                 qPrintable(sessionId), qPrintable(filePath), chunkIndex, fcs.totalChunks,
                 static_cast<qint64>(request.body.size()));

        QJsonObject result;
        result["success"] = true;
        result["chunkIndex"] = chunkIndex;
        result["completedChunks"] = fcs.completedChunks;
        result["totalChunks"] = fcs.totalChunks;
        response = HttpResponse::ok(QJsonDocument(result).toJson(QJsonDocument::Compact), "application/json");

    } else {
        // ===== Small file upload mode (no chunking) =====
        auto& session = m_uploadSessions[sessionId];

        // Parse multipart/form-data to extract the file
        QString contentType = request.headers.value("Content-Type");
        QList<UploadedFile> files = parseMultipartFormData(request.body, contentType);

        if (files.isEmpty()) {
            response = HttpResponse::badRequest("No file found in upload");
            return;
        }

        const UploadedFile& uf = files.first();
        QString dir = uploadDir();
        if (dir.isEmpty()) {
            dir = QDir::tempPath() + "/netshare_uploads";
        }

        // Use X-File-Path if provided, otherwise use the filename from multipart
        QString saveFileName = filePath.isEmpty() ? uf.fileName : filePath;
        QString fullPath = dir + "/" + saveFileName;

        // Ensure parent directory exists
        QDir().mkpath(QFileInfo(fullPath).absolutePath());

        // For small file resume: if file already exists partially, append instead of truncate
        QFileInfo fi(fullPath);
        QIODevice::OpenMode openMode = QIODevice::WriteOnly;
        qint64 existingSize = 0;
        if (fi.exists() && fi.size() > 0 && fi.size() < session.fileChunkStates.value(filePath).fileSize) {
            // Partial file exists - verify size matches what client expects to resume from
            existingSize = fi.size();
            openMode = QIODevice::WriteOnly | QIODevice::Append;
            LOG_INFO("Small file resume: appending to %s (existing=%lld)", qPrintable(saveFileName), existingSize);
        }

        // Write file to disk
        QFile file(fullPath);
        if (!file.open(openMode)) {
            response = HttpResponse::badRequest("Failed to save file");
            return;
        }
        file.write(uf.data);
        file.close();

        // Update session
        qint64 addedSize = uf.data.size();
        if (filePath.isEmpty()) {
            // If no X-File-Path was given, use the multipart filename
            filePath = uf.fileName;
        }

        session.transferredSize += addedSize;

        SavedFileInfo info;
        info.fileName = saveFileName;
        info.savePath = fullPath;
        info.fileSize = addedSize;
        session.savedFiles.append(info);

        // Mark the file as complete in chunk states (if it was tracked)
        if (session.fileChunkStates.contains(filePath)) {
            auto& fcs = session.fileChunkStates[filePath];
            fcs.completedChunks = fcs.totalChunks; // All "chunks" (1) complete
            for (auto& chunk : fcs.chunks) {
                chunk.completed = true;
            }
        }

        // Update transfer engine progress
        if (m_transferEngine) {
            m_transferEngine->updateTaskProgress(session.taskId, session.transferredSize);
        }

        LOG_INFO("Small file upload: session=%s, file=%s, size=%lld",
                 qPrintable(sessionId), qPrintable(saveFileName), addedSize);

        QJsonObject result;
        result["success"] = true;
        result["path"] = saveFileName;
        response = HttpResponse::ok(QJsonDocument(result).toJson(QJsonDocument::Compact), "application/json");
    }
}

void RequestHandler::handleUploadFinalize(const HttpRequest& request, HttpResponse& response)
{
    LOG_INFO("[ReceivePage][upload/finalize] from=%s bodyLen=%d",
        request.remoteAddress.toUtf8().constData(), request.body.size());
    // Parse JSON body
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(request.body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        response = HttpResponse::badRequest("Invalid JSON");
        return;
    }

    QJsonObject root = doc.object();
    QString folder = root.value("folder").toString();
    bool isFolder = root.value("isFolder").toBool();
    qint64 totalSize = root.value("totalSize").toVariant().toLongLong();
    int fileCount = root.value("fileCount").toInt();
    QString token = root.value("token").toString();
    bool isReceive = root.value("isReceive").toBool();
    QString sessionId = root.value("sessionId").toString();

    // For non-receive mode, validate the token
    if (!isReceive) {
        if (token.isEmpty()) {
            response = HttpResponse::badRequest("Missing token");
            return;
        }
        ShareInfo info = m_shareManager->getShareInfo(token);
        if (!info.isValid() || info.isExpired()) {
            response = HttpResponse::notFound("分享不存在或已过期");
            return;
        }
    }

    QString dir = uploadDir();
    if (dir.isEmpty()) {
        dir = QDir::tempPath() + "/netshare_uploads";
    }

    // Merge chunked files before creating share records
    if (!sessionId.isEmpty() && m_uploadSessions.contains(sessionId)) {
        auto& session = m_uploadSessions[sessionId];
        ChunkManager chunkMgr;

        // First pass: check all chunked files are complete
        QStringList incompleteFiles;
        for (auto it = session.fileChunkStates.begin(); it != session.fileChunkStates.end(); ++it) {
            const FileChunkState& fcs = it.value();
            if (fcs.useChunking && fcs.completedChunks < fcs.totalChunks) {
                incompleteFiles << QString("%1 (%2/%3)").arg(fcs.relativePath)
                    .arg(fcs.completedChunks).arg(fcs.totalChunks);
            }
        }

        if (!incompleteFiles.isEmpty()) {
            LOG_WARN("Finalize: refusing to finalize with incomplete chunks: %s",
                     qPrintable(incompleteFiles.join(", ")));
            QJsonObject result;
            result["success"] = false;
            result["error"] = "Upload incomplete: some files have unfinished chunks";
            result["incompleteFiles"] = QJsonArray::fromStringList(incompleteFiles);
            response = HttpResponse::badRequest(
                QJsonDocument(result).toJson(QJsonDocument::Compact));
            return;
        }

        // Second pass: merge all chunked files
        for (auto it = session.fileChunkStates.begin(); it != session.fileChunkStates.end(); ++it) {
            const FileChunkState& fcs = it.value();

            // Skip non-chunked files (already saved via small file mode)
            if (!fcs.useChunking) continue;

            // Merge chunks into final file
            QString chunkDir = session.chunkTempDir + "/" + fcs.relativePath;
            QString outputPath = dir + "/" + fcs.relativePath;

            // Ensure parent directory exists
            QDir().mkpath(QFileInfo(outputPath).absolutePath());

            if (!chunkMgr.mergeChunks(chunkDir, outputPath, fcs.totalChunks)) {
                LOG_ERROR("Finalize: merge failed for %s", qPrintable(fcs.relativePath));
                continue;
            }

            // Verify merged file
            if (!chunkMgr.verifyMergedFile(outputPath, fcs.fileSize)) {
                LOG_ERROR("Finalize: merged file verification failed for %s", qPrintable(fcs.relativePath));
                continue;
            }

            // Clean up chunk files
            chunkMgr.cleanupChunks(chunkDir);

            // Add to saved files for share creation
            SavedFileInfo info;
            info.fileName = QFileInfo(fcs.relativePath).fileName();
            info.savePath = outputPath;
            info.fileSize = fcs.fileSize;
            session.savedFiles.append(info);

            LOG_INFO("Finalize: merged %d chunks for %s -> %s",
                     fcs.totalChunks, qPrintable(fcs.relativePath), qPrintable(outputPath));
        }

        // Clean up chunk temp directory if empty
        if (!session.chunkTempDir.isEmpty()) {
            QDir chunkBaseDir(session.chunkTempDir);
            chunkBaseDir.removeRecursively();
        }
    }

    if (isFolder && !folder.isEmpty()) {
        // Folder upload: create one share record for the folder root
        QString folderPath = dir + "/" + folder;
        m_shareManager->createShare(folderPath, true, 0, 0, QString(), 1);

        // Complete the existing upload task via session, or find and complete the uploading task
        if (!sessionId.isEmpty() && m_uploadSessions.contains(sessionId) && m_transferEngine) {
            auto& session = m_uploadSessions[sessionId];
            QString taskId = session.taskId;
            m_transferEngine->completeTask(taskId);

            // Persist to transfer log database
            if (m_transferLogService) {
                m_transferLogService->logTransfer(1, folder, folderPath, totalSize,
                                                  request.remoteAddress, 2); // UploadLog, Completed
            }

            m_uploadSessions.remove(sessionId);
            m_taskToToken.remove(taskId);
            m_taskToShareToken.remove(taskId);
        } else {
            // Session lost: try to find and complete the existing uploading task by name
            if (m_transferEngine) {
                m_transferEngine->completeTaskByName(folder, TransferTask::Upload);
            }

            // Create log entry only
            if (m_transferLogService) {
                m_transferLogService->logTransfer(1, folder, folderPath, totalSize,
                                                  request.remoteAddress, 2); // UploadLog, Completed
            }
        }

        LOG_INFO("Finalize: folder share created for %s (%d files, %lld bytes)",
                 qPrintable(folder), fileCount, totalSize);
    } else {
        // Single file upload: create share records for each saved file
        if (!sessionId.isEmpty() && m_uploadSessions.contains(sessionId)) {
            auto& session = m_uploadSessions[sessionId];

            for (const auto& info : session.savedFiles) {
                m_shareManager->createShare(info.savePath, false, 0, 0, QString(), 1);
                if (m_transferLogService) {
                    m_transferLogService->logTransfer(1, info.fileName, info.savePath,
                                                      info.fileSize, request.remoteAddress, 2);
                }
            }

            if (m_transferEngine) {
                m_transferEngine->completeTask(session.taskId);
            }

            m_uploadSessions.remove(sessionId);
            m_taskToToken.remove(session.taskId);
            m_taskToShareToken.remove(session.taskId);
        }

        LOG_INFO("Finalize: single file upload completed (%d files, %lld bytes)",
                 fileCount, totalSize);
    }

    // Send response
    if (isReceive) {
        response = HttpResponse::ok(
            QString("{\"success\":true,\"count\":%1}").arg(fileCount).toUtf8(),
            "application/json");
    } else {
        QString host = request.headers.value("Host");
        QString url;
        if (!host.isEmpty()) {
            url = QString("http://%1/upload/success").arg(host);
        } else {
            url = QString("http://%1:8080/upload/success").arg(m_shareManager->localIp());
        }
        response = HttpResponse::ok(
            QString("{\"url\":\"%1\"}").arg(url).toUtf8(),
            "application/json");
    }
}

void RequestHandler::handleUploadAbort(const HttpRequest& request, HttpResponse& response)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(request.body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        response = HttpResponse::badRequest("Invalid JSON");
        return;
    }

    QString sessionId = doc.object().value("sessionId").toString();
    if (sessionId.isEmpty() || !m_uploadSessions.contains(sessionId)) {
        response = HttpResponse::ok("{\"success\":true}", "application/json");
        return;
    }

    auto& session = m_uploadSessions[sessionId];
    QString taskId = session.taskId;

    // Fail the transfer task
    if (m_transferEngine && !taskId.isEmpty()) {
        m_transferEngine->failTask(taskId, "Upload aborted");
    }

    // Clean up chunk temp directory
    if (!session.chunkTempDir.isEmpty()) {
        QDir chunkDir(session.chunkTempDir);
        chunkDir.removeRecursively();
    }

    // Remove token mappings
    m_taskToToken.remove(taskId);
    m_taskToShareToken.remove(taskId);

    // Remove the session
    m_uploadSessions.remove(sessionId);

    LOG_INFO("Upload aborted: session=%s", qPrintable(sessionId));

    response = HttpResponse::ok("{\"success\":true}", "application/json");
}

void RequestHandler::cleanupExpiredSessions()
{
    QDateTime now = QDateTime::currentDateTime();
    QStringList expiredSessions;

    for (auto it = m_uploadSessions.begin(); it != m_uploadSessions.end(); ++it) {
        // Sessions older than 2 hours are considered expired
        if (it->createdAt.secsTo(now) > 7200) {
            expiredSessions.append(it.key());
        }
    }

    for (const QString& sessionId : expiredSessions) {
        auto& session = m_uploadSessions[sessionId];
        QString taskId = session.taskId;

        if (m_transferEngine && !taskId.isEmpty()) {
            m_transferEngine->failTask(taskId, "Session expired");
        }

        LOG_INFO("Expired upload session cleaned up: session=%s", qPrintable(sessionId));

        // Clean up chunk temp directory
        if (!session.chunkTempDir.isEmpty()) {
            QDir chunkDir(session.chunkTempDir);
            chunkDir.removeRecursively();
        }

        m_taskToToken.remove(taskId);
        m_taskToShareToken.remove(taskId);
        m_uploadSessions.remove(sessionId);
    }
}

QList<UploadedFile> RequestHandler::parseMultipartFormData(const QByteArray& body, const QString& contentType) const
{
    QList<UploadedFile> result;

    // Extract boundary from Content-Type: multipart/form-data; boundary=----XXX
    QString boundary;
    int boundaryPos = contentType.indexOf("boundary=");
    if (boundaryPos < 0) return result;

    boundary = contentType.mid(boundaryPos + 9);
    // Remove any quotes around boundary
    if (boundary.startsWith('"') && boundary.endsWith('"')) {
        boundary = boundary.mid(1, boundary.size() - 2);
    }
    // Trim trailing semicolons or spaces
    boundary = boundary.trimmed();

    QByteArray boundaryBytes = "--" + boundary.toUtf8();
    QByteArray endBoundary = boundaryBytes + "--";

    int pos = 0;
    while (pos < body.size()) {
        // Find next boundary
        int partStart = body.indexOf(boundaryBytes, pos);
        if (partStart < 0) break;

        // Skip boundary line
        int afterBoundary = partStart + boundaryBytes.size();
        // Skip \r\n after boundary
        while (afterBoundary < body.size() && (body[afterBoundary] == '\r' || body[afterBoundary] == '\n'))
            afterBoundary++;

        // Find end of part headers
        int headerEnd = body.indexOf("\r\n\r\n", afterBoundary);
        if (headerEnd < 0) break;

        // Parse Content-Disposition header to get filename
        QByteArray partHeaders = body.mid(afterBoundary, headerEnd - afterBoundary);
        QString headersStr = QString::fromUtf8(partHeaders);

        QString fileName;
        QRegularExpression nameRe("filename=\"([^\"]+)\"");
        QRegularExpressionMatch match = nameRe.match(headersStr);
        if (match.hasMatch()) {
            fileName = match.captured(1);
        }

        if (fileName.isEmpty()) {
            // Not a file part, skip
            pos = headerEnd + 4;
            continue;
        }

        int dataStart = headerEnd + 4;

        // Find next boundary (marks end of this part's data)
        int nextBoundary = body.indexOf(boundaryBytes, dataStart);
        if (nextBoundary < 0) break;

        // Data ends before the \r\n preceding the next boundary
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

QByteArray RequestHandler::generateUploadPage(const QString& token) const
{
    return QString(
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>NetShare - 上传文件</title></head>"
        "<body style='font-family:system-ui,sans-serif;background:#0f0f23;color:#eee;"
        "margin:0;padding:0;min-height:100vh;display:flex;align-items:center;justify-content:center'>"
        "<div style='max-width:540px;width:100%;padding:20px'>"
        "<div style='background:#16213e;border-radius:16px;padding:40px;text-align:center'>"
        "<div style='font-size:48px;margin-bottom:16px'>📤</div>"
        "<h1 style='font-size:24px;margin:0 0 8px'>上传文件</h1>"
        "<p style='color:#888;margin:0 0 24px'>选择文件或文件夹上传到局域网分享</p>"
        "<input type='file' multiple style='display:none' id='fileInput'>"
        "<input type='file' multiple webkitdirectory directory mozdirectory style='display:none' id='folderInput'>"
        "<div id='dropZone' style='border:2px dashed #2a2a3e;border-radius:12px;padding:40px;"
        "cursor:pointer;transition:border-color 0.2s'>"
        "<div style='font-size:48px;margin-bottom:12px'>📁</div>"
        "<p style='color:#888;font-size:14px'>将文件或文件夹拖拽到此处</p>"
        "</div>"
        "<div style='display:flex;gap:8px;margin-top:12px'>"
        "<div id='selectFileBtn' "
        "style='flex:1;padding:10px;background:#1a1a3e;color:#4caf50;border:1px solid #2a2a3e;"
        "border-radius:8px;font-size:14px;cursor:pointer;text-align:center'>📄 选择文件</div>"
        "<div id='selectFolderBtn' "
        "style='flex:1;padding:10px;background:#1a1a3e;color:#2196f3;border:1px solid #2a2a3e;"
        "border-radius:8px;font-size:14px;cursor:pointer;text-align:center'>📁 选择文件夹</div>"
        "</div>"
        "<div id='fileList' style='margin:16px 0;max-height:240px;overflow-y:auto'></div>"
        "<div id='progress' style='display:none;margin:16px 0'>"
        "<div style='height:6px;background:#2a2a3e;border-radius:3px;overflow:hidden'>"
        "<div id='progressBar' style='height:100%;background:#4caf50;width:0%;transition:width 0.3s'></div></div>"
        "<div style='display:flex;justify-content:space-between;margin-top:8px'>"
        "<span id='progressText' style='color:#888;font-size:12px'>0%</span>"
        "<span id='statusText' style='color:#888;font-size:12px'>上传中...</span></div></div>"
        "<div id='resumeHint' style='display:none;background:rgba(33,150,243,0.1);"
        "border:1px solid rgba(33,150,243,0.3);border-radius:8px;padding:10px 14px;"
        "margin-bottom:12px;font-size:13px;color:#64b5f6'></div>"
        "<button type='button' id='uploadBtn' disabled style='width:100%;padding:14px;"
        "background:#4caf50;color:#fff;border:none;border-radius:8px;font-size:16px;"
        "font-weight:600;cursor:pointer'>开始上传</button>"
        "<div id='result' style='display:none;margin-top:20px'>"
        "<div style='font-size:48px;margin-bottom:12px'>✅</div>"
        "<p style='color:#888;font-size:14px;margin-bottom:16px'>上传成功！</p>"
        "<a id='resultLink' href='#' style='color:#4caf50;word-break:break-all;font-size:13px'></a></div>"
        "</div></div>"
        "<script>"
        "function _dbg(msg){console.log(msg);try{fetch('/api/debug',{method:'POST',body:msg})}catch(e){}}"
        "const token='%1';let files=[];"
        "const fi=document.getElementById('fileInput'),foi=document.getElementById('folderInput'),"
        "dz=document.getElementById('dropZone'),fl=document.getElementById('fileList'),"
        "ub=document.getElementById('uploadBtn');"
        "function selectFiles(){fi.click()}"
        "function selectFolder(){foi.click()}"
        "function addFiles(fileList){Array.from(fileList).forEach(f=>"
        "files.push({file:f,path:f.webkitRelativePath||f.name}));renderList();ub.disabled=!files.length}"
        "dz.onclick=function(){fi.click()};"
        "document.getElementById('selectFileBtn').onclick=function(){fi.click()};"
        "document.getElementById('selectFolderBtn').onclick=function(){foi.click()};"
        "dz.ondragover=e=>{e.preventDefault();dz.style.borderColor='#4caf50'};"
        "dz.ondragleave=()=>dz.style.borderColor='#2a2a3e';"
        "dz.ondrop=async e=>{e.preventDefault();dz.style.borderColor='#2a2a3e';"
        "var items=e.dataTransfer.items;if(items&&items.length>0){"
        "var entries=[];for(var i=0;i<items.length;i++){"
        "var en=items[i].webkitGetAsEntry?items[i].webkitGetAsEntry():null;if(en)entries.push(en)}"
        "if(entries.length>0){var r=await traverseEntries(entries,'');"
        "r.forEach(x=>{files.push(x)});renderList();ub.disabled=!files.length;return}}"
        "addFiles(e.dataTransfer.files)};"
        "function formatSize(b){if(b<1024)return b+' B';if(b<1048576)return(b/1024).toFixed(1)+' KB';"
        "if(b<1073741824)return(b/1048576).toFixed(1)+' MB';return(b/1073741824).toFixed(1)+' GB'}"
        "function connectWS(){if(ws&&(ws.readyState===1||ws.readyState===0))return;"
        "var wsPort=location.port?parseInt(location.port)+1:8081;"
        "try{ws=new WebSocket('ws://'+location.hostname+':'+wsPort)}catch(e){return}"
        "ws.onopen=function(){wsConnected=true;if(uploadSessionId)"
        "ws.send(JSON.stringify({type:'subscribe',data:{token:uploadSessionId}}))};"
        "ws.onmessage=function(e){try{var m=JSON.parse(e.data);"
        "if(m.type==='transfer_update'&&m.data&&m.data.progress!==undefined&&!xhrProgressActive){"
        "var pb=document.getElementById('progressBar');if(pb)pb.style.width=m.data.progress+'%';"
        "var pp=document.getElementById('progressPct');if(pp)pp.textContent=m.data.progress+'%';"
        "if(m.data.speed){var st=document.getElementById('statusText');"
        "if(st)st.textContent=formatSize(m.data.speed)+'/s'}}}catch(ex){}};"
        "ws.onclose=function(){wsConnected=false;"
        "var hasActive=Object.values(uploadStatus).some(function(s){return s==='uploading'||s==='pending'});"
        "if(hasActive)setTimeout(connectWS,5000)};"
        "ws.onerror=function(){}}"
        "function traverseEntries(entries,basePath){return new Promise(function(resolve){"
        "var results=[],pending=entries.length;if(pending===0){resolve(results);return}"
        "for(var i=0;i<entries.length;i++){var entry=entries[i];"
        "if(entry.isFile){entry.file(function(f){"
        "results.push({file:f,path:basePath?basePath+'/'+f.name:f.name});"
        "if(--pending===0)resolve(results)},function(){if(--pending===0)resolve(results)})"
        "}else if(entry.isDirectory){readAllDir(entry,basePath).then(function(sub){"
        "results=results.concat(sub);if(--pending===0)resolve(results)})"
        "}else{if(--pending===0)resolve(results)}}})}"
        "function readAllDir(dirEntry,parentPath){"
        "var dirPath=parentPath?parentPath+'/'+dirEntry.name:dirEntry.name;"
        "var reader=dirEntry.createReader();"
        "return new Promise(function(resolve){var all=[];"
        "function readBatch(){reader.readEntries(function(batch){"
        "if(batch.length===0){if(all.length===0){resolve([]);return}"
        "traverseEntries(all,dirPath).then(resolve)}else{all=all.concat(batch);readBatch()}}"
        ",function(){resolve([])})}readBatch()})}"
        "function renderList(){fl.innerHTML='';var total=0;"
        "files.forEach(function(item,i){total+=item.file.size;"
        "fl.innerHTML+='<div style=\"padding:8px 12px;background:#0f0f23;border-radius:6px;margin:4px 0;"
        "display:flex;justify-content:space-between;align-items:center\">"
        "<span style=\"overflow:hidden;text-overflow:ellipsis;white-space:nowrap;max-width:70%\">📄 '+item.path+'<\\/span>"
        "<span style=\"color:#888;font-size:11px;flex-shrink:0;margin:0 8px\">'+formatSize(item.file.size)+'<\\/span>"
        "<button onclick=\"removeFile('+i+')\" "
        "style=\"background:#e94560;color:#fff;border:none;border-radius:50%;width:24px;height:24px;"
        "cursor:pointer;flex-shrink:0\">×<\\/button><\\/div>'});"
        "if(files.length>0){fl.innerHTML+='<div style=\"text-align:right;padding:4px 12px;color:#888;font-size:12px\">"
        "共 '+files.length+' 个文件，总大小 '+formatSize(total)+'<\\/div>'}}"
        "function removeFile(i){files.splice(i,1);renderList();ub.disabled=!files.length}"
        "let uploadStatus={};let uploadedSize=0;let totalUploadSize=0;let failedFiles=[];"
        "let uploadSessionId='';let chunkInfo={};let xhrProgressActive=false;"
        "let ws=null;let wsConnected=false;const MAX_CONCURRENT=3;"
        "function connectWS(){if(ws&&(ws.readyState===1||ws.readyState===0))return;"
        "var wsPort=location.port?parseInt(location.port)+1:8081;"
        "try{ws=new WebSocket('ws://'+location.hostname+':'+wsPort)}catch(e){return}"
        "ws.onopen=function(){wsConnected=true;if(uploadSessionId)"
        "ws.send(JSON.stringify({type:'subscribe',data:{token:uploadSessionId}}))};"
        "ws.onmessage=function(e){try{var m=JSON.parse(e.data);"
        "if(m.type==='transfer_update'&&m.data&&m.data.progress!==undefined&&!xhrProgressActive){"
        "var pb=document.getElementById('progressBar');if(pb)pb.style.width=m.data.progress+'%';"
        "var pp=document.getElementById('progressText');if(pp)pp.textContent=m.data.progress+'%';"
        "if(m.data.speed){var st=document.getElementById('statusText');"
        "if(st)st.textContent=formatSize(m.data.speed)+'/s'}}}catch(ex){}};"
        "ws.onclose=function(){wsConnected=false;"
        "var hasActive=Object.values(uploadStatus).some(function(s){return s==='uploading'||s==='pending'});"
        "if(hasActive)setTimeout(connectWS,5000)};ws.onerror=function(){}}"
        "ub.onclick=async function(){"
        "if(!files.length)return;"
        "totalUploadSize=0;files.forEach(x=>totalUploadSize+=x.file.size);"
        "uploadedSize=0;failedFiles=[];uploadStatus={};uploadSessionId='';chunkInfo={};xhrProgressActive=false;"
        "files.forEach(x=>uploadStatus[x.path]='pending');"
        "document.getElementById('progress').style.display='block';ub.disabled=true;"
        "updateProgress(0,files.length,0,totalUploadSize);"
        "var toUpload=[].concat(files);"
        "try{var checkResp=await fetch('/api/upload/check',{method:'POST',"
        "headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({files:files.map(x=>({path:x.path,size:x.file.size})),token:token})});"
        "if(checkResp.ok){var cd=await checkResp.json();"
        "if(cd.sessionId){uploadSessionId=cd.sessionId;connectWS()}"
        "var skipCount=0;var resumeCount=0;"
        "if(cd.existing&&cd.existing.length>0){"
        "var ep=new Set(cd.existing.map(f=>f.path));"
        "var ss=files.filter(x=>ep.has(x.path)).reduce((s,x)=>s+x.file.size,0);"
        "ep.forEach(p=>uploadStatus[p]='done');uploadedSize+=ss;"
        "toUpload=files.filter(x=>!ep.has(x.path));skipCount=ep.size}"
        "if(cd.partial&&cd.partial.length>0){"
        "cd.partial.forEach(function(f){"
        "if(f.useChunking!==false&&f.chunkSize&&f.chunkCount){"
        "chunkInfo[f.path]={chunkSize:f.chunkSize,chunkCount:f.chunkCount,"
        "completedChunks:new Set(f.completedChunks||[]),useChunking:true};"
        "uploadedSize+=(f.completedChunks||[]).length*f.chunkSize}"
        "else{chunkInfo[f.path]={resumeOffset:f.size,useChunking:false};uploadedSize+=f.size}});"
        "resumeCount=cd.partial.length}"
        "if(skipCount>0||resumeCount>0){"
        "document.getElementById('resumeHint').style.display='block';"
        "document.getElementById('resumeHint').textContent='检测到未完成的上传，跳过'+skipCount+'个已完成，续传'+resumeCount+'个部分文件'}}}"
        "catch(ex){}"
        "await uploadConcurrent(toUpload);"
        "var fc=Object.values(uploadStatus).filter(s=>s==='failed').length;"
        "var dc=Object.values(uploadStatus).filter(s=>s==='done').length;"
        "if(fc>0){document.getElementById('statusText').textContent=fc+' 个文件上传失败'}"
        "else if(dc===files.length){await finalize()}};"
        "async function uploadConcurrent(fileList){"
        "if(!fileList.length)return;var idx=0;var total=files.length;"
        "async function next(){while(idx<fileList.length){"
        "var item=fileList[idx++];uploadStatus[item.path]='uploading';"
        "try{await uploadOne(item);uploadStatus[item.path]='done'}"
        "catch(e){uploadStatus[item.path]='failed';failedFiles.push(item)}"
        "var dc=Object.values(uploadStatus).filter(s=>s==='done').length;"
        "updateProgress(dc,total,uploadedSize,totalUploadSize)}}"
        "var workers=[];var cc=Math.min(MAX_CONCURRENT,fileList.length);"
        "for(var i=0;i<cc;i++)workers.push(next());await Promise.all(workers)}"
        "function uploadOne(item){var info=chunkInfo[item.path];"
        "if(info&&info.useChunking)return uploadChunked(item,info);"
        "return uploadSmall(item,info)}"
        "function uploadChunked(item,info){"
        "var fileSize=item.file.size,chunkSize=info.chunkSize,totalChunks=Math.ceil(fileSize/chunkSize);"
        "var pending=[];for(var i=0;i<totalChunks;i++){if(!info.completedChunks.has(i))pending.push(i)}"
        "if(pending.length===0)return Promise.resolve();"
        "var nextCI=0;"
        "function uploadNextChunk(){return new Promise(function(resolveAll){"
        "var promises=[];var cc=Math.min(MAX_CONCURRENT,pending.length);"
        "for(var w=0;w<cc;w++){promises.push(new Promise(function(resolveW){"
        "function doNext(){if(nextCI>=pending.length){resolveW();return}"
        "var ci=pending[nextCI++];var start=ci*chunkSize;"
        "var end=Math.min(start+chunkSize,fileSize);var blob=item.file.slice(start,end);"
        "var xhr=new XMLHttpRequest();xhr.open('POST','/api/upload/file',true);"
        "xhr.setRequestHeader('X-Upload-Session',uploadSessionId);"
        "xhr.setRequestHeader('X-File-Path',item.path);"
        "xhr.setRequestHeader('X-Chunk-Index',ci.toString());"
        "xhr.upload.onprogress=function(e){if(e.lengthComputable){xhrProgressActive=true;"
        "var cb=info.completedChunks.size*chunkSize+e.loaded;"
        "var pct=Math.min(100,Math.round(cb/fileSize*100));"
        "var pb=document.getElementById('progressBar');if(pb)pb.style.width=pct+'%';"
        "var st=document.getElementById('statusText');"
        "if(st)st.textContent=formatSize(cb)+' / '+formatSize(fileSize)}};"
        "xhr.onload=function(){if(xhr.status===200){info.completedChunks.add(ci);"
        "uploadedSize+=blob.size;resolveW();doNext()}"
        "else{resolveW()}};"
        "xhr.onerror=function(){resolveW()};xhr.timeout=0;xhr.send(blob)}"
        "doNext()}))}Promise.all(promises).then(resolveAll)})"
        "return uploadNextChunk()}}"
        "function uploadSmall(item,info){return new Promise(function(resolve,reject){"
        "var offset=(info&&info.resumeOffset)?info.resumeOffset:0;"
        "var blob=offset>0?item.file.slice(offset):item.file;"
        "var fd=new FormData();fd.append('file',blob,item.path);"
        "var xhr=new XMLHttpRequest();xhr.open('POST','/api/upload/file',true);"
        "if(uploadSessionId)xhr.setRequestHeader('X-Upload-Session',uploadSessionId);"
        "xhr.setRequestHeader('X-File-Path',item.path);"
        "xhr.upload.onprogress=function(e){if(e.lengthComputable){"
        "xhrProgressActive=true;"
        "var totalLoaded=offset+e.loaded;var pct=Math.round(totalLoaded/item.file.size*100);"
        "var pb=document.getElementById('progressBar');"
        "var st=document.getElementById('statusText');"
        "if(pb)pb.style.width=pct+'%';"
        "if(st)st.textContent=formatSize(totalLoaded)+' / '+formatSize(item.file.size)}};"
        "xhr.timeout=0;"
        "xhr.onload=function(){if(xhr.status===200){"
        "uploadedSize+=item.file.size-offset;resolve()}"
        "else{reject(new Error(''+xhr.status))}};"
        "xhr.onerror=function(){reject(new Error('network'))};"
        "xhr.ontimeout=function(){reject(new Error('timeout'))};xhr.send(fd)})}"
        "function updateProgress(done,total,doneSz,totalSz){"
        "var pct=totalSz>0?Math.round(doneSz/totalSz*100):(total>0?Math.round(done/total*100):0);"
        "document.getElementById('progressBar').style.width=pct+'%';"
        "document.getElementById('progressText').textContent=pct+'% ('+done+'/'+total+')';"
        "document.getElementById('statusText').textContent='上传中... '+formatSize(doneSz)+' / '+formatSize(totalSz)}"
        "async function finalize(){"
        "var fp=files[0].path;var isFolder=fp.indexOf('/')>=0;"
        "var fr=isFolder?fp.split('/')[0]:'';"
        "try{var resp=await fetch('/api/upload/finalize',{method:'POST',"
        "headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({folder:fr,isFolder:isFolder,totalSize:totalUploadSize,"
        "fileCount:files.length,token:token,isReceive:false,sessionId:uploadSessionId})});"
        "if(resp.ok){ub.style.display='none';document.getElementById('result').style.display='block';"
        "try{var r=await resp.json();"
        "document.getElementById('resultLink').href=r.url;"
        "document.getElementById('resultLink').textContent=r.url}catch(e){}}"
        "else{ub.disabled=false;document.getElementById('statusText').textContent='完成操作失败'}}"
        "catch(e){ub.disabled=false;document.getElementById('statusText').textContent='完成操作失败'}}"
        "</script></body></html>").arg(token).toUtf8();
}

// handleDirectUpload removed - now handled by handleStreamingUpload via streaming route

QString RequestHandler::mimeTypeForFile(const QString& fileName) const
{
    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(fileName);
    if (mime.isValid()) {
        return mime.name();
    }

    QString ext = QFileInfo(fileName).suffix().toLower();
    static QMap<QString, QString> mimeMap = {
        {"txt", "text/plain"}, {"html", "text/html"}, {"htm", "text/html"},
        {"css", "text/css"}, {"js", "application/javascript"}, {"json", "application/json"},
        {"xml", "application/xml"}, {"pdf", "application/pdf"},
        {"zip", "application/zip"}, {"rar", "application/x-rar-compressed"},
        {"7z", "application/x-7z-compressed"}, {"tar", "application/x-tar"},
        {"gz", "application/gzip"},
        {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"}, {"png", "image/png"},
        {"gif", "image/gif"}, {"bmp", "image/bmp"}, {"svg", "image/svg+xml"},
        {"webp", "image/webp"}, {"ico", "image/x-icon"},
        {"mp3", "audio/mpeg"}, {"wav", "audio/wav"}, {"flac", "audio/flac"},
        {"ogg", "audio/ogg"}, {"m4a", "audio/mp4"},
        {"mp4", "video/mp4"}, {"avi", "video/x-msvideo"}, {"mkv", "video/x-matroska"},
        {"mov", "video/quicktime"}, {"webm", "video/webm"},
        {"doc", "application/msword"},
        {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {"xls", "application/vnd.ms-excel"},
        {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {"ppt", "application/vnd.ms-powerpoint"},
        {"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    };

    return mimeMap.value(ext, "application/octet-stream");
}
