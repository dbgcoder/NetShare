# 下载功能修复执行文档（第二轮）

## 0. 问题概述

### 主要问题
1. **传输列表页下载文件没有速度** — 下载进度回调在 CivetWeb 工作线程中直接调用 `updateTaskProgress`，未使用 `QMetaObject::invokeMethod` + `QueuedConnection` 切换到主线程，与上传的线程安全做法不一致；且 `onTaskStarted`/`onTaskCompleted` 都调用 `refreshTasks()` 重建列表，下载快时速度来不及显示就被覆盖为 0
2. **同一任务出现多次，续传一次多一条** — 每次 HTTP 下载请求都创建新 TransferTask（新 UUID），`completeTask`/`failTask` 不从 `m_tasks` 移除任务，`getAllTasks()` 返回全部任务，QML active tasks 部分没有按 `type:fileName` 去重；`recordCompletedTransfer` 和 `failTask` 都写 transferLog 但下载完成时没有先删旧 log
3. **移动端不是断点续传** — 分享页面下载链接是 `<a href="/download/...">` 原生 HTML 标签，浏览器原生下载不支持断点续传，需要用 JavaScript fetch + Range header 实现

### 解决方法
1. 下载进度回调、`completeTask`、`failTask`、`recordCompletedTransfer` 全部改用 `QMetaObject::invokeMethod` + `QueuedConnection` 确保主线程执行
2. 续传时不创建新任务，而是找到失败/暂停的旧任务恢复为 Downloading 状态继续执行；已完成任务重新下载时删除旧任务再创建新的；正在下载的任务不重复创建——从源头消除重复
3. 移动端分享页面增加 JavaScript 断点续传下载功能，会话内用 fetch + Range + 内存 Blob 实现 100% 可靠续传；跨会话用 IndexedDB 存储部分数据实现尽力续传

---

## 1. 下载所有主线程操作改用 QueuedConnection 确保线程安全

- **修改内容**：
  - `RequestHandler.cpp` 的 `handleFileDownload` 中，将以下四处操作全部改用 `QMetaObject::invokeMethod` + `QueuedConnection`：
    1. 进度回调：从直接调用 `m_transferEngine->updateTaskProgress(...)` 改为 `QMetaObject::invokeMethod(m_transferEngine, [...]() { m_transferEngine->updateTaskProgress(...); }, Qt::QueuedConnection)`
    2. `completeTask` 调用：同上
    3. `failTask` 调用：同上
    4. `recordCompletedTransfer` 调用：同上（`logTransfer` 修改 `m_logs` 非线程安全，也必须在主线程执行）
  - `handleFolderDownload` 同样处理上述四项
  - 注意：`LOG_INFO`/`LOG_WARN` 可在工作线程直接调用（线程安全），不需要 QueuedConnection
  - **QueuedConnection 时序保证**：工作线程中先 post 所有 `taskProgress` 事件，最后 post `taskCompleted`/`taskFailed` 事件，主线程按 post 顺序依次执行，不会乱序
- **难易程度**：中
- **完成状态**：完成
- **验证方式**：编译通过；用户确认下载大文件时传输列表页显示速度

## 2. 续传时复用已有任务，从源头消除重复

- **修改内容**：
  - `FileTransferEngine.h` 新增 `QString resumeOrCreateDownloadTask(const QString& fileName, const QString& filePath, qint64 fileSize, qint64 startByte)` 方法声明
  - `FileTransferEngine.cpp` 实现 `resumeOrCreateDownloadTask`，逻辑如下：
    - 遍历 `m_tasks` 查找 `fileName` 相同且 `type == Download` 的任务
    - **情况A：找到且 status == Failed/Paused/Cancelled** → 恢复任务：
      - 重置 status 为 Downloading
      - 设置 transferredSize = startByte（从 Range 请求头解析）
      - 设置 progress = startByte * 100 / fileSize
      - 重置 speed = 0，清空 error
      - 更新 startedAt 为当前时间
      - 清除 m_speedHistory[taskId]
      - 删除旧 Failed/Cancelled log（`m_transferLogService->deleteLogsByFileName`）
      - emit taskResumed(taskId)
      - 返回现有 taskId
    - **情况B：找到且 status == Downloading** → 正在下载中，不创建新任务：
      - 返回现有 taskId（第二个 HTTP 响应共用同一任务的进度追踪）
    - **情况C：找到且 status == Completed** → 重新下载：
      - 删除旧任务（从 m_tasks/m_speedHistory/m_lastProgressTime 移除）
      - 删除旧 Completed log
      - emit taskDeleted(taskId)
      - 继续创建新任务
    - **情况D：未找到** → 创建新任务：
      - 生成新 UUID，设置 fileName/filePath/fileSize/transferredSize=startByte/progress/speed=0
      - m_tasks[taskId] = task
      - emit taskStarted(taskId)
      - 返回新 taskId
  - `RequestHandler.cpp` 的 `handleFileDownload` 中，替换原来的任务创建逻辑：
    - 先解析 Range 头获取 startByte（0 如果无 Range）
    - 调用 `QMetaObject::invokeMethod(m_transferEngine, [...]() -> QString { return m_transferEngine->resumeOrCreateDownloadTask(...); }, Qt::BlockingQueuedConnection)` 获取 taskId
    - 使用返回的 taskId 作为 capturedTaskId 传给进度回调和 completeTask/failTask
  - `handleFolderDownload` 同样处理（用 zipName 作为 fileName）
  - **为什么用 BlockingQueuedConnection**：`resumeOrCreateDownloadTask` 修改 `m_tasks`（主线程数据），必须在线程安全的环境执行；同时需要同步返回 taskId 给工作线程用于后续进度回调。`BlockingQueuedConnection` 阻塞工作线程，等主线程执行完毕后返回结果，线程安全且不改变现有架构
  - **为什么不需要延迟移除**：每个文件最多一个任务（续传复用、重下载替换），不存在重复积累问题。已完成任务保留在 `m_tasks` 中供 QML 显示，重新下载时自动替换
  - **为什么不需要 QML 去重**：`getAllTasks()` 中每个文件最多一条任务，QML 不可能看到重复
