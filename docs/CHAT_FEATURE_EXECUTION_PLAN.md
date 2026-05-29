# 局域网消息功能执行计划

## 0. 问题概述

### 需求描述
1. **标题栏添加消息图标**：在顶部标题栏齿轮图标（⚙）前添加类似微信的消息图标（💬），点击后进入消息页面；消息页的布局和交互风格类似现有的设置页（SettingsPage）
2. **取消左侧"接收管理"导航**：从左侧导航栏中移除"接收管理"项，消息功能取代其位置
3. **消息页功能**：左侧显示局域网所有移动端和电脑端用户列表，点击某个用户后右侧出现聊天区域（输入框 + 发送按钮），输入文字后点击发送即可将消息发送到选中的局域网用户
4. **移动端浏览器消息发送**：当移动端用户扫描"接收二维码"打开 `receive.html` 页面时，页面底部新增消息输入区域（输入框 + 发送按钮），输入文字后点击"开始发送"即可将消息发送到电脑端（即当前 NetShare 服务器所在设备）

### 技术方案概要
- **前端**：新增 `MessagePage.qml`，采用左右分栏布局（左侧用户列表 + 右侧聊天区域），风格对齐现有 SettingsPage
- **导航**：修改 `Main.qml` 标题栏增加消息图标按钮，左侧导航移除"接收管理"、新增"消息"项
- **后端通信**：新增 `ChatService` C++ 类，基于现有 `CivetWebServer` 的 WebSocket 能力实现点对点消息收发；利用现有 `mDNSService` 发现局域网用户
- **协议**：复用现有 `/ws` WebSocket 端点，扩展消息类型（`chat_message`、`chat_ack`、`user_info`）
- **移动端 Web**：修改 `receive.html`，在现有上传功能下方增加消息输入区域，通过 HTTP POST `/api/chat/message` 发送消息到电脑端

---

## 1. 新增 ChatService C++ 后端类

- **修改内容**：
  - 新建 `src/core/chat/ChatService.h` 和 `src/core/chat/ChatService.cpp`
  - `ChatService` 继承 `QObject`，负责：
    - 维护局域网用户列表（从 `mDNSService` 获取发现的服务，解析设备名、IP、端口、设备类型）
    - 维护聊天记录（内存中 `QMap<QString, QList<ChatMessage>>`，按用户 IP 为 key）
    - 发送消息：通过 HTTP POST 到目标设备的 `/api/chat/message` 端点
    - 接收消息：通过 CivetWebServer 新增 `/api/chat/message` 路由处理收到的消息
    - 消息通知：收到新消息时 emit `messageReceived` 信号，QML 绑定刷新
    - 未读计数：维护每个用户的未读消息数，emit `unreadCountChanged` 信号供标题栏图标显示红点
  - `ChatMessage` 结构体：`QString msgId`、`QString fromUser`、`QString toUser`、`QString content`、`QDateTime timestamp`、`bool isSent`、`bool sendFailed`
  - 注册为 QML 上下文：在 `main.cpp` 中 `setContextProperty("chatService", m_chatService)`
  - 修改 `src/core/CMakeLists.txt`：新增 `chat/ChatService.h` 和 `chat/ChatService.cpp` 到构建列表
  - **遗漏修复（对比检查发现）**：
    - `ChatService` 构造函数还需接收 `SettingsManager*` 以读取本地设备名和本机 IP
    - `ChatService` 需要持有 `CivetWebServer*` 指针，以便通过 WebSocket 推送
    - `ChatMessage` 结构体需增加 `QString msgId` 字段（UUID），用于消息去重
    - `onMessageReceived` 方法签名必须从一开始就包含 `const QString& remoteAddress` 参数
- **难易程度**：高
- **完成状态**：完成
- **验证方式**：编译通过；在 QML 中能访问 `chatService` 上下文属性且不报 undefined

## 2. 新增 HTTP 聊天消息 API 路由

