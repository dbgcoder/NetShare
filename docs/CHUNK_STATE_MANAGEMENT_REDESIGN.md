# 分块状态管理重设计计划

> 核心原则：参考 IDM / 迅雷，建立**独立的分块状态管理文件**，作为续传的**唯一权威数据源**。
> 传输过程中**实时更新**分块状态，续传时**严格依据状态文件**决定从哪个分块开始。

---

## 1 现状问题分析

### 1.1 当前架构缺陷

| # | 问题 | 现状 | 后果 |
|---|------|------|------|
| 1 | **无独立状态文件** | 分块完成状态仅存内存（`UploadSession.fileChunkStates`），进程退出即丢失 | 重启后续传无法知道哪些分块已完成 |
| 2 | **ResumeManager 未使用** | `saveResumeInfo()` 从未被调用，`ResumeInfo.completedChunks` 永远为空 | 下载续传功能名存实亡 |
| 3 | **上传续传靠扫描猜测** | `handleUploadCheck` 扫描 `.chunks/` 目录，用 `fi.size() == expectedSize` 猜测分块是否完成 | 不完整分块可能被误判为完成；扫描逻辑复杂且不可靠 |
| 4 | **下载续传靠文件存在性** | `performDownload` 仅检查 chunk 文件是否存在且大小匹配 | 无状态记录，无法区分"正在传输"和"传输中断" |
| 5 | **无实时状态更新** | 分块传输过程中不更新任何持久化状态 | 中断后无法精确知道哪些分块已确认完成 |
| 6 | **上传状态仅存内存** | `UploadSession.fileChunkStates` 在服务端内存中 | 服务重启后上传会话全部丢失，无法续传 |
| 7 | **TransferWorker index 硬编码** | `chunkFinished` 信号 index 固定为 0，无法区分是哪个分块 | 无法精确更新单个分块状态 |
| 8 | **deleteTask 无临时文件清理** | 删除任务只清内存和数据库，不清理临时分块目录和状态文件 | 磁盘残留临时文件 |

### 1.2 IDM / 迅雷参考分析

#### IDM 的做法

```
下载开始时：
  1. 创建 <文件名>.info 状态文件（与临时分块同目录）
  2. 状态文件记录：文件URL、总大小、分块数、每块的偏移/大小/完成状态
  3. 创建 <文件名>.part 临时文件（预分配全部空间）

下载过程中：
  4. 每个分块写入完成后，立即更新 .info 文件中该分块的状态为 completed
  5. 多线程并行下载，每个线程负责不同分块范围

下载中断/暂停时：
  6. 正在传输的分块标记为 partial（记录已下载字节数）
  7. .info 文件实时反映中断时刻的精确状态

续传时：
  8. 读取 .info 文件 → 跳过 completed 分块 → 从第一个非 completed 分块开始
  9. partial 分块：如果支持 Range 则断点续传，否则删除重传

下载完成时：
  10. 合并分块 → 重命名为最终文件名 → 删除 .info 和 .part 文件
```

#### 迅雷的做法

```
与 IDM 类似，额外特点：
  - 使用 .cfg 和 .td 文件组合
  - .cfg 记录任务元信息和分块状态
  - .td 为临时数据文件
  - 支持 MD5/SHA1 校验，分块完成后可验证完整性
  - 续传时先校验已完成分块的完整性，损坏的降级为未完成
```

#### 共同核心原则

| 原则 | 说明 |
|------|------|
| **状态文件是唯一权威** | 续传判断完全依据状态文件，不依赖文件存在性猜测 |
| **实时持久化** | 每个分块完成/中断时立即写入状态文件，不等任务结束 |
| **状态文件按文件名定位** | 状态文件以文件名为标识，不依赖 taskId/sessionId（两者每次请求都变） |
| **completed 分块需验证** | 续传前验证已完成分块文件大小，不一致则降级 |
| **partial 分块精确记录** | 记录已下载字节数，支持断点续传或决定是否重传 |

---

## 2 重设计方案

### 2.1 状态文件格式

参考 IDM 的 `.info` 文件，采用 JSON 格式。

**文件命名**：`<fileName>.netshare`

**关键设计：状态文件按文件名定位，不依赖 taskId / sessionId**

> taskId 和 sessionId 每次请求都会重新生成，不能用于跨会话定位状态文件。
> 状态文件必须以 `fileName` 为标识，放在稳定的目录下，确保续传时能找到。

**存放位置**：

| 场景 | 状态文件路径 | 说明 |
|------|-------------|------|
| 下载 | `<tempDir>/<fileName>.netshare` | `tempDir` = `QStandardPaths::TempLocation + "/NetShare"`，与分块目录同级 |
| 上传 | `<uploadDir>/.chunks/<fileName>.netshare` | `.chunks/` 目录下，不放在 sessionId 子目录内 |

**下载目录结构示例**：

```
C:/Users/xxx/AppData/Local/Temp/NetShare/
├── OPT Camera Demo16.exe.netshare    ← 状态文件
├── a1b2c3d4-.../                     ← taskId 分块目录（第一次下载）
│   ├── chunk_000000
│   ├── chunk_000001
│   └── ...
└── e5f67890-.../                     ← taskId 分块目录（续传时新的 taskId）
    ├── chunk_000000                  ← 从旧目录复制来的已完成分块
    └── ...
```

**上传目录结构示例**：

```
C:/Users/xxx/Downloads/.chunks/
├── OPT Camera Demo16.exe.netshare    ← 状态文件（不在 sessionId 目录内）
├── a1b2c3d4-.../                     ← sessionId 分块目录（第一次上传）
│   └── OPT Camera Demo16.exe/
│       ├── chunk_000000
│       └── ...
└── e5f67890-.../                     ← sessionId 分块目录（续传时新的 sessionId）
    └── OPT Camera Demo16.exe/
        ├── chunk_000000              ← 从旧目录复制来的已完成分块
        └── ...
```

**下载状态文件示例**：

```json
{
  "version": 1,
  "taskId": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
  "type": "download",
  "fileName": "OPT Camera Demo16.exe",
  "fileSize": 115874808,
  "chunkSize": 4194304,
  "totalChunks": 28,
  "transferredSize": 12582912,
  "status": "downloading",
  "url": "http://192.168.1.100:8080/download/abc123/",
  "savePath": "C:/Users/xxx/Downloads",
  "chunkDir": "C:/Users/xxx/AppData/Local/Temp/NetShare/a1b2c3d4-...",
  "createdAt": "2026-05-27T20:00:00",
  "lastUpdated": "2026-05-27T20:05:30",
  "chunks": [
    { "index": 0,  "offset": 0,        "size": 4194304, "status": "completed", "downloaded": 4194304 },
    { "index": 1,  "offset": 4194304,   "size": 4194304, "status": "completed", "downloaded": 4194304 },
    { "index": 2,  "offset": 8388608,   "size": 4194304, "status": "completed", "downloaded": 4194304 },
    { "index": 3,  "offset": 12582912,  "size": 4194304, "status": "partial",   "downloaded": 2097152 },
    { "index": 4,  "offset": 16777216,  "size": 4194304, "status": "pending",   "downloaded": 0 },
    { "index": 27, "offset": 113246208, "size": 2628600, "status": "pending",   "downloaded": 0 }
  ]
}
```

**上传状态文件示例**：

