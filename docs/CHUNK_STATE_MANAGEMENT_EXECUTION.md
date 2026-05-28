# 分块状态管理重设计 - 执行文档

> 基于 `CHUNK_STATE_MANAGEMENT_REDESIGN.md` 计划文档，本文档定义每个步骤的**具体代码改动**、**验证方法**和**回滚策略**。
> 规则文件对照：`.comate/rules/breakpoint-resume-progress.mdr`

---

## 规则文件对照

| 规则要求 | 新方案对应 | 实现方式 |
|----------|-----------|----------|
| 续传优先 | `.netshare` 状态文件持久化分块状态 | 每个分块完成/失败时立即写磁盘 |
| 双源进度 | XHR chunk onprogress + WS transfer_update | 保持不变，状态文件不影响进度显示 |
| 确定性路径 | 状态文件按 fileName 定位 | `<tempDir>/<fileName>.netshare`（下载）、`<uploadDir>/.chunks/<fileName>.netshare`（上传） |
| X-Resume-Offset/X-Resume-Path | 分块续传由状态文件驱动 | partial 分块使用 HTTP Range 断点续传 |
| 客户端重试重新check | retryFailed() 重调 /api/upload/check | 从状态文件读取 completedChunks，响应格式不变 |
| beforeunload不abort | 保持不变 | 页面关闭保留 chunk 文件和状态文件 |
| WS端口+1，5秒重连 | 保持不变 | 现有 connectWebSocket 逻辑不变 |

---

## Phase 1：基础设施（P0）

### Step 1.1：新建 ChunkState.h

**文件**：`src/core/transfer/ChunkState.h`（新建）

**操作**：
- 创建独立头文件，定义 `ChunkState` 和 `ChunkStateInfo` 结构体
- 包含 `Q_GADGET`、`Q_PROPERTY`、`Q_DECLARE_METATYPE`
- 不依赖任何项目内头文件，仅依赖 Qt 基础类型

**代码**：参见计划文档 2.3 节

**验证**：
- 编译通过：`ChunkState.h` 被 `ChunkManager.h`、`ChunkStateManager.h`、`RequestHandler.h` 包含无循环依赖
- QML 兼容：`QVariant::fromValue(chunkState)` 可正常转换

**回滚**：删除 `ChunkState.h`

---

### Step 1.2：新建 ChunkStateManager 类

**文件**：`src/core/transfer/ChunkStateManager.h`（新建）、`src/core/transfer/ChunkStateManager.cpp`（新建）

**操作**：
- 创建 `ChunkStateManager` 类，继承 `QObject`
- 声明所有 API（createStateFile、loadStateFile、saveStateFile、deleteStateFile、updateChunkStatus、updateTaskStatus、updateTransferredSize、scanResumableTasks、validateCompletedChunks、cleanupExpired）
- 成员变量：`QMutex m_mutex`
- 包含 `ChunkState.h`

**验证**：
- 头文件编译通过

**回滚**：删除 `ChunkStateManager.h` 和 `ChunkStateManager.cpp`

---

### Step 1.3：实现状态文件 CRUD

**文件**：`src/core/transfer/ChunkStateManager.cpp`

**操作**：
- 实现 `createStateFile`：将 `ChunkStateInfo` 序列化为 JSON 写入文件
- 实现 `loadStateFile`：读取 JSON 文件反序列化为 `ChunkStateInfo`，校验 version 字段
- 实现 `saveStateFile`：全量覆盖写入（QMutex 保护）
- 实现 `deleteStateFile`：删除 `.netshare` 文件

**JSON 序列化/反序列化**：
- `ChunkStateInfo` → `QJsonObject`：逐字段写入
- `ChunkStateInfo.chunks` → `QJsonArray`：每个 ChunkState 一个 QJsonObject
- 反序列化时校验 `version == 1`，不匹配则返回 false

**验证**：
- 单元测试：create → load → 验证字段一致
- 单元测试：load 损坏文件返回 false
- 单元测试：delete 后文件不存在

**回滚**：清空 `ChunkStateManager.cpp` 实现体

---

### Step 1.4：实现 updateChunkStatus

**文件**：`src/core/transfer/ChunkStateManager.cpp`

