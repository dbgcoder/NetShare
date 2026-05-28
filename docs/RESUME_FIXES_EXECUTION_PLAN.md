# 续传三大问题修复执行文档

> 本文档定义三个续传问题的根因、修复步骤、验证方法和规则依据。仅做规划，不改动代码。

---

## 问题 1：续传时重复创建分块文件夹

### 现象

同一文件续传时，`.chunks/` 下出现多个 UUID 目录，每次 `/api/upload/check` 都生成新 `sessionId` 和新 `chunkTempDir`，旧目录残留未清理。

### 根因

`RequestHandler.cpp` → `handleUploadCheck`：续传时无条件执行 `sessionId = QUuid::createUuid().toString()` 和 `chunkTempDir = dir + "/.chunks/" + sessionId`，未从状态文件中读取并复用已有的 `chunkDir`。

### 修复步骤

#### Step 1.1：续传时复用已有 chunkDir + sessionId

**文件**：`src/network/RequestHandler.cpp` → `handleUploadCheck`

**操作**：

1. 当状态文件存在且文件信息匹配（`fileSize` + `totalChunks` 一致）时：
   - 从 `ChunkStateInfo.chunkDir` 读取已有的分块目录路径
   - 如果该目录存在，直接复用，不创建新 `sessionId`、不创建新 `chunkTempDir`
   - 如果该目录不存在（被手动删除），回退到创建新目录

2. **sessionId 复用**：
   - `chunkTempDir = QFileInfo(chunkDir).absolutePath()`（取 chunkDir 的父目录）
   - 旧 sessionId 提取：`QString oldSessionId = QFileInfo(chunkTempDir).fileName()`
   - 检查 `m_uploadSessions` 中该 key 无冲突后，写入 `m_uploadSessions[oldSessionId] = session`
   - 返回的 `result["sessionId"]` 使用旧值，前端仍用同一 sessionId 交互

3. **删除分块复制逻辑（死代码）**：
   - 复用 chunkDir 且复用旧 sessionId 时，已完成分块已经在正确目录中
   - `if (completedCount > 0) { QString newChunkDir = ...; QDir().mkpath(newChunkDir); for (...) { QFile::rename(...); } }` 整段删除
   - 即使新建目录的场景（旧目录被手动删除），也不需要移动已完成分块——新目录是空的，分块由前端重新上传覆盖

4. **状态文件字段更新策略**：
   - `chunkDir`：复用时不覆盖，保持状态文件中的路径值不变
   - `taskId`：更新为恢复后的 taskId（Step 2.1 恢复时用旧 taskId；新建时用新 taskId）
   - `lastUpdated`：更新为当前时间
   - `status`：保持 `"uploading"`

5. **多文件场景**：
   - 每个文件独立检查和复用其 `chunkDir`
   - 多文件共享同一个 `chunkTempDir`（旧 UUID 目录），所有文件的 chunkDir 均为 `chunkTempDir + "/" + relativePath`
   - 不同文件可能来自不同的旧 session（极端情况），以第一个有状态文件的 chunkDir 父目录作为共享 chunkTempDir

6. 当状态文件不存在或文件信息不匹配时：保持现有逻辑，创建新 `sessionId` 和新 `chunkTempDir`

**验证**：
1. 首次上传 → `.chunks/` 下创建一个 UUID 目录
2. 暂停后续传同一文件 → 不创建新 UUID 目录，复用已有目录
3. 返回的 `result["sessionId"]` 与旧 sessionId 一致
4. 手动删除 UUID 目录后续传 → 自动创建新目录，不报错
5. 多文件文件夹续传 → 共用同一 UUID 目录，不新建

---

#### Step 1.2：清理残留的旧 UUID 目录

**文件**：`src/network/RequestHandler.cpp` → `handleUploadCheck`

**操作**：