- **修改内容**：
  - 修改 `src/network/RequestHandler.h`：新增 `handleChatMessage` 方法声明和 `setChatService(ChatService*)` 方法
  - 修改 `src/network/RequestHandler.cpp`：
    - 实现 `handleChatMessage`：解析 POST JSON body，调用 `ChatService::onMessageReceived` 处理
    - 在 `registerRoutes` 中注册 `POST /api/chat/message` 路由
  - 修改 `src/main.cpp`：创建 `ChatService` 后调用 `requestHandler->setChatService(m_chatService)`
  - **遗漏修复（对比检查发现）**：
    - `RequestHandler` 需要前向声明 `class ChatService` 或 `#include` 对应头文件
    - `handleChatMessage` 必须从 `info.remoteAddress` 提取发送者 IP 并传给 `ChatService::onMessageReceived`
    - `info.remoteAddress` 格式为 `IP:PORT`，需提取纯 IP 部分
    - 返回值需统一使用 `CivetWebServer::sendJsonResponse` 返回 JSON 格式响应
- **难易程度**：中
- **完成状态**：完成
- **验证方式**：编译通过；用 curl 向本机 POST `/api/chat/message` 能收到 200 JSON 响应

## 3. ChatService 与 mDNSService 联动——用户列表同步

- **修改内容**：
  - 修改 `src/core/chat/ChatService.cpp`：
    - 构造函数接收 `mDNSService*` 指针
    - 连接 `mDNSService::serviceDiscovered` 信号 → `ChatService::onUserDiscovered` 槽
    - 连接 `mDNSService::serviceLost` 信号 → `ChatService::onUserLost` 槽
    - 提供 `Q_INVOKABLE QVariantList getUserList()` 供 QML 调用
    - 提供 `Q_INVOKABLE QVariantList getChatHistory(const QString& userIp)` 供 QML 调用
    - 提供 `Q_INVOKABLE void sendMessage(const QString& toIp, int toPort, const QString& content)` 供 QML 调用
  - 修改 `src/main.cpp`：`ChatService` 构造时传入 `m_mdnsService`
  - **遗漏修复（对比检查发现）**：
    - 用户列表需包含"匿名移动端"（来自 HTTP 请求的 remoteAddress）
    - `getUserList()` 返回的 `QVariantList` 中每个元素需包含 `isOnline`、`deviceType`、`lastMessage` 字段
- **难易程度**：中
- **完成状态**：完成
- **验证方式**：编译通过；QML 中调用 `chatService.getUserList()` 能返回局域网设备列表

## 4. 新增 MessagePage.qml 消息页面

- **修改内容**：
  - 新建 `src/gui/qml/MessagePage.qml`
  - 布局采用左右分栏：
    - **左侧面板**（宽度约 260px）：用户列表 ListView，显示设备图标、设备名、最后消息摘要、未读数红点
    - **右侧面板**（填充剩余空间）：聊天记录 ListView + 输入框 + 发送按钮
  - 使用 `Timer` 定时刷新用户列表（3 秒间隔）
  - 匿名移动端禁用发送按钮，显示提示"该用户为移动端，无法接收消息"
  - 颜色、字体、间距全部使用 `Theme.*` 属性
  - **遗漏修复（对比检查发现）**：
    - 使用 `Connections` 监听 `chatService.userListChanged` 和 `chatService.messageReceived` 信号刷新
    - 聊天记录 `ListView` 在新消息到来时自动滚动到底部
    - `sendMessage()` 调用后清空输入框并乐观更新聊天记录
- **难易程度**：高
- **完成状态**：完成
- **验证方式**：编译通过；QML 页面能正常渲染，无 binding loop 或 reference error

## 5+6. 修改 Main.qml——标题栏消息图标 + 导航栏修改（合并执行）

- **修改内容**：
  - 在标题栏 `settingsBtn` 左侧新增 `messageBtn`（💬 图标 + 未读红点）
  - 修改 `dragArea` 的 `anchors.rightMargin` 包含 `messageBtn.width`
  - 修改 `menuList` 的 `ListModel`：将"接收管理"替换为"消息"
  - 修改 `pageStack` 数组：将 `receiveManagementPage` 替换为 `messagePage`
  - 修改 `stackView.initialItem`：从 `receiveManagementPage` 改为 `messagePage`
  - 修改 `settingsPage` 的 `onCloseSettings`：返回消息页
  - **冲突处理**：步骤 5 和步骤 6 必须合并为一步执行，否则中间状态编译失败
  - **遗漏修复（对比检查发现）**：
    - `totalUnreadCount` 必须声明为 Q_PROPERTY（`Q_PROPERTY(int totalUnreadCount READ totalUnreadCount NOTIFY unreadCountChanged)`）
    - `ReceiveManagement.qml` 文件不应删除
- **难易程度**：中
- **完成状态**：完成
- **验证方式**：编译通过；标题栏显示消息图标；点击消息图标能切换到消息页；有未读消息时显示红点

