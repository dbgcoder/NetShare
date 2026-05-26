# 大文件上传卡死修复计划

**版本**: 2.0
**日期**: 2026-05-24
**问题**: 3GB+ 大文件上传卡死，无上传速度，无进度显示

---

## 问题定位

经过对完整上传链路（前端→HTTP服务器→流式处理器→分块缓存→合并）的逐行分析，
定位到 3 个可能导致"卡死、无速度、无进度"的问题，按严重程度排序。

---

## 问题 1（严重）：handleUploadCheck 扫描和复制旧 chunk 文件阻塞事件循环

### 位置
- `src/network/RequestHandler.cpp` 第 908-945 行

### 根因
`handleUploadCheck` 在 `/api/upload/check` 请求中同步执行以下操作：

1. 扫描 `.chunks` 目录下所有旧 session 目录（第 908-922 行）
2. 对每个旧 session 目录，检查每个 chunk 文件是否存在（440 次 `QFileInfo::exists()` + `QFileInfo::size()`）
3. 如果发现已完成的 chunk，**复制到新 session 目录**（第 944 行 `QFile::copy`，每个 8MB）
4. **更严重**：第 935 行对每个已完成的 chunk 都重新扫描 `.chunks` 目录，复杂度 O(chunks × sessions）

### 场景推演
- 用户首次上传 3GB 文件，部分 chunk 上传后失败
- `.chunks` 目录下留下旧 session 目录和部分 chunk 文件（如 200 个 × 8MB = 1.6GB）
- 用户再次上传，`/api/upload/check` 被调用
- `handleUploadCheck` 同步扫描旧 session → 复制 1.6GB 数据 → **阻塞事件循环几十秒**
- 期间所有 HTTP 请求无法处理 → 前端 XHR 等待响应 → 无速度、无进度 → 表现为"卡死"

### 违反的设计规则
- PLAN.md 第 3.2 节：分块策略要求支持 30GB+ 大文件传输
- 当前实现中，续传检查的 O(chunks × sessions) 复杂度在大文件场景下不可接受

### 修复方案

#### 方案 A：将 chunk 扫描和复制移到后台线程（推荐）
1. `handleUploadCheck` 立即返回 sessionId 和基本信息（不扫描旧 chunk）
2. 使用 `QtConcurrent::run` 在后台线程扫描旧 chunk 文件
3. 扫描完成后通过 WebSocket 通知前端更新 chunkInfo
4. 前端收到通知后，对已完成的 chunk 不再发送，直接跳过

#### 方案 B：优化扫描逻辑（降低复杂度）
1. 缓存 `.chunks` 目录扫描结果，避免重复扫描
2. 对每个旧 session 目录只扫描一次，将结果缓存
3. 复制 chunk 文件使用 `QFile::rename`（同磁盘可秒完成）
4. 如果新旧 session 在同一磁盘，优先使用 rename 代替 copy

#### 方案 C：清理旧 session 目录
1. 在 `handleUploadCheck` 开始前，先清理 `.chunks` 目录下的旧 session
2. 如果旧 session 超过一定时间（如 24 小时），直接删除
3. 避免积累大量旧 chunk 文件

### 执行步骤
1. 先实施方案 C（清理旧 session），作为快速修复
2. 再实施方案 B（优化扫描逻辑），降低复杂度
3. 最后实施方案 A（后台线程），彻底解决事件循环阻塞

---

## 问题 2（中等）：readyRead 中 m_requestBuffers 缓冲整个请求体

### 位置
- `src/network/HttpServer.cpp` 第 112 行 `buffer.append(socket->readAll())`
- `src/network/HttpServer.cpp` 第 164 行 `buffer.mid(bodyStart)` 深拷贝

### 根因
流式路由的 `readyRead` handler 中：
1. `socket->readAll()` 一次性读出所有可用数据（包括 8MB body）
2. `buffer.append()` 将其全部存入 `m_requestBuffers`
3. `buffer.mid(bodyStart)` 又深拷贝 8MB body

流式路由的初衷是边收边写磁盘，不缓存整个 body 到内存。
但当前实现在流式路由检测前，`socket->readAll()` 已经把所有数据读入了 `m_requestBuffers`。

### 内存影响
- 每个 8MB chunk 请求，内存峰值 16MB（buffer 8MB + initialChunk 8MB 副本）
- 3 并发请求 = 48MB 峰值
- 不会导致崩溃，但违背流式设计初衷

### 违反的设计规则
- PLAN.md 第 3.2 节：1GB-5GB 文件使用 8MB 分块，5 个线程
- 5 个并发 × 16MB = 80MB 内存峰值，对低端设备不友好

### 修复方案

#### 方案：Content-Length > 1MB 时自动走流式路由，<= 1MB 保持现有逻辑

**核心思路**：不再依赖路由注册类型决定是否流式处理，而是根据请求体大小动态决定。

**阈值选择依据**：
- PLAN.md 分块策略：最小分块为 2MB（< 100MB 文件），最大分块为 16MB（> 5GB 文件）
- 1MB 阈值覆盖所有分块场景：
  - 2MB/8MB/16MB chunk 的请求体均 > 1MB（触发流式）
  - JSON 请求（几KB）和小文件上传（<= 1MB）不受影响
- 1MB 内存缓存对任何设备都安全，不会造成内存压力

**判断规则**：
- Content-Length <= 1MB：使用现有非流式逻辑（`buffer.append` + `handleClientSocket`）
  - `/api/upload/check`、`/api/upload/finalize`、`/api/upload/abort` 等 JSON 请求（几KB）
  - 小文件上传（<= 1MB 的文件整体上传）
  - 这些请求体很小，全部缓存到内存没有问题
- Content-Length > 1MB：自动走流式路由
  - < 100MB 文件的 2MB chunk 上传（Content-Length ≈ 2MB，触发）
  - 1GB-5GB 文件的 8MB chunk 上传（Content-Length ≈ 8MB，触发）
  - > 5GB 文件的 16MB chunk 上传（Content-Length ≈ 16MB，触发）
  - 即使路由未注册为流式，也强制走流式处理
  - 避免大请求体全部缓存到内存

**实现细节**：

1. 修改 `readyRead` handler 中的路由检测逻辑：
   - 解析 Content-Length 后，判断是否 > 1MB
   - 如果 > 1MB，无论路由注册类型，都创建 `StreamingContext` 走流式路径
   - 如果 <= 1MB，使用现有逻辑（先检查 `matchStreamingRoute`，再检查非流式路由）

2. 流式路由检测顺序调整：
   ```
   if (Content-Length > 1MB) {
       // 强制流式：创建 StreamingContext，body 数据边收边传给 handler
       // handler 需要能处理流式调用（isLast=false 时累积数据，isLast=true 时处理）
   } else if (matchStreamingRoute(method, path)) {
       // 注册的流式路由（如 /api/upload/file），保持现有逻辑
   } else {
       // 非流式路由，缓存完整请求体后处理
   }
   ```

3. 对于 Content-Length > 1MB 但路由未注册为流式的情况：
   - 返回 413 Payload Too Large，拒绝请求
   - 强制开发者将大请求体路由注册为流式路由
   - 不实现通用流式 handler（避免遗漏 6 的 OOM 风险）

4. 当前路由分析：
   - `POST /api/upload/file`：已注册为流式路由，8MB chunk → Content-Length ≈ 8MB（> 1MB，触发流式）✓
   - `POST /receive`：已注册为流式路由 ✓
   - `POST /upload/*`：已注册为流式路由 ✓
   - `POST /api/upload/check`：JSON 请求，几KB → 不触发 ✓
   - `POST /api/upload/finalize`：JSON 请求，几KB → 不触发 ✓
   - `POST /api/upload/abort`：JSON 请求，几KB → 不触发 ✓

   **结论**：1MB 阈值对当前场景有效，所有 chunk 请求（2MB/8MB/16MB）都会触发流式路由。
   未来如果添加新的上传路由但忘记注册为流式，1MB 阈值会返回 413 错误，提醒开发者。

### 执行步骤
1. 在 `readyRead` handler 中，解析 Content-Length 后添加阈值判断
2. Content-Length > 1MB 且路由已注册为流式 → 走流式路径（现有逻辑）
3. Content-Length > 1MB 但路由未注册为流式 → 返回 413 Payload Too Large
4. Content-Length <= 1MB → 保持现有逻辑不变
5. 添加日志，记录流式/非流式路由的选择

---

## 问题 3（轻微）：前端 xhrProgressActive 永不重置为 false

### 位置
- `web/receive.html` 第 659 行 `xhrProgressActive = true`
- `web/receive.html` 第 722 行 `xhrProgressActive = true`

### 根因
`xhrProgressActive` 在 `xhr.upload.onprogress` 中设为 `true`，
但**在上传过程中从未被重置为 `false`**。

**注意**：`startUpload()` 函数开头（第 507 行）和重试失败文件时（第 772 行）有重置，
但在 chunk 上传过程中（chunk 之间），`xhrProgressActive` 一直为 true。
一旦设为 true，WebSocket 进度更新永远被忽略（第 307 行 `!xhrProgressActive` 条件）。

### 影响
- 如果 XHR progress 事件不触发（如服务器不响应），WebSocket 进度也无法显示
- 用户看不到任何进度更新，表现为"无进度"

### 违反的设计规则
- PLAN.md 第 6.5 节：上传 API 应支持进度查询
- WebSocket 进度更新作为 XHR progress 的补充，不应被永久禁用

### 修复方案

#### 方案：在每个 chunk 上传完成后重置 xhrProgressActive
1. 在 `xhr.onload` 和 `xhr.onerror` 回调中，将 `xhrProgressActive` 重置为 `false`
2. 这样在 chunk 之间的间隙，WebSocket 进度可以生效
3. 如果 XHR progress 正常触发，WebSocket 进度被忽略（避免冲突）

### 执行步骤
1. 在 `xhr.onload` 回调的 `doNext()` 调用前，添加 `xhrProgressActive = false`
2. 在 `xhr.onerror` 回调中同样处理
3. 确保在 chunk 切换的间隙，WebSocket 进度可以更新 UI

---

## 执行优先级

| 优先级 | 问题 | 预期效果 | 风险 |
|--------|------|---------|------|
| P0 | 问题 1：清理旧 session + 优化扫描 | 解决事件循环阻塞，消除卡死 | 低 |
| P1 | 问题 2：流式路由只读 header | 降低内存峰值，符合流式设计 | 中（需仔细测试） |
| P2 | 问题 3：重置 xhrProgressActive | 修复进度显示 | 低 |

---

## 详细执行步骤

### 第一步：问题 1 快速修复 — 清理旧 session 目录

**修改文件**: `src/network/RequestHandler.cpp`

1. 在 `handleUploadCheck` 开头，添加清理旧 session 目录的逻辑：
   - 扫描 `.chunks` 目录下的所有子目录
   - 对每个子目录，检查其创建时间
   - 如果超过 24 小时，删除整个子目录（`QDir::removeRecursively`）
   - 如果当前没有活跃的上传 session 使用该目录，也可以删除

2. 添加超时清理逻辑：
   - 在 `UploadSession` 中记录 `createdAt` 时间
   - 如果 session 超过 1 小时没有活动，自动清理
   - 清理时删除 session 对应的 chunk 目录和 `m_uploadSessions` 中的条目

### 第二步：问题 1 根本修复 — 优化扫描逻辑

**修改文件**: `src/network/RequestHandler.cpp`

1. 将 `.chunks` 目录扫描结果缓存：
   - 只扫描一次，将结果存入 `QMap<QString, QSet<int>>`（文件路径 → 已完成的 chunk 索引集合）
   - 避免对每个 chunk 都重新扫描 `.chunks` 目录

2. 使用 `QFile::rename` 代替 `QFile::copy`：
   - 如果新旧 session 在同一磁盘分区，rename 是原子操作，瞬间完成
   - 如果不在同一分区，回退到 copy

3. 限制扫描的旧 session 数量：
   - 只扫描最近 N 个旧 session 目录（按修改时间排序）
   - 避免扫描大量历史 session

### 第三步：问题 1 彻底修复 — 后台线程扫描

**修改文件**: `src/network/RequestHandler.cpp`

1. `handleUploadCheck` 立即返回 sessionId 和基本信息
2. 使用 `QtConcurrent::run` 在后台线程执行 chunk 扫描
3. 扫描完成后，通过 WebSocket 通知前端更新 chunkInfo
4. 前端收到通知后，对已完成的 chunk 不再发送

### 第四步：问题 2 修复 — Content-Length > 1MB 自动走流式路由

**修改文件**: `src/network/HttpServer.cpp`

1. 在 `readyRead` handler 中，解析 Content-Length 后添加阈值判断：
   ```cpp
   static const qint64 STREAMING_THRESHOLD = 1 * 1024 * 1024; // 1MB

   // 在 isStreamingRoute 判断之前，先检查 Content-Length
   bool forceStreaming = (expectedSize > STREAMING_THRESHOLD);
   ```

2. 修改流式路由检测逻辑：
   ```cpp
   StreamingBodyHandler streamingHandler;
   bool isStreamingRoute = matchStreamingRoute(method, path, streamingHandler);

   if (forceStreaming) {
       if (!isStreamingRoute) {
           // Content-Length > 1MB 但路由未注册为流式 → 拒绝请求
           HttpResponse response = HttpResponse::status(413, "Payload Too Large");
           sendResponse(socket, response);
           m_requestBuffers.remove(socket);
           m_expectedBodySize.remove(socket);
           return;
       }
       // 路由已注册为流式 → 走流式路径（现有逻辑）
   }
   ```

3. Content-Length <= 1MB 时，保持现有逻辑不变

4. 添加日志：
   ```cpp
   if (forceStreaming) {
       LOG_INFO("Streaming route for large body: %s %s Content-Length=%lld",
                qPrintable(method), qPrintable(path), expectedSize);
   }
   ```

### 第五步：问题 3 修复 — 重置 xhrProgressActive

**修改文件**: `web/receive.html`

1. 在 `xhr.onload` 回调中，`doNext()` 调用前添加 `xhrProgressActive = false`
2. 在 `xhr.onerror` 回调中同样处理
3. 在 `uploadSmall` 函数的 XHR 回调中也同样处理

---

## 验证方案

### 功能验证
1. 首次上传 3GB 文件：确认上传正常，有速度和进度显示
2. 中断后重新上传：确认续传正常，旧 chunk 被正确识别和复用
3. 多次中断后重新上传：确认旧 session 被清理，不会阻塞事件循环
4. 并发上传：确认 3 个并发 chunk 上传正常，内存峰值在合理范围

### 性能验证
1. `handleUploadCheck` 响应时间 < 1 秒（无旧 session）
2. `handleUploadCheck` 响应时间 < 3 秒（有旧 session，200 个 chunk）
3. 上传过程中内存峰值 < 100MB
4. 上传速度接近网络带宽上限

### 边界验证
1. 上传 30GB 文件：确认分块策略正确（16MB chunk），上传不卡死
2. 磁盘空间不足：确认错误提示正确，不崩溃
3. 网络断开重连：确认续传正常
4. 多个浏览器同时上传：确认并发处理正常

---

## 冲突检查

### 冲突 1：方案 B 中 `QFile::rename` 与多 session 共享冲突

**位置**：问题 1 方案 B 第 3 条"使用 `QFile::rename` 代替 `QFile::copy`"

**冲突描述**：
- `QFile::rename` 是移动操作，会从旧 session 目录中移走 chunk 文件
- 如果两个上传 session 同时需要同一个旧 chunk 文件，第一个 session rename 后，第二个 session 找不到文件
- 虽然当前场景中每次只有一个新 session，但设计上应支持并发上传

**解决方案**：
- 保留 `QFile::copy`，不使用 `QFile::rename`
- 或者：先 copy 到新 session，上传全部完成后统一清理旧 session 目录
- rename 只在确认旧 session 不再被使用时才执行（如旧 session 已过期）

### 冲突 2：方案 A 后台线程扫描与前端时序冲突

**位置**：问题 1 方案 A"后台线程扫描旧 chunk 文件"

**冲突描述**：
- `handleUploadCheck` 立即返回 sessionId，前端开始上传 chunk
- 后台线程扫描旧 chunk 完成后，通过 WebSocket 通知前端跳过已完成的 chunk
- 但前端可能已经开始上传这些 chunk 了（因为不知道它们已完成）
- 导致：已完成的 chunk 被重复上传，浪费带宽

**解决方案**：
- 前端在收到 WebSocket 通知后，取消正在上传的已完成 chunk 的 XHR 请求
- 或者：前端在开始上传前等待一小段时间（如 500ms），给后台线程扫描的时间
- 或者：`handleUploadCheck` 同步执行快速扫描（只检查 `.chunks` 目录是否存在，不检查每个 chunk），如果存在旧 session，在响应中标记 `needsChunkScan: true`，前端先上传未确认的 chunk，后台扫描完成后通知

### 冲突 3：~~问题 2 流式路由只读 header 与非流式路由的 buffer 管理冲突~~ 已消除

**原冲突描述**（已通过方案调整消除）：
- 原方案"在流式路由检测前只读 header"会影响非流式路由的 buffer 管理
- 非流式路由需要完整请求体，修改读取逻辑可能影响非流式路由

**消除方式**：
- 新方案改为"Content-Length > 1MB 时自动走流式路由，<= 1MB 保持现有逻辑"
- 小请求（JSON、小文件）仍使用 `buffer.append(socket->readAll())` 缓存完整请求体
- 只有超过 1MB 的大请求才走流式路径
- 非流式路由完全不受影响，冲突消除

---

## 遗漏检查

### 遗漏 1（严重）：`m_transferEngine` 为 null 时上传完全不可用

**位置**：`src/network/RequestHandler.cpp` 第 892 行 `if (m_transferEngine)`

**遗漏描述**：
- `handleUploadCheck` 中，session 创建逻辑（包括 `result["sessionId"]`）全部在 `if (m_transferEngine)` 块内
- 如果 `m_transferEngine` 为 null，`result["sessionId"]` 不会被设置
- 前端 `uploadSessionId` 为空字符串
- 所有 chunk 上传请求被 `sessionId.isEmpty()` 拦截，返回 "Invalid or expired upload session"
- **上传完全不可用，但不会报错提示**

**当前状态**：
- 正常运行时 `m_transferEngine` 不为 null（`FileTransferEngine::initialize()` 失败时应用直接退出）
- 但这是一个脆弱的设计——如果 service locator 出问题，上传会静默失败

**修复方案**：
- 在 `if (m_transferEngine)` 块外也设置 `result["sessionId"]`
- 或者：如果 `m_transferEngine` 为 null，返回 500 错误，明确告知前端上传不可用
- 前端收到错误后，显示明确的错误提示

### 遗漏 2（中等）：首次上传时问题 1 不会触发

**位置**：问题 1 的场景推演

**遗漏描述**：
- 首次上传时，`.chunks` 目录不存在，`searchDirObj.exists()` 返回 false
- 整个扫描逻辑被跳过，`handleUploadCheck` 不会阻塞
- **如果用户说"发送文件就卡死"是首次上传，问题 1 不是根因**
- 问题 1 只在"之前有失败的上传"时才会触发

**影响**：
- 需要区分"首次上传卡死"和"续传卡死"两种场景
- 如果是首次上传卡死，需要找其他原因

**修正方案**：
- 在问题 1 的场景推演中明确说明：此问题只在续传时触发
- 首次上传卡死需要检查其他原因（如问题 2、问题 3 或其他未发现的问题）

### 遗漏 3（轻微）：前端 `uploadedSize` 续传时对最后一个 chunk 计算错误

**位置**：`web/receive.html` 第 554 行

**遗漏描述**：
- `uploadedSize += (f.completedChunks || []).length * f.chunkSize`
- 最后一个 chunk 的大小可能小于 chunkSize（如 3GB 文件最后一个 chunk 只有 4MB）
- 如果最后一个 chunk 已完成，这里用 `chunkSize`（8MB）计算，多算了 4MB
- 导致进度显示偏大

**服务端已计算精确值但未传递**：
- `RequestHandler.cpp` 第 961 行已计算 `completedBytes`（精确累加每个 chunk 的实际 size）
- 第 975 行 `partialSize += completedBytes` 使用了精确值
- 但第 973 行 `pa["completedChunks"] = completedArr` 只传了 chunk 索引数组，**没有传 `completedBytes`**

**影响**：
- 不会导致卡死，但进度显示不准确
- 进度可能超过实际值

**修复方案**：
- 服务端在 `partial` 响应中添加 `completedBytes` 字段：`pa["completedBytes"] = completedBytes`
- 前端使用 `f.completedBytes` 代替 `(f.completedChunks || []).length * f.chunkSize`

### 遗漏 4（轻微）：`QFile::write` 返回值未检查

**位置**：`src/network/RequestHandler.cpp` 第 1172 行 `state->chunkFile->write(chunk)`

**遗漏描述**：
- `QFile::write()` 返回写入的字节数，如果磁盘空间不足，返回 -1
- 代码没有检查返回值，继续累加 `state->bytesReceived`
- 最终 `state->bytesReceived != state->expectedSize`，验证失败
- 但 chunk 文件可能不完整或为空，浪费了上传带宽

**影响**：
- 不会导致卡死，但磁盘空间不足时上传会静默失败
- 用户需要重新上传

**修复方案**：
- 检查 `QFile::write()` 返回值
- 如果写入失败，立即返回错误，避免继续上传

### 遗漏 5（中等）：`xhr.upload.onprogress` 进度计算对最后一个 chunk 不准确

**位置**：`web/receive.html` 第 660 行

**遗漏描述**：
- `var cb = info.completedChunks.size * chunkSize + e.loaded`
- 最后一个 chunk 的 `chunkSize` 可能小于 `info.chunkSize`
- `info.completedChunks.size * chunkSize` 可能多算
- 虽然有 `Math.min(100, ...)` 限制，但进度文字可能显示错误的大小

**影响**：
- 不会导致卡死，但进度显示不准确

**修复方案**：
- 使用 `uploadedSize + e.loaded` 代替 `info.completedChunks.size * chunkSize + e.loaded`
- `uploadedSize` 在 `xhr.onload` 中精确累加 `blob.size`，更准确

### 遗漏 6（~~中等~~ 已消除）：~~通用流式 handler 累积大请求体仍可能 OOM~~

**原遗漏描述**（已通过方案调整消除）：
- 原方案实现通用流式 handler，在 `isLast=false` 时将 chunk 累积到 `accumulatedBody`
- 如果 Content-Length 很大（如 100MB），`accumulatedBody` 会增长到 100MB，可能 OOM

**消除方式**：
- 新方案改为"Content-Length > 1MB 但路由未注册为流式时，返回 413 Payload Too Large"
- 不实现通用流式 handler，避免 OOM 风险
- 强制开发者将大请求体路由注册为流式路由

### 遗漏 7（~~轻微~~ 已消除）：~~8MB chunk 请求不触发阈值~~

**原遗漏描述**（已通过阈值调整消除）：
- 原方案 10MB 阈值对 8MB chunk 请求无效（8MB < 10MB）
- `m_requestBuffers` 仍缓冲整个 8MB 请求体

**消除方式**：
- 阈值从 10MB 降低到 1MB
- 2MB chunk 请求的 Content-Length ≈ 2MB > 1MB，触发流式路由 ✓
- 8MB chunk 请求的 Content-Length ≈ 8MB > 1MB，触发流式路由 ✓
- 16MB chunk 请求的 Content-Length ≈ 16MB > 1MB，触发流式路由 ✓
- JSON 请求的 Content-Length 几KB < 1MB，不触发（<= 1MB 走非流式，缓存安全）✓

---

## 修订后的执行优先级

| 优先级 | 问题 | 类型 | 预期效果 | 风险 |
|--------|------|------|---------|------|
| P0 | 遗漏 1：m_transferEngine null 防护 | 遗漏 | 防止上传静默失败 | 低 |
| P0 | 问题 1：清理旧 session + 优化扫描 | 原问题 | 解决续传时事件循环阻塞 | 低 |
| P1 | 遗漏 2：区分首次/续传卡死场景 | 遗漏 | 准确定位首次上传卡死原因 | 无 |
| P1 | 问题 2：Content-Length > 1MB 自动流式 | 原问题 | 所有 chunk 请求走流式路由，降低内存峰值 | 低 |
| P2 | 问题 3：重置 xhrProgressActive | 原问题 | 修复进度显示 | 低 |
| P2 | 遗漏 3：uploadedSize 续传计算 | 遗漏 | 修复进度显示准确性 | 低 |
| P2 | 遗漏 5：onprogress 进度计算 | 遗漏 | 修复进度显示准确性 | 低 |
| P3 | 遗漏 4：QFile::write 返回值检查 | 遗漏 | 防止磁盘空间不足时静默失败 | 低 |
| ~~P1~~ | ~~遗漏 6：通用流式 handler OOM~~ | ~~已消除~~ | 不实现通用 handler，返回 413 | - |
| ~~P1~~ | ~~遗漏 7：8MB chunk 不触发阈值~~ | ~~已消除~~ | 阈值降至 1MB，所有 chunk > 1MB 触发 | - |