1. 续传复用已有 `chunkDir` 时，扫描 `.chunks/` 目录下其他以 `{` 开头的 UUID 子目录
2. 对每个旧 UUID 目录，检查其目录名是否与当前文件的 `chunkDir` 一致
3. 如果不一致且该目录下无活跃的 UploadSession 使用，删除该目录
4. 判断"活跃的 UploadSession"的方法——遍历 `m_uploadSessions`（`QHash<QString, UploadSession>`）：
   ```cpp
   bool isInUse = false;
   for (auto it = m_uploadSessions.constBegin(); it != m_uploadSessions.constEnd(); ++it) {
       if (it.value().chunkTempDir.startsWith(chunksMetaDir + "/" + dirName)) {
           isInUse = true;
           break;
       }
   }
   ```
5. 仅在 `isInUse == false` 时删除该目录

**验证**：
1. 存在多个残留 UUID 目录时续传 → 仅保留当前文件使用的目录，其余被清理
2. 其他文件正在上传的 UUID 目录 → 不被误删

---

#### Step 1.3：上传完成/中止时清理 chunkDir（已验证完整）

**文件**：`src/network/RequestHandler.cpp`

**验证结论**：代码审查确认以下 4 条清理路径均已完整实现，无需额外修改。

| 路径 | 清理内容 | 状态 |
|------|---------|------|
| `handleUploadFinalize` | `deleteStateFile` + `QDir(chunkTempDir).removeRecursively()` + 空目录自动清理 | ✓ 已完整 |
| `handleUploadAbort` | `failTask` + `deleteStateFile` + `QDir(chunkTempDir).removeRecursively()` + `m_uploadSessions.remove` | ✓ 已完整 |
| `cleanupExpiredSessions` | `failTask` + `deleteStateFile` + `QDir(chunkTempDir).removeRecursively()` + `m_uploadSessions.remove` | ✓ 已完整 |
| `streamingConnDisconnected` | 将 uploading 分块转 partial 并保存状态文件（不删除目录或状态文件） | ✓ 已完整 |

---

## 问题 2：续传时新建任务而非恢复暂停任务

### 现象

同一文件续传时，传输列表显示多个「已暂停」任务，而非将暂停任务恢复为进行中状态。

### 根因

`RequestHandler.cpp` → `handleUploadCheck`：续传时无条件调用 `addUploadingTask` 创建新任务，未检查同文件名的 `Paused` 状态任务。`removeFailedUploadTasksByName` 仅移除 `Failed` 和 `Uploading` 状态，`Paused` 任务未被处理。

### 修复步骤

#### Step 2.1（核心修复）：续传时恢复暂停任务而非新建

**文件**：`src/network/RequestHandler.cpp` → `handleUploadCheck`、`src/core/transfer/FileTransferEngine.cpp`

**操作**：

1. 在 `handleUploadCheck` 中，调整执行顺序为：
   - **① 查找**：遍历 `m_transferEngine` 的上传任务列表，查找同文件名且状态为 `Paused` 的任务
   - **② 找到则恢复**：
     - 调用 `FileTransferEngine::resumeTask(taskId)` 恢复任务为 `Uploading`
     - 调用 `FileTransferEngine::updateTaskProgress(taskId, initialTransferredSize)` 更新进度
     - 覆盖 `taskId` 为旧暂停任务的 ID
     - **同步更新 `m_uploadSessions`**：创建新 `UploadSession` 写入 `m_uploadSessions[oldSessionId]`，设置 `session.paused = false`，更新 `m_taskToToken[oldTaskId] = oldSessionId`
     - **跳过** `addUploadingTask` 和 `removeFailedUploadTasksByName`
   - **③ 未找到则清理+新建**：
     - 调用 `removeFailedUploadTasksByName(taskFileName)`（含 Step 2.2 扩展的 Paused 移除）
     - 创建新任务 `addUploadingTask(task)`

2. 在 `FileTransferEngine` 中新增公开方法 `findUploadTaskByName(const QString& fileName)`：返回匹配 `fileName` 且 `status == Paused` 的 taskId，否则返回空字符串