## 7. 修改 CMake 构建配置

- **修改内容**：
  - 修改 `src/gui/CMakeLists.txt`：添加 `qml/MessagePage.qml` 到 QML_FILES
  - 修改 `src/core/CMakeLists.txt`：添加 chat/ChatService 源文件和 include 路径
  - 修改 `src/main.cpp`：初始化 ChatService、注册上下文属性、设置 RequestHandler
  - **遗漏修复（对比检查发现）**：
    - `m_chatService` 必须在 `initializeNetwork()` 中创建（`m_mdnsService` 之后）
    - 需添加 `${CMAKE_CURRENT_SOURCE_DIR}/../network` 到 include_directories
- **难易程度**：低
- **完成状态**：完成
- **验证方式**：编译通过；运行后 QML 控制台无 "chatService undefined" 错误

## 8. ChatService 消息发送实现——HTTP POST

- **修改内容**：
  - 使用 `QNetworkAccessManager` 发送 HTTP POST 请求到目标设备的 `/api/chat/message`
  - 请求 body 为 JSON：`{ "from", "fromIp", "content", "timestamp" }`
  - 发送成功后追加到本地聊天记录（`isSent = true`），emit `messageSent` 信号
  - 发送失败后 emit `messageSendFailed` 信号
  - **遗漏修复（对比检查发现）**：
    - `QNetworkAccessManager` 应为成员变量，在构造函数中创建
    - HTTP POST 超时需设置（5 秒）
    - `sendMessage` 为异步操作，不能阻塞 UI 线程
- **难易程度**：中
- **完成状态**：完成
- **验证方式**：编译通过；两个 NetShare 实例间能互发消息

## 9. 消息页实时刷新——WebSocket 推送新消息

- **修改内容**：
  - 修改 `src/main.cpp` 的 WebSocket `onData` 回调：支持顶层和嵌套 token 格式的订阅
  - 修改 `src/core/chat/ChatService.cpp`：收到消息后通过 WebSocket 广播 `chat_message` 类型消息
  - 修改 `src/gui/qml/MessagePage.qml`：连接 `chatService.messageReceived` 信号刷新聊天记录
  - **遗漏修复（对比检查发现）**：
    - WebSocket 订阅格式需同时支持 `{"type":"subscribe","token":"xxx"}` 和 `{"type":"subscribe","data":{"token":"xxx"}}`
    - `totalUnreadCount` 必须声明为 Q_PROPERTY
- **难易程度**：中
- **完成状态**：完成
- **验证方式**：编译通过；两台设备间发送消息后，接收方消息页自动刷新

## 10. 集成测试与 UI 微调

- **修改内容**：
  - 代码层面集成检查：验证数据流、边界条件、UI 细节
  - UI 微调与优化：
    - 用户列表排序：有未读消息的排最前，在线的次之，离线的最后
    - 性能优化：`getUserList()` 直接返回 `lastMessage` 字段，避免 QML 端对每个用户调用 `getChatHistory`
    - WebSocket 订阅格式兼容：支持顶层和嵌套 token 格式
  - 步骤 10 执行中发现并修复的问题：
    1. MessagePage.qml 未加入 CMakeLists.txt → 已修复
    2. m_localIp 未在 ChatService 中初始化 → 已修复（从 ShareManager::instance().localIp() 获取）
    3. WebSocket 订阅格式不匹配 → 已修复（main.cpp 同时支持两种格式）
    4. receive.html 的 from 字段应发送空字符串以触发匿名用户命名逻辑 → 已修复
    5. 匿名用户设备类型错误识别为 "browser" → 已修复（统一为 "mobile"）
    6. CMake include 路径缺少 network 目录 → 已修复
  - 深度复检中发现并修复的问题：
    7. receive.html 消息重复显示 → 已修复（添加 `isMsgDuplicate` 去重函数，基于 content+timestamp 在 5 秒内匹配则跳过）
    8. sendMessage 对匿名用户走 HTTP POST 会失败 → 已修复（匿名用户或 port<=0 时走 WebSocket 广播路径）
    9. MessagePage.qml 匿名用户禁用发送按钮 → 已修复（启用发送，匿名用户通过 WebSocket 接收消息）
- **难易程度**：中
- **完成状态**：完成
- **验证方式**：编译通过；所有修复项已验证

## 11. 移动端 HTML 消息发送功能