**操作**：
- 实现 `updateChunkStatus(stateFilePath, chunkIndex, status, downloaded)`：
  1. `QMutexLocker` 加锁
  2. `loadStateFile` 读取当前状态
  3. 更新 `chunks[chunkIndex].status` 和 `chunks[chunkIndex].downloaded`
  4. 更新 `lastUpdated` 为当前时间
  5. `saveStateFile` 写回
- 实现 `updateTaskStatus(stateFilePath, status)`：类似流程，更新顶层 status
- 实现 `updateTransferredSize(stateFilePath, transferredSize)`：类似流程

**验证**：
- 单元测试：updateChunkStatus 后 load 验证字段更新
- 并发测试：多线程同时 updateChunkStatus 不崩溃

**回滚**：移除 update 方法实现

---

### Step 1.5：FileTransferEngine 新增 m_chunkStateManager

**文件**：`src/core/transfer/FileTransferEngine.h`、`src/core/transfer/FileTransferEngine.cpp`

**操作**：
- `FileTransferEngine.h`：
  - 前向声明 `class ChunkStateManager;`
  - 新增成员 `ChunkStateManager* m_chunkStateManager = nullptr;`
  - 新增 getter `ChunkStateManager* chunkStateManager() const;`
- `FileTransferEngine.cpp`：
  - 实现 getter：`return m_chunkStateManager;`

**验证**：
- 编译通过

**回滚**：移除新增成员和 getter

---

### Step 1.6：修改 setManagers 签名

**文件**：`src/core/transfer/FileTransferEngine.h`、`src/core/transfer/FileTransferEngine.cpp`

**操作**：
- `FileTransferEngine.h`：
  - 修改签名：`void setManagers(ShareManager* sm, ChunkManager* cm, ChunkStateManager* csm, BandwidthManager* bm);`
  - 移除前向声明 `class ResumeManager;`
  - 新增前向声明 `class ChunkStateManager;`（如 Step 1.5 未添加）
  - 成员变量：`ResumeManager* m_resumeManager;` → `ChunkStateManager* m_chunkStateManager;`（与 Step 1.5 合并）
- `FileTransferEngine.cpp`：
  - 修改 `setManagers` 实现：`m_chunkStateManager = csm;`
  - 移除 `#include "ResumeManager.h"`
  - 新增 `#include "ChunkStateManager.h"`

**验证**：
- 编译通过（注意：此时 `main.cpp` 和 `DiContainer.h` 尚未更新，需临时适配或同步 Step 1.11/1.12）

**回滚**：恢复原签名和成员

---

### Step 1.7：实现 validateCompletedChunks

**文件**：`src/core/transfer/ChunkStateManager.cpp`

**操作**：
- 实现 `validateCompletedChunks(stateFilePath)`：
  1. loadStateFile 读取状态
  2. 遍历 chunks，对 status == "completed" 的分块：
     - 检查分块文件存在且大小 == chunk.size
     - 不一致：降级为 "pending"，删除损坏分块文件
  3. saveStateFile 写回
  4. 返回是否有降级

**验证**：
- 单元测试：completed 分块文件大小正确 → 不降级
- 单元测试：completed 分块文件不存在 → 降级为 pending

**回滚**：移除实现

---

### Step 1.8：实现 scanResumableTasks

**文件**：`src/core/transfer/ChunkStateManager.cpp`

**操作**：
- 实现 `scanResumableTasks(directory)`：
  1. 遍历目录下所有 `*.netshare` 文件
  2. 对每个文件 loadStateFile
  3. 加载成功且 status 为 "downloading"/"uploading"/"paused" → 加入返回列表
  4. status 为 "completed" → 删除状态文件和临时分块目录
  5. 加载失败 → 删除损坏文件

**验证**：
- 单元测试：目录下有 3 个状态文件（downloading/paused/completed）→ 返回 2 个，completed 被清理

**回滚**：移除实现

---

### Step 1.9：更新 CMakeLists.txt

**文件**：`src/core/CMakeLists.txt`

**操作**：
- `NETSHARE_CORE_TRANSFER_SOURCES` 添加 `transfer/ChunkStateManager.cpp`
- `NETSHARE_CORE_TRANSFER_HEADERS` 添加 `transfer/ChunkStateManager.h` 和 `transfer/ChunkState.h`

**验证**：
- CMake 配置成功，编译通过

