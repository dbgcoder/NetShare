# NetShare API 文档

本文档详细描述 NetShare HTTP API 的所有接口。

---

## 📋 目录

- [概述](#概述)
- [认证](#认证)
- [文件接口](#文件接口)
- [分享接口](#分享接口)
- [传输接口](#传输接口)
- [设置接口](#设置接口)
- [错误响应](#错误响应)

---

## 概述

### 基础信息

| 属性 | 值 |
|------|---|
| 基础 URL | `http://localhost:8080/api` |
| HTTPS URL | `https://localhost:8080/api`（TLS 启用时同端口） |
| WebSocket URL | `ws://localhost:8080/ws`（与 HTTP 同端口） |
| WSS URL | `wss://localhost:8080/ws`（TLS 启用时） |
| 内容类型 | `application/json` |
| 字符编码 | UTF-8 |
| HTTP 服务器 | CivetWeb 嵌入式 Web 服务器 |
| CORS | 所有响应自动附带 CORS 头 |

### 请求格式

```http
GET /api/resource HTTP/1.1
Host: localhost:8080
Accept: application/json
Authorization: Bearer <token>
```

```http
POST /api/resource HTTP/1.1
Host: localhost:8080
Content-Type: application/json
Authorization: Bearer <token>

{
    "key": "value"
}
```

### 响应格式

```json
{
    "code": 0,
    "message": "success",
    "data": {}
}
```

### 通用头信息

| 头信息 | 说明 |
|--------|------|
| `X-Request-ID` | 请求唯一标识 |
| `X-Total-Count` | 列表总数 (分页) |
| `X-Page-Number` | 当前页码 |
| `X-Page-Size` | 每页大小 |

---

## 认证

### 访问令牌

部分接口需要认证才能访问。使用以下方式提供令牌:

```http
Authorization: Bearer <access_token>
```

---

## 文件接口

### 1. 获取文件列表

获取共享文件夹中的文件列表。

**请求**

```http
GET /api/files
```

**Query 参数**

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `path` | string | 否 | 浏览路径，默认根目录 |
| `page` | int | 否 | 页码，默认 1 |
| `pageSize` | int | 否 | 每页数量，默认 50 |
| `sortBy` | string | 否 | 排序字段: name, size, time |
| `sortOrder` | string | 否 | 排序方向: asc, desc |

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "items": [
            {
                "id": "file-001",
                "name": "document.pdf",
                "path": "/shared/documents/document.pdf",
                "type": "file",
                "size": 1048576,
                "mimeType": "application/pdf",
                "createdAt": "2024-01-15T10:30:00Z",
                "modifiedAt": "2024-01-15T10:30:00Z",
                "thumbnail": null
            },
            {
                "id": "folder-001",
                "name": "documents",
                "path": "/shared/documents",
                "type": "folder",
                "size": 0,
                "fileCount": 15,
                "createdAt": "2024-01-10T08:00:00Z",
                "modifiedAt": "2024-01-15T10:30:00Z"
            }
        ],
        "total": 100,
        "page": 1,
        "pageSize": 50,
        "totalPages": 2
    }
}
```

---

### 2. 获取文件信息

获取单个文件的详细信息。

**请求**

```http
GET /api/files/{fileId}
```

**路径参数**

| 参数 | 类型 | 说明 |
|------|------|------|
| `fileId` | string | 文件 ID 或路径 (URL 编码) |

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "id": "file-001",
        "name": "document.pdf",
        "path": "/shared/documents/document.pdf",
        "type": "file",
        "size": 1048576,
        "mimeType": "application/pdf",
        "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "chunkCount": 1,
        "createdAt": "2024-01-15T10:30:00Z",
        "modifiedAt": "2024-01-15T10:30:00Z",
        "accessedAt": "2024-01-15T12:00:00Z",
        "permissions": {
            "read": true,
            "write": false,
            "delete": false
        }
    }
}
```

---

### 3. 获取文件夹内容

获取文件夹下的所有文件和子文件夹。

**请求**

```http
GET /api/files/browse
```

**Query 参数**

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `path` | string | 是 | 文件夹路径 |
| `includeHidden` | bool | 否 | 是否包含隐藏文件，默认 false |

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "path": "/shared/documents",
        "parentPath": "/shared",
        "items": [
            {
                "name": "file1.txt",
                "type": "file",
                "size": 1024
            },
            {
                "name": "subfolder",
                "type": "folder",
                "itemCount": 5
            }
        ],
        "totalSize": 10485760,
        "fileCount": 15,
        "folderCount": 3
    }
}
```

---

### 4. 创建文件夹

创建新文件夹。

**请求**

```http
POST /api/files/folders
```

**请求体**

```json
{
    "path": "/shared/documents/newfolder",
    "mkdirParents": true
}
```

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "path": "/shared/documents/newfolder",
        "created": true
    }
}
```

---

### 5. 删除文件/文件夹

删除指定文件或文件夹。

**请求**

```http
DELETE /api/files/{fileId}
```

**请求体**

```json
{
    "path": "/shared/documents/file.txt"
}
```

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "deleted": true,
        "path": "/shared/documents/file.txt"
    }
}
```

---

### 6. 搜索文件

搜索共享文件。

**请求**

```http
GET /api/files/search
```

**Query 参数**

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `q` | string | 是 | 搜索关键词 |
| `type` | string | 否 | 文件类型: file, folder, all |
| `maxResults` | int | 否 | 最大结果数，默认 50 |

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "query": "document",
        "results": [
            {
                "id": "file-001",
                "name": "document.pdf",
                "path": "/shared/documents/document.pdf",
                "type": "file",
                "size": 1048576,
                "relevance": 0.95
            }
        ],
        "total": 1
    }
}
```

---

## 分享接口

### 1. 创建分享

创建文件或文件夹分享。

**请求**

```http
POST /api/shares
```

**请求体**

```json
{
    "path": "/shared/documents/report.pdf",
    "type": "file",
    "expireHours": 24,
    "maxDownloads": 0,
    "password": "",
    "allowUpload": false,
    "description": "项目报告"
}
```

**参数说明**

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `path` | string | 是 | 要分享的文件/文件夹路径 |
| `type` | string | 是 | 类型: file, folder |
| `expireHours` | int | 否 | 过期小时数，0 表示永不过期，默认 24 |
| `maxDownloads` | int | 否 | 最大下载次数，0 表示无限制，默认 0 |
| `password` | string | 否 | 访问密码，不设置则无密码 |
| `allowUpload` | bool | 否 | 是否允许上传 (仅文件夹) |
| `description` | string | 否 | 分享描述 |

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "shareId": "shr-abc123",
        "token": "a1b2c3d4e5f6",
        "url": "http://192.168.1.100:8080/s/a1b2c3d4e5f6",
        "qrcode": "data:image/png;base64,...",
        "path": "/shared/documents/report.pdf",
        "type": "file",
        "size": 1048576,
        "expireTime": "2024-01-16T10:30:00Z",
        "maxDownloads": 0,
        "downloadCount": 0,
        "passwordRequired": false,
        "createdAt": "2024-01-15T10:30:00Z"
    }
}
```

---

### 2. 获取分享信息

获取分享的详细信息。

**请求**

```http
GET /api/shares/{shareId}
```

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "shareId": "shr-abc123",
        "token": "a1b2c3d4e5f6",
        "path": "/shared/documents/report.pdf",
        "type": "file",
        "size": 1048576,
        "expireTime": "2024-01-16T10:30:00Z",
        "maxDownloads": 0,
        "downloadCount": 5,
        "passwordRequired": true,
        "description": "项目报告",
        "createdAt": "2024-01-15T10:30:00Z",
        "lastAccessedAt": "2024-01-15T15:00:00Z"
    }
}
```

