# 分块状态管理重设计计划

> 核心原则：参考 IDM / 迅雷，建立**独立的分块状态管理文件**，作为续传的**唯一权威数据源**。
> 传输过程中**实时更新**分块状态，续传时**严格依据状态文件**决定从哪个分块开始。

---

## 1 现状问题分析

### 1.1 当前架构缺陷

| # | 问题 | 现状 | 后果 |
|---|------|------|------|
| 1 | **无独立状态文件** | 分块完成状态仅存内存（`FileChunkState.chunks`、`ChunkInfo.status`），进程退出即丢失 | 重启后续传无法知道哪些分块已完成 |
| 2 | **ResumeManager 未使用** | `saveResumeInfo()` 从未被调用，`ResumeInfo.completedChunks` 永远为空 | 下载续传功能名存实亡 |
| 3 | **上传续传靠扫描猜测** | `handleUploadCheck` 扫描 `.chunks/` 目录，用 `fi.size() == expectedSize` 猜测分块是否完成 | 不完整分块可能被误判为完成；扫描逻辑复杂且不可靠 |
| 4 | **下载续传靠文件存在性** | `performDownload` 仅检查 chunk 文件是否存在且大小匹配 | 无状态记录，无法区分"正在传输"和"传输中断" |
| 5 | **无实时状态更新** | 分块传输过程中不更新任何持久化状态 | 中断后无法精确知道哪些分块已确认完成 |
| 6 | **上传状态仅存内存** | `UploadSession.fileChunkStates` 在服务端内存中 | 服务重启后上传会话全部丢失，无法续传 |

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
| **状态文件与分块同目录** | 方便一起清理，避免孤立文件 |
| **completed 分块需验证** | 续传前验证已完成分块文件大小，不一致则降级 |
| **partial 分块精确记录** | 记录已下载字节数，支持断点续传或决定是否重传 |

---

## 2 重设计方案

### 2.1 状态文件格式

参考 IDM 的 `.info` 文件，采用 JSON 格式。状态文件与临时分块存放在同一目录。

**文件命名**：`<fileName>.netshare`

**存放位置**：
- 下载：与临时分块同目录，即 `<tempDir>/<taskId>/<fileName>.netshare`
- 上传：与临时分块同目录，即 `<uploadDir>/.chunks/<sessionId>/<fileName>.netshare`

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

### 2.3 状态文件更新时机（实时更新）

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

### 2.4 续传逻辑

> **核心原则：续传严格依据状态文件，状态文件是唯一权威数据源。**

#### 下载续传流程

```
performDownload(taskId, info, savePath, threads):
│
├─ 1. 构造状态文件路径：<chunkDir>/<fileName>.netshare
│
├─ 2. 状态文件存在？
│   ├─ 是 → ChunkStateManager::loadStateFile() 加载
│   │       ├─ 加载成功 → 校验 version、fileSize、totalChunks 是否匹配
│   │       │             ├─ 匹配 → 使用加载的状态
│   │       │             └─ 不匹配 → 文件已变化，删除旧状态文件，创建新状态文件
│   │       └─ 加载失败（文件损坏）→ 删除损坏文件，回退到扫描分块目录重建状态
│   └─ 否 → 创建新状态文件，所有 chunks 为 pending
│
├─ 3. 遍历 chunks 数组，确定每个分块的续传策略：
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
├─ 4. 更新 transferredSize = 所有 completed 分块的 size 之和
│
├─ 5. 保存更新后的状态文件
│
├─ 6. 开始传输未完成的分块（跳过 completed 的）
│   └─ 每个分块完成 → 立即调用 ChunkStateManager::updateChunkStatus("completed")
│   └─ 每个分块失败 → 立即调用 ChunkStateManager::updateChunkStatus("failed")
│
└─ 7. 全部分块完成 → 合并 → 删除状态文件和临时分块目录
```

#### 上传续传流程