```json
{
  "version": 1,
  "taskId": "f0e1d2c3-b4a5-6789-0abc-def123456789",
  "type": "upload",
  "fileName": "OPT Camera Demo16.exe",
  "fileSize": 115874808,
  "chunkSize": 4194304,
  "totalChunks": 28,
  "transferredSize": 12582912,
  "status": "uploading",
  "remoteAddress": "192.168.1.100",
  "savePath": "C:/Users/xxx/Downloads",
  "chunkDir": "C:/Users/xxx/Downloads/.chunks/f0e1d2c3-.../OPT Camera Demo16.exe",
  "createdAt": "2026-05-27T20:00:00",
  "lastUpdated": "2026-05-27T20:05:30",
  "chunks": [
    { "index": 0,  "offset": 0,        "size": 4194304, "status": "completed", "downloaded": 4194304 },
    { "index": 1,  "offset": 4194304,   "size": 4194304, "status": "completed", "downloaded": 4194304 },
    { "index": 2,  "offset": 8388608,   "size": 4194304, "status": "completed", "downloaded": 4194304 },
    { "index": 3,  "offset": 12582912,  "size": 4194304, "status": "uploading", "downloaded": 2097152 },
    { "index": 4,  "offset": 16777216,  "size": 4194304, "status": "pending",   "downloaded": 0 },
    { "index": 27, "offset": 113246208, "size": 2628600, "status": "pending",   "downloaded": 0 }
  ]
}
```

**多文件上传场景**：一个上传会话包含多个文件时，每个文件有独立的状态文件：

```
C:/Users/xxx/Downloads/.chunks/
├── file1.exe.netshare     ← 文件1的状态文件
├── file2.zip.netshare     ← 文件2的状态文件
└── <sessionId>/
    ├── file1.exe/
    │   └── chunk_000000
    └── file2.zip/
        └── chunk_000000
```

### 2.2 分块状态定义

| 状态 | 含义 | 分块文件状态 | 说明 |
|------|------|-------------|------|
| `pending` | 未开始传输 | 不存在 | 初始状态 |
| `downloading` | 正在下载中 | 存在，大小 < 期望值 | 仅下载场景 |
| `uploading` | 正在上传中 | 存在，大小 < 期望值 | 仅上传场景 |
| `partial` | 传输中断 | 存在，大小 ≤ 期望值 | 中断/暂停时从 downloading/uploading 转入 |
| `completed` | 传输完成 | 存在，大小 == 期望值 | 已确认完成 |
| `failed` | 传输失败 | 可能存在，内容不可靠 | 需删除重传 |

**状态转换图**：

```
                          ┌──────────────────────────────────┐
                          │                                  │
                          ▼                                  │
  ┌─────────┐  开始传输  ┌──────────────┐  传输完成  ┌───────────┐
  │ pending  │─────────▶│ downloading/ │─────────▶│ completed │
  │         │          │  uploading   │          │           │
  └─────────┘          └──────┬───────┘          └───────────┘
       │                      │                       ▲
       │                      │ 中断/暂停              │
       │                      ▼                       │
       │               ┌───────────┐                  │
       └──────────────▶│  partial  │──────────────────┘
                       │           │   续传完成
                       └─────┬─────┘
                         ▲   │
                         │   │ 续传恢复
                         │   ▼
                         │ ┌──────────────┐
                         │ │ downloading/ │
                         └─│  uploading   │
                           └──────┬───────┘
                                  │ 传输错误
                                  ▼
                           ┌───────────┐
                           │  failed   │──重试──▶ downloading/uploading
                           └───────────┘
```

**关键转换规则**：

- `downloading` / `uploading` → `partial`：进程异常退出时，状态文件中仍为 downloading/uploading 的分块，续传时统一当作 `partial` 处理
- `partial` → `downloading` / `uploading`：续传恢复时，先检查分块文件实际大小更新 downloaded 值，再标记为传输中
- `failed` → `downloading` / `uploading`：重试时删除不可靠的分块文件，从头传输
- `completed` 是终态，除非验证发现文件损坏才降级为 `pending`

### 2.3 数据结构统一

**ChunkState 定义在独立头文件 `ChunkState.h`**

`ChunkState` 和 `ChunkStateInfo` 放在独立的最小化头文件 `src/core/transfer/ChunkState.h` 中，`ChunkManager.h`、`ChunkStateManager.h`、`RequestHandler.h` 均包含此头文件，避免类型定义循环依赖。

```cpp
// src/core/transfer/ChunkState.h
#ifndef CHUNKSTATE_H
#define CHUNKSTATE_H

#include <QObject>
#include <QString>
#include <QList>
#include <QDateTime>
#include <QVariantList>

struct ChunkState
{
    Q_GADGET
    Q_PROPERTY(int index MEMBER index)
    Q_PROPERTY(qint64 offset MEMBER offset)
    Q_PROPERTY(qint64 size MEMBER size)
    Q_PROPERTY(QString status MEMBER status)
    Q_PROPERTY(qint64 downloaded MEMBER downloaded)

public:
    int index = 0;
    qint64 offset = 0;
    qint64 size = 0;
    QString status = QStringLiteral("pending");
    qint64 downloaded = 0;
};

struct ChunkStateInfo
{
    Q_GADGET
    Q_PROPERTY(int version MEMBER version)
    Q_PROPERTY(QString taskId MEMBER taskId)
    Q_PROPERTY(QString type MEMBER type)
    Q_PROPERTY(QString fileName MEMBER fileName)
    Q_PROPERTY(qint64 fileSize MEMBER fileSize)
    Q_PROPERTY(int chunkSize MEMBER chunkSize)
    Q_PROPERTY(int totalChunks MEMBER totalChunks)
    Q_PROPERTY(qint64 transferredSize MEMBER transferredSize)
    Q_PROPERTY(QString status MEMBER status)
    Q_PROPERTY(QString url MEMBER url)
    Q_PROPERTY(QString savePath MEMBER savePath)
    Q_PROPERTY(QString remoteAddress MEMBER remoteAddress)
    Q_PROPERTY(QString chunkDir MEMBER chunkDir)
    Q_PROPERTY(QString createdAt MEMBER createdAt)
    Q_PROPERTY(QString lastUpdated MEMBER lastUpdated)

public:
    int version = 1;
    QString taskId;
    QString type;
    QString fileName;
    qint64 fileSize = 0;
    int chunkSize = 0;
    int totalChunks = 0;
    qint64 transferredSize = 0;
    QString status = QStringLiteral("pending");
    QString url;
    QString savePath;
    QString remoteAddress;
    QString chunkDir;
    QString createdAt;
    QString lastUpdated;
    QList<ChunkState> chunks;
};

Q_DECLARE_METATYPE(ChunkState)
Q_DECLARE_METATYPE(ChunkStateInfo)

#endif // CHUNKSTATE_H
```

> 注：`Q_GADGET` + `Q_PROPERTY` + `Q_DECLARE_METATYPE` 确保 `ChunkState` 和 `ChunkStateInfo` 在 QML / QVariant 中可用。`splitFile` 返回 `QList<ChunkState>` 时 QML 可正确解析。

---

**ChunkInfo（ChunkManager.h）废弃，统一使用 ChunkState**

`ChunkInfo` 所有功能由 `ChunkState` 替代，`ChunkManager::splitFile` 返回类型从 `QList<ChunkInfo>` 改为 `QList<ChunkState>`。