---

### 3. 获取我的分享列表

获取当前用户创建的所有分享。

**请求**

```http
GET /api/shares
```

**Query 参数**

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `status` | string | 否 | 状态: active, expired, all |
| `page` | int | 否 | 页码 |
| `pageSize` | int | 否 | 每页数量 |

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "items": [
            {
                "shareId": "shr-abc123",
                "token": "a1b2c3d4e5f6",
                "path": "/shared/documents/report.pdf",
                "type": "file",
                "size": 1048576,
                "status": "active",
                "downloadCount": 5,
                "expireTime": "2024-01-16T10:30:00Z",
                "createdAt": "2024-01-15T10:30:00Z"
            }
        ],
        "total": 10,
        "page": 1,
        "pageSize": 20
    }
}
```

---

### 4. 验证分享访问

验证分享密码或其他访问条件。

**请求**

```http
POST /api/shares/{shareId}/verify
```

**请求体**

```json
{
    "password": "123456"
}
```

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "valid": true,
        "token": "session-token-xyz"
    }
}
```

---

### 5. 取消分享

取消一个分享链接。

**请求**

```http
DELETE /api/shares/{shareId}
```

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "cancelled": true,
        "shareId": "shr-abc123"
    }
}
```

---

### 6. 公开访问分享 (无需认证)

获取分享内容列表。

**请求**

```http
GET /s/{token}
```

**Query 参数**

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `password` | string | 条件 | 密码 (如果需要) |

**响应**

```html
<!DOCTYPE html>
<html>
<head>
    <title>NetShare - 下载</title>
