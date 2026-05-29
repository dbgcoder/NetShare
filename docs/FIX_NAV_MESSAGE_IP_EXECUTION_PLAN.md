# 修复：导航栏 / 消息关闭按钮 / 移动端消息接收 / 消息复制 / 聊天气泡样式

## 0. 问题概述

1. 左侧导航缺少"接收管理"入口
2. 消息页关闭按钮在左侧用户列表标题栏内，应在整个页面右上角
3. 移动端收不到电脑端消息（3个根因叠加）
4. 聊天消息不支持复制粘贴
5. 移动端聊天消息发送和接收无左右区分、无颜色区分
6. 移动端聊天发送按钮显示"开始发送"，应改为"发送"
7. 移动端残留调试面板需清理

---

## 1. 添加接收管理到侧边栏导航

- **修改内容**：
  - [Main.qml](file:///d:/qt6cmake/NetShare/src/gui/qml/Main.qml)：`ListModel` 中"分享管理"后插入 `ListElement { title: qsTr("接收管理"); icon: "📥" }`，`pageStack` 中添加对应页面
- **难易程度**：低
- **完成状态**：完成
- **验证方式**：编译通过，左侧导航可见"接收管理"

---

## 2. 消息页关闭按钮移到右上角

- **修改内容**：
  - [MessagePage.qml](file:///d:/qt6cmake/NetShare/src/gui/qml/MessagePage.qml)：删除左侧标题栏内关闭按钮，添加浮动关闭按钮（`z: 10`，锚定父容器右上角）
- **难易程度**：低
- **完成状态**：完成
- **验证方式**：编译通过，关闭按钮在页面右上角

---

## 3. 修复移动端收不到电脑端消息

### 3.1 根因一：WebSocket Opcode FIN 位未剥离

- **问题**：civetweb 传递给数据回调的 opcode 包含 FIN 位（如 `0x81`），但代码用 `op == MG_WEBSOCKET_OPCODE_TEXT`(0x1) 比较，导致 `0x81 ≠ 0x1`，subscribe 消息从未被处理，PONG 也从未被识别
- **修改内容**：
  - [CivetWebServer.cpp:641](file:///d:/qt6cmake/NetShare/src/network/CivetWebServer.cpp#L641)：添加 `int pureOp = op & 0x0F;` 剥离 FIN 位，所有比较和回调传递改用 `pureOp`
- **难易程度**：高
- **完成状态**：完成

### 3.2 根因二：数据回调返回值约定搞反

- **问题**：civetweb 数据回调返回 `0` 表示关闭连接，返回 `1` 表示保持存活。代码全部返回 `0`，导致每条消息处理后连接立即被关闭
- **修改内容**：
  - [CivetWebServer.cpp:646](file:///d:/qt6cmake/NetShare/src/network/CivetWebServer.cpp#L646)：PONG 处理 `return 0` → `return 1`
  - [CivetWebServer.cpp:663](file:///d:/qt6cmake/NetShare/src/network/CivetWebServer.cpp#L663)：默认返回 `return 0` → `return 1`
  - [main.cpp:372](file:///d:/qt6cmake/NetShare/src/main.cpp#L372)：用户数据回调 `return 0` → `return 1`
- **难易程度**：高
- **完成状态**：完成

### 3.3 根因三：WebSocket 连接断开后不重连

- **问题**：`receive.html` 的 `ws.onclose` 中，重连逻辑 `setTimeout(connectWebSocket, 5000)` 被包裹在 `if (hasActive)` 条件内，无上传任务时连接永久丢失
- **修改内容**：
  - [receive.html](file:///d:/qt6cmake/NetShare/web/receive.html)：将 `setTimeout(connectWebSocket, 5000)` 移到 `if (hasActive)` 外部
- **难易程度**：中
- **完成状态**：完成

### 3.4 辅助修复：心跳初始化与死连接清理

- **修改内容**：
  - [CivetWebServer.cpp:609](file:///d:/qt6cmake/NetShare/src/network/CivetWebServer.cpp#L609)：`staticWsReadyHandler` 中初始化 `m_wsLastPong[conn]`，防止新连接被心跳误判超时
  - [CivetWebServer.cpp:230-252](file:///d:/qt6cmake/NetShare/src/network/CivetWebServer.cpp#L230-L252)：`sendToIp` 和 `broadcastToSubscribers` 中检测 `mg_websocket_write` 返回 ≤0 时自动清理死连接
- **难易程度**：中
- **完成状态**：完成

---

## 4. 消息支持复制

- **修改内容**：
  - **PC 端**：[MessagePage.qml](file:///d:/qt6cmake/NetShare/src/gui/qml/MessagePage.qml)：聊天气泡中 `Label` 替换为 `TextEdit`（`readOnly: true; selectByMouse: true; padding: 0`）
  - **移动端**：[receive.html](file:///d:/qt6cmake/NetShare/web/receive.html)：`.chat-msg-text` 添加 `user-select: text; -webkit-user-select: text`
- **难易程度**：低
- **完成状态**：完成

---

## 5. 移动端聊天气泡左右区分与颜色区分

- **修改内容**：
  - [receive.html](file:///d:/qt6cmake/NetShare/web/receive.html)：CSS 改为 flexbox 布局
    - `.chat-msg` 添加 `display: flex`
    - `.chat-msg-sent` 使用 `justify-content: flex-end`（右侧）
    - `.chat-msg-received` 使用 `justify-content: flex-start`（左侧）
    - 发送气泡：绿色背景 `#4CAF50`，白色文字，右下角小圆角
    - 接收气泡：深灰背景 `#2a2a2a`，浅灰文字 `#e0e0e0`，左下角小圆角
  - `renderChatMessages()` 函数：气泡内部用 flex column 布局，时间戳在气泡下方对齐
- **难易程度**：低
- **完成状态**：完成

---

## 6. 移动端聊天发送按钮改名

- **修改内容**：
  - [receive.html:345](file:///d:/qt6cmake/NetShare/web/receive.html#L345)：`chatSendBtn` 按钮文字从"开始发送"改为"发送"
- **难易程度**：低
- **完成状态**：完成

---

## 7. 清理移动端调试信息

- **修改内容**：
  - [receive.html](file:///d:/qt6cmake/NetShare/web/receive.html)：
    - 删除 `wsDebugPanel` HTML 元素
    - 删除 `wsDebugPanel` 变量和 `wsDebug()` 函数
    - 删除所有 `wsDebug(...)` 调用（共 10 处）
- **难易程度**：低
- **完成状态**：完成

---

## 8. 编译验证

- **修改内容**：执行编译命令验证所有修改无错误无警告
- **难易程度**：低
- **完成状态**：完成
- **验证方式**：编译退出码为 0，VS Code 诊断 0 error 0 warning