- **修改内容**：
  - 修改 `web/receive.html`：新增消息输入区域（输入框 + "开始发送"按钮 + 聊天气泡历史）
  - 通过 HTTP POST `/api/chat/message` 发送消息
  - 通过 WebSocket 接收电脑端回复的消息
  - WebSocket 连接后订阅 "chat" 频道
  - 发送消息时 `from` 字段留空，让服务端根据 `remoteAddress` 自动生成匿名用户名
- **难易程度**：中
- **完成状态**：完成
- **验证方式**：移动端浏览器打开 receive.html，输入消息点击发送，电脑端消息页显示该消息

## 12. 匿名移动端用户识别与显示

- **修改内容**：
  - `onMessageReceived` 中从 `remoteAddress` 提取纯 IP
  - 若 IP 不在 mDNS 发现列表中，自动创建匿名用户条目
  - 匿名用户显示名为"移动端-IP后三位"，设备类型标记为 "mobile"
  - 维护独立的 `m_anonymousUsers` 映射表
  - 匿名用户支持通过 WebSocket 接收消息（电脑端可向移动端发送消息）
- **难易程度**：中
- **完成状态**：完成
- **验证方式**：移动端发送消息后，电脑端用户列表显示"移动端-xxx"条目；电脑端可向移动端发送消息

## 13. WebSocket 定向推送——电脑端向移动端回复消息

- **修改内容**：
  - `CivetWebServer` 新增 `sendToIp(ip, type, data)` 方法：按 IP 查找 WebSocket 连接，定向发送消息
  - `CivetWebServer` 新增 `m_connToIp` 映射：`mg_connection*` → 纯 IP 地址
  - WebSocket 连接建立时（`staticWsReadyHandler`）自动记录 conn→IP 映射
  - WebSocket 断开时（`unsubscribeClientFromAll`）自动清理映射
  - `ChatService::sendMessage` 对匿名用户调用 `m_civetServer->sendToIp(toAddress, "chat_message", data)` 定向推送
  - `receive.html` 移除 `isMsgDuplicate` 去重调用（定向推送不会广播回自己）
  - 方案选择：对比了 3 种方案（连接级定向推送 / 按 IP 频道订阅 / Session ID 频道订阅），选择方案 A 连接级定向推送
- **难易程度**：中
- **完成状态**：完成
- **验证方式**：电脑端向移动端发送消息后，移动端浏览器实时显示；编译通过

---

## 附录：步骤间依赖与执行顺序冲突汇总

### 冲突 1：步骤 5 与步骤 6 必须合并

- **问题**：步骤 5（标题栏消息图标）和步骤 6（导航栏修改）都修改 `Main.qml`，分开执行会导致中间状态编译失败
- **解决**：步骤 5 + 6 合并为一次 `Main.qml` 修改，同时完成标题栏图标、导航栏 ListModel、pageStack、initialItem 的替换

### 冲突 2：步骤 9 与 ChatService.h 声明与步骤 1 重复

- **问题**：步骤 9 要求在 `ChatService.h` 中新增信号和方法，但步骤 1 已定义了 `ChatService` 类
- **解决**：步骤 1 的 `ChatService.h` 一次性定义所有接口，步骤 9 仅负责实现逻辑和 QML 信号连接

### 冲突 3：步骤 12 的 onMessageReceived 签名与步骤 1/2 不一致

- **问题**：步骤 12 要求 `onMessageReceived` 增加 `remoteAddress` 参数，但步骤 1 和步骤 2 的接口定义可能未包含此参数
- **解决**：步骤 1 的 `ChatService.h` 中 `onMessageReceived` 必须从一开始就包含 `const QString& remoteAddress` 参数

### 依赖关系图

```
步骤 1 (ChatService.h/.cpp) ──→ 步骤 2 (API 路由) ──→ 步骤 11 (移动端 HTML)
       │                              │
       ├──→ 步骤 3 (mDNS 联动)        ├──→ 步骤 12 (匿名消息识别)
       │                              │
       ├──→ 步骤 4 (MessagePage.qml)  ├──→ 步骤 13 (WebSocket 推送)
       │         │
       ├──→ 步骤 5+6 (Main.qml 合并修改)
       │
       ├──→ 步骤 7 (main.cpp 初始化)
       │
       └──→ 步骤 8 (HTTP POST 发送)
                │
                └──→ 步骤 9 (WebSocket 实时刷新)
                         │
                         └──→ 步骤 10 (集成测试)
```

