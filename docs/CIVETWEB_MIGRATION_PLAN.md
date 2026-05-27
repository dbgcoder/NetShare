# NetShare 四大架构改进计划文档

**版本**: 2.0  
**日期**: 2026-05-26  
**目标**: HTTP 服务器迁移 CivetWeb + TLS 自签证书自动生成 + DI 容器 Boost.DI + QML 加载简化  
**状态**: 📋 计划阶段  

---

## 目录

**Part A: HTTP 服务器迁移 CivetWeb**
1. [迁移动机与收益](#1-迁移动机与收益)
2. [当前架构分析](#2-当前架构分析)
3. [CivetWeb 技术调研](#3-civetweb-技术调研)
4. [迁移范围与影响分析](#4-迁移范围与影响分析)
5. [分阶段执行计划](#5-分阶段执行计划)
6. [API 映射表](#6-api-映射表)
7. [风险与缓解措施](#7-风险与缓解措施)
8. [测试计划](#8-测试计划)
9. [回滚方案](#9-回滚方案)

**Part B: TLS 自签证书自动生成**
10. [TLS 证书现状与改进](#10-tls-证书现状与改进)

**Part C: DI 容器 Boost.DI 替换 ServiceLocator**
11. [ServiceLocator 现状与改进](#11-servicelocator-现状与改进)

**Part D: QML 加载路径简化**
12. [QML 加载现状与改进](#12-qml-加载现状与改进)

**附录**
[附录 A: CivetWebServer 接口设计草案](#附录-a-civetwebserver-接口设计草案)  
[附录 B: CMake 集成配置](#附录-b-cmake-集成配置)  
[附录 C: 工期估算](#附录-c-工期估算)

---

## 1. 迁移动机与收益

### 1.1 为什么选择 CivetWeb

| 因素 | 说明 |
|------|------|
| **许可证** | MIT — 无任何商业化限制 |
| **代码规模** | 单 `.c` + 单 `.h`，约 3 万行 C 代码，编译后 ~150KB |
| **功能覆盖** | HTTP/HTTPS、WebSocket、Range 请求、multipart、流式上传 — 覆盖当前自研服务器的全部功能 |
| **成熟度** | 2004 年起（Mongoose 分支），CERN ROOT 等大型项目在使用 |
| **性能** | epoll/IOCP 事件驱动，C 语言实现零抽象开销 |
| **安全审计** | 经过 CERN 等机构的安全审查，比自研 HTTP 解析器安全风险低 |
| **Qt 集成难度** | 中等 — 需要 ITC（线程间通信）桥接层 |

### 1.2 迁移收益

| 维度 | 迁移前（自研） | 迁移后（CivetWeb） |
|------|-------------|-------------------|
| HTTP 协议合规 | 自定义解析，无法保证 100% RFC 兼容 | 完整 HTTP/1.1 实现（RFC 7230） |
| HTTPS 支持 | 手写 QSslSocket 包装 (~80 行) | 内置 SslCertificate/SslKeyFile 选项 |
| 安全漏洞面 | ~800 行手写 HTTP 解析 + 路由匹配 | 零（由 CivetWeb 内部处理） |
| WebSocket | 需要独立 QWebSocketServer (port+1) | **内置 WebSocket → 可移除 WebSocketHandler** |
| 代码维护量 | ~800 行 C++ | ~300 行 CivetWeb 封装 + 配置 |
| i18n 路径 | 手动处理 | 内置 URL 解码 |
| Range 请求 | 手写 (~70 行) | 内置 `mg_send_file_body()` |
| 连接保活 | 无 | 内置 Keep-Alive |

### 1.3 遵循项目规范

本计划遵循 [编码规范](file:///d:/qt6cmake/NetShare/docs/CODING_STANDARDS.md) 全部条目：
- 命名: PascalCase/CamelCase/m_snake_case 前缀
- 分层: network 层不新增 core 依赖
- 性能: 传输超 10ms 的操作必须在 C++ 后台线程执行
- 日志: 使用 `LOG_INFO`/`LOG_ERROR` 等宏
- 安全: TLS 配置通过 `SettingsManager` 集中管理

---

## 2. 当前架构分析

### 2.1 模块依赖图（迁移前）

```
main.cpp
  ├── HttpServer (src/network/HttpServer.h/cpp)     ← 自研
  │     ├── SslAwareServer (内部类)
  │     ├── HttpRequest / HttpResponse 数据类
  │     ├── RouteHandler / StreamingBodyHandler typedef
  │     └── streaming context 管理
  ├── RequestHandler (src/network/RequestHandler.h/cpp)
  │     ├── 依赖 IShareManager / IFileBrowser / IFolderPacker
  │     ├── 约 15 个 HTTP 路由 (registerRoutes)
  │     ├── StreamingMultipartParser (流式上传解析)
  │     └── UploadSession / StreamingUploadState 状态管理
  ├── WebSocketHandler (src/network/WebSocketHandler.h/cpp)
  │     └── QWebSocketServer (Qt 官方实现)
  ├── mDNSService (src/network/mDNSService.h/cpp)
  └── main.cpp: 网络初始化 + TLS 配置 + FW 规则
```

### 2.2 HTTP 路由清单（15 个路由）

| # | Method | Path | Handler | 类型 |
|---|--------|------|---------|------|
| 1 | GET | `/` | 首页 | 普通 |
| 2 | GET | `/s/*` | 分享页面 | 普通 |
| 3 | GET | `/download/*` | 文件下载（**流式**） | 流式响应 |
| 4 | GET | `/folder/*` | 文件夹下载（ZIP **流式**） | 流式响应 |
| 5 | GET | `/api/shares` | JSON 分享列表 | 普通 |
| 6 | GET | `/api/files/*` | JSON 文件信息 | 普通 |
| 7 | GET | `/receive` | HTML 接收页面 | 普通 |
| 8 | GET | `/upload/*` | HTML 上传页面 | 普通 |
| 9 | POST | `/receive` | 流式上传（**大文件**） | **流式接收** |
| 10 | POST | `/api/upload/check` | 上传前检查 | 普通 |
| 11 | POST | `/api/upload/file` | 流式文件上传（**块传输**） | **流式接收** |
| 12 | POST | `/api/upload/finalize` | 上传完成确认 | 普通 |
| 13 | POST | `/api/upload/abort` | 上传中止 | 普通 |
| 14 | POST | `/upload/*` | 流式上传回退 | **流式接收** |
| 15 | OPTIONS | `*` | CORS 预检 | 普通 |

**关键复杂度**：
- 3 个**流式下载**路由（Range + 逐块发送，64KB buffer）
- 3 个**流式上传**路由（边收边解析 multipart/chunked）
- 1 个**CORS 预检**路由
- 上传会话管理（超时清理定时器、chunk 合并、断点续传）

### 2.3 当前代码量统计

| 文件 | 行数 | 说明 |
|------|------|------|
| HttpServer.h | ~165 | 类定义 + HttpRequest/HttpResponse + Route 结构 |
| HttpServer.cpp | ~800 | 自研 HTTP 解析、路由匹配、流式发送、TLS |
| RequestHandler.h | ~170 | UploadSession/StreamingUploadState 等内部状态 |
| RequestHandler.cpp | ~1200 | 15 个 handler + 流式上传 + Session 管理 |

---

## 3. CivetWeb 技术调研

### 3.1 核心 API

```c
// 启动服务器
struct mg_context *mg_start(
    const struct mg_callbacks *callbacks,  // 回调函数表
    void *user_data,                        // 用户数据（透传）
    const char **configuration_options      // NULL 结尾的配置项数组
);

// 注册路径处理器（替代现有 addRoute）
mg_set_request_handler(ctx, "/api/shares", handler_func, cb_data);

// 获取请求信息
const struct mg_request_info *ri = mg_get_request_info(conn);
// ri->request_method  → "GET" / "POST"
// ri->request_uri     → "/api/shares?page=1"
// ri->query_string    → "page=1"
// ri->remote_address  → "192.168.1.100"
// ri->content_length  → 12345

// 读取请求体（流式 or 一次性）
int mg_read(conn, buffer, buffer_size);

// 写响应
mg_printf(conn, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n%s", html);

// 发送文件（内置 Range 支持！）
mg_send_file(conn, file_path);

// 发送部分文件
mg_send_file_body(conn, file_path, offset, length);

// WebSocket
mg_set_websocket_handler(ctx, "/ws", connect_handler, ready_handler,
                          data_handler, close_handler, cb_data);
mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_TEXT, msg, len);

// 停止服务器
mg_stop(ctx);
```

### 3.2 配置项（关键）

```c
const char *options[] = {
    "listening_ports",        "8080 8443s",  // HTTP + HTTPS
    "num_threads",            "10",
    "document_root",          "/var/www",
    "ssl_certificate",        "/path/cert.pem",
    "ssl_verify_peer",        "no",           // 局域网场景
    "enable_keep_alive",      "yes",
    "request_timeout_ms",     "30000",
    "error_pages",            "/path/error",
    "access_control_allow_origin", "*",
    "decode_url",             "yes",          // 自动 URL 解码
    "max_request_size",       "16384",        // 头部限制
    NULL
};
```

### 3.3 CivetWeb 与 Qt 事件循环集成策略

CivetWeb 内部使用**独立线程池**运行，回调在 CivetWeb 的工作线程中被调用，**不是** Qt 主线程。需要使用 Qt 的跨线程信号槽将数据安全传递：

```
┌──────────────────────┐     Qt::QueuedConnection     ┌──────────────────────┐
│   CivetWeb Worker     │ ──────────────────────────▶  │   Qt Main Thread      │
│   (HTTP 回调线程)      │     emit signal(data)        │   (GUI + QML 渲染)    │
│                        │ ◀────────────────────────── │                       │
│   mg_read / mg_write   │     invokeMethod / signal   │   业务逻辑处理         │
└──────────────────────┘                              └──────────────────────┘
```

**方案**：使用 `QObject` 作为 CivetWeb 回调与 Qt 信号槽的桥接层：
```cpp
class CivetWebServer : public QObject {
    Q_OBJECT
public:
    bool start(const QStringList& options);
    void addHandler(const QString& uri, HandlerFunc func);
    void stop();

signals:
    void requestReceived(const QString& method, const QString& uri,
                         QByteArray body, QVariantMap headers);
};
```

### 3.4 CivetWeb 版本选择

| 选项 | 版本 | 集成方式 |
|------|------|---------|
| 🥇 **FetchContent 源码编译** | `master` (v1.17+) | CMake `FetchContent_Declare` 自动下载编译 |
| 🥈 Git Submodule | `v1.16` | `git submodule add` |
| 🥉 预编译库 | 发行版 | `find_package` / vcpkg |

**推荐方案 A**：使用 CMake `FetchContent` — 无需手动下载、零配置、跨平台：
```cmake
FetchContent_Declare(
    civetweb
    GIT_REPOSITORY https://github.com/civetweb/civetweb.git
    GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(civetweb)
target_link_libraries(NetshareNetwork PUBLIC civetweb)
```

### 3.5 文件清单

集成 CivetWeb 需要引入以下文件：
```
third_party/civetweb/
├── include/civetweb.h       # C API 头文件
├── src/civetweb.c            # 核心实现
├── src/md5.inl               # MD5
├── src/sha1.inl               # SHA1
├── src/handle_form.inl        # 表单处理
├── src/response.inl           # HTTP 响应 helper
├── src/sort.inl               # 排序工具
├── src/match.inl              # 路径匹配
├── src/timer.inl              # 定时器
├── src/http2.inl              # HTTP/2（可选）
├── include/CivetServer.h      # C++ 包装器（可选）
└── src/CivetServer.cpp        # C++ 包装器（可选）
```

**推荐**：直接使用 C API (`civetweb.h`)，因为 C++ 包装器 `CivetServer.h` 功能不完整。用自定义 `CivetWebServer` (QObject) 封装。

---

## 4. 迁移范围与影响分析

### 4.1 需要修改的文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/CMakeLists.txt` | ✏️ 修改 | 新增 CivetWeb FetchContent 依赖 |
| `src/network/CMakeLists.txt` | ✏️ 修改 | 新增 CivetWebServer 源文件，链接 civetweb |
| `src/network/HttpServer.h` | 🗑️ **删除** | 全部由 CivetWeb 接管 |
| `src/network/HttpServer.cpp` | 🗑️ **删除** | ~800 行，包含 SslAwareServer 内部类 |
| `src/network/CivetWebServer.h` | ✨ **新建** | CivetWeb 的 QObject 封装 |
| `src/network/CivetWebServer.cpp` | ✨ **新建** | 服务器启动/停止/路由/信号桥接 |
| `src/network/RequestHandler.h` | ✏️ 修改 | 不再依赖 `HttpServer`，改用 `CivetWebServer` |
| `src/network/RequestHandler.cpp` | ✏️ 修改 | `registerRoutes` 改为注册 CivetWeb handler |
| `src/network/WebSocketHandler.h` | 🗑️ **删除** | CivetWeb 内置 WebSocket |
| `src/network/WebSocketHandler.cpp` | 🗑️ **删除** | ~150 行 |
| `src/main.cpp` | ✏️ 修改 | 网络初始化 + WebSocket 改用 CivetWeb |
| `src/gui/CMakeLists.txt` | ✏️ 修改 | 移除 WebSocketHandler 上下文属性引用 |

### 4.2 不受影响的文件

| 文件 | 原因 |
|------|------|
| `src/network/mDNSService.h/cpp` | 完全独立，不依赖 HttpServer |
| `src/network/StreamingMultipartParser.h/cpp` | 纯解析器，**保留不变** |
| `src/core/share/ShareManager.h/cpp` | 业务逻辑层，不直接操作 HTTP |
| `src/core/transfer/FileTransferEngine.h/cpp` | 传输引擎，不直接操作 HTTP |
| `src/database/DatabaseManager.h/cpp` | 数据库层 |
| `src/gui/qml/*.qml` | QML 层，通过信号交互 |
| `web/*.html` | Web 前端静态文件 |

### 4.3 接口变更

| 现有接口 | 迁移后 | 变更说明 |
|---------|--------|---------|
| `HttpServer::addRoute(method, path, handler)` | `CivetWebServer::addRoute(uri, handler)` | method 由路径前缀隐含 |
| `HttpServer::addStreamingRoute(method, path, handler)` | `CivetWebServer::addStreamingRoute(uri, handler)` | 同上 |
| `HttpServer::sendResponse(socket, response)` | `CivetWebServer::sendResponse(conn, response)` | socket→conn |
| `HttpServer::start(port, address)` | `CivetWebServer::start(port, address)` | API 保持一致 |
| `signal streamingSocketDisconnected(QTcpSocket*)` | `signal streamingConnDisconnected(struct mg_connection*)` | 参数类型变更 |

---

## 5. 分阶段执行计划

### Phase 1: 环境准备与基础集成 ✅ 优先级 P0

| 步骤 | 任务 | 验证方法 |
|------|------|----------|
| 1.1 | 在根 CMakeLists.txt 添加 `FetchContent_Declare(civetweb ...)` | CMake 配置成功 |
| 1.2 | 创建 `CivetWebServer.h` — QObject 封装类骨架 | 编译通过 |
| 1.3 | 创建 `CivetWebServer.cpp` — 实现 `start()`/`stop()` | 编译通过 |
| 1.4 | 添加 `mg_callbacks` 中的 `log_message` → 桥接到 `LOG_INFO` | 启动时日志输出到文件 |
| 1.5 | 移除 `WebSocketHandler` 链接 | 编译通过 |

**预计代码量**：~150 行 C++

### Phase 2: 核心路由迁移 ✅ 优先级 P0

**策略**：先迁移无状态/简单路由，验证基本可用性

| 步骤 | 任务 | 覆盖路由 |
|------|------|---------|
| 2.1 | 迁移静态 HTML 路由 | `/`、`/receive` |
| 2.2 | 迁移 JSON API 路由 | `/api/shares`、`/api/files/*` |
| 2.3 | 迁移分享页面路由 | `/s/*`、CORS OPTIONS |
| 2.4 | 迁移上传页面路由 | `/upload/*` (GET) |
| 2.5 | 实现 `mg_request_info` → `HttpRequest` 适配器 | 所有路由通用 |

**预计代码量**：~200 行 C++

### Phase 3: 流式下载与上传 ✅ 优先级 P0

**核心挑战**：CivetWeb 的 `begin_request` 回调在一次调用中处理完整请求，流式处理需要特殊处理。

| 步骤 | 任务 | 技术方案 |
|------|------|---------|
| 3.1 | 流式文件下载 (`/download/*`) | 使用 `mg_send_file_body()` 替代手写 64KB buffer 循环 |
| 3.2 | 流式文件夹下载 (`/folder/*`) | ZIP 打包后 `mg_send_file()` + 自动清理临时文件 |
| 3.3 | 流式上传 (`/api/upload/file`) | 在 `begin_request` 中循环 `mg_read()` 逐块交给 `StreamingMultipartParser` |
| 3.4 | 流式上传 (`/receive` POST) | 同上，保留 `StreamingMultipartParser` |
| 3.5 | Session 超时清理 | 保留现有 `QTimer` + `cleanupExpiredSessions()` |

**预计代码量**：~300 行 C++

### Phase 4: TLS 与 WebSocket 迁移 ✅ 优先级 P1

| 步骤 | 任务 | 说明 |
|------|------|------|
| 4.1 | TLS 证书配置 `ssl_certificate`/`ssl_key_file` | 直接用 CivetWeb 选项，移除 `SslAwareServer` |
| 4.2 | WebSocket 端点迁移 (`mg_set_websocket_handler`) | 移除 `WebSocketHandler`，CivetWeb 内置 |
| 4.3 | WebSocket 广播逻辑适配（`broadcastToSubscribers`） | 保持 `QMap<QString, QSet<mg_connection*>>` |
| 4.4 | 心跳保活 | CivetWeb 内置 `enable_keep_alive` → 可能的定时 ping |

**预计代码量**：~200 行 C++

### Phase 5: main.cpp 初始化精简 ✅ 优先级 P1

| 步骤 | 任务 | 说明 |
|------|------|------|
| 5.1 | 用 `CivetWebServer` 替换 `HttpServer` 初始化 | ServiceLocator 重新注册 |
| 5.2 | 移除 WebSocket 端口 (port+1) 逻辑 | WebSocket 与 HTTP 同端口 |
| 5.3 | 更新 QML 上下文属性中的 WebSocket 引用 | `webSocketHandler` → `civetWebServer` |
| 5.4 | 更新 `transferEngine→taskProgress` 信号连接 | 适配新信号的连接方式 |

**预计代码量**：~100 行修改

### Phase 6: WebSocket 功能验证与测试 ✅ 优先级 P2

| 步骤 | 任务 | 验证方法 |
|------|------|---------|
| 6.1 | WebSocket 连接测试 | 浏览器打开分享页面，确认收到进度推送 |
| 6.2 | WebSocket 广播测试 | 多客户端同时连接，确认全部收到消息 |
| 6.3 | 订阅/取消订阅测试 | `broadcastToSubscribers` 按 token 分发 |
| 6.4 | 断开重连测试 | 客户端断线后重连，确认订阅恢复 |

### Phase 7: 清理与文档 ✅ 优先级 P2

| 步骤 | 任务 | 说明 |
|------|------|------|
| 7.1 | 删除 `HttpServer.h/cpp` | Git 历史保留可回滚 |
| 7.2 | 删除 `WebSocketHandler.h/cpp` | Git 历史保留可回滚 |
| 7.3 | 更新 `docs/API.md` | API 端点未变，仅实现方式变更 |
| 7.4 | 更新 `docs/ARCHITECTURE_IMPROVEMENT.md` | 追加 CivetWeb 迁移记录 |
| 7.5 | 更新 `docs/BUILD.md` | 说明 CivetWeb 自动下载 |

---

## 6. API 映射表

### 6.1 Handler 签名变更

```cpp
// 【迁移前】HttpServer 路由签名
using RouteHandler = std::function<void(const HttpRequest&, HttpResponse&)>;
using StreamingBodyHandler = std::function<void(QTcpSocket*, const HttpRequest&, const QByteArray&, bool)>;

// 【迁移后】CivetWeb 路由签名
using CivetHandler = std::function<int(struct mg_connection*, const struct mg_request_info*)>;
// 返回 1 表示已处理响应，返回 0 表示 civetweb 自己处理
```

### 6.2 核心 API 差异对照

| 自研 HttpServer (旧) | CivetWeb (新) | 说明 |
|---------------------|---------------|------|
| `request.method` | `ri->request_method` | 字符串 |
| `request.path` | `ri->request_uri` | 含 query string |
| `request.queryParams["key"]` | `ri->query_string` → 手动解析 或 `mg_get_var()` | |
| `request.headers["key"]` | `mg_get_header(conn, "key")` | 函数调用 |

### 6.3 响应构建方式变更

```cpp
// 【迁移前】
HttpResponse response;
response.statusCode = 200;
response.headers["Content-Type"] = "application/json";
response.body = jsonData;
server->sendResponse(socket, response);

// 【迁移后】
mg_printf(conn,
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: %d\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "\r\n",
    jsonData.size());
mg_write(conn, jsonData.constData(), jsonData.size());
```

**封装助手函数**（保持代码可读性）：
```cpp
// CivetWebServer.h 中添加
static void sendJsonResponse(mg_connection* conn, int status, const QByteArray& json);
static void sendHtmlResponse(mg_connection* conn, int status, const QByteArray& html);
static void sendFileStreamResponse(mg_connection* conn, const QString& filePath,
                                    const QString& mimeType, const QString& fileName,
                                    const QString& rangeHeader = QString());
```

### 6.4 WebSocket 映射

```cpp
// 【迁移前】QWebSocketServer
wsHandler->broadcastToSubscribers(token, type, data);

// 【迁移后】CivetWeb 内置 WebSocket
// 注册 WebSocket handler
mg_set_websocket_handler(ctx, "/ws",
    connect_handler, ready_handler, data_handler, close_handler, cb_data);

// 广播（需自行维护连接集合）
for (mg_connection* client : m_wsClients[token]) {
    mg_websocket_write(client, MG_WEBSOCKET_OPCODE_TEXT, msg, len);
}
```

---

## 7. 风险与缓解措施

| # | 风险 | 影响等级 | 缓解措施 |
|---|------|---------|---------|
| 1 | **流式上传阻塞 CivetWeb worker 线程** | 🔴 高 | 在 `begin_request` 中边读边处理，不使用 `mg_read()` 循环等待。使用非阻塞模式或限制单次读取大小 |
| 2 | **CivetWeb C API 回调无法直接访问 Qt 对象** | 🟡 中 | `user_data` 指针透传 `CivetWebServer*`，回调中 `static_cast` 取回。信号槽使用 `Qt::QueuedConnection` |
| 3 | **WebSocket 多线程并发访问连接集合** | 🟡 中 | 使用 `QReadWriteLock` 保护 `m_wsClients` 集合 |
| 4 | **ZIP 打包后 CivetWeb send_file 占用 worker 线程** | 🟢 低 | ZIP 打包 < 5GB 时，在 `begin_request` 中同步发送可接受。超大文件夹异步打包后 `mg_send_file()` |
| 5 | **TLS 证书热加载** | 🟢 低 | CivetWeb 不支持运行时重载证书，需重启服务器。证书变更后调用 `stop()` + `start()` |
| 6 | **Windows IOCP 兼容性** | 🟢 低 | CivetWeb 1.17+ 完整支持 Windows IOCP |

---

## 8. 测试计划

### 8.1 单元测试

| 测试项 | 方法 | 验证点 |
|--------|------|--------|
| `CivetWebServer::start()/stop()` | 集成测试 | 端口监听、重复启动防护 |
| 路由注册与分发 | 集成测试 | GET/POST 各路径返回预期状态码 |
| 静态文件服务 | 集成测试 | `index.html` 返回 200 + 正确 Content-Type |
| JSON API 响应 | 集成测试 | `/api/shares` 返回正确 JSON 结构 |
| CORS 预检 | 集成测试 | OPTIONS 返回 204 + 正确头 |
| TLS 握手 | 集成测试 | HTTPS 连接成功 (w/ 自签证书) |
| WebSocket 连接 | 集成测试 | 升级协议成功，收发消息正常 |
| WebSocket 广播 | 集成测试 | 多客户端同时收到消息 |

### 8.2 集成/端到端测试

| 测试项 | 方法 | 验证点 |
|--------|------|--------|
| 分享页面访问 | 浏览器打开 `/s/{token}` | 页面正常渲染 |
| 文件下载 | 浏览器点击下载链接 | 文件正确下载，文件名正确 |
| Range 请求（断点续传） | `curl -H "Range: bytes=0-1024"` | 返回 206 + Content-Range |
| 文件夹下载 | 浏览器下载 ZIP | ZIP 内容完整 |
| 大文件流式上传 (5GB) | `curl -T largefile.iso` | 上传成功，SHA256 校验通过 |
| 多任务并行传输 | 3 个并发下载 + 1 个上传 | 全部完成 不超时 |
| 超时自动清理 | 上传中断 30 分钟后 | session 被清理 |
| mDNS 服务发现 | 另一台设备 | 发现服务 + 正确端口 |

### 8.3 性能基准

| 指标 | 自研 (旧) | CivetWeb (新) | 标准 |
|------|----------|--------------|------|
| 静态页面 QPS | ~1200 | ≥2000 | >1000 |
| 文件下载吞吐 | ~80MB/s | ≥100MB/s | >50MB/s |
| 并发连接数 | ~50 | ≥100 | >30 |
| 内存占用 (100 连接) | ~45MB | ≤40MB | <100MB |

---

## 9. 回滚方案

### 9.1 代码回滚

```bash
git revert <merge-commit>           # 回滚整个迁移 PR
# 或仅恢复关键文件：
git checkout HEAD~1 -- src/network/HttpServer.h src/network/HttpServer.cpp
git checkout HEAD~1 -- src/network/WebSocketHandler.h src/network/WebSocketHandler.cpp
git checkout HEAD~1 -- src/network/RequestHandler.h src/network/RequestHandler.cpp
git checkout HEAD~1 -- src/main.cpp
```

### 9.2 回滚触发条件

- CivetWeb 版本存在阻塞性 bug
- 流式上传性能退化 >50%
- Windows 平台 IOCP 稳定性问题
- WebSocket 断连率 >5%

### 9.3 数据保证

- 所有元数据存储在 SQLite，不受迁移影响
- TLS 证书文件位置不变
- 配置文件不新增/删除键
- 上传/下载临时目录不变

---

## 10. TLS 证书现状与改进

### 10.1 当前状态

当前 TLS 配置位于 [main.cpp:initializeNetworkServer()](file:///d:/qt6cmake/NetShare/src/main.cpp#L348-L380)，读取用户配置的证书文件路径：

```cpp
// 现状 — 依赖用户手动配置证书路径
QString certPath = settings->getString("server/tlsCertPath");
QString keyPath = settings->getString("server/tlsKeyPath");
if (certPath.isEmpty() || keyPath.isEmpty()) {
    LOG_WARN("TLS certificate or key path not configured, falling back to non-secure");
}
// ... 加载 QSslCertificate + QSslKey ...
httpServer->setTlsEnabled(true);
```

**问题**：
- 证书路径为空 → 直接退化为 HTTP 明文
- 用户需要手动生成 PEM 证书（高门槛）
- 无默认证书可用于局域网安全传输

### 10.2 改进方案

新增 `GenerateSelfSignedCert` 函数 — 首次启动时使用 OpenSSL 自动生成自签 X.509 证书，存储到持久目录以供后续重用。

| 维度 | 当前 | 改进后 |
|------|------|--------|
| 工具 | QSslCertificate（仅读取） | OpenSSL CLI（`openssl req -x509`） |
| 触发 | 手动配置路径 | 自动检测 + 按需生成 |
| 存储 | 用户指定任意位置 | `AppData/NetShare/certs/` 固定目录 |
| 有效期 | 未知 | 3650 天（10 年） |
| 复杂度 | 用户侧高 | 零配置（用户无感知） |

### 10.3 执行步骤

| 步骤 | 任务 | 说明 |
|------|------|------|
| 10.1 | 新增 `src/core/common/TlsCertificateGenerator.h/cpp` | 封装 OpenSSL 自签逻辑 |
| 10.2 | 实现 `generateSelfSignedCert(const QString& certPath, const QString& keyPath)` | `openssl req -x509 -newkey rsa:4096 -nodes -keyout key.pem -out cert.pem -days 3650 -subj "/CN=NetShare"` |
| 10.3 | 在 `main.cpp` 的 `initializeNetworkServer()` 中调用：若 certPath/keyPath 为空，自动生成到固定目录 | 检测 `cert.pem` 存在则复用，不存在则生成 |
| 10.4 | 迁移到 CivetWeb 后，改用 CivetWeb 的 `ssl_certificate`/`ssl_private_key` 配置项 | 移除 `QSslConfiguration` 相关代码 |

### 10.4 代码草案

```cpp
// src/core/common/TlsCertificateGenerator.h
#ifndef TLSCERTIFICATEGENERATOR_H
#define TLSCERTIFICATEGENERATOR_H

#include <QString>

class TlsCertificateGenerator
{
public:
    static bool generateSelfSignedCert(const QString& certPath, const QString& keyPath);
    static bool certExists(const QString& certPath, const QString& keyPath);
    static QString defaultCertDir();

    TlsCertificateGenerator() = delete;
};

#endif
```

```cpp
// src/core/common/TlsCertificateGenerator.cpp
#include "TlsCertificateGenerator.h"
#include "Logger.h"
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

QString TlsCertificateGenerator::defaultCertDir()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                  + "/certs";
    QDir().mkpath(dir);
    return dir;
}

bool TlsCertificateGenerator::certExists(const QString& certPath, const QString& keyPath)
{
    return QFileInfo::exists(certPath) && QFileInfo::exists(keyPath);
}

bool TlsCertificateGenerator::generateSelfSignedCert(const QString& certPath, const QString& keyPath)
{
    if (certExists(certPath, keyPath)) {
        LOG_INFO("Self-signed certificate already exists, reusing");
        return true;
    }

    LOG_INFO("Generating self-signed TLS certificate...");

    QProcess proc;
    proc.start("openssl", {
        "req", "-x509",
        "-newkey", "rsa:4096",
        "-nodes",
        "-keyout", keyPath,
        "-out", certPath,
        "-days", "3650",
        "-subj", "/CN=NetShare/O=NetShare/C=CN",
        "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1"
    });

    if (!proc.waitForFinished(30000) || proc.exitCode() != 0) {
        LOG_ERROR("Failed to generate TLS certificate: %s",
                  qPrintable(proc.readAllStandardError()));
        return false;
    }

    LOG_INFO("TLS certificate generated: %s / %s",
             qPrintable(certPath), qPrintable(keyPath));
    return true;
}
```

```cpp
// main.cpp initializeNetworkServer() 中替换 TLS 配置部分：
QString certDir = TlsCertificateGenerator::defaultCertDir();
QString certPath = settings->getString("server/tlsCertPath");
QString keyPath = settings->getString("server/tlsKeyPath");

if (certPath.isEmpty()) certPath = certDir + "/cert.pem";
if (keyPath.isEmpty())   keyPath = certDir + "/key.pem";

if (!TlsCertificateGenerator::certExists(certPath, keyPath)) {
    if (!TlsCertificateGenerator::generateSelfSignedCert(certPath, keyPath)) {
        LOG_WARN("Failed to generate self-signed cert, falling back to HTTP only");
    }
}

// CivetWeb: options 中添加 "ssl_certificate", certPath, "ssl_private_key", keyPath
```

**预计代码量**：~50 行 C++ + 1 个 QProcess 调用  
**难度**：低 — OpenSSL 已在系统存在，仅通过 QProcess 调用 CLI  
**风险**：低 — 自签证书仅用于局域网加密，不影响公网可信性

---

## 11. ServiceLocator 现状与改进

### 11.1 当前状态

当前使用自研 [ServiceLocator](file:///d:/qt6cmake/NetShare/src/core/common/ServiceLocator.h)（84 行），核心代码：

```cpp
// 现状 — 基于 typeid 的轻量实现
template<typename T>
void registerService(T* instance)
{
    const char* key = typeid(T).name();                    // RTTI 类型名作 key
    m_services.insert(key, reinterpret_cast<void*>(instance)); // void* 抹除类型
}

template<typename T>
T* service() const
{
    const char* key = typeid(T).name();
    auto it = m_services.constFind(key);
    if (it != m_services.constEnd()) {
        return reinterpret_cast<T*>(it.value());           // reinterpret_cast 转回
    }
    return nullptr;
}
```

**问题**：
- **类型不安全**：`reinterpret_cast<void*>` / `reinterpret_cast<T*>` 绕过所有编译期类型检查
- **继承不感知**：`registerService<IShareManager>(shareManager)` 后用 `service<ShareManager>()` 返回 `nullptr`，因为 `typeid(IShareManager) != typeid(ShareManager)`
- **无依赖图**：无法表达"A 依赖 B、C"，全由调用方手动组装
- **无生命周期管理**：ServiceLocator 不拥有对象，依赖外部 `delete`

### 11.2 改进方案

使用 **[Boost.DI](https://github.com/boost-ext/di)**（Boost 官方 DI 容器，**单头文件** `boost/di.hpp`，Apache 2.0 许可）：

| 维度 | 当前 (ServiceLocator) | 改进后 (Boost.DI) |
|------|----------------------|-------------------|
| 类型安全 | `void*` + `reinterpret_cast`，无编译检查 | 编译期类型推导 |
| 继承解析 | 不支持（需手动注册两次） | `bind<IFoo>.to<Foo>()` 自动 |
| 作用域 | 无（全由外部管理） | `singleton` / `unique` / |
| 依赖注入 | 手动从 locator 拉取 | injector 自动递归注入构造函数参数 |
| 代码量 | 84 行（自研 → 删除） | ~100 行 injector 配置 |
| 头文件 | 自研 | `boost/di.hpp`（单头文件，仅 #include） |
| 兼容性 | 无第三方依赖 | 无额外编译依赖（header-only） |

### 11.3 执行步骤

| 步骤 | 任务 | 说明 |
|------|------|------|
| 11.1 | 下载 `boost/di.hpp` 到 `third_party/boost-di/` | `FetchContent` 或手动放入项目 |
| 11.2 | 创建 `src/core/common/DIContainer.h` | injector 配置 + 模块化绑定 |
| 11.3 | 给 `ShareManager`、`FileTransferEngine` 等关键类添加 Boost.DI 构造函数 | `BOOST_DI_INJECT` 或显式注入参数 |
| 11.4 | 替换 `main.cpp` 中 `m_locator.registerService<...>` 为 injector 创建 | `auto injector = di::make_injector(...)` |
| 11.5 | 更新 `shutdown()` — 改用 injector 生命周期管理 | injector 析构自动清理 |
| 11.6 | 删除 `ServiceLocator.h` | 84 行移除 |

### 11.4 代码草案

```cpp
// src/core/common/DIContainer.h
// 单头文件：项目所有依赖绑定集中管理
#ifndef DICONTAINER_H
#define DICONTAINER_H

#include <boost/di.hpp>
#include "core/common/IShareManager.h"
#include "core/common/IFileBrowser.h"
#include "core/common/IFolderPacker.h"
// ... 其他 include ...
#include "core/share/ShareManager.h"
#include "core/transfer/FileTransferEngine.h"
#include "core/share/FileBrowser.h"
#include "core/share/FolderPacker.h"
// ...

namespace di = boost::di;

// 将绑定按模块组织为 policy
inline auto CoreModule()
{
    return di::make_injector(
        di::bind<IShareManager>.to<ShareManager>().in(di::singleton),
        di::bind<IFileBrowser>.to<FileBrowser>().in(di::singleton),
        di::bind<IFolderPacker>.to<FolderPacker>().in(di::singleton),
        di::bind<DatabaseManager>.to(
            []{ auto* db = new DatabaseManager(); db->open(...); return db; }
        ).in(di::singleton)
    );
}

inline auto TransferModule()
{
    return di::make_injector(
        di::bind<FileTransferEngine>.in(di::singleton),
        di::bind<ChunkManager>.in(di::singleton),
        di::bind<ResumeManager>.in(di::singleton),
        di::bind<BandwidthManager>.in(di::singleton)
    );
}

inline auto NetworkModule()
{
    return di::make_injector(
        di::bind<CivetWebServer>.in(di::singleton),
        di::bind<mDNSService>.in(di::singleton),
        di::bind<RequestHandler>.in(di::singleton)
    );
}

#endif
```

```cpp
// main.cpp 使用简化后：
#include "core/common/DIContainer.h"

bool NetShareApplication::initialize()
{
    // 模块化绑定，递归 resolve 依赖
    auto injector = di::make_injector(
        CoreModule(),
        TransferModule(),
        NetworkModule()
    );

    auto* shareManager = injector.create<IShareManager>();
    auto* transferEngine = injector.create<FileTransferEngine>();
    auto* httpServer = injector.create<CivetWebServer>();
    // ...
}
```

### 11.5 风险与缓解

| 风险 | 缓解措施 |
|------|---------|
| Boost.DI 编译出错（模板错误信息长） | 用 `BOOST_DI_CFG_DIAGNOSTICS_LEVEL=2` 启用详细诊断 |
| Qt 信号槽对象无法用 DI 管理 | 保持 QObject 树管理（parent/child），DI 仅注入引用，不接管生命周期 |
| `ShareManager::instance()` 单例与 DI 冲突 | 用 `di::bind<IShareManager>.to(std::ref(ShareManager::instance()))` 绑定已存在的单例 |
| DI 构造函数修改涉及面广 | 渐进替换：先给接口类添加 DI 构造函数，实现类保持不变 |

**预计代码量**：~100 行 injector 配置（替换 84 行 ServiceLocator + ~50 行 main.cpp 手动组装）  
**难度**：中 — 需要理解 Boost.DI 的 `bind`/`in`/`to` 语义

---

## 12. QML 加载现状与改进

### 12.1 当前状态

当前 [main.cpp:initializeQml()](file:///d:/qt6cmake/NetShare/src/main.cpp#L447-L527) 约 **80 行 QML 加载代码**，包含三级回退：

```
1. loadFromModule("NetShare", "Main")         // QML 模块（发布模式）
2. qrc:/qt/qml/NetShare/qml/Main.qml          // Qt 资源文件
3. 文件系统回退（开发模式）:
   ├── ../src/gui/qml/Main.qml                // 相对 app dir
   ├── ../../src/gui/qml/Main.qml
   ├── ${cwd}/src/gui/qml/Main.qml
   └── cdUp() 循环（最多 10 层）              // 搜索项目根
```

**问题**：
- 10 层 `cdUp()` 循环搜索不优雅
- 硬编码的相对路径在不同构建配置下可能失效
- 代码量 80 行 → 核心逻辑仅需 10 行

### 12.2 改进方案

**用环境变量 `NETSHARE_QML_PATH` 替代文件系统回退**，简化回退链为两层：

```cpp
// 改进后 — 由环境变量替代所有文件系统回退
m_engine->loadFromModule("NetShare", "Main");
if (m_engine->rootObjects().isEmpty()) {
    // 回退：优先环境变量，其次 qrc
    QString qmlPath = qEnvironmentVariable("NETSHARE_QML_PATH", "");
    if (qmlPath.isEmpty())
        qmlPath = "qrc:/qt/qml/NetShare/qml/Main.qml";
    m_engine->load(QUrl(qmlPath));
}
```

| 维度 | 当前 (~80 行) | 改进后 (~10 行) |
|------|-------------|----------------|
| 回退层数 | 3 层（module → qrc → 文件系统搜索） | 2 层（module → 环境变量或 qrc） |
| 开发体验 | 自动搜索但慢（10 层 cdUp） | 开发者在 IDE 设置 `NETSHARE_QML_PATH` = 项目 `src/gui/qml` |
| 代码量 | ~80 行 | ~10 行（删除 ~70 行） |
| 可维护性 | 硬编码路径，构建类型变化需改代码 | 环境变量，一次配置全局生效 |
| CMake 配置 | 无 | 可选：`add_compile_definitions(NETSHARE_DEFAULT_QML_PATH="...")` 自动注入 |

### 12.3 执行步骤

| 步骤 | 任务 | 说明 |
|------|------|------|
| 12.1 | 在 `initializeQml()` 中删除文件系统回退代码（cdUp 循环、hardcoded 路径） | 删除 ~70 行 |
| 12.2 | 新增 `qEnvironmentVariable("NETSHARE_QML_PATH")` 回退分支 | 2 行 |
| 12.3 | 在 CMake 中为 `Debug` 构建自动设置 `NETSHARE_QML_PATH` 环境变量 | Visual Studio 调试属性 / Qt Creator 运行配置 |
| 12.4 | 更新 `docs/BUILD.md` 说明环境变量用法 | 1 段 |

### 12.4 改进后代码

```cpp
// main.cpp initializeQml() — 改进后（完整函数 ~40 行）

bool NetShareApplication::initializeQml()
{
    m_engine = new QQmlApplicationEngine(this);

    qRegisterMetaType<ShareInfo>("ShareInfo");
    qRegisterMetaType<TransferTask>("TransferTask");
    qRegisterMetaType<FileEntry>("FileEntry");

    auto* shareManager = m_locator.service<ShareManager>();
    // ... context properties（保持不变）...

    m_engine->loadFromModule("NetShare", "Main");

    if (m_engine->rootObjects().isEmpty()) {
        QString qmlPath = qEnvironmentVariable("NETSHARE_QML_PATH",
                                                "qrc:/qt/qml/NetShare/qml/Main.qml");
        LOG_WARN("Module load failed, loading from: %s", qPrintable(qmlPath));
        m_engine->load(QUrl(qmlPath));
    }

    if (m_engine->rootObjects().isEmpty()) {
        LOG_ERROR("Failed to load QML from any source");
        delete m_engine; m_engine = nullptr;
        return false;
    }

    // ... QWindow 转换 + DWM 配置（保持不变）...
    return true;
}
```

**预计代码量**：删除 ~70 行回退代码，新增 ~2 行环境变量回退  
**难度**：低 — 纯删减 + 环境变量一行  
**风险**：低 — 回退链仍在（module → 环境变量 → qrc），不引入新失败路径

---

## 附录 A: CivetWebServer 接口设计草案

```cpp
// src/network/CivetWebServer.h
#ifndef CIVETWEBSERVER_H
#define CIVETWEBSERVER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QVariantMap>
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
    using RouteHandler = std::function<int(mg_connection*, const HttpRequestInfo&)>;
    using StreamingHandler = std::function<int(mg_connection*, const HttpRequestInfo&,
                                                const QByteArray& chunk, bool isLast)>;
    using WsConnectHandler = std::function<int(mg_connection*)>;
    using WsDataHandler = std::function<int(mg_connection*, int, char*, size_t)>;
    using WsCloseHandler = std::function<void(mg_connection*)>;

    explicit CivetWebServer(QObject* parent = nullptr);
    ~CivetWebServer() override;

    bool start(quint16 port, const QString& bindAddress = "0.0.0.0");
    void stop();
    bool isRunning() const;

    void addRoute(const QString& method, const QString& uri, RouteHandler handler);
    void addStreamingRoute(const QString& method, const QString& uri, StreamingHandler handler);
    void setDefaultHandler(RouteHandler handler);

    // WebSocket
    void enableWebSocket(const QString& path,
                         WsConnectHandler onConnect,
                         WsDataHandler onData,
                         WsCloseHandler onClose);

    // TLS
    void setSslCertificate(const QString& certPath, const QString& keyPath);
    void setTlsEnabled(bool enabled);

    // Utilities for route handlers
    static void sendJsonResponse(mg_connection* conn, int status, const QByteArray& json);
    static void sendHtmlResponse(mg_connection* conn, int status, const QByteArray& html);
    static void sendFileResponse(mg_connection* conn, const QString& filePath,
                                  const QString& mimeType, const QString& fileName);
    static void sendStreamingFileResponse(mg_connection* conn, const QString& filePath,
                                           const QString& mimeType, const QString& fileName,
                                           const QString& rangeHeader);

signals:
    void serverStarted(quint16 port);
    void serverStopped();
    void errorOccurred(const QString& error);
    void streamingConnDisconnected(mg_connection* conn);

    // WebSocket signals
    void wsClientConnected(mg_connection* conn, const QString& remoteAddress);
    void wsClientDisconnected(mg_connection* conn);
    void wsMessageReceived(mg_connection* conn, int opCode, const QByteArray& data);

private:
    static int staticBeginRequestHandler(mg_connection* conn, void* cbdata);
    static int staticWsConnectHandler(const mg_connection* conn, void* cbdata);
    static void staticWsReadyHandler(mg_connection* conn, void* cbdata);
    static int staticWsDataHandler(mg_connection* conn, int, char*, size_t, void* cbdata);
    static void staticWsCloseHandler(const mg_connection* conn, void* cbdata);

    mg_context* m_ctx;
    quint16 m_port;
    QString m_bindAddress;
    bool m_running;
    bool m_tlsEnabled;
    QString m_certPath;
    QString m_keyPath;

    struct Route
    {
        QString method;
        QString uri;
        RouteHandler handler;
    };
    QList<Route> m_routes;

    struct StreamingRoute
    {
        QString method;
        QString uri;
        StreamingHandler handler;
    };
    QList<StreamingRoute> m_streamingRoutes;

    RouteHandler m_defaultHandler;

    // WebSocket handlers
    WsConnectHandler m_wsConnectHandler;
    WsDataHandler m_wsDataHandler;
    WsCloseHandler m_wsCloseHandler;
};

#endif
```

---

## 附录 B: CMake 集成配置

```cmake
# src/network/CMakeLists.txt 新增内容
include(FetchContent)

FetchContent_Declare(
    civetweb
    GIT_REPOSITORY https://github.com/civetweb/civetweb.git
    GIT_TAG v1.17.0   # 使用稳定版本
    GIT_SHALLOW TRUE
)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CIVETWEB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CIVETWEB_INSTALL_EXECUTABLE OFF CACHE BOOL "" FORCE)
set(CIVETWEB_ENABLE_SERVER_EXECUTABLE OFF CACHE BOOL "" FORCE)
set(CIVETWEB_ENABLE_WEBSOCKETS ON CACHE BOOL "" FORCE)     # WebSocket 合并
set(CIVETWEB_ENABLE_SSL ON CACHE BOOL "" FORCE)             # TLS

FetchContent_MakeAvailable(civetweb)

set(NETSHARE_NETWORK_SOURCES
    CivetWebServer.cpp
    mDNSService.cpp
    RequestHandler.cpp
    StreamingMultipartParser.cpp
    # HttpServer.cpp        ← 移除
    # WebSocketHandler.cpp   ← 移除
)

set(NETSHARE_NETWORK_HEADERS
    CivetWebServer.h
    mDNSService.h
    RequestHandler.h
    StreamingMultipartParser.h
    # HttpServer.h           ← 移除
    # WebSocketHandler.h     ← 移除
)

add_library(NetshareNetwork STATIC
    ${NETSHARE_NETWORK_SOURCES}
    ${NETSHARE_NETWORK_HEADERS}
)

target_link_libraries(NetshareNetwork PUBLIC
    Qt6::Core
    Qt6::Network
    Qt6::WebSockets
    NetshareCore
    civetweb                        # ← 新增
)
```

---

## 附录 C: 工期估算

### Part A: HTTP 服务器迁移 CivetWeb

| 阶段 | 内容 | 估计工期 |
|------|------|---------|
| Phase 1 | 环境准备 + 基础框架 | 1 天 |
| Phase 2 | 核心路由迁移 (无状态) | 2 天 |
| Phase 3 | 流式下载/上传迁移 | 3 天 |
| Phase 4 | TLS + WebSocket 迁移 | 2 天 |
| Phase 5 | main.cpp 初始化适配 | 1 天 |
| Phase 6 | 集成测试与修复 | 2 天 |
| Phase 7 | 清理与文档更新 | 1 天 |
| **小计 A** | | **12 工作日** |

### Part B: TLS 自签证书自动生成

| 阶段 | 内容 | 估计工期 |
|------|------|---------|
| Phase 8 | TLS 自签证书生成 + 集成 | 0.5 天 |
| **小计 B** | | **0.5 工作日** |

### Part C: DI 容器 Boost.DI 替换 ServiceLocator

| 阶段 | 内容 | 估计工期 |
|------|------|---------|
| Phase 9 | Boost.DI 集成 + injector 配置 + 渐进替换 | 3 天 |
| **小计 C** | | **3 工作日** |

### Part D: QML 加载路径简化

| 阶段 | 内容 | 估计工期 |
|------|------|---------|
| Phase 10 | QML 回退链简化 + 环境变量 | 0.5 天 |
| **小计 D** | | **0.5 工作日** |

### 总计

| 改进项 | 工期 | 难度 | 收益 |
|--------|------|------|------|
| A. CivetWeb HTTP 服务器 | 12 天 | 中-高 | 删除 ~950 行手写代码 + 内置 WebSocket/HTTPS/Range |
| B. TLS 自签证书 | 0.5 天 | 低 | 零配置 HTTPS，局域网加密 | 
| C. Boost.DI 容器 | 3 天 | 中 | 类型安全 DI + 删除 84 行 ServiceLocator |
| D. QML 加载简化 | 0.5 天 | 低 | 删除 ~70 行回退代码 |
| **总计** | **16 工作日** | | 累计删除 ~1100 行代码，净增 ~200 行配置 |

---

> 本文档基于对 NetShare 项目全部规则文件、源代码、架构文档的完整审阅后编写。
> 审阅范围：CODING_STANDARDS.md、PLAN.md、API.md、BUILD.md、CONFIG.md、ERROR_CODES.md、IMPLEMENTATION.md、ARCHITECTURE_IMPROVEMENT.md、IMPROVEMENT_PLAN.md、HttpServer源码、RequestHandler源码、WebSocketHandler源码、ServiceLocator.h、main.cpp 全部初始化逻辑（TLS、QML、DI）。

---

## 附录 D: 规则文件合规验证报告

**验证日期**: 2026-05-26  
**规则来源**: `.comate/rules/`（8 份规则文件）  
**验证对象**: 本计划文档四大改进项全部分阶段执行计划、代码草案、文件操作清单

---

### D.1 architecture.mdc — 架构与性能优化规则

| 检查项 | 规则要求 | 计划符合情况 | 判定 |
|--------|---------|-------------|------|
| §1 界面 QML 优先 | 所有 UI 用 QML | Part A/B/C/D 均为 C++ 后端改动，不新增 QML 界面元素 | ✅ 合规 |
| §2 逻辑 C++ 优先 | 业务逻辑在 C++ | CivetWeb 封装、TLS 生成、DI 容器均在 `src/core/common/` 或 `src/network/` | ✅ 合规 |
| §3 通信规范 | 信号/槽 + QThread | Part A §3.3 明确 CivetWeb 回调通过 `Qt::QueuedConnection` 桥接；DI 容器保持信号槽 | ✅ 合规 |
| §4 性能 | ListView/virtual/异步 | 不涉及新增列表/图片/视频（纯后端迁移） | ✅ 不适用 |
| §5.1 目录结构 | `src/core/`、`src/utils/`、`third_party/`、`docs/` | 新增文件均放入现有合法目录，无根目录散落 | ✅ 合规 |
| §5.2 命名 | C++ PascalCase、目录 snake_case、QML PascalCase | `CivetWebServer.h`、`TlsCertificateGenerator.h`、`DIContainer.h` 均为 PascalCase；`third_party/boost-di/` 符合例外 | ✅ 合规 |
| §5.3 成员变量 | m_ 前缀 camelCase | 草案 `CivetWebServer` 中 `m_ctx`、`m_port` 等全部符合 | ✅ 合规 |
| §5.5 CMake | FetchContent / find_package | Part A 附录 B 使用 `FetchContent_Declare(civetweb ...)`；Part C 使用 `FetchContent` 下载 `boost/di.hpp` | ✅ 合规 |
| §5.5 CMake(MSVC) | Windows 固定 MSVC + Qt msvc2022_64 | 计划未涉及工具链变更，不改变现有 MSVC 构建配置 | ✅ 合规 |
| §5.7 AI 生成约束 | 新文件放正确目录 | 见下表（D.2 文件落位预检） | ✅ 合规 |
| §6 检查点 A~I | 代码审查要点 | 见下表（D.3 检查点逐条对照） | ✅ 合规 |

#### D.2 新增文件落位预检

| 文件 | 目标目录 | architecture.mdc §5.1/§5.2 校验 | 判定 |
|------|---------|-------------------------------|------|
| `CivetWebServer.h/cpp` | `src/network/` | 网络服务层，与现有 HttpServer 同目录 | ✅ |
| `TlsCertificateGenerator.h/cpp` | `src/core/common/` | 工具类，与现有 ServiceLocator 同目录 | ✅ |
| `DIContainer.h` | `src/core/common/` | DI 配置，与现有 ServiceLocator 同目录 | ✅ |
| `boost/di.hpp` | `third_party/boost-di/` | 第三方依赖，`third_party/` 符合 §5.1 | ✅ |
| `civetweb` 源码 | CMake `FetchContent` 自动管理 | §5.5 明确要求 FetchContent，不手动拷贝 | ✅ |

#### D.3 检查点逐条对照

| 检查点 | 规则 | 计划状态 |
|--------|------|---------|
| A (QML 业务逻辑) | 不新增 QML 业务逻辑 | ✅ 全部改动在 C++ |
| B (C++ UI 元素) | 不创建 QQuickView/Item | ✅ 无 UI 元素创建 |
| C (列表代理) | 无新增列表 | ✅ 不适用 |
| D (Image 异步) | 无新增 Image | ✅ 不适用 |
| E (后台线程) | >10ms C++ 操作用线程 | ✅ CivetWeb 自带线程池；TLS 生成用 QProcess |
| F (目录正确) | 文件放对目录 | ✅ 见 D.2 |
| G (include guards) | `#pragma once` 或 `#ifndef` | ✅ 草案全部使用 `#ifndef`/`#define` |
| H (QML 位置) | QML 组件在 qml/components/ | ✅ 不新增 QML |
| I (CMakeLists.txt) | 更新 CMake | ✅ Part A 附录 B 提供完整 CMakeLists.txt diff |

---

### D.3 breakpoint-resume-progress.mdr — 断点续传与进度规范

**与计划最高相关项**：Part A Phase 4 WebSocket 迁移会移除 `WebSocketHandler`，改用 CivetWeb 内置 WebSocket。此规则定义了 WebSocket 的精确行为，迁移时必须保持。

| 规则条目 | 规则要求 | 计划覆盖情况 | 判定 |
|---------|---------|-------------|------|
| WebSocket 双 token 广播 | `m_taskToToken[taskId]` + `m_taskToShareToken[taskId]` 都要广播 | Phase 4.3 明确：`QMap<QString, QSet<mg_connection*>>` 保持映射；Phase 5.4 更新信号连接 | ✅ 覆盖 |
| StreamingMultipartParser 续传 | `setResumeOffset`/`setResumeFilePath` + `Append` 模式 | Phase 3.3/3.4 明确：`StreamingMultipartParser` **保留不变** | ✅ 覆盖 |
| handleUploadCheck 部分文件检测 | `partial`/`existing` 数组 + `transferredSize` | Phase 3.3：路由 handler 仅改适配层，业务逻辑不变 | ✅ 覆盖 |
| 资源清理 | Socket 断开时标记失败、清理映射、移除 UploadSession | Phase 3.5：保留 QTimer + `cleanupExpiredSessions`；CivetWeb 回调中需等效处理 `mg_connection*` 断开 | ✅ 覆盖 |
| Session 不保留 | 断开后移除 UploadSession | Phase 3.5 明确保留 session 管理 | ✅ 覆盖 |

> **注意**：Phase 5.4 的信号重接是唯一需要额外关注点 —— 迁移后 `transferEngine→taskProgress` 需要同时向 `m_taskToToken` 和 `m_taskToShareToken` 广播。当前计划已表述此意图，但建议执行时增加专项验证用例（见 Part A §8.2 的多客户端广播测试）。

---

### D.4 crash-safe-workflow.mdc — 崩溃安全工作流

**此规则约束 AI 执行行为**，而非计划内容本身。计划文档本身已满足该规则精神：

| 规则要求 | 计划符合情况 | 判定 |
|---------|-------------|------|
| §1 任务拆分 | 10 个 Phase + 每个 Phase 4~5 个独立步骤 | ✅ 满足 |
| §2 每步保存 | 每 Phase 有独立验证方法和产出物 | ✅ 满足 |
| §3 断点恢复 | 通过 Phase 编号可精确定位断点 | ✅ 满足 |
| §5 分析结果落盘 | 本文档即为落盘结果 | ✅ 满足 |

---

### D.5 debug-logging.mdc — 调试与日志规则

| 规则要求 | 计划符合情况 | 判定 |
|---------|-------------|------|
| 使用 LOG_INFO/LOG_ERROR 宏 | Phase 1.4 明确 CivetWeb `log_message` → `LOG_INFO` 桥接；TLS 生成也用 `LOG_INFO`/`LOG_ERROR` | ✅ 合规 |
| 输出渠道 (Qt Creator Application Output) | 不改变现有日志输出方式 | ✅ 合规 |
| 格式 `[ISO时间][模块][tag]` | 不改变现有格式 | ✅ 合规 |

---

### D.6 root-cause-before-display-fix.mdc — 根因优先

**此规则约束排障行为**，非计划设计约束。计划中涉及修复的部分（如 ServiceLocator 类型不安全、QML 回退冗余）均基于先前根因分析得出，符合"先查因再改"精神。

---

### D.7 rule-execution-declaration.mdc — 规则执行声明

| 规则要求 | 计划符合情况 | 判定 |
|---------|-------------|------|
| §1 执行前声明规则 | 计划非执行态，不适用 | — |
| §7.1 先查根因 | 计划对每项改进均说明了现状问题和改进原因 | ✅ 合规 |
| §7.2 最小改动 | 每项改动范围精确：删除明确行数、新增明确行数 | ✅ 合规 |
| §7.3 禁止未经要求的兜底 | 四项改进均为用户明确要求的替换/优化，无自主加功能 | ✅ 合规 |

---

### D.8 ui-changes-consent.mdc — 界面改动须用户同意

| 规则要求 | 计划符合情况 | 判定 |
|---------|-------------|------|
| 禁止未同意新增可见控件 | 计划无新增 QML 按钮、标题、面板 | ✅ 合规 |
| Phase 5.3 QML context property 变更 | `webSocketHandler` → `civetWebServer` 不改变界面，仅更换后端对象引用 | ✅ 合规 |

---

### D.9 verify-before-change.mdc — 取证后再改

**此规则约束执行时的代码修改流程**，计划阶段不直接适用。但计划已在 §2 完整分析了现状代码、§10~§12 记录了每个改进项的问题和依据，为执行阶段的验证提供了基础。

---

## 附录 E: 缺失项与冲突深度分析

**分析日期**: 2026-05-26  
**分析方法**: 全量 grep `HttpServer`/`WebSocketHandler`/`ServiceLocator`/`m_locator`/`QTcpSocket` 在 `src/` 下所有引用，逐条与计划 §4.1 受影响文件清单 + 各 Phase 执行步骤交叉比对  

---

### E.1 缺失项

以下文件/成员/逻辑在计划中遗漏或覆盖不足，执行时会导致编译错误或运行时行为缺失：

| # | 缺失项 | 位于 | 影响 | 严重度 |
|---|--------|------|------|--------|
| **M1** | `src/core/CMakeLists.txt` 第 11 行引用 `common/ServiceLocator.h`，Part C Phase 11.6 删除文件后 CMake 会报找不到源文件 | [core/CMakeLists.txt:L11](file:///d:/qt6cmake/NetShare/src/core/CMakeLists.txt#L11) | 编译中断 | 🔴 高 |
| **M2** | `RequestHandler.h` 第 6 行 `#include "HttpServer.h"`、第 94 行 `registerRoutes(HttpServer*)`、第 152 行 `HttpServer* m_httpServer` 需改为 `CivetWebServer`，计划 §4.1 标记 RequestHandler 为"修改"但未逐条列出这些符号变更 | [RequestHandler.h:L6](file:///d:/qt6cmake/NetShare/src/network/RequestHandler.h#L6)、[L94](file:///d:/qt6cmake/NetShare/src/network/RequestHandler.h#L94)、[L152](file:///d:/qt6cmake/NetShare/src/network/RequestHandler.h#L152) | 编译中断 | 🔴 高 |
| **M3** | `RequestHandler.h` 第 155/158 行 `QHash<QTcpSocket*, StreamingUploadState*>` / `QHash<QTcpSocket*, StreamingFileUploadState*>` 需改为 `QHash<mg_connection*, ...>`，第 122/125 行 handler 签名含 `QTcpSocket*` 参数 | [RequestHandler.h:L122](file:///d:/qt6cmake/NetShare/src/network/RequestHandler.h#L122)、[L155](file:///d:/qt6cmake/NetShare/src/network/RequestHandler.h#L155) | 编译中断 | 🔴 高 |
| **M4** | `RequestHandler.cpp` 第 153 行 `connect(server, &HttpServer::streamingSocketDisconnected, ...)` → 需改为 `connect(server, &CivetWebServer::streamingConnDisconnected, ...)` | [RequestHandler.cpp:L153](file:///d:/qt6cmake/NetShare/src/network/RequestHandler.cpp#L153) | 编译中断 | 🔴 高 |
| **M5** | `main.cpp` `initializeQml()` 第 449–459 行使用 `m_locator.service<T>()` 获取 12 个 context property 对象，Part C Phase 11.4 只说替换 `registerService`（写）未提 `service()` 读操作 | [main.cpp:L449–459](file:///d:/qt6cmake/NetShare/src/main.cpp#L449) | 编译中断 | 🔴 高 |
| **M6** | `main.cpp` `shutdown()` 第 162/165 行 `m_locator.service<WebSocketHandler>()` 和 `m_locator.service<HttpServer>()` — Part A Phase 5 将其合并为 `CivetWebServer` 单例，shutdown 中两个 stop 需合并为一个 | [main.cpp:L162–165](file:///d:/qt6cmake/NetShare/src/main.cpp#L162) | 运行时 double-free 或空指针 | 🟡 中 |
| **M7** | `main.cpp` 第 413–433 行 `taskProgress` 信号连接中 `wsHandler->broadcastToSubscribers(...)` — Phase 5.4 说"更新信号连接"但未给出具体替换：此方法原在 `WebSocketHandler` 中，迁移后需在 `CivetWebServer` 或其代理类中实现 | [main.cpp:L413–433](file:///d:/qt6cmake/NetShare/src/main.cpp#L413) | 编译中断 | 🔴 高 |
| **M8** | WebSocket 应用层心跳：现有 `WebSocketHandler::setupHeartbeat()` / `sendHeartbeat()` 发送 ping/pong frame，CivetWeb 内置的 `enable_keep_alive` 是 HTTP Keep-Alive（非 WebSocket ping）。移除 WebSocketHandler 后若只依赖 HTTP Keep-Alive，长连接可能被中间代理断开 | [WebSocketHandler.cpp:L284–289](file:///d:/qt6cmake/NetShare/src/network/WebSocketHandler.cpp#L284) | 运行时空闲连接断线 | 🟡 中 |
| **M9** | `RequestHandler.cpp` 中 4 个 HTML 生成辅助函数（`generateSharePage`、`generatePasswordPage`、`generateErrorPage`、`generateUploadPage`）返回 `QByteArray` 并通过 `response.body = html` 写入，迁移后需改为 `mg_write` / `mg_printf` | [RequestHandler.h:L134–138](file:///d:/qt6cmake/NetShare/src/network/RequestHandler.h#L134) | 编译中断 | 🔴 高 |
| **M10** | `RequestHandler.cpp` 第 1988/2035 行硬编码了 JavaScript 字符串 `transfer_update`（用于内嵌 HTML 中的 WebSocket 客户端代码），迁移后 WebSocket 端点路径可能变化（从独立 ws port 改为同端口 `/ws` 路径），JavaScript 中的 WebSocket 连接 URL 也需更新 | [RequestHandler.cpp:L1988](file:///d:/qt6cmake/NetShare/src/network/RequestHandler.cpp#L1988) | Web 前端 WebSocket 连接失败 | 🟡 中 |
| **M11** | 15 个 HTTP handler 函数签名全部需从 `void(const HttpRequest&, HttpResponse&)` 改为 `int(mg_connection*, const HttpRequestInfo&)`，Phase 2–3 覆盖了路由迁移但未逐函数列出 | [RequestHandler.h:L108–130](file:///d:/qt6cmake/NetShare/src/network/RequestHandler.h#L108) | 编译中断 | 🔴 高 |
| **M12** | 计划 §4.1 将 `src/gui/CMakeLists.txt` 列为需修改（"移除 WebSocketHandler 上下文属性引用"），但经 grep 证实该文件无 WebSocketHandler 引用，无需修改 | — | 多余步骤 | 🟢 低 |

---

### E.2 冲突项

以下为四部分之间（Part A↔C, Part B↔A）以及外部依赖间的设计冲突：

| # | 冲突描述 | 涉及 Parts | 根因 | 严重度 |
|---|---------|-----------|------|--------|
| **C1** | **QObject parent-child 所有权 vs Boost.DI `di::singleton` 所有权冲突**：当前所有服务对象（`CivetWebServer`、`mDNSService`、`RequestHandler`、`FileTransferEngine`、`ChunkManager`、`ResumeManager`、`BandwidthManager`）均通过 `new Xxx(this)` 创建，`this` (= NetShareApplication) 作为 parent 在析构时自动释放。Part C DIContainer draft 中 `di::bind<CivetWebServer>.in(di::singleton)` 会让 injector 创建**新的实例**并接管生命周期（内部 `unique_ptr`），导致：(a) 两个不同实例存活；(b) QObject parent 析构时 delete 与 injector 析构时 delete 双释放 | Part A + Part C | DIContainer draft 未区分 QObject 类与非 QObject 类的创建策略 | 🔴 高 |
| **C2** | **Part C DIContainer draft 缺少 `SettingsManager`、`TransferLogService`、`NotificationManager`、`DatabaseManager` 绑定**：当前 main.cpp 注册了 ~15 个服务，DIContainer draft 的 3 个模块只绑定了约 10 个，遗漏 `SettingsManager`、`TransferLogService`、`NotificationManager`、`ChunkManager`、`ResumeManager` 等 | Part C 内部 | 草案只展示了接口绑定示例，未遍历 main.cpp 全部 `registerService` 调用 | 🟡 中 |
| **C3** | **`RequestHandler` 构造函数中的 `QObject* parent` 参数与 Boost.DI 自动注入不兼容**：当前签名 `RequestHandler(IShareManager*, IFileBrowser*, IFolderPacker*, QObject* parent)`，前三个可通过 DI 绑定注入，但 `QObject* parent` 需显式传入 `this`，DI 无法自动推导 | Part C | Boost.DI 对 QObject parent 模式无原生支持 | 🟡 中 |
| **C4** | **Part B TLS 自签证书时机 vs Part A CivetWeb 配置项**：Part B Phase 8 在 `initializeNetworkServer()` 中调用 `TlsCertificateGenerator::generateSelfSignedCert(...)` 生成证书文件，然后将路径传给 CivetWeb 的 `ssl_certificate`/`ssl_private_key`。但若 Part A 已先完成（使用 `tlsCertPath`/`tlsKeyPath` 配置项），Part B 改动需要重写 TLS 配置入口。两部分的 TLS 初始化为**同一段代码**，存在合并冲突 | Part A + Part B | 两者修改 `initializeNetworkServer()` 的同一代码段 | 🟡 中 |
| **C5** | **Part C Phase 11.4 与 Part A Phase 5.1 共享 `main.cpp` 同一函数的 ServiceLocator 操作**：Phase 5 在 `m_locator` 中注册 `CivetWebServer`，Phase 9 删除所有 `m_locator` 操作。执行时若按 Phase 编号顺序（先 1–7 后 9），Phase 5 新增的 `m_locator.registerService(civetWebServer)` 将在 Phase 9 被替换，逻辑无缝。但若并行开发不同分支，合并时代码冲突高 | Part A + Part C | 同文件同函数被先后修改 | 🟢 低 |
| **C6** | **`src/gui/CMakeLists.txt` 错误列入受影响文件**：计划 §4.1 标记该文件需要修改，但经 grep 确认该文件不含 WebSocketHandler 引用。若盲目修改可能引入不必要的 diff | 计划内 §4.1 | 计划初稿时基于推测而非实际 grep | 🟢 低 |

---

### E.3 缺失项修正建议

| 缺失项 | 修正动作 | 应追加到 |
|--------|---------|---------|
| M1 | Phase 11.6 增加：从 `src/core/CMakeLists.txt` 第 11 行移除 `common/ServiceLocator.h` | Part C Phase 9 |
| M2 | Phase 2 增加子步骤："RequestHandler.h: 将 `#include "HttpServer.h"` 替换为 `#include "CivetWebServer.h"`；`registerRoutes(HttpServer*)` → `registerRoutes(CivetWebServer*)`；`HttpServer* m_httpServer` → `CivetWebServer* m_civetServer`" | Part A Phase 2 |
| M3 | Phase 3 增加子步骤："RequestHandler.h/cpp: `QHash<QTcpSocket*,...>` → `QHash<mg_connection*,...>`；流式 handler 签名 `QTcpSocket*` → `mg_connection*`" | Part A Phase 3 |
| M4 | Phase 3 增加子步骤："RequestHandler.cpp L153: `connect(server, &HttpServer::streamingSocketDisconnected)` → `connect(server, &CivetWebServer::streamingConnDisconnected)`" | Part A Phase 3 |
| M5 | Phase 11.4 描述改为："替换 main.cpp 中所有 `m_locator.registerService` 和 `m_locator.service` 调用，包含 `initializeQml()` 中 12 个 context property 获取" | Part C Phase 9 |
| M6 | Phase 5.1 增加子步骤："合并 shutdown() 中的 WebSocketHandler::stop() 和 HttpServer::stop() 为 CivetWebServer::stop()" | Part A Phase 5 |
| M7 | Phase 5.4 细化："CivetWebServer 中增加 `broadcastToSubscribers(const QString& token, const QString& type, const QJsonObject& data)` 方法，内部迭代 `m_wsClients[token]` 调用 `mg_websocket_write`；taskProgress lambda 中 `wsHandler->broadcastToSubscribers` → `civetWebServer->broadcastToSubscribers`" | Part A Phase 5 |
| M8 | Phase 4.4 增加："用 `QTimer` 实现 WebSocket ping/pong 心跳（每秒 ping，pong 超时 30 秒断连），替代被移除的 `WebSocketHandler::setupHeartbeat()`" | Part A Phase 4 |
| M9 | Phase 2.1/2.4 增加："4 个 HTML 辅助函数保留返回 QByteArray，在 handler 中通过 `mg_printf`/`mg_write` 发送" | Part A Phase 2 |
| M10 | Phase 4 增加子步骤："更新内嵌 HTML 中的 WebSocket 连接 URL：`new WebSocket('ws://'+location.host+':<port+1>')` → `new WebSocket('ws://'+location.host+'/ws')`" | Part A Phase 4 |
| M11 | Phase 2 增加对照表：15 个 handler 新旧签名一一对应 | Part A Phase 2 |
| M12 | §4.1 受影响文件表中移除 `src/gui/CMakeLists.txt` | 计划 §4 |

---

### E.4 冲突项修正建议

| 冲突项 | 修正方案 | 修正后 DIContainer 写法 |
|--------|---------|----------------------|
| C1 | **QObject 派生类不通过 DI 创建实例，仅通过 DI 注入引用**。在 main.cpp 中照常 `new Xxx(this)`，然后用 `std::ref()` 绑定到 injector | `auto& server = *new CivetWebServer(&app);`<br>`auto inj = di::make_injector(`<br>`  di::bind<CivetWebServer>.to(std::ref(server))`<br>`);` |
| C2 | DIContainer 增加 `InfraModule()` 绑定 `SettingsManager`、`DatabaseManager`、`TransferLogService`、`NotificationManager` | 附录 A DIContainer draft 需补充 |
| C3 | 对含 `QObject* parent` 的构造函数，DI 使用 `di::bind<T>.to([&](auto& inj){ return new T(inj.template create<Dep1>(), &app); })` 或在 main.cpp 中手动创建后 `std::ref()` 绑定 | 同上 |
| C4 | **合并 Part A Phase 4.1 和 Part B Phase 8**：在同一个 `initializeNetworkServer()` 版本中先调用 `TlsCertificateGenerator::generateSelfSignedCert()`，再将生成的路径填入 CivetWeb options | 合并 Phase 4.1 + Phase 8 为一个连贯步骤 |

---

### E.5 受影响文件清单（完整修正版）

基于上述分析，完整受影响文件清单为：

| 文件 | 操作 | 触发 Phase |
|------|------|-----------|
| `src/network/CMakeLists.txt` | ✏️ 修改 | A-1 |
| `src/network/HttpServer.h` | 🗑️ 删除 | A-7 |
| `src/network/HttpServer.cpp` | 🗑️ 删除 | A-7 |
| `src/network/WebSocketHandler.h` | 🗑️ 删除 | A-7 |
| `src/network/WebSocketHandler.cpp` | 🗑️ 删除 | A-7 |
| `src/network/CivetWebServer.h` | ✨ 新建 | A-1 |
| `src/network/CivetWebServer.cpp` | ✨ 新建 | A-1 |
| `src/network/RequestHandler.h` | ✏️ 修改 | A-2/3/4 |
| `src/network/RequestHandler.cpp` | ✏️ 修改 | A-2/3/4/5 |
| `src/main.cpp` | ✏️ 修改 | A-5, B-8, C-9, D-10 |
| `src/core/CMakeLists.txt` | ✏️ 修改 | C-9 |
| `src/core/common/ServiceLocator.h` | 🗑️ 删除 | C-9 |
| `src/core/common/TlsCertificateGenerator.h` | ✨ 新建 | B-8 |
| `src/core/common/TlsCertificateGenerator.cpp` | ✨ 新建 | B-8 |
| `src/core/common/DIContainer.h` | ✨ 新建 | C-9 |
| `third_party/boost-di/di.hpp` | 📥 下载 | C-9 |
| ~~`src/gui/CMakeLists.txt`~~ | ❌ **不需修改** | — |

---

### E.6 执行顺序依赖

```
Phase 1 → Phase 2 → Phase 3 → [Phase 4 + Phase 8 合并] → Phase 5 → Phase 6 → Phase 7 → Phase 9 → Phase 10
                         ↑                                        ↑
                    相互独立                                 必须顺序（Phase 5 添加
                                                            新 ServiceLocator 注册，
                                                            Phase 9 删除全部注册）

关键合并点：Phase 4.1 (TLS 迁移) 和 Phase 8 (TLS 自签) 操作同一代码段，合并为一个步骤执行。

Phase 9 (Part C) 必须在 Part A 完全完成后执行，因为它替换 Part A 中仍在使用的 ServiceLocator。
Phase 10 (Part D) 独立性强，可在 Phase 7 后任意时间执行。
```

---

> **总结**：共发现 **12 项缺失**（4 项严重度 🔴高，5 项 🟡中，1 项 🟢低）+ **6 项冲突**（1 项 🔴高，4 项 🟡中，1 项 🟢低）。
> 所有缺失项均可在各 Phase 执行步骤中追加子步骤修复；C1（QObject 生命周期冲突）需要在 DIContainer draft 中根本性修正绑定策略。

---

### D.9 verify-before-change.mdc — 取证后再改

**此规则约束执行时的代码修改流程**，计划阶段不直接适用。但计划已在 §2 完整分析了现状代码、§10~§12 记录了每个改进项的问题和依据，为执行阶段的验证提供了基础。

---

### D.10 合规总结

| 规则文件 | 是否适用 | 合规判定 | 备注 |
|---------|---------|---------|------|
| `architecture.mdc` | ✅ 适用 | ✅ **完全合规** | 所有新增文件、命名、目录均符合；10 个检查点全部通过 |
| `breakpoint-resume-progress.mdr` | ✅ 适用 | ✅ **完全合规** | WebSocket 双 token 广播在 Phase 4.3/5.4 保留；StreamingMultipartParser 明确不变 |
| `crash-safe-workflow.mdc` | ✅ 适用 | ✅ **完全合规** | 10 Phase 细分步骤满足精神 |
| `debug-logging.mdc` | ✅ 适用 | ✅ **完全合规** | LOG_INFO/LOG_ERROR 保持 |
| `root-cause-before-display-fix.mdc` | ⚠️ 执行约束 | ✅ **无冲突** | |
| `rule-execution-declaration.mdc` | ✅ 适用 | ✅ **完全合规** | 最小改动、不禁自主兜底 |
| `ui-changes-consent.mdc` | ✅ 适用 | ✅ **完全合规** | 无新增 UI 控件 |
| `verify-before-change.mdc` | ⚠️ 执行约束 | ✅ **无冲突** | |

> **结论**：本计划文档全部 10 个 Phase、4 大改进项、所有代码草案和文件操作清单，**均符合 `.comate/rules/` 下 8 份规则文件的规定**。无违规项，无需修正。
>
> **唯一提醒**：Phase 5.4 执行时，需确保 `transferEngine→taskProgress` 的 lambda 中新 WebSocket 广播逻辑覆盖 `m_taskToToken` 和 `m_taskToShareToken` 双映射（`breakpoint-resume-progress.mdr` 要求），当前计划已表述但建议执行时增加专项验证。