| 对比项 | ChunkInfo（旧 → 废弃） | ChunkState（新） |
|--------|----------------------|-----------------|
| 用途 | 分块分割计算，纯内存 | 分块状态持久化，读写磁盘 |
| 状态类型 | 枚举 `Pending/Downloading/Completed/Failed` | 字符串 `"pending"/"downloading"/"uploading"/"partial"/"completed"/"failed"` |
| 额外字段 | 无 | `downloaded`（已传输字节数） |
| 生命周期 | `splitFile` 返回后即丢弃 | 贯穿整个传输过程，持久化到磁盘 |

**改造方式**：`ChunkState` 完全覆盖 `ChunkInfo` 的所有字段（index/offset/size），直接将 `splitFile` 返回类型改为 `QList<ChunkState>`，删除 `ChunkInfo` 定义。

---

**FileChunkState / ChunkUploadInfo（RequestHandler.h）废弃，统一使用 ChunkStateInfo + ChunkState**

| 对比项 | FileChunkState / ChunkUploadInfo（旧 → 废弃） | ChunkStateInfo + ChunkState（新） |
|--------|---------------------------------------------|---------------------------------|
| 用途 | 上传会话内存中的分块状态 | 磁盘上的持久化分块状态 + 内存缓存 |
| 生命周期 | 随 UploadSession 存在 | 跨会话持久化 |
| 状态粒度 | `completed: bool` | `status: string` + `downloaded: qint64` |

**改造方式**：

- 删除 `FileChunkState` 和 `ChunkUploadInfo` 结构体定义
- 上传会话中直接使用 `ChunkStateInfo` 作为内存状态缓存，通过 `ChunkStateManager` 加载/保存
- 每次内存状态变更时，立即调用 `ChunkStateManager::updateChunkStatus()` 持久化，不再维护两套状态

### 2.4 状态文件更新时机（实时更新）

> **核心要求：每个分块状态变化时立即持久化到状态文件，与 IDM/迅雷一致。**

#### 下载场景更新时机

| 事件 | 更新内容 | 触发位置 |
|------|---------|---------|
| 任务创建 | 创建状态文件，所有 chunks 为 `pending`，status 为 `downloading` | `performDownload` 开始时 |
| 分块开始下载 | `chunks[i].status = "downloading"` | TransferWorker 启动时 |
| **分块下载完成** | **`chunks[i].status = "completed"`，`chunks[i].downloaded = size`，累加 `transferredSize`** | **TransferWorker::chunkFinished 成功回调** |
| 分块下载失败 | `chunks[i].status = "failed"` | TransferWorker::chunkFinished 失败回调 |
| 任务暂停 | status 为 `paused`，所有 `downloading` 的 chunks 改为 `partial` | `pauseTask` |
| 任务完成 | 删除状态文件 + 临时分块目录 | `performDownload` 合并完成后 |
| 任务删除 | 删除状态文件 + 临时分块目录 | `deleteTask` |

#### 上传场景更新时机

| 事件 | 更新内容 | 触发位置 |
|------|---------|---------|
| 上传会话创建 | 创建状态文件，所有 chunks 为 `pending`，status 为 `uploading` | `handleUploadCheck` |
| 分块开始上传 | `chunks[i].status = "uploading"` | `handleUploadData` 收到分块数据开始写入时 |
| **分块上传完成** | **`chunks[i].status = "completed"`，`chunks[i].downloaded = size`，累加 `transferredSize`** | **分块写入完成 + verifyChunk 通过后** |
| 分块上传失败 | `chunks[i].status = "failed"` | 分块写入失败或校验失败时 |
| 上传暂停 | status 为 `paused`，所有 `uploading` 的 chunks 改为 `partial` | 暂停回调 |
| 上传完成 | 删除状态文件 | `handleUploadFinalize` 合并完成后 |
| 上传删除 | 删除状态文件 + 临时分块目录 | `deleteTask` |
| 客户端断开连接 | 所有 `uploading` 的 chunks 改为 `partial`，持久化 | `streamingConnDisconnected` 回调 |

#### 非分块小文件的状态管理

小于 `CHUNK_THRESHOLD` 的文件不走分块逻辑，直接通过 `StreamingMultipartParser` 写入。

**状态文件处理**：

| 场景 | 处理方式 |
|------|---------|
| 非分块文件上传 | 不创建 `.netshare` 状态文件。`StreamingMultipartParser` 已有 `setResumeOffset` 支持断点续传，通过文件大小判断续传位置即可 |
| 非分块文件下载 | 不创建 `.netshare` 状态文件。小文件直接整体下载，无需分块状态管理 |

**理由**：非分块文件只有一个数据块，不存在"从哪个分块开始续传"的问题，状态文件无额外价值。

### 2.5 续传逻辑

> **核心原则：续传严格依据状态文件，状态文件是唯一权威数据源。**

#### 下载续传流程

```
performDownload(taskId, info, savePath, threads):
│
├─ 1. 构造状态文件路径：<tempDir>/<fileName>.netshare
│     （按 fileName 定位，不按 taskId）
│
├─ 2. 状态文件存在？
│   ├─ 是 → ChunkStateManager::loadStateFile() 加载
│   │       ├─ 加载成功 → 校验 version、fileSize、totalChunks 是否匹配
│   │       │             ├─ 匹配 → 使用加载的状态
│   │       │             └─ 不匹配 → 文件已变化，删除旧状态文件，创建新状态文件
│   │       └─ 加载失败（文件损坏）→ 删除损坏文件，回退到扫描分块目录重建状态
│   └─ 否 → 创建新状态文件，所有 chunks 为 pending
│
├─ 3. 更新状态文件中的 taskId 和 chunkDir 为当前值
│     （taskId 每次请求都变，但状态文件按 fileName 定位，taskId 仅作记录）
│
├─ 4. 遍历 chunks 数组，确定每个分块的续传策略：
│   ├─ status == "completed"
│   │   └─ 验证分块文件存在且大小 == chunk.size
│   │       ├─ 验证通过 → 跳过该分块
│   │       └─ 验证失败 → 降级为 pending，删除损坏分块文件
│   │
│   ├─ status == "partial" 或 "downloading"（异常退出时可能残留）
│   │   └─ 检查分块文件实际大小
│   │       ├─ 文件存在且 downloaded > 0 → 更新 downloaded 为实际文件大小
│   │       │   → 使用 HTTP Range: bytes=<downloaded>- 断点续传
│   │       └─ 文件不存在或 downloaded == 0 → 标记为 pending，从头传输
│   │
│   ├─ status == "pending"
│   │   └─ 从头传输该分块
│   │
│   └─ status == "failed"
│       └─ 删除不可靠的分块文件，标记为 pending，从头传输
│
├─ 5. 将已完成分块从旧 chunkDir 复制到新 chunkDir（taskId 变了）
│     └─ 复制完成后删除旧 chunkDir
│
├─ 6. 更新 transferredSize = 所有 completed 分块的 size 之和
│
├─ 7. 保存更新后的状态文件
│
├─ 8. 开始传输未完成的分块（跳过 completed 的）
│   └─ 每个分块完成 → 立即调用 ChunkStateManager::updateChunkStatus("completed")
│   └─ 每个分块失败 → 立即调用 ChunkStateManager::updateChunkStatus("failed")
│
└─ 9. 全部分块完成 → 合并 → 删除状态文件和当前 chunkDir
```

