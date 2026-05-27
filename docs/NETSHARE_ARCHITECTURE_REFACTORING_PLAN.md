# NetShare 四大架构重构执行计划

**版本**: 3.0  
**日期**: 2026-05-26  
**状态**: 计划阶段  
**前身**: `CIVETWEB_MIGRATION_PLAN.md` v2.0（已作废，本版包含 12 项缺失修正 + 6 项冲突消解）

---

## 目录

1. [总览](#1-总览)
2. [当前架构分析](#2-当前架构分析)
3. [完整受影响文件清单](#3-完整受影响文件清单)
4. [Phase 1: 依赖部署与框架搭建](#phase-1-依赖部署与框架搭建)
5. [Phase 2: 核心路由迁移（15 个 handler）](#phase-2-核心路由迁移15-个-handler)
6. [Phase 3: 流式下载与上传迁移](#phase-3-流式下载与上传迁移)
7. [Phase 4: TLS 自签 + WebSocket 一体化迁移](#phase-4-tls-自签--websocket-一体化迁移)
8. [Phase 5: main.cpp 初始化整合](#phase-5-maincpp-初始化整合)
9. [Phase 6: 集成测试与验收](#phase-6-集成测试与验收)
10. [Phase 7: 清理旧代码与文档更新](#phase-7-清理旧代码与文档更新)
11. [Phase 8: QML 加载路径简化](#phase-8-qml-加载路径简化)
12. [Phase 9: Boost.DI 容器替换 ServiceLocator](#phase-9-boostdi-容器替换-servicelocator)

**附录**
[附录 A: 修正后 CivetWebServer 接口设计](#附录-a-修正后-civetwebserver-接口设计)  
[附录 B: 修正后 DIContainer 绑定配置](#附录-b-修正后-dicontainer-绑定配置)  
[附录 C: 15 个 Handler 签名迁移对照表](#附录-c-15-个-handler-签名迁移对照表)  
[附录 D: CMake 集成配置](#附录-d-cmake-集成配置)  
[附录 E: Phase 执行依赖图](#附录-e-phase-执行依赖图)  
[附录 F: 工期估算](#附录-f-工期估算)

---

## 1. 总览

### 1.1 四大改进项

| # | 改进项 | 技术选型 | 收益 |
|---|--------|---------|------|
| A | HTTP 服务器 | 自研 → **CivetWeb** (MIT) | 删除 ~950 行手写代码 / 内置 WebSocket+HTTPS+Range |
| B | TLS 证书 | 手动配置 → **OpenSSL 自签** 自动生成 | 零配置 HTTPS / 局域网加密 |
| C | DI 容器 | ServiceLocator → **Boost.DI** (Apache 2.0) | 类型安全 DI / 删除 84 行 ServiceLocator |
| D | QML 加载 | 10 层 cdUp 搜索 → **环境变量** | 删除 ~70 行回退代码 |

### 1.2 新增 / 删除文件总览

| 操作 | 文件 |
|------|------|
| ✨ 新建 | `src/network/CivetWebServer.h/cpp`、`src/core/common/TlsCertificateGenerator.h/cpp`、`src/core/common/DIContainer.h` |
| 📥 下载 | `third_party/boost-di/di.hpp`（单头文件）、CivetWeb 源码（CMake FetchContent 自动） |
| 🗑️ 删除 | `src/network/HttpServer.h/cpp`、`src/network/WebSocketHandler.h/cpp`、`src/core/common/ServiceLocator.h` |
| ✏️ 修改 | `src/network/RequestHandler.h/cpp`、`src/network/CMakeLists.txt`、`src/core/CMakeLists.txt`、`src/main.cpp` |

### 1.3 基于 v2.0 的关键修正

| v2.0 问题 | v3.0 修正 |
|-----------|----------|
| 遗漏 `src/core/CMakeLists.txt` 修改 | Phase 9 追加 |
| `RequestHandler` 符号变更未逐条列出 | Phase 2 完整枚举 |
| `QHash<QTcpSocket*,...>` → `mg_connection*` 未提及 | Phase 3 明确替换 |
| QObject 生命周期 vs DI singleton 冲突 | Phase 9 全部用 `std::ref()` 绑定 |
| TLS Phase 4 + Phase 8 同一代码段冲突 | 合并为 Phase 4 |
| DIContainer 遗漏 SettingsManager 等绑定 | 附录 B 补齐全部服务 |
| WebSocket 心跳混淆 HTTP Keep-Alive | Phase 4 增加 QTimer ping/pong |
| `broadcastToSubscribers` 迁移目标不明确 | 明确定位于 CivetWebServer |
| `wsHandler->broadcastToSubscribers` 调用点遗漏 | Phase 5 逐行替换 |
| `gui/CMakeLists.txt` 误列 | 移除 |
| `shutdown()` 合并遗漏 | Phase 5 追加 |
| 内嵌 HTML 中 WebSocket URL 未更新 | Phase 4 追加 |

---

## 2. 当前架构分析

### 2.1 现有依赖图

```
main.cpp (NetShareApplication)
  ├── ServiceLocator m_locator
  │     ├── registerService<DatabaseManager>
  │     ├── registerService<SettingsManager>
  │     ├── registerService<IShareManager> / registerService<ShareManager>
  │     ├── registerService<FileTransferEngine>
  │     ├── registerService<TransferLogService>
  │     ├── registerService<IFileBrowser> / registerService<FileBrowser>
  │     ├── registerService<IFolderPacker> / registerService<FolderPacker>
  │     ├── registerService<ChunkManager>
  │     ├── registerService<ResumeManager>
  │     ├── registerService<BandwidthManager>
  │     ├── registerService<HttpServer>          ← 将替换
  │     ├── registerService<WebSocketHandler>    ← 将删除（合并入 WebSocket）
  │     ├── registerService<mDNSService>
  │     └── registerService<NotificationManager>
  ├── HttpServer (src/network/HttpServer.h/cpp)
  │     ├── SslAwareServer (内部 QSslSocket 派生)
  │     ├── 15 个路由回调
  │     └── streamingSocketDisconnected 信号
  ├── RequestHandler (src/network/RequestHandler.h/cpp)
  │     ├── 依赖 IShareManager / IFileBrowser / IFolderPacker
  │     ├── registerRoutes(HttpServer*)
  │     ├── StreamingMultipartParser（保留不变）
  │     ├── m_streamingStates: QHash<QTcpSocket*, StreamingUploadState*>
  │     ├── m_streamingFileStates: QHash<QTcpSocket*, StreamingFileUploadState*>
  │     ├── m_taskToToken / m_taskToShareToken
  │     └── 4 个 HTML 生成辅助函数
  ├── WebSocketHandler (src/network/WebSocketHandler.h/cpp)
  │     ├── QWebSocketServer (独立 port+1)
  │     ├── broadcastToSubscribers(token, type, data)
  │     ├── subscribeClient / unsubscribeClient / unsubscribeClientFromAll
  │     └── setupHeartbeat / sendHeartbeat (QTimer ping/pong)
  └── mDNSService (独立，不受影响)
```

### 2.2 HTTP 路由清单（15 个）

| # | Method | Path | Handler | 类型 |
|---|--------|------|---------|------|
| 1 | GET | `/` | `handleIndex` | 普通 HTML |
| 2 | GET | `/s/*` | `handleSharePage` | 普通 HTML |
| 3 | GET | `/download/*` | `handleFileDownload` | 流式响应 |
| 4 | GET | `/folder/*` | `handleFolderDownload` | 流式响应 |
| 5 | GET | `/api/shares` | `handleApiShares` | JSON API |
| 6 | GET | `/api/files/*` | `handleApiFiles` | JSON API |
| 7 | GET | `/receive` | `handleReceivePage` | 普通 HTML |
| 8 | GET | `/upload/*` | `handleUploadPage` | 普通 HTML |
| 9 | POST | `/receive` | `handleStreamingUpload` | 流式接收 |
| 10 | POST | `/api/upload/check` | `handleUploadCheck` | JSON API |
| 11 | POST | `/api/upload/file` | `handleStreamingFileUpload` | 流式接收 |
| 12 | POST | `/api/upload/finalize` | `handleUploadFinalize` | JSON API |
| 13 | POST | `/api/upload/abort` | `handleUploadAbort` | JSON API |
| 14 | POST | `/upload/*` | `handleStreamingUpload` (fallback) | 流式接收 |
| 15 | OPTIONS | `*` | `handleCorsPreflight` | CORS 预检 |

---

## 3. 完整受影响文件清单

基于全量 grep 交叉比对后的**最终清单**（覆盖 v2.0 所有遗漏）：

| 文件 | 操作 | 触发 Phase | 变更要点 |
|------|------|-----------|---------|
| `src/network/CMakeLists.txt` | ✏️ 修改 | 1 | CivetWeb FetchContent；移除 HttpServer/WebSocketHandler；新增 CivetWebServer |
| `src/network/CivetWebServer.h` | ✨ 新建 | 1 | QObject 封装类（见附录 A） |
| `src/network/CivetWebServer.cpp` | ✨ 新建 | 1 | 启动/停止/路由/WebSocket/心跳/TLS |
| `src/network/RequestHandler.h` | ✏️ 修改 | 2, 3 | `#include` `HttpServer→CivetWebServer`；`registerRoutes` 签名；`m_httpServer→m_civetServer`；`QHash<QTcpSocket*→mg_connection*>`；15 个 handler 签名 |
| `src/network/RequestHandler.cpp` | ✏️ 修改 | 2, 3, 4 | 所有 handler 实现适配；`connect` 信号更新；HTML 辅助函数适配；JS WebSocket URL |
| `src/network/HttpServer.h` | 🗑️ 删除 | 7 | |
| `src/network/HttpServer.cpp` | 🗑️ 删除 | 7 | |
| `src/network/WebSocketHandler.h` | 🗑️ 删除 | 7 | |
| `src/network/WebSocketHandler.cpp` | 🗑️ 删除 | 7 | |
| `src/core/common/TlsCertificateGenerator.h` | ✨ 新建 | 4 | OpenSSL 自签证书封装 |
| `src/core/common/TlsCertificateGenerator.cpp` | ✨ 新建 | 4 | QProcess 调用 openssl CLI |
| `src/core/common/DIContainer.h` | ✨ 新建 | 9 | 全部服务绑定（见附录 B） |
| `src/core/common/ServiceLocator.h` | 🗑️ 删除 | 9 | |
| `src/core/CMakeLists.txt` | ✏️ 修改 | 9 | 移除 `common/ServiceLocator.h` 引用 |
| `src/main.cpp` | ✏️ 修改 | 4, 5, 8, 9 | 网络初始化 / shutdown / QML 加载 / DI 替换 |
| `third_party/boost-di/di.hpp` | 📥 下载 | 9 | Boost.DI 单头文件 |
| `third_party/civetweb/` | 📥 自动 | 1 | CMake FetchContent 管理 |

---

## Phase 1: 依赖部署与框架搭建

**工期**: 1.5 天  
**目标**: CivetWeb 编译通过，CivetWebServer 骨架可用，Boost.DI 头文件就位

### 步骤

| 步骤 | 任务 | 产出物 |
|------|------|--------|
| 1.1 | 在 `src/network/CMakeLists.txt` 添加 CivetWeb FetchContent | 附录 D 配置 |
| 1.2 | CMake 配置验证（确保 `civetweb` target 可用） | `cmake --build` 成功 |
| 1.3 | 创建 `CivetWebServer.h` — QObject 封装类骨架（完整接口定义） | 附录 A |
| 1.4 | 创建 `CivetWebServer.cpp` — 实现 `start()`/`stop()`/`isRunning()` | 编译通过 |
| 1.5 | 实现 `mg_callbacks.log_message` → `LOG_INFO` 桥接 | 启动日志可见 |
| 1.6 | 下载 `boost/di.hpp` 到 `third_party/boost-di/` | 单头文件就位 |
| 1.7 | 验证：CivetWeb 监听的 HTTP 端口返回空响应 | `curl -i http://localhost:8080/` |

### 验证方法

```bash
cmake --build . --target NetshareNetwork   # 编译通过
curl -i http://localhost:8080/              # 返回 404 或空（CivetWeb 路由未配）
```

---

## Phase 2: 核心路由迁移（15 个 handler）

**工期**: 3 天  
**目标**: 全部 15 个 HTTP 路由迁移到 CivetWeb，handler 签名和实现全部适配

### 步骤

| 步骤 | 任务 | 覆盖路由 |
|------|------|---------|
| 2.1 | **RequestHandler.h 头文件变更**：<br>— `#include "HttpServer.h"` → `#include "CivetWebServer.h"`<br>— `registerRoutes(HttpServer*)` → `registerRoutes(CivetWebServer*)`<br>— `HttpServer* m_httpServer` → `CivetWebServer* m_civetServer`<br>— 15 个 handler 签名从 `void(const HttpRequest&, HttpResponse&)` 改为 `int(mg_connection*, const HttpRequestInfo&)`（见附录 C）<br>— 4 个 HTML 辅助函数保持返回 `QByteArray` 不变 | 全部 |
| 2.2 | **迁移静态 HTML 路由**：`handleIndex` (`GET /`)、`handleReceivePage` (`GET /receive`)<br>实现：handler 中调用 `generateXxxPage()` 返回 QByteArray，通过 `CivetWebServer::sendHtmlResponse(conn, 200, html)` 发送 | #1, #7 |
| 2.3 | **迁移分享/上传页面 + CORS**：`handleSharePage` (`GET /s/*`)、`handleUploadPage` (`GET /upload/*`)、`handleCorsPreflight` (`OPTIONS *`)<br>实现：`handleCorsPreflight` 返回 `mg_printf(conn, "HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\n...\r\n\r\n")` 返回 1 | #2, #8, #15 |
| 2.4 | **迁移 JSON API 路由**：`handleApiShares` (`GET /api/shares`)、`handleApiFiles` (`GET /api/files/*`)<br>实现：`CivetWebServer::sendJsonResponse(conn, 200, json)` | #5, #6 |
| 2.5 | **迁移上传业务路由**（非流式）：`handleUploadCheck` (POST)、`handleUploadFinalize` (POST)、`handleUploadAbort` (POST)<br>实现：JSON 解析 `mg_read(conn, buf, ri->content_length)` → 业务逻辑 → `sendJsonResponse` | #10, #12, #13 |
| 2.6 | **实现 `mg_request_info` → `HttpRequestInfo` 适配器**：`static HttpRequestInfo fromCivetWeb(mg_connection*, const mg_request_info*)`<br>提取 method / uri / queryString / headers / body / remoteAddress | 全部通用 |
| 2.7 | **实现 CivetWebServer 响应助手**：<br>`sendJsonResponse(conn, status, json)`<br>`sendHtmlResponse(conn, status, html)`<br>`sendFileResponse(conn, filePath, mimeType, fileName)` | 全部通用 |
| 2.8 | **把 15 个路由注册到 CivetWebServer**：逐一 `mg_set_request_handler(ctx, uri, handler, cbdata)` | 全部 |
| 2.9 | 验证：curl 测试每个 GET 路由返回预期状态码和 Content-Type | 全部 |

### 验证方法

```bash
curl -i http://localhost:8080/                  # 200 text/html
curl -i http://localhost:8080/api/shares         # 200 application/json
curl -i http://localhost:8080/s/test123          # 200 text/html（分享页）
curl -i -X OPTIONS http://localhost:8080/api/shares  # 204 + CORS headers
```

---

## Phase 3: 流式下载与上传迁移

**工期**: 3 天  
**目标**: 3 个流式下载 + 3 个流式上传路由迁移，QTcpSocket 全部替换为 mg_connection

### 步骤

| 步骤 | 任务 | 关键变更 |
|------|------|---------|
| 3.1 | **流式文件下载** `handleFileDownload` (`GET /download/*`)<br>使用 `mg_send_file_body(conn, filePath, offset, length)` 替代手写 64KB buffer 循环，内置 Range 支持 | — |
| 3.2 | **流式文件夹下载** `handleFolderDownload` (`GET /folder/*`)<br>ZIP 打包后用 `mg_send_file(conn, tempZipPath)` + 响应后删除临时文件 | — |
| 3.3 | **RequestHandler.h/cpp 流式类型替换**：<br>— `QHash<QTcpSocket*, StreamingUploadState*>` → `QHash<mg_connection*, StreamingUploadState*>`<br>— `QHash<QTcpSocket*, StreamingFileUploadState*>` → `QHash<mg_connection*, StreamingFileUploadState*>`<br>— `handleStreamingUpload(QTcpSocket*, ...)` → `handleStreamingUpload(mg_connection*, ...)`<br>— `handleStreamingFileUpload(QTcpSocket*, ...)` → `handleStreamingFileUpload(mg_connection*, ...)` | 消除 QTcpSocket 依赖 |
| 3.4 | **RequestHandler.cpp L153 信号连接更新**：<br>`connect(server, &HttpServer::streamingSocketDisconnected, ...)`<br>→ `connect(server, &CivetWebServer::streamingConnDisconnected, ...)`<br>对应的 lambda 中参数类型从 `QTcpSocket*` 改为 `mg_connection*` | — |
| 3.5 | **流式上传** `handleStreamingUpload` (`POST /receive`, `POST /upload/*`)<br>在 `begin_request` 中循环 `mg_read(conn, buf, CHUNK_SIZE)` 逐块交给 `StreamingMultipartParser`<br>`StreamingMultipartParser` **保留不变** | — |
| 3.6 | **流式文件上传** `handleStreamingFileUpload` (`POST /api/upload/file`)<br>同上，逐块读取 → `StreamingMultipartParser` / chunk file writing | — |
| 3.7 | Session 超时清理：保留现有 `QTimer` + `cleanupExpiredSessions()`；CivetWeb 回调中 `mg_connection*` 断开时清理映射 | — |
| 3.8 | `cancelStreamingRequest` 逻辑适配：从 `QTcpSocket*` 查找 → `mg_connection*` 查找；调用 `mg_close_connection(conn)` 替代 `socket->close()` | — |

### 验证方法

```bash
# 流式下载
curl -o /dev/null -w "%{http_code}" http://localhost:8080/download/test.bin  # 200
curl -H "Range: bytes=0-1024" -o /dev/null -w "%{http_code} %{size_download}" http://localhost:8080/download/test.bin  # 206 1025

# 流式上传
curl -F "file=@largefile.bin" http://localhost:8080/receive               # 200
curl -F "file=@chunked.bin" http://localhost:8080/api/upload/file         # 200
```

---

## Phase 4: TLS 自签 + WebSocket 一体化迁移

**工期**: 2.5 天  
**目标**: TLS 自签证书自动生成 + CivetWeb HTTPS + WebSocket 迁移（含心跳、广播、JS URL）  
**说明**: **合并了原 v2.0 的 Phase 4.1 (TLS 迁移) + Phase 8 (TLS 自签)**，消解 C4 冲突 — 两者操作同一段 `initializeNetworkServer()` 代码

### 步骤

| 步骤 | 任务 | 说明 |
|------|------|------|
| 4.1 | **创建 `TlsCertificateGenerator.h/cpp`**<br>— `static bool generateSelfSignedCert(certPath, keyPath)`<br>— `static bool certExists(certPath, keyPath)`<br>— `static QString defaultCertDir()` → `AppData/NetShare/certs/`<br>实现：`QProcess("openssl", {"req","-x509","-newkey","rsa:4096","-nodes",...})` | 50 行 C++ |
| 4.2 | **在 `main.cpp` `initializeNetworkServer()` 中集成自签逻辑**：<br>— 读 settings `tlsCertPath`/`tlsKeyPath`（可空）<br>— 空则用 `defaultCertDir() + "/cert.pem"` / `"/key.pem"`<br>— `certExists()` 检查，不存在则 `generateSelfSignedCert()`<br>— 将路径填入 CivetWeb options：`"ssl_certificate", certPath, "ssl_private_key", keyPath`<br>— 移除旧的 `QSslCertificate`/`QSslKey`/`QSslConfiguration` 加载代码 | 删除 ~30 行，新增 ~15 行 |
| 4.3 | **WebSocket 端点迁移**：<br>— `mg_set_websocket_handler(ctx, "/ws", connect, ready, data, close, this)`<br>— CivetWebServer 中实现 4 个 static 回调：`staticWsConnectHandler` / `staticWsReadyHandler` / `staticWsDataHandler` / `staticWsCloseHandler`<br>— 回调中 `static_cast<CivetWebServer*>(cbdata)` 取回 `this`<br>— `m_wsClients: QMap<QString, QSet<mg_connection*>>` 维护 token → 连接集合映射<br>— 迁移 `subscribeClient` / `unsubscribeClient` / `unsubscribeClientFromAll` 逻辑 | ~150 行 C++ |
| 4.4 | **WebSocket ping/pong 心跳**（修正 M8 — 不能只用 HTTP Keep-Alive）：<br>— CivetWebServer 构造函数中创建 `QTimer`，每 30 秒对所有 `m_wsClients` 中的连接发送 `mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_PING, "", 0)`<br>— 在 `data_handler` 中识别 `MG_WEBSOCKET_OPCODE_PONG`，更新 `lastPong` 时间戳<br>— 超时 60 秒未收到 pong → `mg_close_connection(conn)` + 清理映射 | ~40 行 C++ |
| 4.5 | **`broadcastToSubscribers` 在 CivetWebServer 中实现**：<br>`void broadcastToSubscribers(const QString& token, const QString& type, const QJsonObject& data)`<br>— 遍历 `m_wsClients[token]`<br>— `QJsonDocument(doc).toJson(QJsonDocument::Compact)` 序列化<br>— `mg_websocket_write(client, MG_WEBSOCKET_OPCODE_TEXT, msg.constData(), msg.size())` | ~20 行 C++ |
| 4.6 | **更新内嵌 HTML 中的 WebSocket 连接 URL**（修正 M10）：<br>— 在 `generateSharePage` / `generateUploadPage` / `generateReceivePage` 的 JavaScript 中<br>— `new WebSocket('ws://'+location.host+':<port+1>')` 改为 `new WebSocket('ws://'+location.host+'/ws')`<br>— 对应 `wss://` 用于 HTTPS 场景 | ~5 行修改 |
| 4.7 | 移除 `main.cpp` 中 `wsHandler = new WebSocketHandler(this)` 和 `wsHandler->start(wsPort)` 代码块<br>移除 TLS 配置中的 `wsHandler->setSslConfiguration()` 调用 | main.cpp 清理 |

### 验证方法

```bash
# TLS 证书自动生成
dir %APPDATA%\NetShare\certs\            # cert.pem + key.pem 存在

# HTTPS
curl -k https://localhost:8443/          # 200（忽略自签证书验证）

# WebSocket
# 浏览器打开 http://localhost:8080/s/{token} → 开发者工具 Network WS 标签 → 确认 /ws 连接成功

# WebSocket ping/pong
# 保持页面打开 5 分钟 → WS 连接不断开

# 多客户端广播
# 打开 2 个浏览器标签页 → 触发上传 → 2 个标签页同时收到 transfer_update
```

---

## Phase 5: main.cpp 初始化整合

**工期**: 1.5 天  
**目标**: `initializeNetworkServer()` / `shutdown()` / `initializeQml()` 中所有 HttpServer 和 WebSocketHandler 引用替换为 CivetWebServer

### 步骤

| 步骤 | 任务 | 说明 |
|------|------|------|
| 5.1 | **替换 HttpServer 初始化**：<br>— `auto* httpServer = new HttpServer(this)` → `auto* civetServer = new CivetWebServer(this)`<br>— `m_locator.registerService(httpServer)` → `m_locator.registerService(civetServer)`<br>— `requestHandler->registerRoutes(httpServer)` → `requestHandler->registerRoutes(civetServer)`<br>— `httpServer->start(port, ...)` → `civetServer->start(port, ...)`（CivetWebServer 已合并 WebSocket，不需要独立 wsPort） | — |
| 5.2 | **移除 WebSocket 独立端口逻辑**：<br>— 删除 `quint16 wsPort = port + 1;` 和 `wsHandler->start(wsPort, ...)` 代码块<br>— WebSocket 已通过 Phase 4 集成在同一 `port` 的 `/ws` 路径<br>— mDNS 注册 `mdnsService->registerService(serviceName, port)` 保持不变（port 不变） | — |
| 5.3 | **更新 `taskProgress` 信号连接**（修正 M7）：<br>将 lambda 中 `wsHandler->broadcastToSubscribers(...)` 替换为 `civetServer->broadcastToSubscribers(...)`<br>— Line 425: `wsHandler->broadcastToSubscribers(token, "transfer_update", data)` → `civetServer->broadcastToSubscribers(token, "transfer_update", data)`<br>— Line 431: `wsHandler->broadcastToSubscribers(shareToken, "transfer_update", data)` → `civetServer->broadcastToSubscribers(shareToken, "transfer_update", data)`<br>— 双 token 广播（`m_taskToToken` + `m_taskToShareToken`）保持不变 | — |
| 5.4 | **更新 QML context property**：<br>`m_engine->rootContext()->setContextProperty("webSocketHandler", wsHandler)`<br>→ `m_engine->rootContext()->setContextProperty("webSocketHandler", civetServer)`<br>（QML 侧引用名 `webSocketHandler` 不变，仅替换指向的对象） | — |
| 5.5 | **合并 shutdown() 逻辑**（修正 M6）：<br>— 删除 `m_locator.service<WebSocketHandler>()->stop()`<br>— 保留唯一 `m_locator.service<CivetWebServer>()->stop()`（同时停止 HTTP + WebSocket）<br>— 其他 shutdown 项（mDNS/TransferEngine/BandwidthManager/Settings/Database）保持不变 | — |
| 5.6 | **移除 `#include "network/HttpServer.h"` 和 `#include "network/WebSocketHandler.h"`** | — |

### 验证方法

```bash
# 启动服务
cmake --build . && .\NetShare.exe

# 验证端口
netstat -ano | findstr :8080           # 仅一个端口（HTTP + WS 共用）

# shutdown 测试
# 托盘退出 → 日志无 crash/空指针/double-free
```

---

## Phase 6: 集成测试与验收

**工期**: 2 天  
**目标**: 端到端功能验证 + 性能基准对比

### 测试项

| # | 测试项 | 方法 | 验收标准 |
|---|--------|------|---------|
| 6.1 | 分享页面访问 | 浏览器 `http://ip:8080/s/{token}` | 200 + 正确渲染 |
| 6.2 | 文件下载 | 浏览器点击下载 | 文件完整，SHA256 一致 |
| 6.3 | Range 请求 | `curl -H "Range: bytes=0-1024" ...` | 206 + Content-Range |
| 6.4 | 文件夹 ZIP 下载 | 浏览器下载 ZIP | ZIP 完整可解压 |
| 6.5 | CORS 预检 | `curl -X OPTIONS -H "Origin: http://other" ...` | 204 + CORS headers |
| 6.6 | 大文件流式上传 (5GB) | `curl -T largefile.iso` | HTTP 200，文件完整 |
| 6.7 | 多任务并行 | 3 个并发下载 + 1 个上传 | 全部完成，无超时 |
| 6.8 | 超时 Session 清理 | 上传中断 30 分钟 | Session 被清理 |
| 6.9 | TLS HTTPS | `curl -k https://localhost:8443/` | 证书自动生成，握手成功 |
| 6.10 | WebSocket 连接 | 浏览器开发者工具 WS 标签 | 101 Switching Protocols |
| 6.11 | WebSocket 广播 | 2 个标签页同时连接 | 双端收到 transfer_update |
| 6.12 | WebSocket 心跳 | 页面闲置 5 分钟 | 连接不断开 |
| 6.13 | mDNS 服务发现 | 另一台设备 | 发现服务 + 正确端口 |
| 6.14 | 性能基准 | ApacheBench / wrk | QPS ≥2000 / 吞吐 ≥100MB/s |

---

## Phase 7: 清理旧代码与文档更新

**工期**: 1 天  
**目标**: Git 删除旧文件、更新项目文档

### 步骤

| 步骤 | 任务 |
|------|------|
| 7.1 | `git rm src/network/HttpServer.h src/network/HttpServer.cpp` |
| 7.2 | `git rm src/network/WebSocketHandler.h src/network/WebSocketHandler.cpp` |
| 7.3 | 更新 `docs/API.md`（API 端点不变，仅实现方式变更） |
| 7.4 | 更新 `docs/BUILD.md`（CivetWeb 自动下载、NETSHARE_QML_PATH 环境变量、OpenSSL 前置条件） |
| 7.5 | 更新 `docs/ARCHITECTURE_IMPROVEMENT.md`（追加重构记录） |

---

## Phase 8: QML 加载路径简化

**工期**: 0.5 天  
**目标**: 删除 ~70 行文件系统回退代码，用环境变量替代  
**独立性**: 本 Phase 不依赖其他 Phase 完成，可在 Phase 1–7 之间任意时间执行

### 步骤

| 步骤 | 任务 | 说明 |
|------|------|------|
| 8.1 | 删除 `initializeQml()` 中 Option 3 全部代码：`cdUp()` 循环、`searchPaths` 列表、文件系统 `QFileInfo::exists` 检查 | 删除 ~70 行 |
| 8.2 | 在 Option 2 (qrc) 之前插入环境变量回退：<br>`QString qmlPath = qEnvironmentVariable("NETSHARE_QML_PATH", "");`<br>若非空且 `QFile::exists(qmlPath)` → 加载 | 新增 ~5 行 |
| 8.3 | 更新 `docs/BUILD.md` 说明环境变量用法 | 1 段 |

### 改进后代码

```cpp
// main.cpp initializeQml() — 改进后完整函数
bool NetShareApplication::initializeQml()
{
    m_engine = new QQmlApplicationEngine(this);

    qRegisterMetaType<ShareInfo>("ShareInfo");
    qRegisterMetaType<TransferTask>("TransferTask");
    qRegisterMetaType<FileEntry>("FileEntry");

    // ... context properties（与 Phase 5.4 一致）...

    m_engine->loadFromModule("NetShare", "Main");

    if (m_engine->rootObjects().isEmpty()) {
        // 回退: 环境变量 → qrc
        QString qmlPath = qEnvironmentVariable("NETSHARE_QML_PATH");
        if (!qmlPath.isEmpty() && QFileInfo::exists(qmlPath)) {
            m_engine->load(QUrl::fromLocalFile(qmlPath));
        } else {
            m_engine->load(QUrl("qrc:/qt/qml/NetShare/qml/Main.qml"));
        }
    }

    if (m_engine->rootObjects().isEmpty()) {
        LOG_ERROR("Failed to load QML from any source");
        delete m_engine; m_engine = nullptr;
        return false;
    }
    // ... QWindow + DWM ...
    return true;
}
```

---

## Phase 9: Boost.DI 容器替换 ServiceLocator

**工期**: 3 天  
**目标**: 用 Boost.DI 替换 `m_locator` 全部注册/读取操作，QObject 类全部用 `std::ref()` 绑定  
**前置条件**: Part A (Phase 1–7) 必须完全完成，因为 Phase 5 在 ServiceLocator 中注册了 `CivetWebServer`，Phase 9 将移除整个 ServiceLocator

### 步骤

| 步骤 | 任务 | 说明 |
|------|------|------|
| 9.1 | 确认 `third_party/boost-di/di.hpp` 已就位（Phase 1.6） | — |
| 9.2 | 创建 `src/core/common/DIContainer.h`（完整绑定见附录 B）<br>4 个模块：`CoreModule()` / `TransferModule()` / `NetworkModule()` / `InfraModule()` | ~120 行 |
| 9.3 | **修正 QObject 生命周期策略**（消解 C1）：<br>**所有 QObject 派生类不在 DIContainer 中创建，仅在 main.cpp 中 `new Xxx(this)` 后通过 `std::ref()` 绑定到 injector** | 关键修正 |
| 9.4 | **替换 `initializeDatabase()` 中的 `m_locator.registerService(db)`**<br>→ 保持 `new DatabaseManager(this)`，但改为保存到成员变量 `m_database`，后续 `std::ref(*m_database)` 绑定 | — |
| 9.5 | **替换 `initializeSettings()` 中的 `m_locator.registerService(settings)`**<br>→ 同理，保存到 `m_settings` 成员，后续 `std::ref()` | — |
| 9.6 | **替换 `initializeCoreServices()` 中全部 `m_locator.registerService<T>()`**<br>→ 照常 `new Xxx(this)`，保存到成员变量，`std::ref()` 绑定<br>覆盖：`IShareManager` / `ShareManager` / `FileTransferEngine` / `TransferLogService` / `IFileBrowser` / `FileBrowser` / `IFolderPacker` / `FolderPacker` / `ChunkManager` / `ResumeManager` / `BandwidthManager` | — |
| 9.7 | **替换 `initializeNetworkServer()` 中 `m_locator.registerService(civetServer)` 和 `m_locator.registerService(mdnsService)`**<br>→ 同理 | — |
| 9.8 | **替换 `initializeQml()` 中 12 个 `m_locator.service<T>()` context property 获取**（修正 M5）<br>→ `auto* shareManager = m_injector->create<ShareManager&>();` 等 | — |
| 9.9 | **替换 `shutdown()` 中全部 `m_locator.service<T>()`**<br>→ 改用 injector 获取 | — |
| 9.10 | **替换 `initializeTrayIcon()` 中 `m_locator.service<QSystemTrayIcon>()`**<br>→ tray icon 已保存为成员 `m_trayIcon`，直接使用 | — |
| 9.11 | 删除 `m_locator` 成员变量：`src/main.cpp` L717 `ServiceLocator m_locator;` | — |
| 9.12 | 从 `src/core/CMakeLists.txt` 第 11 行移除 `common/ServiceLocator.h`（修正 M1） | — |
| 9.13 | `git rm src/core/common/ServiceLocator.h` | — |

### 修正后 main.cpp 结构

```cpp
// main.cpp — 使用 std::ref() 绑定，DI 不接管 QObject 生命周期

class NetShareApplication : public QApplication
{
    // ... 原有成员 ...

    // Boost.DI injector（仅用于服务发现，不管理生命周期）
    std::unique_ptr<decltype(di::make_injector())> m_injector;

    // 所有服务对象仍为 QObject child（通过 new Xxx(this) 创建）
    DatabaseManager*   m_database      = nullptr;
    SettingsManager*   m_settings      = nullptr;
    ShareManager*      m_shareManager  = nullptr;
    FileTransferEngine* m_transferEngine = nullptr;
    TransferLogService* m_transferLog  = nullptr;
    IFileBrowser*      m_fileBrowser   = nullptr;
    IFolderPacker*     m_folderPacker  = nullptr;
    ChunkManager*      m_chunkManager  = nullptr;
    ResumeManager*     m_resumeManager = nullptr;
    BandwidthManager*  m_bandwidthManager = nullptr;
    CivetWebServer*    m_civetServer   = nullptr;
    mDNSService*       m_mdnsService   = nullptr;
};

bool NetShareApplication::initialize()
{
    // ... Logger / Database / Settings / CoreServices / Network / Tray / QML ...
    // 每个步骤中: auto* obj = new Xxx(this);

    // 全部服务创建后，构建 injector
    using namespace boost::di;
    m_injector = std::make_unique<decltype(make_injector(
        std::declval<decltype(CoreModule())>(),
        std::declval<decltype(TransferModule())>(),
        std::declval<decltype(NetworkModule())>(),
        std::declval<decltype(InfraModule())>()
    ))>(make_injector(
        CoreModule(),
        TransferModule(),
        NetworkModule(),
        InfraModule()
    ));
}

// 使用示例:
// auto* shareManager = m_injector->create<IShareManager&>();
```

### 验证方法

```bash
cmake --build .    # 编译通过，无 ServiceLocator 残留引用
.\NetShare.exe     # 启动 → 分享功能 / 上传下载 / QML 全部正常
# 退出 → 日志无 crash / double-free
```

---

## 附录 A: 修正后 CivetWebServer 接口设计

```cpp
// src/network/CivetWebServer.h
#ifndef CIVETWEBSERVER_H
#define CIVETWEBSERVER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include <QJsonObject>
#include <functional>
#include "civetweb.h"

struct HttpRequestInfo
{
    QString method;
    QString uri;
    QString queryString;
    QVariantMap headers;
    QByteArray body;
    QString remoteAddress;
};

class CivetWebServer : public QObject
{
    Q_OBJECT

public:
    // ── 路由 handler ──
    using RouteHandler = std::function<int(mg_connection*, const HttpRequestInfo&)>;
    using StreamingHandler = std::function<int(mg_connection*, const HttpRequestInfo&,
                                                const QByteArray& chunk, bool isLast)>;
    // ── WebSocket handler ──
    using WsConnectHandler = std::function<int(const mg_connection*)>;
    using WsReadyHandler   = std::function<void(mg_connection*)>;
    using WsDataHandler    = std::function<int(mg_connection*, int, char*, size_t)>;
    using WsCloseHandler   = std::function<void(const mg_connection*)>;

    explicit CivetWebServer(QObject* parent = nullptr);
    ~CivetWebServer() override;

    // ── 生命周期 ──
    bool start(quint16 port, const QString& bindAddress = "0.0.0.0");
    void stop();
    bool isRunning() const;
    quint16 port() const;

    // ── 路由注册 ──
    void addRoute(const QString& method, const QString& uri, RouteHandler handler);
    void addStreamingRoute(const QString& method, const QString& uri, StreamingHandler handler);
    void setDefaultHandler(RouteHandler handler);

    // ── WebSocket ──
    void enableWebSocket(const QString& path,
                         WsConnectHandler onConnect,
                         WsReadyHandler   onReady,
                         WsDataHandler    onData,
                         WsCloseHandler   onClose);

    // WebSocket 广播（替代原 WebSocketHandler::broadcastToSubscribers）
    void broadcastToSubscribers(const QString& token, const QString& type,
                                const QJsonObject& data);
    void subscribeClient(mg_connection* conn, const QString& token);
    void unsubscribeClient(mg_connection* conn, const QString& token);
    void unsubscribeClientFromAll(mg_connection* conn);
    int connectedClientCount() const;

    // ── TLS ──
    void setSslCertificate(const QString& certPath, const QString& keyPath);
    void setTlsEnabled(bool enabled);

    // ── 静态辅助函数（供 handler 使用） ──
    static void sendJsonResponse(mg_connection* conn, int status, const QByteArray& json);
    static void sendHtmlResponse(mg_connection* conn, int status, const QByteArray& html);
    static void sendFileResponse(mg_connection* conn, const QString& filePath,
                                  const QString& mimeType, const QString& fileName);
    static void sendStreamingFileResponse(mg_connection* conn, const QString& filePath,
                                           const QString& mimeType, const QString& fileName,
                                           const QString& rangeHeader);
    static HttpRequestInfo fromCivetWeb(mg_connection* conn, const mg_request_info* ri);

signals:
    void serverStarted(quint16 port);
    void serverStopped();
    void errorOccurred(const QString& error);
    void streamingConnDisconnected(mg_connection* conn);
    void wsClientConnected(mg_connection* conn, const QString& remoteAddress);
    void wsClientDisconnected(mg_connection* conn);
    void wsMessageReceived(mg_connection* conn, int opCode, const QByteArray& data);

private:
    // ── CivetWeb 静态回调 → this 桥接 ──
    static int staticBeginRequestHandler(mg_connection* conn, void* cbdata);
    static int staticWsConnectHandler(const mg_connection* conn, void* cbdata);
    static void staticWsReadyHandler(mg_connection* conn, void* cbdata);
    static int staticWsDataHandler(mg_connection* conn, int op, char* data, size_t len, void* cbdata);
    static void staticWsCloseHandler(const mg_connection* conn, void* cbdata);

    // ── 内部方法 ──
    void setupHeartbeat();
    void sendHeartbeat();
    void checkHeartbeatTimeout();
    int beginRequestHandler(mg_connection* conn);

    // ── 成员 ──
    mg_context* m_ctx = nullptr;
    quint16 m_port = 0;
    QString m_bindAddress;
    bool m_running = false;
    bool m_tlsEnabled = false;
    QString m_certPath;
    QString m_keyPath;

    struct Route {
        QString method;
        QString uri;
        RouteHandler handler;
    };
    QList<Route> m_routes;
    RouteHandler m_defaultHandler;

    // WebSocket
    WsConnectHandler m_wsConnectHandler;
    WsReadyHandler   m_wsReadyHandler;
    WsDataHandler    m_wsDataHandler;
    WsCloseHandler   m_wsCloseHandler;

    QMap<QString, QSet<mg_connection*>> m_wsClients;  // token → 连接集合
    QMap<mg_connection*, QSet<QString>> m_wsSubscriptions; // 连接 → 已订阅 token 集合
    QMap<mg_connection*, qint64> m_wsLastPong;         // 心跳时间戳

    QTimer* m_heartbeatTimer = nullptr;
};

#endif
```

---

## 附录 B: 修正后 DIContainer 绑定配置

**关键原则**：所有 QObject 派生类不通过 DI 创建实例，main.cpp 照常 `new Xxx(this)`，仅用 `std::ref()` 绑定。DI 仅用于类型安全的服务发现。

```cpp
// src/core/common/DIContainer.h
#ifndef DICONTAINER_H
#define DICONTAINER_H

#include <boost/di.hpp>
#include "core/common/IShareManager.h"
#include "core/common/IFileBrowser.h"
#include "core/common/IFolderPacker.h"
#include "core/share/ShareManager.h"
#include "core/share/FileBrowser.h"
#include "core/share/FolderPacker.h"
#include "core/transfer/FileTransferEngine.h"
#include "core/transfer/ChunkManager.h"
#include "core/transfer/ResumeManager.h"
#include "core/transfer/BandwidthManager.h"
#include "core/transfer/TransferLogService.h"
#include "core/common/SettingsManager.h"
#include "database/DatabaseManager.h"
#include "network/CivetWebServer.h"
#include "network/mDNSService.h"
#include "gui/NotificationManager.h"

namespace di = boost::di;

// 所有模块函数接受外部引用参数，不创建对象
// 调用方式: auto inj = di::make_injector(
//     CoreModule(shareMgr, fileBrowser, folderPacker),
//     TransferModule(engine, chunk, resume, bw),
//     NetworkModule(server, mdns, notif),
//     InfraModule(db, settings, logSvc)
// );

inline auto CoreModule(
    IShareManager& shareMgr,
    IFileBrowser& fileBrowser,
    IFolderPacker& folderPacker)
{
    return di::make_injector(
        di::bind<IShareManager>.to(std::ref(shareMgr)),
        di::bind<IFileBrowser>.to(std::ref(fileBrowser)),
        di::bind<IFolderPacker>.to(std::ref(folderPacker))
    );
}

inline auto TransferModule(
    FileTransferEngine& engine,
    ChunkManager& chunkMgr,
    ResumeManager& resumeMgr,
    BandwidthManager& bwMgr)
{
    return di::make_injector(
        di::bind<FileTransferEngine>.to(std::ref(engine)),
        di::bind<ChunkManager>.to(std::ref(chunkMgr)),
        di::bind<ResumeManager>.to(std::ref(resumeMgr)),
        di::bind<BandwidthManager>.to(std::ref(bwMgr))
    );
}

inline auto NetworkModule(
    CivetWebServer& server,
    mDNSService& mdns,
    NotificationManager& notif)
{
    return di::make_injector(
        di::bind<CivetWebServer>.to(std::ref(server)),
        di::bind<mDNSService>.to(std::ref(mdns)),
        di::bind<NotificationManager>.to(std::ref(notif))
    );
}

inline auto InfraModule(
    DatabaseManager& db,
    SettingsManager& settings,
    TransferLogService& logSvc)
{
    return di::make_injector(
        di::bind<DatabaseManager>.to(std::ref(db)),
        di::bind<SettingsManager>.to(std::ref(settings)),
        di::bind<TransferLogService>.to(std::ref(logSvc))
    );
}

#endif
```

### main.cpp 中使用 pattern

```cpp
// 所有服务对象在各自 init 函数中 new Xxx(this)，保存到成员变量
bool NetShareApplication::initializeAll()
{
    // ... 按原有顺序初始化，但改为保存到成员变量 ...

    // 构建 DI injector（全部用引用绑定）
    m_injector = std::make_unique<di::injector<...>>(
        di::make_injector(
            CoreModule(*m_shareManager, *m_fileBrowser, *m_folderPacker),
            TransferModule(*m_transferEngine, *m_chunkManager, *m_resumeManager, *m_bandwidthManager),
            NetworkModule(*m_civetServer, *m_mdnsService, *m_notificationManager),
            InfraModule(*m_database, *m_settings, *m_transferLog)
        )
    );
}

// 使用
auto* sm = m_injector->create<IShareManager&>();     // 类型安全
auto* db = m_injector->create<DatabaseManager&>();
```

---

## 附录 C: 15 个 Handler 签名迁移对照表

| # | 路由 | 旧签名 | 新签名 | Phase |
|---|------|--------|--------|-------|
| 1 | `GET /` | `void(const HttpRequest&, HttpResponse&)` | `int(mg_connection*, const HttpRequestInfo&)` | 2 |
| 2 | `GET /s/*` | 同上 | 同上 | 2 |
| 3 | `GET /download/*` | 同上 → 流式 | `int(mg_connection*, const HttpRequestInfo&)` | 3 |
| 4 | `GET /folder/*` | 同上 → 流式 | 同上 | 3 |
| 5 | `GET /api/shares` | 同上 | 同上 | 2 |
| 6 | `GET /api/files/*` | 同上 | 同上 | 2 |
| 7 | `GET /receive` | 同上 | 同上 | 2 |
| 8 | `GET /upload/*` | 同上 | 同上 | 2 |
| 9 | `POST /receive` | `void(QTcpSocket*, const HttpRequest&, const QByteArray&, bool)` | `int(mg_connection*, const HttpRequestInfo&, const QByteArray&, bool)` | 3 |
| 10 | `POST /api/upload/check` | `void(const HttpRequest&, HttpResponse&)` | `int(mg_connection*, const HttpRequestInfo&)` | 2 |
| 11 | `POST /api/upload/file` | `void(QTcpSocket*, const HttpRequest&, const QByteArray&, bool)` | `int(mg_connection*, const HttpRequestInfo&, const QByteArray&, bool)` | 3 |
| 12 | `POST /api/upload/finalize` | `void(const HttpRequest&, HttpResponse&)` | `int(mg_connection*, const HttpRequestInfo&)` | 2 |
| 13 | `POST /api/upload/abort` | 同上 | 同上 | 2 |
| 14 | `POST /upload/*` | `void(QTcpSocket*, const HttpRequest&, const QByteArray&, bool)` | `int(mg_connection*, const HttpRequestInfo&, const QByteArray&, bool)` | 3 |
| 15 | `OPTIONS *` | `void(const HttpRequest&, HttpResponse&)` | `int(mg_connection*, const HttpRequestInfo&)` | 2 |

**HTML 辅助函数**（签名不变，在 handler 中通过静态方法发送）：
- `QByteArray generateSharePage(token, filePath, isFolder) const;`
- `QByteArray generatePasswordPage(token) const;`
- `QByteArray generateErrorPage(title, message) const;`
- `QByteArray generateUploadPage(token) const;`

**使用方式**：
```cpp
int RequestHandler::handleSharePage(mg_connection* conn, const HttpRequestInfo& info) {
    // ... token 解析 + 权限验证 ...
    QByteArray html = generateSharePage(token, filePath, isFolder);
    CivetWebServer::sendHtmlResponse(conn, 200, html);
    return 1;
}
```

---

## 附录 D: CMake 集成配置

```cmake
# src/network/CMakeLists.txt

# ── CivetWeb ──
include(FetchContent)
FetchContent_Declare(
    civetweb
    GIT_REPOSITORY https://github.com/civetweb/civetweb.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CIVETWEB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CIVETWEB_INSTALL_EXECUTABLE OFF CACHE BOOL "" FORCE)
set(CIVETWEB_ENABLE_SERVER_EXECUTABLE OFF CACHE BOOL "" FORCE)
set(CIVETWEB_ENABLE_WEBSOCKETS ON CACHE BOOL "" FORCE)
set(CIVETWEB_ENABLE_SSL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(civetweb)

# ── NetShare Network 源码 ──
set(NETSHARE_NETWORK_SOURCES
    CivetWebServer.cpp
    mDNSService.cpp
    RequestHandler.cpp
    StreamingMultipartParser.cpp
    # HttpServer.cpp        ← 移除
    # WebSocketHandler.cpp  ← 移除
)

set(NETSHARE_NETWORK_HEADERS
    CivetWebServer.h
    mDNSService.h
    RequestHandler.h
    StreamingMultipartParser.h
    # HttpServer.h          ← 移除
    # WebSocketHandler.h    ← 移除
)

add_library(NetshareNetwork STATIC
    ${NETSHARE_NETWORK_SOURCES}
    ${NETSHARE_NETWORK_HEADERS}
)

set_target_properties(NetshareNetwork PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)

target_include_directories(NetshareNetwork PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../core
)

target_link_libraries(NetshareNetwork PUBLIC
    Qt6::Core
    Qt6::Network
    Qt6::WebSockets
    NetshareCore
    civetweb    # ← 新增
)
```

```cmake
# src/core/CMakeLists.txt — 修正：移除 ServiceLocator.h
set(NETSHARE_CORE_HEADERS
    common/IShareManager.h
    common/IFileBrowser.h
    common/IFolderPacker.h
    common/NetShareError.h
    # common/ServiceLocator.h   ← 移除
    common/Logger.h
    common/SettingsManager.h
    common/TlsCertificateGenerator.h     # ← 新增
    common/DIContainer.h                 # ← 新增
)
```

---

## 附录 E: Phase 执行依赖图

```
Phase 1 (依赖部署)
  │
  ├──→ Phase 2 (核心路由)
  │      │
  │      ├──→ Phase 3 (流式传输)
  │      │      │
  │      │      └──→ Phase 4 (TLS + WebSocket)  ← 合并原 P4 + P8
  │      │             │
  │      │             └──→ Phase 5 (main.cpp 整合)
  │      │                    │
  │      │                    ├──→ Phase 6 (集成测试)
  │      │                    │      │
  │      │                    │      └──→ Phase 7 (清理)
  │      │                    │
  │      │                    └──→ Phase 9 (Boost.DI) ← 必须 Phase 5 完成后
  │      │
  │      └──→ Phase 8 (QML 简化) ← 完全独立，任意时间可执行
  │
  └──→ Phase 9 前置: Phase 1.6 (boost-di.hpp 下载)


强依赖链: P1 → P2 → P3 → P4 → P5 → P9
独立模块: P8 (QML 简化)
后置模块: P6 (测试) → P7 (清理)

并行机会:
  - Phase 4 结束后: Phase 5 + Phase 8 可并行（不同函数）
  - Phase 1 中: 1.6 (boost-di) 与 1.1-1.5 (CivetWeb) 可并行
```

---

## 附录 F: 工期估算

| Phase | 内容 | 工期 | 前置 |
|-------|------|------|------|
| Phase 1 | 依赖部署与框架搭建 | 1.5 天 | — |
| Phase 2 | 核心路由迁移（15 handler） | 3 天 | P1 |
| Phase 3 | 流式下载与上传迁移 | 3 天 | P2 |
| Phase 4 | TLS 自签 + WebSocket 一体化 | 2.5 天 | P3 |
| Phase 5 | main.cpp 初始化整合 | 1.5 天 | P4 |
| Phase 6 | 集成测试与验收 | 2 天 | P5 |
| Phase 7 | 清理旧代码与文档 | 1 天 | P6 |
| Phase 8 | QML 加载简化 | 0.5 天 | — (独立) |
| Phase 9 | Boost.DI 容器 | 3 天 | P7 |
| **总计** | | **18 工作日** | |

> **说明**: 较 v2.0 增加 2 天 —— Phase 1 从 1 天 → 1.5 天（增加 boost-di 下载），Phase 2 从 2 天 → 3 天（增加 15 handler 逐条适配 + HTML 辅助函数适配），Phase 4 从 2 天 → 2.5 天（增加心跳 + JS URL + broadcastToSubscribers）。其余不变。

| 改进维度 | v2.0 估算 | v3.0 估算 | 差异原因 |
|---------|-----------|-----------|---------|
| CivetWeb HTTP | 12 天 | 14.5 天 | +handler 签名对照表 +心跳 +JS URL |
| TLS 自签 | 0.5 天 | → 合并入 P4 | 消解 C4 冲突 |
| Boost.DI | 3 天 | 3 天 | 不变 |
| QML 简化 | 0.5 天 | 0.5 天 | 不变 |
| **总计** | **16 天** | **18 天** | +2 天确保零遗漏 |

---

> **本文档取代 `CIVETWEB_MIGRATION_PLAN.md` v2.0**。
> 修正项数量: 12 缺失 + 6 冲突 = 18 项，全部已整合入各 Phase 执行步骤。
> 规则合规: 继承 v2.0 附录 D 全部合规验证结果，本版无新增违规。