3. **`resumeTask` 回调验证**：
   - `resumeTask` 内部调用 `m_uploadResumeCallback(taskId)` 更新 UploadSession 的 paused 状态
   - 确认 `setUploadResumeCallback` 是否已在 RequestHandler 初始化时设置
   - 若未设置：在 `resumeTask` 中添加 `if (m_uploadResumeCallback)` 空检查

4. **多文件场景**：文件夹场景下 `taskFileName = folderRoot`，查找暂停任务时使用 folderRoot 作为匹配条件，同一文件夹下所有文件共享一个 task

**验证**：
1. 暂停上传后重新上传同一文件 → 暂停任务变为进行中，不新增任务
2. 传输进度从暂停时的进度继续
3. 上传失败后重新上传 → 旧失败任务被覆盖为新任务
4. 上传进行中时重新上传 → 旧上传中任务被覆盖为新任务

---

#### Step 2.2（辅助防护）：扩展 removeFailedUploadTasksByName 以移除 Paused 任务

**文件**：`src/core/transfer/FileTransferEngine.cpp` → `removeFailedUploadTasksByName`

**操作**：

1. 将移除条件从 `Failed || Uploading` 扩展为 `Failed || Uploading || Paused`
2. **生效条件**：仅在 Step 2.1 步骤③（未找到可恢复的 Paused 任务）的分支中触发
3. Step 2.1 步骤②找到暂停任务时，整个清理+新建被跳过，此步骤不触发

**验证**：
1. 暂停上传后重新上传（Step 2.1 未生效的极端情况）→ 旧暂停任务被移除，仅显示新任务
2. Step 2.1 正常生效时 → 此步骤的 Paused 移除条件不触发

---

## 问题 3：软件重启后手机端上传未对接续传

### 现象

软件重启后，手机端浏览器未退出时直接开始上传，但电脑端从 0 开始接收。

### 根因

- `UploadSession` 是纯内存结构（`QHash<QString, UploadSession> m_uploadSessions`），重启后丢失
- 前端 WebSocket 断连重连后，仅重新订阅旧 `uploadSessionId`，未重新发起 `/api/upload/check`
- 服务端收到旧 sessionId 请求时返回 400，未提供续传引导信息

### 修复步骤

#### Step 3.1：前端 WebSocket 重连后仅订阅，不主动 recheck

**文件**：`web/receive.html`

**操作**：

1. WS 重连后仅订阅旧 `uploadSessionId`，继续正常发送分块
2. 不添加 `serverRestarted` 标志（WS 断连可能是网络闪断，非一定是服务端重启；闪断时旧 sessionId 仍有效，强制 recheck 不必要且有害）
3. recheck 触发改为被动检测：由 Step 3.2 的 410 响应驱动 Step 3.3 的自动 recheck

**验证**：
1. 正常上传中，服务端重启 → WS 断连 → 自动重连 → 仅订阅旧 sessionId → 继续发送分块 → XHR 收到 410 → 自动 recheck → 续传
2. 网络短暂断开 → 重连后不触发 recheck，分块上传继续正常进行

---

#### Step 3.2：后端 handleUploadSingleFile 返回 410 引导续传

**文件**：`src/network/RequestHandler.cpp` → `handleUploadSingleFile`

**操作**：

1. 当前端使用失效 `sessionId` 发送分块上传请求时（`m_uploadSessions.contains(sessionId)` 返回 false）：
   - 读取 `X-File-Path` header 获取 `filePath`
   - **若 `filePath` 为空**：直接返回 400（`{"error":"Invalid or expired upload session"}`）
   - **若 `filePath` 非空**：构造状态文件路径 `<uploadDir>/.chunks/<filePath>.netshare`，由 `ChunkStateManager::loadStateFile` 判断是否存在
   - 如果状态文件存在，返回 **410 Gone** + `{"error":"session_expired","hint":"recheck_required"}`
   - 如果状态文件不存在，返回 400（保持现有行为）
2. `handleUploadData`（旧非流式上传入口）不修改，保持现有 400 行为