#### 上传续传流程

```
handleUploadCheck(conn, info):
│
├─ 1. 构造状态文件路径：<uploadDir>/.chunks/<fileName>.netshare
│     （按 fileName 定位，不按 sessionId）
│
├─ 2. 状态文件存在？
│   ├─ 是 → ChunkStateManager::loadStateFile() 加载
│   │       ├─ 加载成功 → 校验 fileSize、totalChunks 是否匹配当前上传请求
│   │       │             ├─ 匹配 → 使用加载的状态，获取已完成分块列表
│   │       │             └─ 不匹配 → 文件已变化，删除旧状态文件，创建新状态文件
│   │       └─ 加载失败 → 删除损坏文件，回退到扫描分块目录重建状态
│   └─ 否 → 创建新状态文件，所有 chunks 为 pending
│
├─ 3. 更新状态文件中的 taskId 和 chunkDir 为当前 session 的值
│
├─ 4. 遍历 chunks 数组，确定每个分块状态：
│   ├─ status == "completed" → 验证分块文件，通过则跳过
│   ├─ status == "partial" / "uploading" → 检查文件大小，更新 downloaded
│   └─ status == "pending" / "failed" → 需要重新上传
│
├─ 5. 将已完成分块从旧 session 的 chunkDir 复制到新 session 的 chunkDir
│     └─ 复制完成后删除旧 session 的 chunkDir
│
├─ 6. 构造返回给浏览器的 partial 信息（从状态文件读取，不再扫描猜测）
│   └─ completedChunks: [0, 1, 2]  ← 直接从状态文件读取
│   └─ completedBytes: 12582912    ← 直接从状态文件读取
│
└─ 7. 保存更新后的状态文件
```

```
handleUploadData / handleUploadSingleFile（分块上传完成时）:
│
├─ 1. 分块写入完成 + verifyChunk 通过
│
├─ 2. 更新内存状态：info.chunks[chunkIndex].status = "completed"
│
├─ 3. 立即持久化：ChunkStateManager::updateChunkStatus("completed")
│   └─ 更新 chunks[i].status = "completed"
│   └─ 更新 chunks[i].downloaded = size
│   └─ 累加 transferredSize
│   └─ 写入磁盘
│
└─ 4. 返回给浏览器：{ success: true, completedChunks: N, totalChunks: M }
```

#### 服务重启后的续传恢复

```
应用启动时：
│
├─ 1. 扫描下载临时目录（<tempDir>/），查找所有 *.netshare 文件
│   ├─ 读取状态文件
│   ├─ status == "downloading" / "paused" → 创建可续传的下载任务
│   └─ status == "completed" → 清理状态文件和临时分块
│
├─ 2. 扫描上传临时目录（<uploadDir>/.chunks/），查找所有 *.netshare 文件
│   ├─ 读取状态文件
│   ├─ status == "uploading" / "paused" → 创建可续传的上传任务
│   └─ status == "completed" → 清理状态文件和临时分块
│
└─ 3. 在传输列表中显示可续传任务，用户可选择续传或删除
```

#### 数据库与状态文件合并策略

启动恢复时同时扫描数据库（`transfer_logs` 表）和 `.netshare` 状态文件，**状态文件为权威数据源**。合并规则如下：

| 场景 | DB 有记录 | `.netshare` 有记录 | 合并策略 |
|------|----------|-------------------|---------|
| 正常恢复 | 有，status=Paused/Started | 有，信息一致 | 合并展示，以状态文件的 `transferredSize` 为准 |
| DB 残留 | 有，status=Paused/Started | 无（文件被手动删除） | 标记任务为失败，删除 DB 记录 |
| 状态文件孤儿 | 无 | 有，status=downloading/uploading/paused | 从状态文件创建新任务，写入 DB |
| 已完成残留 | 有，status=Completed | 有（异常未清理） | 删除状态文件；若 DB 记录类型是 Download 则检查下载文件是否已存在 |
| 进度不一致 | 有，transferredSize=X | 有，transferredSize=Y（Y > X） | **以状态文件的 Y 为准**，更新内存任务和 DB |

---

## 3 模块改造计划

### 3.1 ChunkStateManager（新增模块）

**职责**：独立管理分块状态文件的创建、读取、更新、删除，是续传的**唯一权威数据源**。

**文件位置**：`src/core/transfer/ChunkStateManager.h` / `.cpp`

**类接口**（`ChunkState` 和 `ChunkStateInfo` 定义在 `ChunkState.h`，详见 2.3 节）：

```cpp
#include "ChunkState.h"

class ChunkStateManager : public QObject
{
    Q_OBJECT

public:
    explicit ChunkStateManager(QObject* parent = nullptr);

    // 状态文件 CRUD
    bool createStateFile(const QString& stateFilePath, const ChunkStateInfo& info);
    bool loadStateFile(const QString& stateFilePath, ChunkStateInfo& info);
    bool saveStateFile(const QString& stateFilePath, const ChunkStateInfo& info);
    bool deleteStateFile(const QString& stateFilePath);

    // 分块状态实时更新（每次调用立即持久化到磁盘，线程安全）
    bool updateChunkStatus(const QString& stateFilePath, int chunkIndex,
                           const QString& status, qint64 downloaded);
    bool updateTaskStatus(const QString& stateFilePath, const QString& status);
    bool updateTransferredSize(const QString& stateFilePath, qint64 transferredSize);

    // 扫描可续传任务
    QList<ChunkStateInfo> scanResumableTasks(const QString& directory);

    // 分块状态验证（续传前调用）
    bool validateCompletedChunks(const QString& stateFilePath);

    // 清理过期状态文件
    void cleanupExpired(const QString& directory, int maxAgeDays = 7);

private:
    QMutex m_mutex;     // 保护状态文件的并发写入（跨线程安全）
};
```

**关键方法说明**：

| 方法 | 说明 |
|------|------|
| `createStateFile` | 创建新的状态文件，写入完整初始状态 |
| `loadStateFile` | 读取状态文件，返回 `ChunkStateInfo`；文件损坏返回 false |
| `saveStateFile` | 全量写入状态文件（用于批量更新后保存） |
| `updateChunkStatus` | **核心方法**：更新单个分块状态并立即持久化，用于实时更新。内部加 QMutex 保证线程安全 |
| `updateTaskStatus` | 更新任务整体状态（如暂停、完成）并持久化 |
| `validateCompletedChunks` | 续传前验证所有 completed 分块文件大小是否匹配，不匹配的降级 |
| `scanResumableTasks` | 扫描目录下所有 `.netshare` 文件，返回可续传任务列表 |

**线程安全策略**：

| 场景 | 线程 | 安全措施 |
|------|------|---------|
| 下载分块完成 → updateChunkStatus | QtConcurrent 工作线程 | QMutex 保护写入 |
| 上传分块完成 → updateChunkStatus | CivetWeb 工作线程 | QMutex 保护写入 |
| 上传暂停 → updateTaskStatus | UI 线程 | QMutex 保护写入 |
| 启动恢复 → scanResumableTasks | UI 线程 | 只读，无需加锁 |

### 3.2 ResumeManager（废弃 → 由 ChunkStateManager 替代）