**回滚**：移除新增条目

---

### Step 1.10：删除 ChunkInfo，统一使用 ChunkState

**文件**：`src/core/transfer/ChunkManager.h`、`src/core/transfer/ChunkManager.cpp`、`src/core/transfer/FileTransferEngine.cpp`、`src/network/RequestHandler.cpp`

**操作**：

1. `ChunkManager.h`：
   - 删除 `ChunkInfo` 类定义（第 10-27 行）
   - 添加 `#include "ChunkState.h"`
   - `splitFile` 和 `splitFileForThreads` 返回类型不变（仍为 `QVariantList`），但内部使用 `ChunkState` 替代 `ChunkInfo`

2. `ChunkManager.cpp`：
   - `splitFile` 中 `ChunkInfo chunk;` → `ChunkState chunk;`
   - `chunk.status = ChunkInfo::Pending;` → `chunk.status = QStringLiteral("pending");`

3. `FileTransferEngine.cpp`（第 274 行）：
   - `ChunkInfo chunk = v.value<ChunkInfo>();` → `ChunkState chunk = v.value<ChunkState>();`
   - 后续 `chunk.size`、`chunk.index`、`chunk.offset` 字段名不变（ChunkState 兼容）

4. `RequestHandler.cpp`（第 794 行）：
   - `ChunkInfo ci = v.value<ChunkInfo>();` → `ChunkState ci = v.value<ChunkState>();`
   - 后续 `ci.size`、`ci.index`、`ci.offset` 字段名不变

**验证**：
- 全项目编译通过，无 ChunkInfo 残留引用
- `grep -r "ChunkInfo" src/` 无结果

**回滚**：恢复 ChunkInfo 定义和所有引用

---

### Step 1.11：更新 main.cpp

**文件**：`src/main.cpp`

**操作**：
- `#include "core/transfer/ResumeManager.h"` → `#include "core/transfer/ChunkStateManager.h"`
- `m_resumeManager = new ResumeManager(this);` → `m_chunkStateManager = new ChunkStateManager(this);`
- `LOG_INFO("ResumeManager initialized");` → `LOG_INFO("ChunkStateManager initialized");`
- `m_transferEngine->setManagers(m_shareManager, m_chunkManager, m_resumeManager, m_bandwidthManager);` → `m_transferEngine->setManagers(m_shareManager, m_chunkManager, m_chunkStateManager, m_bandwidthManager);`
- 成员变量：`ResumeManager* m_resumeManager = nullptr;` → `ChunkStateManager* m_chunkStateManager = nullptr;`
- `buildInjector()` 中：`TransferModule(*m_transferEngine, *m_chunkManager, *m_resumeManager, *m_bandwidthManager)` → `TransferModule(*m_transferEngine, *m_chunkManager, *m_chunkStateManager, *m_bandwidthManager)`
- 添加 `qRegisterMetaType<ChunkState>();` 和 `qRegisterMetaType<ChunkStateInfo>();`（在 `initializeCoreServices()` 末尾）

**验证**：
- 全项目编译通过
- 运行程序，日志显示 "ChunkStateManager initialized"

**回滚**：恢复 ResumeManager 引用

---

### Step 1.12：更新 DiContainer.h

**文件**：`src/core/common/DiContainer.h`

**操作**：
- `#include "core/transfer/ResumeManager.h"` → `#include "core/transfer/ChunkStateManager.h"`
- `TransferModule` 签名：`ResumeManager& resumeMgr` → `ChunkStateManager& chunkStateMgr`
- `di::bind<ResumeManager>.to(std::ref(resumeMgr))` → `di::bind<ChunkStateManager>.to(std::ref(chunkStateMgr))`
- `NetShareInjector` 类型别名：`std::declval<ResumeManager&>()` → `std::declval<ChunkStateManager&>()`

**验证**：
- 全项目编译通过
- Boost.DI injector 构建成功

**回滚**：恢复 ResumeManager 引用

---

## Phase 2：下载续传改造（P0）

### Step 2.1：TransferWorker 增加 chunkIndex

**文件**：`src/core/transfer/FileTransferEngine.h`、`src/core/transfer/FileTransferEngine.cpp`