```
handleUploadCheck(conn, info):
│
├─ 1. 构造状态文件路径：<chunkDir>/<fileName>.netshare
│
├─ 2. 状态文件存在？
│   ├─ 是 → ChunkStateManager::loadStateFile() 加载
│   │       ├─ 加载成功 → 校验 fileSize、totalChunks 是否匹配当前上传请求
│   │       │             ├─ 匹配 → 使用加载的状态，获取已完成分块列表
│   │       │             └─ 不匹配 → 文件已变化，删除旧状态文件，创建新状态文件
│   │       └─ 加载失败 → 删除损坏文件，回退到扫描分块目录重建状态
│   └─ 否 → 创建新状态文件，所有 chunks 为 pending
│
├─ 3. 遍历 chunks 数组，确定每个分块状态：
│   ├─ status == "completed" → 验证分块文件，通过则跳过
│   ├─ status == "partial" / "uploading" → 检查文件大小，更新 downloaded
│   └─ status == "pending" / "failed" → 需要重新上传
│
├─ 4. 将已完成分块信息复制到新 session 的 chunkDir
│
├─ 5. 构造返回给浏览器的 partial 信息（从状态文件读取，不再扫描猜测）
│   └─ completedChunks: [0, 1, 2]  ← 直接从状态文件读取
│   └─ completedBytes: 12582912    ← 直接从状态文件读取
│
└─ 6. 保存更新后的状态文件
```

```
handleUploadData / handleUploadSingleFile（分块上传完成时）:
│
├─ 1. 分块写入完成 + verifyChunk 通过
│
├─ 2. 更新内存状态：fcs.chunks[chunkIndex].completed = true
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

---

## 3 模块改造计划

### 3.1 ChunkStateManager（新增模块）

**职责**：独立管理分块状态文件的创建、读取、更新、删除，是续传的**唯一权威数据源**。

**文件位置**：`src/core/transfer/ChunkStateManager.h` / `.cpp`

**类接口**：

```cpp
struct ChunkState
{
    int index = 0;
    qint64 offset = 0;
    qint64 size = 0;
    QString status;     // "pending" / "downloading" / "uploading" / "partial" / "completed" / "failed"
    qint64 downloaded = 0;
};