| 对比项 | ResumeManager（旧） | ChunkStateManager（新） |
|--------|--------------------|-----------------------|
| 状态文件位置 | `AppData/resume/<taskId>.json` | `<tempDir>/<fileName>.netshare` 或 `<uploadDir>/.chunks/<fileName>.netshare` |
| 文件定位方式 | 按 taskId（每次请求都变，不可靠） | 按 fileName（稳定，跨会话可定位） |
| 记录粒度 | 仅 `completedChunks` 列表（索引） | 每块详细状态（status + downloaded） |
| 是否被使用 | 从未被调用 | 全流程使用 |
| 更新时机 | 无（从未调用） | 每个分块完成/失败时实时更新 |
| 续传信息 | 仅知道哪些块完成 | 知道每块精确状态，支持 partial 断点续传 |

**改造方案**：删除 `ResumeManager` 类，所有功能由 `ChunkStateManager` 替代。

### 3.3 FileTransferEngine 下载改造

**当前流程**：

```
performDownload():
  1. 检查 ResumeManager::hasResumeInfo()（永远返回 false）
  2. 遍历分块，检查文件存在性 + 大小匹配 → 跳过
  3. 不完整分块直接删除重传
  4. 传输完成 → 删除 resume info → 合并分块
```

**改造后流程**：

```
performDownload():
  1. 构造状态文件路径：<tempDir>/<fileName>.netshare（按 fileName 定位）
  2. ChunkStateManager::loadStateFile() 或 createStateFile()
  3. 更新状态文件中的 taskId 和 chunkDir 为当前值
  4. validateCompletedChunks() → 验证已完成分块，降级不一致的
  5. 将已完成分块从旧 chunkDir 复制到新 chunkDir
  6. 遍历 chunks，按状态决定续传策略（见 2.5 下载续传流程）
  7. 传输每个分块：
     - 开始时 → updateChunkStatus("downloading")
     - 完成时 → updateChunkStatus("completed", size)    ← 实时持久化
     - 失败时 → updateChunkStatus("failed")
  8. 全部完成 → 合并 → deleteStateFile() + cleanupChunks()
  9. 任务暂停 → updateTaskStatus("paused")，downloading 的 chunks 改为 partial
  10. 任务删除 → deleteStateFile() + cleanupChunks()
```

### 3.4 RequestHandler 上传改造

**当前流程**：

```
handleUploadCheck():
  1. 扫描 .chunks/ 目录下所有历史 session 目录
  2. 对每个分块文件，fi.size() == expectedSize → 标记完成
  3. fi.size() != expectedSize → 删除
  4. 复制已完成分块到新 session 目录
  5. 返回 completedChunks 列表给浏览器
```

**改造后流程**：

```
handleUploadCheck():
  1. 构造状态文件路径：<uploadDir>/.chunks/<fileName>.netshare（按 fileName 定位）
  2. ChunkStateManager::loadStateFile() 或 createStateFile()
  3. 更新状态文件中的 taskId 和 chunkDir 为当前 session 的值
  4. validateCompletedChunks() → 验证已完成分块
  5. 对 partial/uploading 状态的分块，检查文件实际大小更新 downloaded
  6. 将已完成分块从旧 session 的 chunkDir 复制到新 session 的 chunkDir
  7. 从状态文件读取 completedChunks 信息返回给浏览器（不再扫描猜测）
  8. 保存更新后的状态文件

handleUploadData / handleUploadSingleFile（分块上传完成时）:
  1. 分块写入 + verifyChunk 通过
  2. 更新内存状态：ChunkStateInfo.chunks[chunkIndex].status = "completed"
  3. 立即持久化：ChunkStateManager::updateChunkStatus("completed", size)
  4. 返回给浏览器

handleUploadFinalize（上传完成时）:
  1. 合并分块
  2. 删除状态文件：ChunkStateManager::deleteStateFile()
  3. 清理临时分块目录
```

**RequestHandler 获取 ChunkStateManager 的方式**：

通过 `FileTransferEngine` 间接访问。在 `FileTransferEngine` 中新增 `chunkStateManager()` 方法，`RequestHandler` 通过已持有的 `m_transferEngine` 指针调用。

```cpp
// FileTransferEngine 新增
ChunkStateManager* chunkStateManager() const { return m_chunkStateManager; }

// RequestHandler 使用
m_transferEngine->chunkStateManager()->updateChunkStatus(...)
```

### 3.5 TransferWorker 改造

**当前问题**：

1. `chunkFinished` 信号 index 固定为 0，无法区分是哪个分块
2. 构造函数接收 `offset` 和 `length`（整个分块范围），不支持从分块中间位置续传

**改造方案**：

```cpp
class TransferWorker : public QObject
{
    Q_OBJECT
public:
    explicit TransferWorker(const QString& url, qint64 offset, qint64 length,
                            const QString& chunkPath, int chunkIndex,
                            QObject* parent = nullptr);
    // 新增 chunkIndex 参数

    // 新增：支持 partial 分块断点续传
    void setResumeOffset(qint64 resumeOffset);

    void start();

signals:
    void chunkStarted(int index);              // 新增：分块开始传输
    void chunkFinished(int index, bool success); // 修改：传递实际 chunkIndex
    void chunkProgress(int index, qint64 bytesTransferred); // 修改：传递 index

private:
    // ... 原有成员 ...
    int m_chunkIndex;      // 新增：分块索引
    qint64 m_resumeOffset; // 新增：断点续传偏移量
};
```

**关键改动**：

| 改动 | 说明 |
|------|------|
| 构造函数增加 `chunkIndex` | 传递给 `chunkFinished` 信号，替代硬编码的 0 |
| 新增 `setResumeOffset` | partial 分块断点续传：`Range: bytes=<resumeOffset>-<offset+length-1>` |
| `chunkStarted` 信号 | 分块开始传输时发出，用于实时更新状态为 "downloading" |
| `chunkProgress` 增加 index | 进度回调携带分块索引 |

**断点续传 Range 头构造**：

```
正常传输：Range: bytes=<offset>-<offset+length-1>
断点续传：Range: bytes=<offset+resumeOffset>-<offset+length-1>
           （resumeOffset > 0 时，从分块中间位置开始）
```

### 3.6 deleteTask 改造

**当前问题**：`deleteTask` 只清理内存任务和数据库日志，不清理临时分块目录和状态文件。

**改造后**：

```
deleteTask(taskId):
  1. 获取任务的 fileName、type、savePath
  2. 构造状态文件路径：
     - 下载：<tempDir>/<fileName>.netshare
     - 上传：<uploadDir>/.chunks/<fileName>.netshare
  3. 删除状态文件：ChunkStateManager::deleteStateFile()
  4. 删除临时分块目录：
     - 下载：QDir(chunkDir).removeRecursively()
     - 上传：QDir(chunkTempDir).removeRecursively()
  5. 清理内存任务（现有逻辑）
  6. 清理数据库日志（现有逻辑）
```

### 3.7 应用启动恢复改造

**当前**：应用启动时从数据库读取 transfer_logs 恢复任务，但无分块状态信息。

**改造后**：