**操作**：
- `TransferWorker` 构造函数增加 `int chunkIndex` 参数
- 新增成员 `int m_chunkIndex;`
- 新增信号 `void chunkStarted(int index);`
- `chunkFinished` 信号：`emit chunkFinished(m_chunkIndex, success);`（替代硬编码 0）
- `chunkProgress` 信号：增加 index 参数 `void chunkProgress(int index, qint64 bytesTransferred);`

**验证**：
- 编译通过
- 下载文件成功，日志显示正确的 chunkIndex

**回滚**：恢复原构造函数和信号

---

### Step 2.2：TransferWorker 增加 setResumeOffset

**文件**：`src/core/transfer/FileTransferEngine.h`、`src/core/transfer/FileTransferEngine.cpp`

**操作**：
- 新增成员 `qint64 m_resumeOffset = 0;`
- 新增方法 `void setResumeOffset(qint64 offset);`
- `start()` 中构造 Range 头：
  - `m_resumeOffset > 0`：`Range: bytes=<m_offset+m_resumeOffset>-<m_offset+m_length-1>`
  - `m_resumeOffset == 0`：`Range: bytes=<m_offset>-<m_offset+m_length-1>`（原逻辑）
- 文件打开模式：`m_resumeOffset > 0` 时使用 `WriteOnly | Append`，先 seek 到正确位置

**验证**：
- 下载完整文件成功（resumeOffset == 0）
- 断点续传成功（resumeOffset > 0，Range 头正确）

**回滚**：移除 setResumeOffset 和相关逻辑

---

### Step 2.3：修改 chunkFinished 传递实际 chunkIndex

**文件**：`src/core/transfer/FileTransferEngine.cpp`

**操作**：
- `onChunkFinished()` 中 `emit chunkFinished(0, success);` → `emit chunkFinished(m_chunkIndex, success);`
- 此步骤与 Step 2.1 合并实施

**验证**：同 Step 2.1

**回滚**：同 Step 2.1

---

### Step 2.4：改造 performDownload

**文件**：`src/core/transfer/FileTransferEngine.cpp`

**操作**：
- 在 `performDownload` 开头：
  1. 构造状态文件路径：`m_tempDirectory + "/" + fileName + ".netshare"`
  2. 状态文件存在？加载并校验 fileSize/totalChunks → 匹配则使用，不匹配则删除重建
  3. 不存在？创建新状态文件（type="download"，所有 chunks 为 pending）
  4. 更新状态文件中 taskId 和 chunkDir 为当前值
- 遍历 chunks 确定续传策略（参见计划文档 2.5 节下载续传流程）
- 复制已完成分块从旧 chunkDir 到新 chunkDir，完成后删除旧 chunkDir
- 更新 transferredSize

**验证**：
- 首次下载：创建状态文件，所有分块 pending
- 续传下载：加载状态文件，跳过 completed 分块
- 文件变化：旧状态文件被删除重建

**回滚**：恢复原 performDownload 逻辑

---

### Step 2.5-2.6：分块完成/失败时实时更新状态

**文件**：`src/core/transfer/FileTransferEngine.cpp`

**操作**：
- `onChunkFinished` 成功回调：`m_chunkStateManager->updateChunkStatus(stateFilePath, index, "completed", chunkSize);`
- `onChunkFinished` 失败回调：`m_chunkStateManager->updateChunkStatus(stateFilePath, index, "failed", 0);`
- 需在 performDownload 中将 stateFilePath 传递到 chunkFinished 回调（通过 lambda 捕获或成员变量）

**验证**：
- 下载过程中断开，状态文件中已完成分块为 "completed"
- 下载失败分块为 "failed"

**回滚**：移除 updateChunkStatus 调用

---

### Step 2.7：续传时从状态文件读取分块状态

**文件**：`src/core/transfer/FileTransferEngine.cpp`

**操作**：
- 此步骤与 Step 2.4 合并实施
- 续传策略：completed → 验证跳过；partial → 断点续传；pending → 从头传；failed → 删除重传

**验证**：同 Step 2.4

**回滚**：同 Step 2.4

---

### Step 2.8：续传后删除旧 chunkDir

**文件**：`src/core/transfer/FileTransferEngine.cpp`

**操作**：
- 复制已完成分块到新 chunkDir 后：`QDir(oldChunkDir).removeRecursively();`
- 此步骤与 Step 2.4 合并实施