### 推荐执行顺序（考虑依赖后的调整）

1. 步骤 1：ChatService.h/.cpp（一次性定义所有接口）
2. 步骤 7：main.cpp 初始化（创建 ChatService、注册上下文属性）
3. 步骤 2：API 路由（依赖步骤 1 的 ChatService 和步骤 7 的 setChatService）
4. 步骤 3：mDNS 联动（依赖步骤 1 的 ChatService）
5. 步骤 8：HTTP POST 发送（依赖步骤 1 的 ChatService）
6. 步骤 4：MessagePage.qml（依赖步骤 1 的 Q_INVOKABLE 接口）
7. 步骤 5+6：Main.qml 合并修改（依赖步骤 4 的 MessagePage Component）
8. 步骤 9：WebSocket 实时刷新（依赖步骤 1-8 全部完成）
9. 步骤 11：移动端 HTML（依赖步骤 2 的 API 路由）
10. 步骤 12：匿名消息识别（依赖步骤 2 和步骤 11）
11. 步骤 13：WebSocket 推送（依赖步骤 9 和步骤 11）
12. 步骤 10：集成测试（最后执行）

---

## 附录：文件变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/core/chat/ChatService.h` | 新建 | ChatService 类声明（含 Q_PROPERTY、信号、Q_INVOKABLE） |
| `src/core/chat/ChatService.cpp` | 新建 | ChatService 类实现（含 m_anonymousUsers 管理） |
| `src/gui/qml/MessagePage.qml` | 新建 | 消息页面 QML（左右分栏、聊天记录、发送框） |
| `src/gui/qml/Main.qml` | 修改 | 标题栏加消息图标+红点、导航栏替换、pageStack 替换（步骤 5+6 合并执行） |
| `src/gui/CMakeLists.txt` | 修改 | 添加 MessagePage.qml 到 QML 资源 |
| `src/core/CMakeLists.txt` | 修改 | 添加 chat/ChatService 源文件到构建列表；添加 chat/ 和 network/ 到 include_directories |
| `src/network/RequestHandler.h` | 修改 | 新增 handleChatMessage、setChatService、ChatService* 成员、前向声明 |
| `src/network/RequestHandler.cpp` | 修改 | 实现聊天消息 API 路由；从 remoteAddress 提取纯 IP |
| `src/main.cpp` | 修改 | 在 initializeNetwork() 中创建 ChatService；WebSocket 订阅格式兼容；setContextProperty |
| `web/receive.html` | 修改 | 新增消息输入区域（输入框 + 发送按钮 + 聊天气泡 + WebSocket 接收） |

## 附录：数据流示意

### 电脑端 → 电脑端（P2P 消息）

```
发送方 QML (MessagePage)
  → chatService.sendMessage(toIp, toPort, content)
  → ChatService (C++)
  → QNetworkAccessManager POST /api/chat/message
  → 接收方 RequestHandler
  → handleChatMessage → ChatService::onMessageReceived
  → ChatService (C++)
  → emit messageReceived(fromIp)
  → emit unreadCountChanged()
  → 接收方 QML (MessagePage)
  → 刷新聊天记录 ListView
  → 更新用户列表未读红点
```

### 移动端浏览器 → 电脑端（Web 消息）

```
移动端浏览器 (receive.html)
  → 用户在输入框输入文字 → 点击"开始发送"
  → fetch('/api/chat/message', { method: 'POST', body: JSON })
  → 电脑端 RequestHandler
  → handleChatMessage → ChatService::onMessageReceived(remoteAddress)
  → ChatService (C++)
  → 以 remoteAddress 为 key 存储聊天记录
  → 用户列表新增"移动端"用户
  → emit messageReceived(fromIp)
  → emit unreadCountChanged()
  → 电脑端 QML (MessagePage)
  → 左侧用户列表出现"移动端"用户项（带未读红点）
  → 点击后右侧显示聊天记录
```

### 电脑端 → 移动端浏览器（回复消息）

```
电脑端 QML (MessagePage)
  → 选中"移动端"用户 → 输入回复 → 点击发送
  → chatService.sendMessage(mobileIp, port, content)
  → ChatService (C++)
  → 检测目标 IP 为 Web 浏览器连接
  → 通过 WebSocket 推送 chat_message 到对应连接
  → 移动端浏览器 (receive.html)
  → ws.onmessage 收到 chat_message
  → appendChatBubble(content, false)  // 对方消息靠左显示
```