```
应用启动时：
  1. 扫描下载临时目录，查找 *.netshare 文件
     → 对每个状态文件：
       - loadStateFile() 读取
       - status 为 downloading/paused → 恢复为可续传下载任务
       - status 为 completed → 清理状态文件和临时分块
  2. 扫描上传临时目录，查找 *.netshare 文件
     → 对每个状态文件：
       - loadStateFile() 读取
       - status 为 uploading/paused → 恢复为可续传上传任务
       - status 为 completed → 清理状态文件和临时分块
  3. 合并数据库日志和状态文件信息，在传输列表中显示
```

---

## 4 实施步骤

### Phase 1：基础设施（P0）

| 步骤 | 内容 | 产出 | 涉及文件 |
|------|------|------|---------|
| 1.1 | 新建 `ChunkState.h` 独立头文件，定义 `ChunkState` 和 `ChunkStateInfo`（含 Q_GADGET/Q_PROPERTY/Q_DECLARE_METATYPE） | 数据结构定义 | `src/core/transfer/ChunkState.h` |
| 1.2 | 新建 `ChunkStateManager` 类 | `ChunkStateManager.h` / `.cpp` | 新建 |
| 1.3 | 实现状态文件 CRUD（create/load/save/delete） | 基本读写能力 | `ChunkStateManager.cpp` |
| 1.4 | 实现 `updateChunkStatus`（单块实时更新，QMutex 保护） | 核心实时更新能力 | `ChunkStateManager.cpp` |
| 1.5 | `FileTransferEngine` 新增 `m_chunkStateManager` 成员和 `chunkStateManager()` getter 方法 | `FileTransferEngine` 可访问 ChunkStateManager | `FileTransferEngine.h` / `.cpp` |
| 1.6 | 修改 `FileTransferEngine::setManagers` 签名：`ResumeManager*` → `ChunkStateManager*` | 依赖注入适配 | `FileTransferEngine.h` / `.cpp` |
| 1.7 | 实现 `validateCompletedChunks`（续传前验证） | 数据一致性保障 | `ChunkStateManager.cpp` |
| 1.8 | 实现 `scanResumableTasks`（扫描可续传任务） | 启动恢复能力 | `ChunkStateManager.cpp` |
| 1.9 | 更新 `CMakeLists.txt`，添加 `ChunkStateManager` 和 `ChunkState.h` | 编译集成 | `src/core/CMakeLists.txt` |
| 1.10 | 删除 `ChunkInfo` 定义，`ChunkManager::splitFile` / `splitFileForThreads` 返回类型改为 `QList<ChunkState>`；`ChunkManager.h` 包含 `ChunkState.h`；**同步修改 `FileTransferEngine.cpp` 和 `RequestHandler.cpp` 中所有 `ChunkInfo` 引用**（C8/C9） | 数据结构统一 | `ChunkManager.h` / `.cpp`、`FileTransferEngine.cpp`、`RequestHandler.cpp` |
| 1.11 | 更新 `main.cpp`，创建 `ChunkStateManager` 替代 `ResumeManager`，调用 `setManagers` 时传 `m_chunkStateManager`；**添加 `qRegisterMetaType<ChunkState>()` 和 `qRegisterMetaType<ChunkStateInfo>()`**（O11） | 初始化改造 | `src/main.cpp` |
| 1.12 | 更新 `DiContainer.h`，`TransferModule` 签名和 `NetShareInjector` 类型别名中 `ResumeManager&` → `ChunkStateManager&` | 依赖注入改造 | `src/core/common/DiContainer.h` |

### Phase 2：下载续传改造（P0）

| 步骤 | 内容 | 涉及文件 |
|------|------|---------|
| 2.1 | TransferWorker 增加 `chunkIndex` 参数和 `chunkStarted` 信号 | `FileTransferEngine.h` |
| 2.2 | TransferWorker 增加 `setResumeOffset` 支持 partial 断点续传 | `FileTransferEngine.h` / `.cpp` |
| 2.3 | 修改 `chunkFinished` 信号传递实际 chunkIndex（替代硬编码 0） | `FileTransferEngine.cpp` |
| 2.4 | 改造 `performDownload`，按 fileName 定位状态文件 | `FileTransferEngine.cpp` |
| 2.5 | 分块完成时实时调用 `updateChunkStatus("completed")` | `FileTransferEngine.cpp` |
| 2.6 | 分块失败时实时调用 `updateChunkStatus("failed")` | `FileTransferEngine.cpp` |
| 2.7 | 续传时从状态文件读取分块状态，按策略续传 | `FileTransferEngine.cpp` |
| 2.8 | 续传复制完已完成分块后删除旧 chunkDir | `FileTransferEngine.cpp` |
| 2.9 | 任务暂停时更新状态文件 | `FileTransferEngine.cpp` |
| 2.10 | 任务完成时清理状态文件和临时分块目录 | `FileTransferEngine.cpp` |

### Phase 3：上传续传改造（P0）

| 步骤 | 内容 | 涉及文件 |
|------|------|---------|
| 3.1 | 改造 `handleUploadCheck`，按 fileName 定位状态文件，返回 partial 信息 | `RequestHandler.cpp` |
| 3.2 | 分块上传完成时（`handleStreamingFileUpload` / `handleUploadSingleFile`）实时调用 `updateChunkStatus("completed")` | `RequestHandler.cpp` |
| 3.3 | 分块上传失败时调用 `updateChunkStatus("failed")` | `RequestHandler.cpp` |
| 3.4 | 上传暂停（`handleUploadPause`）时更新状态文件（QMutex 保证线程安全） | `RequestHandler.cpp` |
| 3.5 | 上传恢复（`handleUploadResume`）时重新加载状态文件，更新整体 status 为 `uploading` | `RequestHandler.cpp` |
| 3.6 | 上传完成（`handleUploadFinalize`）时清理状态文件（先检查是否存在，非分块文件不报错） | `RequestHandler.cpp` |
| 3.7 | 上传中止（`handleUploadAbort`）时删除状态文件和临时分块目录 | `RequestHandler.cpp` |
| 3.8 | 删除旧的 `.chunks/` 目录扫描逻辑，改为从状态文件读取 | `RequestHandler.cpp` |
| 3.9 | 续传复制完已完成分块后删除旧 session 的 chunkDir | `RequestHandler.cpp` |
| 3.10 | 客户端断连（`streamingConnDisconnected`）时将 `uploading` 分块转为 `partial` 并持久化 | `RequestHandler.cpp` |
| 3.11 | `cleanupExpiredSessions()` 中同步清理对应的状态文件（O14） | `RequestHandler.cpp` |
| 3.12 | 删除 `FileChunkState` 和 `ChunkUploadInfo` 定义，`UploadSession.fileChunkStates` 改为 `QMap<QString, ChunkStateInfo>`；`RequestHandler.h` 包含 `ChunkState.h`（C7/O9/O16） | `RequestHandler.h` / `.cpp` |

### Phase 4：deleteTask 和启动恢复改造（P0）

| 步骤 | 内容 | 涉及文件 |
|------|------|---------|
| 4.1 | `deleteTask` 增加状态文件和临时分块目录清理 | `FileTransferEngine.cpp` |
| 4.2 | 应用启动时扫描临时目录，恢复可续传任务（按 2.5 节合并策略） | `main.cpp` 或初始化逻辑 |
| 4.3 | 合并数据库日志和状态文件信息 | `TransferLogService` / `FileTransferEngine` |
| 4.4 | 启动恢复时调用 `ChunkStateManager::cleanupExpired()` 清理过期（>7天）状态文件 | `main.cpp` |
| 4.5 | `FileTransferEngine::stopAllTasks()` 中对 Downloading/Uploading 任务更新状态文件（status 改为 "paused"，分块改为 partial）（O12） | `FileTransferEngine.cpp` |
| 4.6 | `FileTransferEngine::initialize()` 中整合 `scanResumableTasks()` 扫描结果到恢复的任务（O13） | `FileTransferEngine.cpp` |