**验证**：
- 续传后旧 taskId 目录被删除
- 新 taskId 目录包含已完成分块

**回滚**：移除删除旧目录逻辑

---

### Step 2.9：任务暂停时更新状态文件

**文件**：`src/core/transfer/FileTransferEngine.cpp`

**操作**：
- `pauseTask()` 中：
  - `m_chunkStateManager->updateTaskStatus(stateFilePath, "paused");`
  - 遍历 chunks，将 downloading 的改为 partial
- 需通过 taskId 查找对应的 stateFilePath（可维护 `QMap<QString, QString> m_taskStateFiles`）

**验证**：
- 暂停任务后状态文件 status 为 "paused"
- downloading 分块变为 partial

**回滚**：移除状态文件更新

---

### Step 2.10：任务完成时清理状态文件

**文件**：`src/core/transfer/FileTransferEngine.cpp`

**操作**：
- 下载完成（合并成功后）：`m_chunkStateManager->deleteStateFile(stateFilePath);`
- 同时清理临时分块目录：`QDir(chunkDir).removeRecursively();`
- 从 `m_taskStateFiles` 移除映射

**验证**：
- 下载完成后 .netshare 文件被删除
- 临时分块目录被删除

**回滚**：移除清理逻辑

---

## Phase 3：上传续传改造（P0）

### Step 3.1：改造 handleUploadCheck

**文件**：`src/network/RequestHandler.cpp`

**操作**：
- 按 fileName 定位状态文件：`uploadDir + "/.chunks/" + fileName + ".netshare"`
- 状态文件存在？加载并读取 completedChunks
- 不存在？创建新状态文件
- 响应 JSON 格式不变（chunkSize、chunkCount、completedChunks），前端无需改动
- 移除旧的 `.chunks/` 目录扫描逻辑（逐步替换，此步骤先保留扫描作为 fallback）

**验证**：
- 首次上传：无状态文件，创建新状态文件，completedChunks 为空
- 续传上传：从状态文件读取 completedChunks
- 前端 upload.html 正常工作

**回滚**：恢复扫描逻辑

---

### Step 3.2：分块上传完成时实时更新状态

**文件**：`src/network/RequestHandler.cpp`

**操作**：
- `handleStreamingFileUpload` 分块写入完成后：
  - `m_transferEngine->chunkStateManager()->updateChunkStatus(stateFilePath, chunkIndex, "completed", chunkSize);`
- `handleUploadSingleFile` 分块写入完成后：
  - 同上
- 需在 UploadSession 或 StreamingFileUploadState 中记录 stateFilePath

**验证**：
- 上传分块完成后状态文件中对应分块为 "completed"

**回滚**：移除 updateChunkStatus 调用

---

### Step 3.3：分块上传失败时更新状态

**文件**：`src/network/RequestHandler.cpp`

**操作**：
- 分块写入失败或校验失败时：`updateChunkStatus(stateFilePath, chunkIndex, "failed", 0);`

**验证**：
- 上传分块失败后状态文件中对应分块为 "failed"

**回滚**：移除调用

---

### Step 3.4：上传暂停时更新状态文件

**文件**：`src/network/RequestHandler.cpp`

**操作**：
- `handleUploadPause` 中：
  - 遍历 session 中所有文件的 fileChunkStates
  - 对每个文件的 uploading 分块：`updateChunkStatus(stateFilePath, chunkIndex, "partial", downloaded);`
  - `updateTaskStatus(stateFilePath, "paused");`

**验证**：
- 暂停上传后状态文件 status 为 "paused"
- uploading 分块变为 partial

**回滚**：移除状态文件更新

---

### Step 3.5：上传恢复时更新状态文件

**文件**：`src/network/RequestHandler.cpp`

**操作**：
- `handleUploadResume` 中：
  - 遍历 session 中所有文件：`updateTaskStatus(stateFilePath, "uploading");`

**验证**：
- 恢复上传后状态文件 status 为 "uploading"

**回滚**：移除调用

---

### Step 3.6：上传完成时清理状态文件

**文件**：`src/network/RequestHandler.cpp`

**操作**：
- `handleUploadFinalize` 合并完成后：
  - 对每个文件：检查 `.netshare` 文件是否存在，存在则删除
  - 非分块文件不创建状态文件，检查后跳过即可

