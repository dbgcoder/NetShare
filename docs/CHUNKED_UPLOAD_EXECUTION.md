# 分块上传执行文档

## 规则文件对照 (breakpoint-resume-progress.mdr)

| 规则要求 | 新方案对应 | 实现方式 |
|----------|-----------|----------|
| 续传优先 | 保留chunk文件供续传 | .chunks目录保留，/api/upload/check检测completedChunks |
| 双源进度 | XHR chunk onprogress + WS transfer_update | 每个chunk的XHR提供精确进度，WS广播总进度 |
| 确定性路径 | chunk合并到原始路径 | mergeChunks直接到最终路径，不重命名 |
| X-Resume-Offset/X-Resume-Path | 改为X-Chunk-Index + X-File-Path | 分块偏移由index隐含，无需手动偏移 |
| 客户端重试重新check | retryFailed()重调/api/upload/check | 获取新session+completedChunks |
| beforeunload不abort | 保持不变 | 页面关闭保留chunk文件 |
| WS端口+1，5秒重连 | 保持不变 | 现有connectWebSocket逻辑不变 |

## 实施步骤

### Step 1: RequestHandler.h 数据结构
- 新增 ChunkUploadInfo, FileChunkState 结构体
- 扩展 UploadSession 增加 fileChunkStates, chunkTempDir
- 移除 m_singleFileStates
- 修改 handleUploadSingleFile 签名为 regular handler

### Step 2: handleUploadCheck 增加分块计算
- 用 ChunkManager::calculateChunkSize 计算每个文件的 chunkSize
- 用 ChunkManager::splitFile 生成分块列表
- 检测 .chunks 目录中已完成的 chunk
- 响应增加 chunkSize, chunkCount, completedChunks, useChunking

### Step 3: handleUploadSingleFile 改为 regular handler + 双模式
- 注册从 addStreamingRoute 改为 addRoute
- 分块模式: X-Chunk-Index 头存在 → writeChunk
- 小文件模式: 无 X-Chunk-Index → multipart 直接写

### Step 4: handleUploadFinalize 增加合并
- 检查所有文件分块完成
- mergeChunks → verifyMergedFile → cleanupChunks
- 合并失败保留分块

### Step 5: handleUploadAbort + cleanupExpiredSessions
- 清理 .chunks 目录

### Step 6: 移除 m_singleFileStates 相关代码
- 析构函数、socket disconnect handler、registerRoutes

### Step 7-9: 三个客户端页面改为分块上传
- upload.html (主页面)
- generateUploadPage (C++内联)
- generateReceivePage (C++内联)
