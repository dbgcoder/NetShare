# 下载功能修复执行文档

## 0. 问题概述

### 主要问题
1. **下载中的文件不在传输列表页显示** — 下载走 HTTP 直接流式响应，没有在 FileTransferEngine 中创建任务
2. **下载未完成却显示为已完成** — recordCompletedTransfer 在 HTTP 响应后无条件标记 Completed
3. **下载没有速度显示** — 下载没有通过 FileTransferEngine 管理，无进度/速度数据
4. **下载没有分块** — performDownload 已有分块代码但从未被调用

### 解决方法
让下载通过 FileTransferEngine 管理：修改 sendStreamingFileResponse 增加进度回调和返回值，在 handleFileDownload/handleFolderDownload 中创建 TransferTask 并跟踪进度，下载完成标记 Completed，中断标记 Failed。

---

## 1. 修改 sendStreamingFileResponse 增加进度回调和返回值

- **修改内容**：
  - `CivetWebServer.h`：`sendStreamingFileResponse` 签名增加 `std::function<void(qint64 totalSent, qint64 fileSize)> progressCallback = nullptr` 参数，返回值从 `void` 改为 `qint64`（实际发送字节数，-1 表示失败）
  - `CivetWebServer.cpp`：检查 `mg_write` 返回值（`<= 0` 均视为失败，0 表示连接关闭，-1 表示错误），写入失败时返回 -1；每次写入成功后累计已发送字节数，调用 `progressCallback(startByte + totalSent, fileSize)`；`remaining -= written`（非 bytesRead，避免部分写入时多减）；循环结束后返回总发送字节数
  - 同步修改 `handleFileDownload` 和 `handleFolderDownload` 中的调用点，暂不传回调（传 nullptr）
- **难易程度**：中
- **完成状态**：完成
- **验证方式**：编译通过 ✅

## 2. FileTransferEngine 新增 addDownloadTask 方法

- **修改内容**：
  - `FileTransferEngine.h`：新增 `void addDownloadTask(const TransferTask& task)` 声明
  - `FileTransferEngine.cpp`：实现与 addUploadingTask 相同逻辑（m_tasks[taskId] = task; m_lastProgressTime 记录; emit taskStarted）
- **难易程度**：低
- **完成状态**：完成
- **验证方式**：编译通过 ✅

## 3. handleFileDownload 中创建下载任务并跟踪进度

- **修改内容**：
  - 在 `handleFileDownload` 中，发送文件前创建 TransferTask（type=Download, status=Downloading, fileName=文件名, fileSize=文件大小），调用 `m_transferEngine->addDownloadTask(task)`
  - 传入进度回调给 sendStreamingFileResponse，回调中调用 `m_transferEngine->updateTaskProgress(taskId, totalSent)`
  - 根据 sendStreamingFileResponse 返回值判断：返回值 >= 0 则 `m_transferEngine->completeTask(taskId)` + `recordCompletedTransfer(0, ...)`；返回值 < 0 则 `m_transferEngine->failTask(taskId, "Download interrupted")`
  - 删除末尾原有的 `recordCompletedTransfer` 调用
- **难易程度**：中
- **完成状态**：完成
- **验证方式**：编译通过 ✅，需用户确认桌面端传输列表页显示下载任务

## 4. handleFolderDownload 中创建下载任务

- **修改内容**：
  - 与步骤3类似，为文件夹下载创建 TransferTask（type=Download, fileName=zipName, fileSize=zip文件大小）
  - 传入进度回调，跟踪发送进度
  - 根据 sendStreamingFileResponse 返回值判断完成或失败
- **难易程度**：低
- **完成状态**：完成
- **验证方式**：编译通过 ✅，需用户确认文件夹下载也显示在传输列表中

## 5. 构建验证

- **修改内容**：编译整个项目，确保无编译错误
- **难易程度**：低
- **完成状态**：完成
- **验证方式**：cmake --build 成功，无错误 ✅

## 6. 端到端测试

- **修改内容**：无代码修改，全流程测试
- **难易程度**：低
- **完成状态**：进行中
- **验证方式**：用户确认以下场景：
  - 从手机端下载文件，桌面端传输列表显示下载任务
  - 下载中有进度和速度
  - 下载完成后状态变为"已完成"
  - 下载中断后状态变为"失败"
  - 文件夹下载也显示在传输列表中