**验证**：
- 上传完成后 .netshare 文件被删除
- 非分块文件上传不报错

**回滚**：移除清理逻辑

---

### Step 3.7：上传中止时清理状态文件

**文件**：`src/network/RequestHandler.cpp`

**操作**：
- `handleUploadAbort` 中：
  - 遍历 session 中所有文件：删除对应的 `.netshare` 状态文件
  - 删除临时分块目录（已有逻辑）

**验证**：
- 中止上传后状态文件和临时目录均被删除

**回滚**：移除状态文件删除

---

### Step 3.8：删除旧扫描逻辑

**文件**：`src/network/RequestHandler.cpp`

**操作**：
- 移除 `handleUploadCheck` 中扫描 `.chunks/` 目录查找已完成分块的逻辑
- 完全依赖状态文件读取 completedChunks
- 保留旧 session 目录清理（>24h 的空目录）

**验证**：
- 续传上传仅从状态文件读取，不扫描目录
- 前端正常工作

**回滚**：恢复扫描逻辑

---

### Step 3.9：续传后删除旧 session chunkDir

**文件**：`src/network/RequestHandler.cpp`

**操作**：
- 续传复制完已完成分块后：`QDir(oldSessionChunkDir).removeRecursively();`
- 在 `handleUploadCheck` 中，复制完成后记录旧目录路径，在新 session 第一个分块上传成功后删除

**验证**：
- 续传后旧 session 目录被删除

**回滚**：移除删除逻辑

---

### Step 3.10：客户端断连时持久化状态

**文件**：`src/network/RequestHandler.cpp`

**操作**：
- `streamingConnDisconnected` 回调中：
  - 对 `m_streamingFileStates` 中的状态：
    - 如果 `state->isChunked` 且正在上传：
      - `updateChunkStatus(stateFilePath, state->chunkIndex, "partial", state->bytesReceived);`
  - 对 `m_streamingStates` 中的状态（非分块上传）：无需处理（不创建状态文件）

**验证**：
- 上传中断连后状态文件中 uploading 分块变为 partial
- 重启后续传可从 partial 分块恢复

**回滚**：移除断连持久化逻辑

---

### Step 3.11：cleanupExpiredSessions 同步清理状态文件

**文件**：`src/network/RequestHandler.cpp`

**操作**：
- `cleanupExpiredSessions()` 中，移除 session 前：
  - 遍历 `session.fileChunkStates`（现在是 `ChunkStateInfo`）
  - 对每个文件：构造状态文件路径并删除 `.netshare` 文件

**验证**：
- 过期 session 清理后对应状态文件也被删除

**回滚**：移除状态文件清理

---

### Step 3.12：删除 FileChunkState/ChunkUploadInfo，UploadSession 改用 ChunkStateInfo

**文件**：`src/network/RequestHandler.h`、`src/network/RequestHandler.cpp`

**操作**：

1. `RequestHandler.h`：
   - 添加 `#include "ChunkState.h"`
   - 删除 `ChunkUploadInfo` 结构体定义（第 53-57 行）
   - 删除 `FileChunkState` 结构体定义（第 60-69 行）
   - `UploadSession.fileChunkStates`：`QMap<QString, FileChunkState>` → `QMap<QString, ChunkStateInfo>`

2. `RequestHandler.cpp`：
   - 所有 `FileChunkState fcs;` → `ChunkStateInfo fcs;`
   - 所有 `ChunkUploadInfo cui;` → `ChunkState cs;`
   - `fcs.completedChunks` → 遍历 `fcs.chunks` 计算
   - `cui.completed` → `cs.status == "completed"`
   - `fcs.chunks[chunkIndex].completed` → `fcs.chunks[chunkIndex].status == "completed"`
   - 所有 `.completed = true` → `.status = "completed"`

**验证**：
- 全项目编译通过
- `grep -r "FileChunkState\|ChunkUploadInfo" src/` 无结果
- 上传功能正常（分块/非分块）

**回滚**：恢复旧结构体定义和引用

---

## Phase 4：deleteTask 和启动恢复改造（P0）

### Step 4.1：deleteTask 增加状态文件清理

**文件**：`src/core/transfer/FileTransferEngine.cpp`