### Phase 5：清理（P1）

| 步骤 | 内容 | 涉及文件 |
|------|------|---------|
| 5.1 | 删除 `ResumeManager` 类 | `ResumeManager.h` / `.cpp` |
| 5.2 | 从 `CMakeLists.txt` 移除 ResumeManager | `src/core/CMakeLists.txt` |
| 5.3 | 清理 `FileTransferEngine` 中 `m_resumeManager` 成员残留引用 | `FileTransferEngine.h` / `.cpp` |
| 5.4 | 清理 `main.cpp` 中 ResumeManager 的头文件包含和创建代码 | `main.cpp` |
| 5.5 | 验证并清理 `RequestHandler.h` / `.cpp` 中 `FileChunkState` / `ChunkUploadInfo` 的残留引用（定义已在 Phase 3.12 删除） | `RequestHandler.h` / `.cpp` |
| 5.6 | 清理 `ChunkManager.h` 和 `RequestHandler.h` 中废弃结构体的引用 | 相关文件 |

---

## 5 关键设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| 状态文件格式 | JSON | Qt 原生支持 QJsonDocument，可读性好，调试方便 |
| 状态文件定位方式 | 按 fileName 定位 | taskId/sessionId 每次请求都变，无法跨会话定位；fileName 稳定可靠 |
| 下载状态文件位置 | `<tempDir>/<fileName>.netshare` | 与分块目录同级，按文件名定位，续传时可找到 |
| 上传状态文件位置 | `<uploadDir>/.chunks/<fileName>.netshare` | 不放在 sessionId 子目录内，续传时可找到 |
| 更新策略 | 每个分块完成/失败时立即写磁盘 | 与 IDM 一致，牺牲微量性能换取可靠性 |
| partial 分块处理（下载） | 支持 HTTP Range 断点续传 | 服务端支持 Range，大文件续传效率高 |
| partial 分块处理（上传） | 删除重传 | 上传场景客户端不可控，分块本身不大（1-16MB），重传更可靠 |
| downloading/uploading 残留 | 续传时当作 partial 处理 | 进程异常退出时状态文件中可能残留，需检查文件实际大小 |
| 状态文件损坏 | 删除后回退到扫描分块目录重建 | 极端情况下的容错策略 |
| 非分块小文件 | 不创建状态文件 | 只有一个数据块，不存在"从哪个分块开始"的问题 |
| 多文件上传 | 每个文件独立状态文件 | 与 IDM 一致，每个文件独立管理 |
| RequestHandler 访问 ChunkStateManager | 通过 FileTransferEngine 间接访问 | 避免新增构造函数参数，利用已有的 m_transferEngine 指针 |
| ChunkInfo 与 ChunkState 关系 | ChunkInfo 废弃，统一使用 ChunkState | ChunkState 完全覆盖 ChunkInfo 所有字段，splitFile 返回类型改为 QList<ChunkState> |
| FileChunkState / ChunkUploadInfo 处理 | 直接删除，统一使用 ChunkStateInfo + ChunkState | 上传会话直接使用 ChunkStateInfo 作为内存缓存，不维护两套状态 |
| ChunkState 定义位置 | 独立头文件 `ChunkState.h` | 多模块需要此类型（ChunkManager/ChunkStateManager/RequestHandler），独立头文件避免循环依赖和不必要的包含 |
| ChunkInfo 处理 | 随 Phase 1.10 直接删除，ChunkManager 使用 ChunkState | ChunkState 完全覆盖 ChunkInfo 所有字段，无需分阶段共存 |
| setManagers 签名变更 | Phase 1.6 改为 `ChunkStateManager*` | Phase 2 下载改造需要 ChunkStateManager，必须在 Phase 1 完成 |
| DiContainer 替换策略 | 仅在 Phase 1.12 一次性完成，不再在 Phase 5 重复 | 避免重复工作 |
| handleUploadFinalize 状态文件清理 | 先检查 `.netshare` 文件是否存在再删除 | 非分块文件不创建状态文件，避免删除不存在文件时报错 |
| cleanupExpired 调用时机 | 应用启动时调用一次 | 每次启动自动清理 >7 天的过期状态文件，防止磁盘堆积 |
| 客户端断连 | `streamingConnDisconnected` 中将 uploading 分块转为 partial 并持久化 | 断连是常见中断场景，必须持久化状态以支持续传 |

---

## 6 风险与对策

| 风险 | 对策 |
|------|------|
| 频繁写磁盘影响性能 | 分块大小最小 1MB，写操作频率低（每块完成一次），影响可忽略 |
| 状态文件损坏 | version 字段 + JSON 解析校验，读取失败时删除并回退到扫描重建 |
| 状态文件与实际分块不一致 | `validateCompletedChunks` 验证 completed 分块文件大小，不一致则降级为 pending |
| 并发写入冲突（下载工作线程 vs UI 线程 vs CivetWeb 线程） | `QMutex` 保护所有状态文件写入操作 |
| 旧版本兼容 | 检测无 `.netshare` 文件但有 `.chunks` 目录时，自动扫描生成状态文件 |
| 分块目录被手动删除 | 续传时发现 chunkDir 不存在，所有分块降级为 pending，重新传输 |
| 同名文件冲突 | 状态文件按 fileName 定位，同目录下同名文件会覆盖。通过 fileSize 校验区分不同文件 |
| taskId 变更后旧分块目录残留 | 续传时将已完成分块从旧 chunkDir 复制到新 chunkDir，完成后删除旧目录 |

---

## 7 代码审查发现的遗漏与冲突

> 以下问题通过对比计划文档与当前项目代码（截至 2026-05-27）发现。

### 7.1 遗漏

