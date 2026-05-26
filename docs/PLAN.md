# NetShare 项目计划书

**版本**: 1.0
**日期**: 2026-04-25
**技术栈**: Qt 6.8.3 + CMake + Qt Quick (QML)
**平台**: Windows (桌面) + 浏览器 (手机端)

---

## 目录

1. [项目概述](#1-项目概述)
2. [项目结构](#2-项目结构)
3. [核心架构](#3-核心架构)
4. [功能模块详解](#4-功能模块详解)
5. [数据库设计](#5-数据库设计)
6. [API 设计](#6-api-设计)
7. [QML UI 设计](#7-qml-ui-设计)
8. [Web 前端设计](#8-web-前端设计)
9. [技术选型](#9-技术选型)
10. [开发阶段](#10-开发阶段)

---

## 1. 项目概述

### 1.1 项目目标

NetShare 是一款局域网文件分享工具，支持：

- 电脑端生成二维码，手机扫码下载文件/文件夹
- 支持 30GB+ 大文件传输
- 断点续传
- 多任务并行下载
- 单任务多线程下载
- 手机端上传文件到电脑
- 传输日志和统计
- 带宽控制
- TLS 加密传输

### 1.2 系统架构

```
┌─────────────────────────────────────────────────────────────────┐
│                         桌面客户端                                │
│  ┌──────────────┐     ┌─────────────────┐     ┌──────────────┐ │
│  │   QML UI     │────▶│   Core Engine   │────▶│  QRGenerator │ │
│  │  (主界面)     │     │ (传输/文件/会话) │     │  (二维码)     │ │
│  └──────────────┘     └────────┬────────┘     └──────────────┘ │
│                                │                                  │
│                       ┌────────▼────────┐                       │
│                       │   TcpServer     │                       │
│                       │   HttpServer    │                       │
│                       │   (TLS支持)     │                       │
│                       │   mDNS注册      │                       │
│                       └─────────────────┘                       │
└─────────────────────────────────────────────────────────────────┘
                                │
              ┌─────────────────┼─────────────────┐
              │                 │                  │
    ┌─────────▼─────────┐ ┌─────▼─────┐ ┌─────────▼─────────┐
    │    桌面客户端B      │ │  手机浏览器 │ │    桌面客户端C      │
    │   (TCP 直连)       │ │(HTTP/HTTPS)│ │   (TCP 直连)       │
    │   多线程下载        │ │ 多线程上传  │ │   多线程下载        │
    └───────────────────┘ └───────────┘ └───────────────────┘
```

---

## 2. 项目结构

```
NetShare/
├── CMakeLists.txt                     # 根级 CMake 配置
├── cmake/
│   └── QtHelper.cmake                 # Qt6 辅助宏
├── src/
│   ├── CMakeLists.txt
│   ├── main.cpp                       # 应用程序入口
│   ├── core/                          # 核心功能层
│   │   ├── FileTransferEngine.h/cpp       # 文件传输引擎
│   │   ├── ChunkManager.h/cpp              # 分块管理
│   │   ├── ResumeManager.h/cpp            # 断点续传管理
│   │   ├── DownloadSession.h/cpp          # 下载会话管理
│   │   ├── UploadHandler.h/cpp            # 上传处理器
│   │   ├── FolderPacker.h/cpp             # 文件夹打包
│   │   ├── ShareManager.h/cpp             # 分享管理
│   │   ├── TransferLogService.h/cpp       # 传输日志
│   │   ├── BandwidthManager.h/cpp         # 带宽管理
│   │   ├── FileBrowser.h/cpp              # 文件浏览器
│   │   └── TransferTask.h/cpp             # 传输任务模型
│   ├── network/                       # 网络通信层
│   │   ├── TcpServer.h/cpp                  # TCP服务器
│   │   ├── TcpClient.h/cpp                  # TCP客户端
│   │   ├── HttpServer.h/cpp                 # HTTP服务器
│   │   ├── HttpsServer.h/cpp                # HTTPS服务器
│   │   ├── WebSocketHandler.h/cpp           # WebSocket处理器
│   │   ├── mDNSService.h/cpp               # mDNS服务注册
│   │   └── Protocol.h/cpp                   # 传输协议
│   ├── qrcode/                       # 二维码模块
│   │   └── QRGenerator.h/cpp             # 二维码生成
│   ├── qml/                          # QML界面层
│   │   ├── main.qml                      # 主界面
│   │   ├── HomePage.qml                  # 首页
│   │   ├── FileListPage.qml             # 文件列表
│   │   ├── TransferPage.qml             # 传输页面
│   │   ├── SharePage.qml                # 分享页面
│   │   ├── SettingsPage.qml             # 设置页面
│   │   └── Components/                  # 通用组件
│   │       ├── TaskCard.qml
│   │       ├── FileItem.qml
│   │       └── ProgressBar.qml
│   ├── resources/
│   │   └── resources.qrc
│   └── database/
│       └── DatabaseManager.h/cpp         # 数据库管理
├── web/                              # Web前端
│   ├── index.html                    # 主页
│   ├── css/
│   │   └── style.css
│   ├── js/
│   │   ├── download_manager.js        # 下载管理器
│   │   ├── upload_manager.js         # 上传管理器
│   │   ├── multi_thread_download.js  # 多线程下载
│   │   ├── notification_manager.js   # 通知管理
│   │   └── service_discovery.js      # 服务发现
│   ├── pages/
│   │   ├── browse.html               # 文件浏览
│   │   ├── download.html             # 下载页面
│   │   ├── upload.html               # 上传页面
│   │   ├── transfer_logs.html        # 传输日志
│   │   └── preview.html              # 预览页面
│   └── templates/
│       ├── file_download.html
│       └── folder_download.html
└── tests/                           # 单元测试
```

---

## 3. 核心架构

### 3.1 传输协议设计

**自定义 TCP 协议（桌面间传输）：**

```
┌──────────┬──────────┬──────────┬─────────────────┬──────────────────┐
│ Magic(4B)│ Version   │ Type(1B) │ Payload Length  │    Payload       │
│ 0xNS     │ (1B)     │ CMD/DATA │    (4B)         │   (可变长度)       │
└──────────┴──────────┴──────────┴─────────────────┴──────────────────┘

CMD 类型:
- 0x01: HANDSHAKE        # 握手
- 0x02: FILE_INFO        # 文件信息（名称、大小、chunk数）
- 0x03: CHUNK_REQUEST    # 请求下载某个chunk
- 0x04: CHUNK_DATA       # chunk数据
- 0x05: CHUNK_COMPLETE   # chunk完成确认
- 0x06: RESUME_QUERY     # 查询已下载的chunk
- 0x07: RESUME_RESPONSE  # 返回已下载chunk信息
- 0x08: TRANSFER_COMPLETE # 传输完成
- 0x09: CANCEL           # 取消传输
```

**HTTP API（手机端浏览器）：** 见第 6 节 API 设计

### 3.2 分块策略

| 文件大小 | 分块大小 | 线程数 |
|---------|---------|--------|
| < 100MB | 1MB | 3 |
| 100MB - 1GB | 4MB | 4 |
| 1GB - 5GB | 8MB | 5 |
| > 5GB | 16MB | 6 |

### 3.3 数据流

```
桌面端下载 (多线程):
┌──────────┐     ┌──────────┐     ┌──────────┐
│ Chunk #0 │     │ Chunk #1 │     │ Chunk #2 │
│ Thread 1 │     │ Thread 2 │     │ Thread 3 │
└────┬─────┘     └────┬─────┘     └────┬─────┘
     │                │                │
     └────────────────┼────────────────┘
                      ▼
              ┌──────────────┐
              │ ChunkMerger  │
              │ (SHA256校验) │
              └──────────────┘
```

---

## 4. 功能模块详解

### 4.1 分享管理 (ShareManager)

```cpp
struct ShareInfo {
    QString token;              // UUID
    ShareType type;             // FILE / FOLDER
    QString filePath;           // 文件/文件夹绝对路径
    QString fileName;           // 显示名称
    qint64 fileSize;            // 文件大小
    QString fileHash;           // SHA256校验
    quint16 port;               // 服务端口
    QHostAddress hostIP;        // 电脑IP
    QDateTime createdAt;        // 创建时间
    QDateTime expiresAt;        // 过期时间
    int downloadCount;          // 下载次数
    int maxDownloads;           // 最大下载次数 (0=无限)
    bool passwordProtected;      // 是否密码保护
    QString passwordHash;       // 密码Hash
};
```

### 4.2 下载会话管理 (DownloadSession)

```cpp
struct DownloadSession {
    QString sessionId;           // 唯一会话ID (UUID)
    QString clientId;            // 客户端唯一标识
    QString shareToken;          // 分享Token
    QString fileId;              // 文件ID
    QString fileName;            // 文件名
    qint64 totalSize;           // 文件总大小
    qint64 downloadedSize;      // 已下载大小
    qint64 lastPosition;         // 最后下载位置
    QString tempPath;            // 临时文件路径
    QDateTime createdAt;        // 创建时间
    QDateTime updatedAt;        // 更新时间
    QString etag;                // 文件ETag
};
```

### 4.3 上传处理 (UploadHandler)

```cpp
struct UploadSession {
    QString uploadId;
    QString fileName;
    QString targetPath;
    qint64 totalSize;
    qint64 uploadedSize;
    QString tempDir;                      // 临时目录
    QSet<quint32> completedChunks;        // 已完成的分块
    QDateTime createdAt;
};
```

### 4.4 带宽管理 (BandwidthManager)

```cpp
struct BandwidthSettings {
    quint64 maxUploadSpeed;     // 最大上传速度 (bytes/s), 0=不限制
    quint64 maxDownloadSpeed;   // 最大下载速度 (bytes/s), 0=不限制
    bool perTaskLimit;          // 是否每个任务单独限制
    quint64 maxUploadPerTask;
    quint64 maxDownloadPerTask;
    QString scheduleStart;      // 限速时段开始 (如 "22:00")
    QString scheduleEnd;        // 限速时段结束 (如 "08:00")
    bool scheduleEnabled;
    quint64 scheduledMaxSpeed;
};
```

### 4.5 mDNS 服务注册

```cpp
struct ServiceInfo {
    QString id;                 // 设备ID
    QString name;               // 显示名称
    QString host;               // IP地址
    quint16 port;               // HTTP端口
    quint16 httpsPort;          // HTTPS端口
    bool tlsEnabled;
    QString os;
    QString version;
    QStringList sharedFolders;
    QDateTime discoveredAt;
};
```

### 4.6 传输日志 (TransferLog)

```cpp
struct TransferLog {
    QString logId;              // UUID
    TransferDirection direction; // Upload / Download
    QString clientId;
    QString clientName;
    QString fileName;
    QString filePath;
    qint64 fileSize;
    qint64 transferredSize;
    TransferStatus status;      // InProgress / Completed / Failed / Cancelled
    QString errorMessage;
    qint64 speedAvg;            // 平均速度 (bytes/s)
    qint64 durationMs;          // 耗时 (毫秒)
    QDateTime startedAt;
    QDateTime completedAt;
    QDateTime createdAt;
};
```

### 4.7 文件浏览器 (FileBrowser)

```cpp
struct FileItem {
    QString id;                 // 路径hash
    QString name;
    QString path;
    FileType type;             // FILE / DIRECTORY
    qint64 size;               // 文件大小
    QString mimeType;
    QDateTime modifiedAt;
    bool isShared;
    QString shareToken;
};

enum class FileType {
    Unknown, Image, Video, Audio, Document, Text, Archive, Other
};
```

---

## 5. 数据库设计

### 5.1 SQLite 表结构

```sql
-- 分享记录
CREATE TABLE shares (
    token TEXT PRIMARY KEY,
    type TEXT NOT NULL,
    file_path TEXT NOT NULL,
    file_name TEXT NOT NULL,
    file_size INTEGER NOT NULL,
    file_hash TEXT,
    host_ip TEXT NOT NULL,
    port INTEGER NOT NULL,
    created_at INTEGER NOT NULL,
    expires_at INTEGER NOT NULL,
    download_count INTEGER DEFAULT 0,
    max_downloads INTEGER DEFAULT 0,
    password_hash TEXT,
    description TEXT
);

-- 下载会话
CREATE TABLE download_sessions (
    session_id TEXT PRIMARY KEY,
    client_id TEXT NOT NULL,
    share_token TEXT NOT NULL,
    file_id TEXT NOT NULL,
    file_name TEXT NOT NULL,
    total_size INTEGER NOT NULL,
    downloaded_size INTEGER DEFAULT 0,
    temp_path TEXT,
    etag TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    UNIQUE(client_id, share_token, file_id)
);

CREATE INDEX idx_sessions_client ON download_sessions(client_id, file_id);
CREATE INDEX idx_sessions_updated ON download_sessions(updated_at);

-- 上传会话
CREATE TABLE upload_sessions (
    upload_id TEXT PRIMARY KEY,
    file_name TEXT NOT NULL,
    target_path TEXT NOT NULL,
    total_size INTEGER NOT NULL,
    uploaded_size INTEGER DEFAULT 0,
    temp_dir TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    completed_at INTEGER
);

-- 传输日志
CREATE TABLE transfer_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    log_id TEXT UNIQUE NOT NULL,
    direction TEXT NOT NULL,
    client_id TEXT,
    client_name TEXT,
    file_name TEXT NOT NULL,
    file_path TEXT NOT NULL,
    file_size INTEGER NOT NULL,
    transferred_size INTEGER DEFAULT 0,
    status TEXT NOT NULL,
    error_message TEXT,
    speed_avg INTEGER,
    duration_ms INTEGER,
    started_at INTEGER NOT NULL,
    completed_at INTEGER,
    created_at INTEGER DEFAULT (strftime('%s', 'now'))
);

CREATE INDEX idx_logs_direction ON transfer_logs(direction);
CREATE INDEX idx_logs_status ON transfer_logs(status);
CREATE INDEX idx_logs_created ON transfer_logs(created_at);
CREATE INDEX idx_logs_client ON transfer_logs(client_id);

-- 应用设置
CREATE TABLE settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL,
    updated_at INTEGER DEFAULT (strftime('%s', 'now'))
);

-- 共享文件夹
CREATE TABLE shared_folders (
    id TEXT PRIMARY KEY,
    path TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL,
    enabled INTEGER DEFAULT 1,
    created_at INTEGER NOT NULL
);
```

---

## 6. API 设计

### 6.1 认证与发现

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/discover` | 获取局域网内的 NetShare 服务列表 |
| `POST` | `/api/auth/password` | 验证访问密码 |

**GET /api/discover**
```json
Response:
{
    "services": [
        {
            "id": "ABC123",
            "name": "我的电脑",
            "host": "192.168.1.100",
            "port": 8080,
            "httpsPort": 8443,
            "tlsEnabled": true,
            "os": "Windows 10",
            "version": "1.0.0",
            "sharedFolders": ["D:\\共享", "E:\\资料"]
        }
    ]
}
```

### 6.2 文件浏览

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/browse` | 浏览共享文件夹 |
| `GET` | `/api/browse/tree` | 获取目录树 |
| `GET` | `/api/files/{id}` | 获取文件信息 |
| `GET` | `/api/files/{id}/thumbnail` | 获取缩略图 |
| `GET` | `/api/preview/{id}` | 获取预览 |
| `GET` | `/api/preview/{id}/video` | 视频预览流 |
| `GET` | `/api/preview/{id}/audio` | 音频预览流 |
| `GET` | `/api/search` | 搜索文件 |

**GET /api/browse**
```
Query: path=/shared&page=0&pageSize=50&sortBy=name&order=asc

Response:
{
    "path": "/shared/documents",
    "parentPath": "/shared",
    "items": [
        {
            "id": "hash123",
            "name": "工作报告.pdf",
            "path": "/shared/documents/工作报告.pdf",
            "type": "file",
            "size": 5242880,
            "mimeType": "application/pdf",
            "modifiedAt": "2024-01-15T10:30:00Z",
            "isShared": true,
            "shareToken": "abc123"
        }
    ],
    "totalCount": 45,
    "page": 0,
    "pageSize": 50,
    "hasMore": true
}
```

### 6.3 分享管理

| 方法 | 路径 | 说明 |
|------|------|------|
| `POST` | `/api/share` | 创建分享 |
| `GET` | `/api/share/{token}` | 获取分享信息 |
| `DELETE` | `/api/share/{token}` | 取消分享 |
| `GET` | `/api/shares` | 获取所有活跃分享 |

**POST /api/share**
```json
Request:
{
    "path": "/shared/documents/工作报告.pdf",
    "type": "file",
    "expireHours": 24,
    "maxDownloads": 0,
    "password": "",
    "description": "工作报告"
}

Response:
{
    "token": "abc123-def456",
    "shareUrl": "http://192.168.1.100:8080/s/abc123-def456",
    "qrCode": "data:image/png;base64,...",
    "expiresAt": "2024-01-16T10:30:00Z",
    "fileSize": 5242880,
    "fileCount": 1
}
```

### 6.4 下载

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/s/{token}` | 分享页面 |
| `GET` | `/s/{token}/download` | 下载文件 (支持 Range) |
| `GET` | `/s/{token}/download/folder` | 下载文件夹 (ZIP) |
| `POST` | `/api/download/init` | 初始化下载会话 |
| `GET` | `/api/download/{sessionId}/status` | 获取下载状态 |

**GET /s/{token}/download**
```
支持 Range 请求:
Range: bytes=0-499           返回前500字节
Range: bytes=500-999         返回第500-999字节
Range: bytes=500-            从第500字节到文件末尾

响应:
206 Partial Content
Content-Type: application/octet-stream
Content-Length: 500
Content-Range: bytes 0-499/1073741824
Accept-Ranges: bytes
```

### 6.5 上传

| 方法 | 路径 | 说明 |
|------|------|------|
| `POST` | `/api/upload/init` | 初始化上传 |
| `POST` | `/api/upload/{uploadId}/chunk/{index}` | 上传分块 |
| `POST` | `/api/upload/{uploadId}/complete` | 完成上传 |
| `POST` | `/api/upload/{uploadId}/cancel` | 取消上传 |

**POST /api/upload/init**
```json
Request:
{
    "fileName": "视频.mp4",
    "fileSize": 1073741824,
    "targetPath": "/shared/uploads",
    "chunkSize": 4194304
}

Response:
{
    "uploadId": "upload789",
    "chunkSize": 4194304,
    "chunkCount": 256,
    "maxParallelUploads": 3,
    "serverUploadUrl": "/api/upload/upload789/chunk"
}
```

### 6.6 传输管理

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/transfers` | 获取传输列表 |
| `POST` | `/api/transfers/{id}/pause` | 暂停传输 |
| `POST` | `/api/transfers/{id}/resume` | 继续传输 |
| `POST` | `/api/transfers/{id}/cancel` | 取消传输 |

### 6.7 日志

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/logs` | 获取传输日志 |
| `GET` | `/api/logs/stats` | 获取传输统计 |
| `DELETE` | `/api/logs/cleanup` | 清理旧日志 |

**GET /api/logs**
```
Query: direction=upload&status=completed&startDate=2024-01-01&page=0&pageSize=20

Response:
{
    "logs": [
        {
            "logId": "log001",
            "direction": "upload",
            "clientId": "client123",
            "clientName": "iPhone 15",
            "fileName": "照片.jpg",
            "filePath": "/shared/uploads/照片.jpg",
            "fileSize": 5242880,
            "transferredSize": 5242880,
            "status": "completed",
            "speedAvg": 2097152,
            "durationMs": 2500,
            "startedAt": "2024-01-15T10:30:00Z",
            "completedAt": "2024-01-15T10:30:02Z"
        }
    ],
    "totalCount": 150,
    "page": 0,
    "pageSize": 20
}
```

### 6.8 通知

| 方法 | 路径 | 说明 |
|------|------|------|
| `WS` | `/api/notifications/ws` | WebSocket 实时通知 |
| `POST` | `/api/notify` | 发送通知 (内部) |

### 6.9 设置

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/settings` | 获取设置 |
| `PUT` | `/api/settings` | 更新设置 |
| `GET` | `/api/settings/bandwidth` | 获取带宽设置 |
| `PUT` | `/api/settings/bandwidth` | 更新带宽设置 |

**GET /api/settings/bandwidth**
```json
Response:
{
    "maxUploadSpeed": 10485760,
    "maxDownloadSpeed": 20971520,
    "perTaskLimit": true,
    "maxUploadPerTask": 5242880,
    "maxDownloadPerTask": 10485760,
    "scheduleEnabled": true,
    "scheduleStart": "22:00",
    "scheduleEnd": "08:00",
    "scheduledMaxSpeed": 1048576
}
```

---

## 7. QML UI 设计

### 7.1 页面结构

```
main.qml (StackView)
├── HomePage.qml            # 首页 - 快速入口
│   ├── 电脑名称 + IP显示
│   ├── 共享文件夹快捷访问
│   ├── 最近分享
│   └── 创建分享按钮
│
├── FileListPage.qml        # 文件列表页
│   ├── 路径导航
│   ├── 文件/文件夹网格
│   ├── 多选操作栏
│   └── 分享按钮
│
├── TransferPage.qml        # 传输页面
│   ├── 下载列表 (Tab)
│   │   ├── 任务卡片 (显示分块进度)
│   │   └── 操作: 暂停/继续/取消/重试
│   │
│   └── 上传列表 (Tab)
│       ├── 任务卡片
│       └── 操作: 暂停/取消
│
├── SharePage.qml           # 分享页面
│   ├── 分享设置 (有效期/密码/次数)
│   ├── 二维码显示
│   └── 链接复制
│
└── SettingsPage.qml        # 设置页面
    ├── 共享文件夹管理
    ├── 下载目录设置
    ├── 带宽设置
    ├── TLS 设置
    ├── 传输日志
    └── 关于
```

### 7.2 分享页面组件

```qml
SharePage.qml:
- GroupBox: 选择分享内容
  - RowLayout: PathField + BrowseButton
  
- GroupBox: 分享设置
  - GridLayout: 有效期 / 下载次数 / 访问密码
  
- GroupBox: 二维码显示
  - Image: QRCode (来自 image://qrcode/)
  - Label: ShareUrl
  - RowLayout: CopyLinkButton / RefreshQRButton
  
- Button: 创建分享
```

### 7.3 传输页面组件

```qml
TransferPage.qml:
- TabBar: 下载 / 上传

- ListView: 任务列表
  - delegate: TaskCard
    - FileName + FileSize
    - ProgressBar (总体进度)
    - ChunkProgressRows (每个分块的进度)
    - SpeedLabel
    - RowLayout: PauseButton / ResumeButton / CancelButton
```

---

## 8. Web 前端设计

### 8.1 页面列表

| 页面 | 路径 | 功能 |
|------|------|------|
| 主页 | `/` 或 `/index.html` | 快速入口、服务发现 |
| 文件浏览 | `/browse.html` | 浏览共享文件夹 |
| 下载页 | `/pages/download.html` | 文件/文件夹下载 |
| 上传页 | `/pages/upload.html` | 文件上传 |
| 传输管理 | `/pages/transfers.html` | 下载/上传任务管理 |
| 日志页 | `/pages/transfer_logs.html` | 传输历史 |
| 预览页 | `/pages/preview.html` | 媒体预览 |

### 8.2 下载页面 (download.html)

```html
<div class="download-container">
    <div class="file-info">
        <div class="file-name" id="fileName">document.zip</div>
        <div class="file-size" id="fileSize">1.2 GB</div>
    </div>
    
    <div class="resume-notice" id="resumeNotice" style="display:none">
        <strong>检测到未完成的下载</strong>
        <button onclick="startResumeDownload()">继续下载</button>
    </div>
    
    <div class="progress-container" id="progressContainer">
        <div class="chunk-container">
            <!-- 分块进度 -->
            <div class="chunk-row">
                <span class="chunk-label">#0</span>
                <div class="chunk-bar">
                    <div class="chunk-fill" style="width: 80%"></div>
                </div>
                <span class="chunk-percent">80%</span>
            </div>
        </div>
        <div class="progress-summary">
            <span id="progressText">45%</span>
            <span id="speedText">12.5 MB/s</span>
        </div>
        <div class="task-actions">
            <button onclick="pauseDownload()">暂停</button>
            <button onclick="cancelDownload()">取消</button>
        </div>
    </div>
</div>
```

### 8.3 上传页面 (upload.html)

```html
<div class="upload-container">
    <div class="upload-zone" id="uploadZone">
        <div>拖拽文件到这里，或点击选择</div>
        <input type="file" id="fileInput" multiple>
    </div>
    
    <div class="target-path">
        <label>上传到: </label>
        <select id="targetFolder">
            <option value="/shared">共享文件夹</option>
            <option value="/shared/uploads">上传目录</option>
        </select>
    </div>
    
    <div id="uploadList"></div>
</div>
```

### 8.4 JavaScript 模块

| 模块 | 文件 | 功能 |
|------|------|------|
| DownloadManager | download_manager.js | 单文件下载管理 |
| MultiThreadDownloader | multi_thread_download.js | 多线程分块下载 |
| UploadManager | upload_manager.js | 分块上传管理 |
| NotificationManager | notification_manager.js | WebSocket 通知 |
| ServiceDiscovery | service_discovery.js | mDNS 服务发现 |

---

## 9. 技术选型

| 组件 | 选择 | 版本 |
|------|------|------|
| **Qt 框架** | Qt | 6.8.3 |
| **构建系统** | CMake | 3.30.5+ |
| **UI 框架** | Qt Quick (QML) | Qt 6 |
| **网络** | Qt Network | Qt 6 |
| **HTTP 服务器** | 自实现 | - |
| **数据库** | SQLite | 3.x |
| **mDNS** | QtConnectivity | Qt 6 |
| **二维码** | QtBarcode / ZXing | Qt 6.8+ |
| **TLS** | QSslSocket | Qt 6 |
| **压缩** | QZipWriter/QZipReader | Qt 6 |
| **线程池** | QThreadPool | Qt 6 |

### 9.1 CMake 配置要点

```cmake
cmake_minimum_required(VERSION 3.21)
project(NetShare LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Network
    Qml
    Quick
    SQL
    Widgets
    WebSockets
    Bluetooth
)

qt_add_executable(NetShare
    src/main.cpp
    src/core/*.cpp
    src/network/*.cpp
    src/qrcode/*.cpp
    src/database/*.cpp
)

target_link_libraries(NetShare PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::SQL
    Qt6::Widgets
    Qt6::WebSockets
)

# Web 资源
qt_add_resources(NetShare "resources"
    PREFIX "/"
    FILES
        web/index.html
        web/css/style.css
        web/js/*.js
        web/pages/*.html
)
```

---

## 10. 开发阶段

### Phase 1: 基础架构 (30%)
- [ ] 项目搭建 (CMake 配置)
- [ ] 数据库初始化
- [ ] HTTP 服务器基础
- [ ] 文件浏览器基础
- [ ] QML 基础 UI

### Phase 2: 下载功能 (35%)
- [ ] 分块下载
- [ ] 多线程下载
- [ ] 断点续传
- [ ] 多任务并行
- [ ] 下载页面 UI

### Phase 3: 分享功能 (20%)
- [ ] 分享创建/管理
- [ ] 二维码生成
- [ ] 密码保护
- [ ] 有效期控制

### Phase 4: 上传功能 (15%)
- [ ] 分块上传
- [ ] 多线程上传
- [ ] 上传页面 UI

### Phase 5: 增强功能 (可选)
- [ ] mDNS 服务发现
- [ ] TLS 支持
- [ ] 带宽控制
- [ ] 传输日志
- [ ] 媒体预览
- [ ] WebSocket 通知

---

## 附录 A: 通知类型

| 类型 | 触发时机 | 显示内容 |
|------|---------|---------|
| `download_complete` | 下载完成 | "{文件名} 下载完成" |
| `upload_complete` | 上传完成 | "{文件名} 上传完成" |
| `download_failed` | 下载失败 | "{文件名} 下载失败: {原因}" |
| `disk_full` | 磁盘空间不足 | "磁盘空间不足" |
| `file_changed` | 源文件被修改 | "{文件名} 已被修改" |

## 附录 B: 错误码

| 错误码 | 说明 |
|-------|------|
| 400 | 请求参数错误 |
| 401 | 密码错误 |
| 403 | 访问被拒绝 |
| 404 | 文件/分享不存在 |
| 409 | 下载冲突 (文件被修改) |
| 413 | 文件太大 |
| 416 | Range 不满足 |
| 429 | 请求过于频繁 |
| 500 | 服务器内部错误 |
| 507 | 磁盘空间不足 |

---

**计划完成**