**操作**：
- `deleteTask()` 中，在清理内存任务和数据库日志之前：
  1. 获取任务的 fileName、type
  2. 构造状态文件路径：
     - 下载：`m_tempDirectory + "/" + fileName + ".netshare"`
     - 上传：需从任务信息中获取 uploadDir（可通过 m_taskStateFiles 映射）
  3. `m_chunkStateManager->deleteStateFile(stateFilePath);`
  4. 删除临时分块目录（从 m_taskStateFiles 或任务信息中获取 chunkDir）
  5. 从 `m_taskStateFiles` 移除映射

**验证**：
- 删除任务后 .netshare 文件和临时分块目录均被删除

**回滚**：移除清理逻辑

---

### Step 4.2：应用启动时扫描可续传任务

**文件**：`src/core/transfer/FileTransferEngine.cpp`（`initialize()` 方法）

**操作**：
- 在 `initialize()` 中，`restorableLogs()` 恢复任务之后：
  - 调用 `m_chunkStateManager->scanResumableTasks(m_tempDirectory)` 扫描下载状态文件
  - 调用 `m_chunkStateManager->scanResumableTasks(uploadDir + "/.chunks")` 扫描上传状态文件
  - 对扫描到的可续传任务，合并到 `m_tasks` 中（更新 transferredSize、progress 等）
  - 合并策略：状态文件为权威源（参见计划文档 2.5 节合并策略）

**验证**：
- 下载中断后重启，传输列表显示正确的进度和状态
- 上传中断后重启，传输列表显示正确的进度和状态

**回滚**：移除 scanResumableTasks 调用

---

### Step 4.3：合并数据库日志和状态文件信息

**文件**：`src/core/transfer/TransferLogService.cpp`、`src/core/transfer/FileTransferEngine.cpp`

**操作**：
- `restorableLogs()` 返回的日志中，对有对应状态文件的任务：
  - 用状态文件中的 transferredSize 覆盖数据库中的值
  - 用状态文件中的 status 覆盖数据库中的状态
- 对数据库中有记录但无状态文件的任务：保持数据库信息不变

**验证**：
- 数据库显示 "Started" 但状态文件显示 "paused" → 传输列表显示 paused
- 数据库 transferredSize 与状态文件不一致 → 以状态文件为准

**回滚**：移除合并逻辑

---

### Step 4.4：启动时清理过期状态文件

**文件**：`src/main.cpp`（`initializeCoreServices()` 末尾）

**操作**：
- `m_chunkStateManager->cleanupExpired(m_tempDirectory, 7);`
- `m_chunkStateManager->cleanupExpired(uploadDir + "/.chunks", 7);`

**验证**：
- >7 天的 .netshare 文件被自动删除

**回滚**：移除调用

---

### Step 4.5：stopAllTasks 更新状态文件

**文件**：`src/core/transfer/FileTransferEngine.cpp`

**操作**：
- `stopAllTasks()` 中，对 Downloading/Uploading 任务：
  - 通过 `m_taskStateFiles` 查找状态文件路径
  - `updateTaskStatus(stateFilePath, "paused");`
  - 遍历 chunks，将 downloading/uploading 的改为 partial
- 然后再标记为 Cancelled 并写入数据库

**验证**：
- 正常关闭程序后，.netshare 文件中 downloading 分块变为 partial
- 下次启动可从 partial 恢复

**回滚**：移除状态文件更新

---

### Step 4.6：initialize 整合 scanResumableTasks

**文件**：`src/core/transfer/FileTransferEngine.cpp`

**操作**：
- 此步骤与 Step 4.2 合并实施

**验证**：同 Step 4.2

**回滚**：同 Step 4.2

---

## Phase 5：清理（P1）

### Step 5.1：删除 ResumeManager 类

**文件**：`src/core/transfer/ResumeManager.h`、`src/core/transfer/ResumeManager.cpp`

**操作**：
- 删除 `ResumeManager.h` 和 `ResumeManager.cpp`

**验证**：
- 编译通过（需先完成 Step 5.2-5.4）

**回滚**：恢复文件

---

### Step 5.2：从 CMakeLists.txt 移除 ResumeManager

**文件**：`src/core/CMakeLists.txt`