struct ChunkStateInfo
{
    int version = 1;
    QString taskId;
    QString type;           // "download" / "upload"
    QString fileName;
    qint64 fileSize = 0;
    qint64 chunkSize = 0;
    int totalChunks = 0;
    qint64 transferredSize = 0;
    QString status;         // "downloading" / "uploading" / "paused" / "completed" / "failed"
    QString url;            // 下载URL（仅下载）
    QString remoteAddress;  // 远端地址（仅上传）
    QString savePath;
    QString chunkDir;
    QDateTime createdAt;
    QDateTime lastUpdated;
    QList<ChunkState> chunks;
};

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

    // 分块状态实时更新（每次调用立即持久化到磁盘）
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
    QMutex m_mutex;     // 保护同一状态文件的并发写入
};
```

**关键方法说明**：

| 方法 | 说明 |
|------|------|
| `createStateFile` | 创建新的状态文件，写入完整初始状态 |
| `loadStateFile` | 读取状态文件，返回 `ChunkStateInfo`；文件损坏返回 false |
| `saveStateFile` | 全量写入状态文件（用于批量更新后保存） |
| `updateChunkStatus` | **核心方法**：更新单个分块状态并立即持久化，用于实时更新 |
| `updateTaskStatus` | 更新任务整体状态（如暂停、完成）并持久化 |
| `validateCompletedChunks` | 续传前验证所有 completed 分块文件大小是否匹配，不匹配的降级 |
| `scanResumableTasks` | 扫描目录下所有 `.netshare` 文件，返回可续传任务列表 |

### 3.2 ResumeManager（废弃 → 由 ChunkStateManager 替代）

| 对比项 | ResumeManager（旧） | ChunkStateManager（新） |
|--------|--------------------|-----------------------|
| 状态文件位置 | `AppData/resume/<taskId>.json` | `<chunkDir>/<fileName>.netshare`（与分块同目录） |
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
  1. 构造状态文件路径：<chunkDir>/<fileName>.netshare
  2. ChunkStateManager::loadStateFile() 或 createStateFile()
  3. validateCompletedChunks() → 验证已完成分块，降级不一致的
  4. 遍历 chunks，按状态决定续传策略（见 2.4 下载续传流程）
  5. 传输每个分块：
     - 开始时 → updateChunkStatus("downloading")
     - 完成时 → updateChunkStatus("completed", size)    ← 实时持久化
     - 失败时 → updateChunkStatus("failed")
  6. 全部完成 → 合并 → deleteStateFile() + cleanupChunks()
  7. 任务暂停 → updateTaskStatus("paused")，downloading 的 chunks 改为 partial
  8. 任务删除 → deleteStateFile() + cleanupChunks()
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
  1. 构造状态文件路径：<chunkDir>/<fileName>.netshare
  2. ChunkStateManager::loadStateFile() 或 createStateFile()
  3. validateCompletedChunks() → 验证已完成分块
  4. 对 partial/uploading 状态的分块，检查文件实际大小更新 downloaded
  5. 复制已完成分块到新 session 目录
  6. 从状态文件读取 completedChunks 信息返回给浏览器（不再扫描猜测）
  7. 保存更新后的状态文件

handleUploadData / handleUploadSingleFile（分块上传完成时）:
  1. 分块写入 + verifyChunk 通过
  2. 更新内存状态：fcs.chunks[chunkIndex].completed = true
  3. 立即持久化：ChunkStateManager::updateChunkStatus("completed", size)
  4. 返回给浏览器

handleUploadFinalize（上传完成时）:
  1. 合并分块
  2. 删除状态文件：ChunkStateManager::deleteStateFile()
  3. 清理临时分块目录
```

### 3.5 TransferWorker 改造

**当前**：TransferWorker 只在完成时发 `chunkFinished` 信号，无中间状态。

**改造**：增加分块开始/失败信号的实时状态更新。

```
TransferWorker:
  - 开始下载 → emit chunkStarted(index)     ← 新增
  - 下载完成 → emit chunkFinished(index, true)
  - 下载失败 → emit chunkFinished(index, false)

FileTransferEngine 连接信号：
  - chunkStarted → ChunkStateManager::updateChunkStatus("downloading")
  - chunkFinished(true) → ChunkStateManager::updateChunkStatus("completed", size)
  - chunkFinished(false) → ChunkStateManager::updateChunkStatus("failed")
```

### 3.6 应用启动恢复改造

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

| 步骤 | 内容 | 产出 |
|------|------|------|
| 1.1 | 新建 `ChunkStateManager` 类 | `ChunkStateManager.h` / `.cpp` |
| 1.2 | 定义 `ChunkStateInfo` 和 `ChunkState` 数据结构 | 状态文件读写接口 |
| 1.3 | 实现状态文件 CRUD（create/load/save/delete） | 基本读写能力 |
| 1.4 | 实现 `updateChunkStatus`（单块实时更新） | 核心实时更新能力 |
| 1.5 | 实现 `validateCompletedChunks`（续传前验证） | 数据一致性保障 |
| 1.6 | 实现 `scanResumableTasks`（扫描可续传任务） | 启动恢复能力 |

### Phase 2：下载续传改造（P0）

| 步骤 | 内容 | 涉及文件 |
|------|------|---------|
| 2.1 | 改造 `performDownload`，启动时创建/加载状态文件 | `FileTransferEngine.cpp` |
| 2.2 | TransferWorker 增加 `chunkStarted` 信号 | `FileTransferEngine.h` |
| 2.3 | 分块完成时实时调用 `updateChunkStatus("completed")` | `FileTransferEngine.cpp` |
| 2.4 | 分块失败时实时调用 `updateChunkStatus("failed")` | `FileTransferEngine.cpp` |
| 2.5 | 续传时从状态文件读取分块状态，按策略续传 | `FileTransferEngine.cpp` |
| 2.6 | 任务暂停时更新状态文件 | `FileTransferEngine.cpp` |
| 2.7 | 任务完成/删除时清理状态文件 | `FileTransferEngine.cpp` |