| # | 遗漏 | 影响 | 补充方案 | 涉及步骤 |
|---|------|------|---------|---------|
| O9 | **`RequestHandler.h` 需包含 `ChunkState.h`** | `UploadSession.fileChunkStates` 从 `QMap<QString, FileChunkState>` 改为 `QMap<QString, ChunkStateInfo>` 后，RequestHandler.h 必须包含 `ChunkState.h` | Phase 3 改造时在 `RequestHandler.h` 添加 `#include "ChunkState.h"` | Phase 3 / Phase 5.5 |
| O10 | **`RequestHandler.cpp` 中 `ChunkInfo` 的使用（第 794 行）** | `handleUploadCheck` 中 `ChunkInfo ci = v.value<ChunkInfo>()` 需改为 `ChunkState`，计划文档未提及 RequestHandler.cpp 中对 ChunkInfo 的引用 | Phase 1.10 删除 ChunkInfo 后，需同步修改 `RequestHandler.cpp` 中的 ChunkInfo 引用 | Phase 1.10 |
| O11 | **`qRegisterMetaType<ChunkState>` 和 `qRegisterMetaType<ChunkStateInfo>` 调用时机** | 当前代码中 `ChunkInfo` 未注册 QMetaType（仅 Q_GADGET + Q_DECLARE_METATYPE），但 `ChunkState` 和 `ChunkStateInfo` 作为新类型在跨线程信号槽和 QML 中使用时，需在 `main()` 中调用 `qRegisterMetaType` | 在 `main.cpp` 初始化阶段添加注册调用 | Phase 1.11 |
| O12 | **`FileTransferEngine::stopAllTasks()` 需更新状态文件** | 当前 `stopAllTasks()` 将 Downloading/Uploading 任务标记为 Cancelled 并写入数据库，但未更新 `.netshare` 状态文件。进程退出后，状态文件仍为 "downloading"/"uploading"，下次启动时会被当作 partial 恢复 | `stopAllTasks()` 中对 Downloading/Uploading 任务：调用 `updateTaskStatus("paused")` 并将所有 downloading/uploading 分块改为 partial | Phase 4 |
| O13 | **`FileTransferEngine::initialize()` 启动恢复需整合状态文件** | 当前 `initialize()` 仅从 `restorableLogs()` 恢复任务到 `m_tasks`，无分块状态信息。改造后需同时扫描 `.netshare` 文件，将分块进度信息合并到恢复的任务中 | 在 `initialize()` 中增加 `scanResumableTasks()` 调用，合并状态文件信息到恢复的任务 | Phase 4.2 |
| O14 | **`cleanupExpiredSessions()` 需同步清理状态文件** | 当前 `RequestHandler::cleanupExpiredSessions()` 清理超过 2 小时的 UploadSession 内存，但未清理对应的 `.netshare` 状态文件。过期 session 被移除后，状态文件残留磁盘 | 在 `cleanupExpiredSessions()` 中，移除 session 前遍历其 `fileChunkStates`，删除对应的状态文件 | Phase 3 |
| O15 | **`StreamingMultipartParser` 的 `setResumeOffset` / `setResumeFilePath` 在分块上传中的使用** | 计划文档提到非分块小文件用 `StreamingMultipartParser` 的续传功能，但未说明分块上传场景下 `handleStreamingFileUpload` 如何利用这些接口 | 分块上传场景下每个分块是独立文件，不需要 `setResumeOffset`；仅非分块小文件续传使用 | 已在 2.4 节说明，无需额外步骤 |
| O16 | **`UploadSession` 结构体改造过渡期** | Phase 3 改造上传时，`UploadSession.fileChunkStates` 从 `QMap<QString, FileChunkState>` 改为 `QMap<QString, ChunkStateInfo>`，但 Phase 5.5 才删除 `FileChunkState` 定义。两阶段之间 `UploadSession` 需同时兼容两种类型 | Phase 3 改造时直接将 `UploadSession.fileChunkStates` 改为 `QMap<QString, ChunkStateInfo>`，同时删除 `FileChunkState` 和 `ChunkUploadInfo` 定义（合并到 Phase 3） | Phase 3 / Phase 5.5 |
| O17 | **`handleUploadCheck` 响应格式变更需同步前端** | 改造后 `handleUploadCheck` 返回的 `completedChunks` 来源从扫描 `.chunks` 目录变为读取状态文件，但响应 JSON 格式不变（`chunkSize`、`chunkCount`、`completedChunks`），前端无需改动 | 确认前端 `upload.html` 中 `chunkInfo` 解析逻辑兼容新响应格式 | Phase 3.1 |
| O18 | **`handleUploadSingleFile` 中 `ChunkInfo` 引用** | `handleUploadSingleFile` 中无直接 ChunkInfo 引用，但 `fcs.chunks[chunkIndex].completed` 需改为 `fcs.chunks[chunkIndex].status == "completed"` | Phase 3 改造时同步修改 | Phase 3 |

### 7.2 冲突

| # | 冲突 | 影响 | 解决方案 | 涉及步骤 |
|---|------|------|---------|---------|
| C7 | **Phase 5.5 删除 FileChunkState/ChunkUploadInfo 与 Phase 3 改造 UploadSession 存在时序冲突** | Phase 3 改造上传时需将 `UploadSession.fileChunkStates` 改为 `ChunkStateInfo`，此时 `FileChunkState` 已不再使用。Phase 5.5 再删除定义是多余的，且 Phase 3~5 之间代码无法编译 | 将 FileChunkState/ChunkUploadInfo 的删除合并到 Phase 3，Phase 3 完成后即删除旧定义，Phase 5.5 改为"清理 RequestHandler.h 中旧结构体的残留引用" | Phase 3 / Phase 5.5 |
| C8 | **Phase 1.10 删除 ChunkInfo 后 RequestHandler.cpp 编译失败** | `RequestHandler.cpp:794` 使用 `ChunkInfo ci = v.value<ChunkInfo>()`，Phase 1.10 删除 ChunkInfo 后此行编译失败。计划文档 Phase 1.10 仅提及 `ChunkManager.h/.cpp`，未提及 `RequestHandler.cpp` | Phase 1.10 需同步修改 `RequestHandler.cpp` 中的 ChunkInfo 引用，改为 `ChunkState` | Phase 1.10 |
| C9 | **`FileTransferEngine::performDownload` 中 `ChunkInfo` 引用（第 274 行）** | `performDownload` 中 `ChunkInfo chunk = v.value<ChunkInfo>()` 需改为 `ChunkState`，Phase 2.4 改造 performDownload 时会处理，但 Phase 1.10 删除 ChunkInfo 后到 Phase 2.4 之间代码无法编译 | Phase 1.10 需同步修改 `FileTransferEngine.cpp` 中的 ChunkInfo 引用 | Phase 1.10 |

### 7.3 补充的步骤调整

基于以上遗漏和冲突，对实施步骤做如下调整：

1. **Phase 1.10 扩展**：删除 `ChunkInfo` 时，同步修改所有引用 `ChunkInfo` 的文件：
   - `ChunkManager.h` / `ChunkManager.cpp`（已在计划中）
   - `FileTransferEngine.cpp`（第 274 行 `ChunkInfo chunk = v.value<ChunkInfo>()` → `ChunkState chunk = v.value<ChunkState>()`）
   - `RequestHandler.cpp`（第 794 行 `ChunkInfo ci = v.value<ChunkInfo>()` → `ChunkState ci = v.value<ChunkState>()`）

2. **Phase 3 合并 FileChunkState/ChunkUploadInfo 删除**：Phase 3 改造上传时，直接将 `UploadSession.fileChunkStates` 从 `QMap<QString, FileChunkState>` 改为 `QMap<QString, ChunkStateInfo>`，同时删除 `FileChunkState` 和 `ChunkUploadInfo` 结构体定义。Phase 5.5 改为"验证并清理 RequestHandler.h/.cpp 中旧结构体的残留引用"。

3. **Phase 4 新增步骤**：
   - 4.5：`FileTransferEngine::stopAllTasks()` 中对 Downloading/Uploading 任务更新状态文件（status 改为 "paused"，分块改为 partial）
   - 4.6：`FileTransferEngine::initialize()` 中整合 `scanResumableTasks()` 扫描结果

4. **Phase 3 新增步骤**：
   - 3.11：`RequestHandler::cleanupExpiredSessions()` 中同步清理状态文件

5. **Phase 1.11 扩展**：`main.cpp` 中添加 `qRegisterMetaType<ChunkState>()` 和 `qRegisterMetaType<ChunkStateInfo>()` 调用