</head>
<body>
    <h1>报告.pdf</h1>
    <p>大小: 1.0 MB</p>
    <button id="downloadBtn">下载</button>
</body>
</html>
```

---

## 传输接口

### 1. 获取传输任务列表

获取当前传输任务列表。

**请求**

```http
GET /api/transfers
```

**Query 参数**

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `type` | string | 否 | 类型: download, upload, all |
| `status` | string | 否 | 状态: active, completed, failed, all |

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "items": [
            {
                "taskId": "task-001",
                "type": "download",
                "fileName": "report.pdf",
                "fileSize": 104857600,
                "downloadedSize": 52428800,
                "progress": 50,
                "speed": 1048576,
                "status": "active",
                "threads": 3,
                "chunks": [
                    {"index": 0, "status": "completed", "size": 4194304},
                    {"index": 1, "status": "completed", "size": 4194304},
                    {"index": 2, "status": "downloading", "size": 4194304, "progress": 50}
                ],
                "error": null,
                "createdAt": "2024-01-15T10:30:00Z",
                "completedAt": null
            }
        ],
        "activeCount": 2,
        "completedCount": 10,
        "failedCount": 1
    }
}
```

---

### 2. 获取传输统计

获取传输统计信息。

**请求**

```http
GET /api/transfers/stats
```

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "today": {
            "downloadCount": 5,
            "downloadSize": 524288000,
            "uploadCount": 3,
            "uploadSize": 104857600
        },
        "total": {
            "downloadCount": 100,
            "downloadSize": 10737418240,
            "uploadCount": 50,
            "uploadSize": 2147483648
        },
        "bandwidth": {
            "currentDownload": 2097152,
            "currentUpload": 524288,
            "maxDownload": 10485760,
            "maxUpload": 5242880
        }
    }
}
```

---

### 3. 开始下载

开始一个文件下载任务。

**请求**

```http
POST /api/transfers/download
```

**请求体**

```json
{
    "shareId": "shr-abc123",
    "savePath": "C:/Users/Downloads",
    "threads": 3,
    "resume": true
}
```

**参数说明**

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `shareId` | string | 是 | 分享 ID |
| `savePath` | string | 否 | 保存路径，默认下载目录 |
| `threads` | int | 否 | 下载线程数，默认 3 |
| `resume` | bool | 否 | 是否支持断点续传，默认 true |

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "taskId": "task-002",
        "fileName": "report.pdf",
        "fileSize": 104857600,
        "savePath": "C:/Users/Downloads/report.pdf",
        "threads": 3,
        "chunkSize": 4194304,
        "status": "preparing"
    }
}
```

---

### 4. 暂停下载

暂停下载任务。

**请求**

```http
POST /api/transfers/{taskId}/pause
```

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "taskId": "task-002",
        "status": "paused",
        "downloadedSize": 52428800
    }
}
```

---

### 5. 继续下载

继续暂停的下载任务。

**请求**

```http
POST /api/transfers/{taskId}/resume
```

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "taskId": "task-002",
        "status": "active"
    }
}
```

---

### 6. 取消下载

取消下载任务。

**请求**

```http
POST /api/transfers/{taskId}/cancel
```

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "taskId": "task-002",
        "status": "cancelled"
    }
}
```

---

### 7. 获取传输日志

获取传输操作日志。

**请求**

```http
GET /api/transfers/logs
```

**Query 参数**

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `taskId` | string | 否 | 任务 ID |
| `startTime` | string | 否 | 开始时间 ISO8601 |
| `endTime` | string | 否 | 结束时间 ISO8601 |
| `page` | int | 否 | 页码 |
| `pageSize` | int | 否 | 每页数量 |

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "items": [
            {
                "id": "log-001",
                "taskId": "task-002",
                "action": "download_started",
                "details": "开始下载 report.pdf",
                "timestamp": "2024-01-15T10:30:00Z"
            },
            {
                "id": "log-002",
                "taskId": "task-002",
                "action": "chunk_completed",
                "details": "分块 0 下载完成",
                "timestamp": "2024-01-15T10:31:00Z"
            }
        ],
        "total": 100
    }
}
```