**验证**：
1. 服务端重启后，前端用旧 sessionId 上传分块 → 返回 410 + `recheck_required`
2. 前端收到 410 后自动 recheck → 获取新 sessionId
3. 无状态文件的请求 → 返回 400

---

#### Step 3.3：前端 XHR 捕获 410 后自动 recheck 续传

**文件**：`web/receive.html`

**操作**：

1. 在分块上传的 XHR `onerror` / `onload` 回调中检查 HTTP 状态码是否为 410
2. 如果是 410，暂停当前所有上传，自动调用 `/api/upload/check` 获取新 `sessionId`
3. 获取新 `sessionId` 后，从断点继续上传（跳过已完成分块）
4. 添加重试计数器，最多 3 次，超过后显示错误提示

**验证**：
1. 上传过程中服务端重启 → XHR 收到 410 → 自动 recheck → 续传
2. 连续多次重启 → 最多重试 3 次，超过后显示错误提示

---

### 补充说明：页面刷新后续传已自动覆盖

页面刷新后，前端 JS 变量全部丢失，但用户重新选择文件触发 `startUpload()` → `fetch('/api/upload/check')` → 后端状态文件仍在 → 返回 `partial` 信息 → 前端跳过已完成分块。此流程已有完整支持，无需额外修复。Step 1.1 的复用 chunkDir 在此场景下自动生效。

---

## 执行顺序

```
Step 1.1（复用 chunkDir + 复用 sessionId + 删除分块复制死代码 + 多文件处理）
    ↓
Step 1.2（清理残留目录）── 依赖 1.1
    ↓
Step 1.3（验证清理路径完整性）── 已验证完整，无需修改

Step 2.1（恢复暂停任务 + 调整执行顺序 + m_uploadSessions 同步 + 回调验证）
    │  执行顺序：① 查找 Paused → ② 找到则 resumeTask + updateTaskProgress + 同步 session → ③ 未找到则清理+新建
    ↓
Step 2.2（扩展移除条件为 Failed|Uploading|Paused）── 辅助防护，仅在 Step 2.1-③ 分支生效

Step 3.1（WS 重连后仅订阅，不主动 recheck）
    ↓
Step 3.2（handleUploadSingleFile 返回 410 + recheck_required，依赖 X-File-Path header）
    ↓
Step 3.3（前端 XHR 捕获 410 → 自动 recheck → 续传）
```

**推荐执行顺序**：1.1 → 1.2 → 1.3 → 2.1 → 2.2 → 3.1 → 3.2 → 3.3

**关键约束**：
- Step 2.1 和 2.2 有严格顺序依赖：2.1 先查找恢复，2.2 的 Paused 移除仅在 2.1 未找到时触发
- Step 3.1/3.2/3.3 需前后端协议一致：后端 410 响应格式决定前端解析逻辑
- recheck 的唯一入口是 Step 3.3 收到 410 后，Step 3.1 的 WS 重连不触发 recheck

---

## 伪代码