**操作**：
- `NETSHARE_CORE_TRANSFER_SOURCES` 移除 `transfer/ResumeManager.cpp`
- `NETSHARE_CORE_TRANSFER_HEADERS` 移除 `transfer/ResumeManager.h`

**验证**：编译通过

**回滚**：恢复条目

---

### Step 5.3：清理 FileTransferEngine 中 m_resumeManager 残留

**文件**：`src/core/transfer/FileTransferEngine.h`、`src/core/transfer/FileTransferEngine.cpp`

**操作**：
- 确认 `m_resumeManager` 成员已替换为 `m_chunkStateManager`（Step 1.5/1.6 已完成）
- 确认所有 `ResumeManager` 引用已移除
- 移除 `#include "ResumeManager.h"`（Step 1.6 已完成）

**验证**：
- `grep -r "ResumeManager\|m_resumeManager" src/core/transfer/FileTransferEngine.*` 无结果

**回滚**：恢复引用

---

### Step 5.4：清理 main.cpp 中 ResumeManager 残留

**文件**：`src/main.cpp`

**操作**：
- 确认 `#include "core/transfer/ResumeManager.h"` 已移除（Step 1.11 已完成）
- 确认 `m_resumeManager` 成员已替换（Step 1.11 已完成）
- 确认 `buildInjector()` 中已更新（Step 1.11 已完成）

**验证**：
- `grep -r "ResumeManager\|m_resumeManager" src/main.cpp` 无结果

**回滚**：恢复引用

---

### Step 5.5：验证 RequestHandler 中旧结构体残留

**文件**：`src/network/RequestHandler.h`、`src/network/RequestHandler.cpp`

**操作**：
- 确认 `FileChunkState` 和 `ChunkUploadInfo` 定义已删除（Step 3.12 已完成）
- 确认所有引用已替换
- `grep -r "FileChunkState\|ChunkUploadInfo\|\.completed\b" src/network/RequestHandler.*` 无结果

**验证**：
- 编译通过
- 上传功能正常

**回滚**：恢复旧定义

---

### Step 5.6：清理废弃结构体引用

**文件**：相关文件

**操作**：
- 全局搜索 `ChunkInfo`、`FileChunkState`、`ChunkUploadInfo`、`ResumeManager`、`ResumeInfo`
- 确认无残留引用

**验证**：
- `grep -r "ChunkInfo\|FileChunkState\|ChunkUploadInfo\|ResumeManager\|ResumeInfo" src/` 仅在注释中出现（如有）

**回滚**：恢复引用

---

## 编译验证检查点

| 检查点 | 位置 | 验证内容 |
|--------|------|---------|
| CP1 | Phase 1 完成后 | 全项目编译通过，程序可启动，下载/上传基本功能正常 |
| CP2 | Phase 2 完成后 | 下载续传功能正常，状态文件正确创建/更新/删除 |
| CP3 | Phase 3 完成后 | 上传续传功能正常，FileChunkState/ChunkUploadInfo 已删除 |
| CP4 | Phase 4 完成后 | deleteTask 清理完整，启动恢复正常，stopAllTasks 更新状态文件 |
| CP5 | Phase 5 完成后 | ResumeManager 已删除，无残留引用 |

---

## 关键数据结构映射

| 旧类型 | 新类型 | 字段映射 |
|--------|--------|---------|
| `ChunkInfo::Pending` | `ChunkState.status = "pending"` | 枚举 → 字符串 |
| `ChunkInfo::Downloading` | `ChunkState.status = "downloading"` | 枚举 → 字符串 |
| `ChunkInfo::Completed` | `ChunkState.status = "completed"` | 枚举 → 字符串 |
| `ChunkInfo::Failed` | `ChunkState.status = "failed"` | 枚举 → 字符串 |
| — | `ChunkState.status = "partial"` | 新增状态 |
| — | `ChunkState.status = "uploading"` | 新增状态 |
| — | `ChunkState.downloaded` | 新增字段 |
| `ChunkUploadInfo.completed` | `ChunkState.status == "completed"` | bool → 字符串比较 |
| `FileChunkState.completedChunks` | 遍历 `ChunkStateInfo.chunks` 计算 | 计算属性 |
| `FileChunkState.useChunking` | `ChunkStateInfo.totalChunks > 1` | 推断属性 |
| `ResumeInfo` | `ChunkStateInfo` | 完全替代 |