---

## 设置接口

### 1. 获取配置

获取当前配置。

**请求**

```http
GET /api/settings
```

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "server": {
            "port": 8080,
            "tlsEnabled": false,
            "bindAddress": "0.0.0.0"
        },
        "transfer": {
            "maxParallelTasks": 2,
            "maxThreadsPerTask": 3,
            "chunkSize": 4194304
        },
        "ui": {
            "language": "zh_CN",
            "theme": "system"
        }
    }
}
```

---

### 2. 更新配置

更新配置项。

**请求**

```http
PUT /api/settings
```

**请求体**

```json
{
    "server.port": 9090,
    "transfer.maxParallelTasks": 4
}
```

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "updated": true
    }
}
```

---

### 3. 重置配置

重置所有配置为默认值。

**请求**

```http
POST /api/settings/reset
```

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "reset": true
    }
}
```

---

### 4. 获取共享文件夹列表

获取所有共享文件夹。

**请求**

```http
GET /api/settings/shared-folders
```

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "items": [
            {
                "path": "D:\\Shared",
                "name": "Shared",
                "description": "共享文件夹",
                "enabled": true,
                "createdAt": "2024-01-01T00:00:00Z"
            }
        ]
    }
}
```

---

### 5. 添加共享文件夹

添加新的共享文件夹。

**请求**

```http
POST /api/settings/shared-folders
```

**请求体**

```json
{
    "path": "E:\\Public",
    "name": "Public",
    "description": "公共文件夹"
}
```

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "added": true,
        "path": "E:\\Public"
    }
}
```

---

### 6. 移除共享文件夹

移除共享文件夹。

**请求**

```http
DELETE /api/settings/shared-folders/{path}
```

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "removed": true,
        "path": "E:\\Public"
    }
}
```

---

### 7. 获取服务器状态

获取服务器运行状态。

**请求**

```http
GET /api/status
```

**响应**

```json
{
    "code": 0,
    "message": "success",
    "data": {
        "version": "1.0.0",
        "uptime": 86400,
        "status": "running",
        "network": {
            "localIp": "192.168.1.100",
            "port": 8080,
            "connections": 5
        },
        "system": {
            "cpuUsage": 15.5,
            "memoryUsage": 45.2,
            "diskUsage": 67.8
        }
    }
}
```

---

## 错误响应

### 错误格式

```json
{
    "code": 1001,
    "message": "文件不存在",
    "error": {
        "code": "NS-002001-001",
        "details": "请求的文件路径不存在",
        "suggestion": "请检查文件路径是否正确",
        "requestId": "req-123456"
    }
}
```

### 错误码参考

详见 [ERROR_CODES.md](ERROR_CODES.md)

---

## WebSocket 接口

> CivetWeb 迁移后，WebSocket 与 HTTP 共用同一端口，端点路径为 `/ws`。
> 支持 `ws://` 和 `wss://`（TLS 启用时）协议。

### 连接

```http
ws://localhost:8080/ws
```

TLS 启用时：

```http
wss://localhost:8080/ws
```

### 订阅传输进度

连接后发送订阅消息，绑定到特定分享 token：

```json
{
    "type": "subscribe",
    "data": {
        "token": "a1b2c3d4e5f6"
    }
}
```

### 取消订阅

```json
{
    "type": "unsubscribe",
    "data": {
        "token": "a1b2c3d4e5f6"
    }
}
```

### 接收进度更新

服务器会向订阅了特定 token 的客户端广播传输进度：

```json
{
    "type": "transfer_update",
    "data": {
        "progress": 50,
        "speed": 1048576,
        "taskId": "task-001"
    }
}
```

### 心跳保活

服务器每 30 秒发送 WebSocket Ping 帧，客户端应回复 Pong。
若 60 秒内未收到 Pong，服务器将断开连接。
客户端断开后会自动清理订阅关系。