```cpp
// handleUploadCheck 中（Step 1.1 + 2.1 + 2.2 合并逻辑）：

// === Step 1.1：复用 chunkDir + sessionId ===
QString sessionId = QUuid::createUuid().toString();
QString chunkTempDir = dir + "/.chunks/" + sessionId;

if (chunkStateMgr) {
    for (auto it = fileChunkStates.begin(); it != fileChunkStates.end(); ++it) {
        ChunkStateInfo& csi = it.value();
        if (csi.totalChunks <= 0) continue;

        QString stateFilePath = chunksMetaDir + "/" + it.key() + ".netshare";
        ChunkStateInfo existingInfo;
        if (chunkStateMgr->loadStateFile(stateFilePath, existingInfo)
            && existingInfo.fileSize == csi.fileSize
            && existingInfo.totalChunks == csi.totalChunks) {

            // 复用 chunkDir
            QString oldChunkDir = existingInfo.chunkDir;
            if (QDir(oldChunkDir).exists()) {
                chunkTempDir = QFileInfo(oldChunkDir).absolutePath();
                sessionId = QFileInfo(chunkTempDir).fileName();  // 复用旧 sessionId
            }
            // 状态文件字段更新
            existingInfo.taskId = taskId;  // taskId 由 Step 2.1 决定最终值
            existingInfo.status = "uploading";
            existingInfo.lastUpdated = QDateTime::currentDateTime().toString(Qt::ISODate);
            // chunkDir 不覆盖
            chunkStateMgr->saveStateFile(stateFilePath, existingInfo);

            // 不执行分块复制/移动（死代码已删除）
            csi = existingInfo;
        } else {
            // 新建：创建新 sessionId + chunkTempDir
            csi.taskId = taskId;
            csi.chunkDir = chunkTempDir + "/" + it.key();
            chunkStateMgr->createStateFile(stateFilePath, csi);
        }
    }
}

// === Step 1.2：清理残留目录 ===
// 扫描 .chunks/ 下 UUID 目录，删除非活跃且非当前 session 的目录

// === Step 2.1 + 2.2：任务恢复或新建 ===
QString pausedTaskId = findUploadTaskByName(taskFileName);
if (!pausedTaskId.isEmpty()) {
    // Step 2.1：恢复暂停任务
    resumeTask(pausedTaskId);
    updateTaskProgress(pausedTaskId, initialTransferredSize);
    // 同步 m_uploadSessions
    m_uploadSessions[sessionId].paused = false;
    m_taskToToken[pausedTaskId] = sessionId;
    taskId = pausedTaskId;  // 使用旧 taskId
} else {
    // Step 2.2：清理（含 Paused）+ 新建
    removeFailedUploadTasksByName(taskFileName);
    addUploadingTask(newTask);
}
```

```javascript
// receive.html 中（Step 3.1 + 3.3 合并逻辑）：

// Step 3.1：WS 重连后仅订阅
ws.onopen = function() {
    wsConnected = true;
    if (uploadSessionId) {
        ws.send(JSON.stringify({ type: 'subscribe', data: { token: uploadSessionId } }));
    }
    // 不主动 recheck
};

// Step 3.3：XHR 捕获 410 → recheck
xhr.onload = function() {
    if (xhr.status === 410) {
        // 自动 recheck
        recheckCount++;
        if (recheckCount <= 3) {
            fetch('/api/upload/check', { ... })
                .then(r => r.json())
                .then(data => {
                    uploadSessionId = data.sessionId;
                    // 从断点继续上传
                });
        } else {
            showError('续传失败，请重新上传');
        }
    }
};
```

---

## 验证检查清单

| Step | 验证项 | 期望结果 | 验证方式 |
|------|--------|---------|---------|
| 1.1 | 同一文件续传后 UUID 目录数量 | 始终为 1 | 文件系统观察 |
| 1.1 | 返回的 sessionId 与旧值一致 | 一致 | 对比前后端日志 |
| 1.1 | 已完成分块未被移动/复制 | 死代码已删除，无 rename | 日志确认 |
| 1.1 | 状态文件 chunkDir 字段 | 指向正确目录，未被覆盖 | 读取 .netshare 文件 |
| 1.1 | 多文件续传 | 共用同一 UUID 目录 | 文件系统观察 |
| 1.1 | 手动删除 UUID 目录后续传 | 自动创建新目录，不报错 | 文件系统观察 |
| 1.2 | 残留 UUID 目录清理 | 无残留，活跃 session 不被误删 | 文件系统观察 |
| 1.3 | 4 条清理路径 | 均已完整实现 | 代码审查确认 |
| 2.1 | 续传后传输列表任务数量 | 同文件名仅 1 个任务 | UI 观察 |
| 2.1 | 暂停任务恢复后状态 | 从 Paused 变为 Uploading | 日志确认 |
| 2.1 | 续传后 transferredSize | 从暂停时的值继续累加 | 日志 + UI 进度 |
| 2.1 | m_uploadSessions 同步 | session.paused = false，m_taskToToken 正确 | 日志确认 |
| 2.1 | callback 空检查 | 输出 `[UploadCheck][callback.check]` | 日志确认 |
| 2.2 | 旧 Paused 任务被移除（兜底） | 仅在 Step 2.1 未找到时触发 | 日志确认 |
| 3.1 | WS 重连后不自动 recheck | 仅订阅，无 `/api/upload/check` 请求 | 前端控制台 |
| 3.2 | 旧 sessionId 分块请求返回 410 | 返回 `session_expired` | 前端控制台 + 后端日志 |
| 3.2 | X-File-Path 为空时返回 400 | 返回 400 而非 410 | 后端日志 |
| 3.2 | handleUploadData 保持 400 | 返回 400，不返回 410 | 前端控制台 |
| 3.3 | 410 后自动续传 | 进度从断点继续 | UI 进度观察 |
| 3.3 | 重试次数 ≤ 3 | 超过后显示错误提示 | UI 观察 |