### Phase 3：上传续传改造（P0）

| 步骤 | 内容 | 涉及文件 |
|------|------|---------|
| 3.1 | 改造 `handleUploadCheck`，从状态文件读取分块信息 | `RequestHandler.cpp` |
| 3.2 | 分块上传完成时实时调用 `updateChunkStatus("completed")` | `RequestHandler.cpp` |
| 3.3 | 分块上传失败时调用 `updateChunkStatus("failed")` | `RequestHandler.cpp` |
| 3.4 | 上传暂停时更新状态文件 | `RequestHandler.cpp` |
| 3.5 | 上传完成/删除时清理状态文件 | `RequestHandler.cpp` |
| 3.6 | 删除旧的 `.chunks/` 目录扫描逻辑 | `RequestHandler.cpp` |

### Phase 4：启动恢复改造（P0）

| 步骤 | 内容 | 涉及文件 |
|------|------|---------|
| 4.1 | 应用启动时扫描临时目录，恢复可续传任务 | `main.cpp` 或初始化逻辑 |
| 4.2 | 合并数据库日志和状态文件信息 | `TransferLogService` / `FileTransferEngine` |

### Phase 5：清理（P2）

| 步骤 | 内容 | 涉及文件 |
|------|------|---------|
| 5.1 | 删除 `ResumeManager` 类 | `ResumeManager.h` / `.cpp` |
| 5.2 | 清理 `DiContainer` 中 ResumeManager 的引用 | `DiContainer.h` |
| 5.3 | 清理 `FileTransferEngine` 中 ResumeManager 的引用 | `FileTransferEngine.h` / `.cpp` |
| 5.4 | 清理 `FileChunkState` 中冗余的分块状态管理代码 | `RequestHandler.h` |

---

## 5 关键设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| 状态文件格式 | JSON | Qt 原生支持 QJsonDocument，可读性好，调试方便 |
| 状态文件位置 | 与临时分块同目录 | IDM/迅雷做法，生命周期一致，方便一起清理 |
| 状态文件命名 | `<fileName>.netshare` | 不与系统文件冲突，扩展名标识来源 |
| 更新策略 | 每个分块完成/失败时立即写磁盘 | 与 IDM 一致，牺牲微量性能换取可靠性 |
| partial 分块处理（下载） | 支持 HTTP Range 断点续传 | 服务端支持 Range，大文件续传效率高 |
| partial 分块处理（上传） | 删除重传 | 上传场景客户端不可控，分块本身不大（1-16MB），重传更可靠 |
| downloading/uploading 残留 | 续传时当作 partial 处理 | 进程异常退出时状态文件中可能残留，需检查文件实际大小 |
| 状态文件损坏 | 删除后回退到扫描分块目录重建 | 极端情况下的容错策略 |

---

## 6 风险与对策

| 风险 | 对策 |
|------|------|
| 频繁写磁盘影响性能 | 分块大小最小 1MB，写操作频率低（每块完成一次），影响可忽略 |
| 状态文件损坏 | version 字段 + JSON 解析校验，读取失败时删除并回退到扫描重建 |
| 状态文件与实际分块不一致 | `validateCompletedChunks` 验证 completed 分块文件大小，不一致则降级为 pending |
| 并发写入冲突 | `QMutex` 保护同一状态文件的写入操作 |
| 旧版本兼容 | 检测无 `.netshare` 文件但有 `.chunks` 目录时，自动扫描生成状态文件 |
| 分块目录被手动删除 | 续传时发现 chunkDir 不存在，所有分块降级为 pending，重新传输 |