- **难易程度**：高
- **完成状态**：完成
- **验证方式**：编译通过；用户确认以下场景：
  - 同一文件多次下载只显示一条任务
  - 下载失败后再次点击，任务恢复为下载中，不新增条目
  - 下载完成后再次下载，旧任务被替换，不新增条目
  - 下载进行中再次点击，不新增条目

## 3. 修复 sendStreamingFileResponse 返回值判断逻辑（区分 Range 请求）

- **修改内容**：
  - `RequestHandler.cpp` 的 `handleFileDownload` 中：
    - 非 Range 请求时：`if (sent > 0 && sent == fi.size())` 判断成功
    - Range 请求时：`if (sent > 0)` 即判断成功（Range 请求只发送部分内容，`sent` 不等于 `fi.size()`）
  - 判断方式：检查 `rangeHeader` 是否为空来区分
    ```cpp
    if (sent > 0 && (rangeHeader.isEmpty() ? sent == fi.size() : true))
    ```
  - `handleFolderDownload` 同样处理
  - 当前 `sent >= 0` 判断有两个问题：
    1. `file.read` 中途返回 0 时，`totalSent < fileSize` 但 `totalSent > 0`，会被误判为成功
    2. Range 请求时 `sent < fi.size()` 是正常的，不应判断为失败
- **难易程度**：低
- **完成状态**：完成
- **验证方式**：编译通过

## 4. 移动端分享页面增加 JavaScript 断点续传下载

- **修改内容**：
  - `RequestHandler.cpp` 的 `generateSharePage` 中，将文件下载按钮从 `<a href='/download/...'>` 改为带 JavaScript 的按钮，点击后执行断点续传下载逻辑
  - **会话内续传（100% 可靠）**：
    - 使用 `fetch` 发送 HEAD 请求获取文件大小（Content-Length + Accept-Ranges）
    - 使用 `fetch` + `Range: bytes=已下载字节数-` 请求剩余部分
    - 将数据追加到内存中的 `ArrayBuffer` 数组（`chunks.push(result.value)`）
    - 下载中断后重新点击，从 `chunks` 已有数据量位置继续
    - 下载完成后用 `new Blob(chunks)` + `URL.createObjectURL` + `<a download>` 保存文件
    - 显示下载进度条和速度
  - **跨会话续传**：当前实现为会话内续传，跨会话需要 IndexedDB 支持（未实现，大文件受浏览器内存限制）
  - **超大文件（>500MB）**：显示提示"文件较大，建议使用下载管理器"，同时提供"直接下载"回退选项
  - 文件夹下载仍使用原生 `<a href>` 方式（ZIP 打包下载不需要断点续传）
  - **技术限制说明**：
    - 移动端浏览器不支持 `showSaveFilePicker`（File System Access API），只能用 `<a download>` 触发浏览器原生保存
    - Blob 方式受浏览器内存限制，超过 500MB 的文件可能导致浏览器标签页崩溃
    - 对于超大文件，提供"直接下载"选项（回退到 `<a href>` 原生下载），虽然不支持断点续传但不会崩溃
  - **遗漏修复（对比检查发现）**：
    - `RequestHandler.cpp` 注册 HEAD 路由：`server->addRoute("HEAD", "/download/*", ...)`，否则 JS 的 `fetch(url, {method: 'HEAD'})` 会返回 404
    - `handleFileDownload` 添加 HEAD 请求处理：只返回头部（Content-Type、Content-Length、Accept-Ranges、CORS 头），不创建任务也不发送文件内容
    - `CivetWebServer.cpp` CORS OPTIONS 预检响应中 `Access-Control-Allow-Methods` 添加 `HEAD`
    - `CivetWebServer.cpp` 的 `sendStreamingFileResponse` 中 200 和 206 响应添加 CORS 头（`Access-Control-Allow-Origin: *` + `Access-Control-Expose-Headers`），否则移动端 JS 的 `fetch` 无法读取 Content-Length、Accept-Ranges、Content-Range 等响应头
- **难易程度**：高
- **完成状态**：完成
- **验证方式**：用户确认移动端下载文件中断后重新点击可从断点继续（会话内 100%）

## 5. 构建验证

- **修改内容**：编译整个项目，确保无编译错误
- **难易程度**：低
- **完成状态**：完成
- **验证方式**：cmake --build 成功，无错误

## 6. 端到端测试

- **修改内容**：无代码修改，全流程测试
- **难易程度**：低
- **完成状态**：进行中
- **验证方式**：用户确认以下场景：
  - 下载大文件时传输列表页显示速度
  - 同一文件多次下载只显示一条任务
  - 下载失败后再次点击，任务恢复为下载中，不新增条目
  - 下载完成后再次下载，旧任务被替换，不新增条目
  - 下载进行中再次点击，不新增条目
  - 移动端下载文件中断后可从断点继续（会话内）
  - 文件夹下载也正常显示