---

## 日志打点

### 后端 C++ 日志

| 位置 | Tag | 内容 |
|------|-----|------|
| `handleUploadCheck` 复用 chunkDir | `[UploadCheck][chunkdir.reuse]` | fileName、旧 chunkDir、旧 sessionId |
| `handleUploadCheck` 新建 chunkDir | `[UploadCheck][chunkdir.create]` | fileName、新 chunkDir、新 sessionId |
| `handleUploadCheck` 清理残留目录 | `[UploadCheck][chunkdir.cleanup]` | 被删除的目录路径列表 |
| `handleUploadCheck` 查找并恢复暂停任务 | `[UploadCheck][task.resume]` | fileName、旧 taskId、transferredSize |
| `handleUploadCheck` 未找到暂停任务 | `[UploadCheck][task.create]` | fileName、新 taskId |
| `handleUploadCheck` callback 空检查 | `[UploadCheck][callback.check]` | `resumeCallback is null: true/false` |
| `removeFailedUploadTasksByName` 移除 Paused | `[TaskManager][remove.paused]` | fileName、被移除的 taskId 列表 |
| `handleUploadSingleFile` 返回 410 | `[UploadSingleFile][session.expired]` | 旧 sessionId、filePath、状态文件是否存在 |

### 前端日志

| 位置 | Tag | 内容 |
|------|-----|------|
| WS 重连后订阅 | `[WSReconnect][subscribe]` | uploadSessionId、当前活跃任务数 |
| XHR 收到 410 | `[Upload][session.expired]` | fileName、重试次数 |
| Step 3.3 触发 recheck | `[Upload][recheck.triggered]` | 原因：410、文件列表 |
| recheck 后续传 | `[Upload][resume.from_check]` | fileName、completedChunks 数量 |

---

## 回滚策略

| Step | 回滚方式 |
|------|---------|
| 1.1 | 恢复 `sessionId` 和 `chunkTempDir` 的原始生成逻辑（每次新建）；恢复已删除的分块复制死代码；移除 sessionId 复用的 `m_uploadSessions` 写入 |
| 1.2 | 移除残留目录清理代码 |
| 2.1 | 移除暂停任务查找和恢复逻辑、`findUploadTaskByName` 方法；恢复执行顺序为直接清理+新建；移除 `m_uploadSessions` 同步代码 |
| 2.2 | 将 `removeFailedUploadTasksByName` 条件恢复为 `Failed \|\| Uploading` |
| 3.1 | 恢复 `ws.onopen` 为仅订阅（当前已为仅订阅，无需回滚） |
| 3.2 | 将 `handleUploadSingleFile` 的 410 响应恢复为 400 |
| 3.3 | 移除 XHR 410 处理逻辑和重试计数器 |

---

## 编译验证

每步修改后执行：

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" --build "D:\qt6cmake\NetShare\build" --config Release
```

确保编译器为 MSVC，无编译错误和链接错误。
