# NetShare 改进计划文档

> 基于项目分析报告中的 6 个潜在问题，制定本改进计划。
> 遵循 `.comate/rules/breakpoint-resume-progress.mdr` 中的开发规范。
>
> **版本**: v4（已根据代码审查修正所有冲突和遗漏）

---

## 问题 1：无 README 文件

### 现状
项目缺少任何 README 文件，新开发者无法快速了解项目用途、构建步骤和使用方法。

### 目标
创建 `README.md`，包含项目简介、技术栈、构建指南、使用说明和架构说明。

### 执行计划

**1.1 创建 `README.md`**

内容结构：
- 项目名称与一句话简介
- 功能特性列表（文件分享、断点续传、分块传输、mDNS 发现、Web 界面、桌面 GUI）
- 技术栈（C++17 / Qt 6.8 / CMake / SQLite）
- 快速开始（环境要求、构建步骤、运行方式）
- 使用说明（创建分享、接收文件、Web 端访问）
- 目录结构说明
- 配置项说明
- 开发规范引用（断点续传规则）

### 涉及文件
- 新建：`README.md`

### 风险
- 无

---

## 问题 2：上传目录配置化

### 现状
`main.cpp` 中上传目录硬编码为：
```cpp
requestHandler->setUploadDir(QDir::homePath() + "/NetShare/Uploads");
```

`SettingsManager` 已有 `getDefaultUploadPath()` 方法，但 `main.cpp` 未调用它，也不支持用户自定义覆盖。

### 目标
让 `main.cpp` 使用 `SettingsManager` 获取上传路径，支持用户通过设置页面自定义。

### 执行计划

**2.1 修改 `SettingsManager`**

添加可覆盖的上传路径方法：
```cpp
// .h
Q_INVOKABLE QString getUploadPath() const;
Q_INVOKABLE void setUploadPath(const QString& path);

// .cpp
QString SettingsManager::getUploadPath() const {
    // 优先读取用户自定义值，无则返回默认值
    QString custom = m_settings->value("Paths/UploadDir").toString();
    return custom.isEmpty() ? getDefaultUploadPath() : custom;
}

void SettingsManager::setUploadPath(const QString& path) {
    m_settings->setValue("Paths/UploadDir", path);
}
```

**2.2 修改 `main.cpp` 中的初始化逻辑**

```cpp
QString uploadDir = settings->getUploadPath();
QDir().mkpath(uploadDir);
requestHandler->setUploadDir(uploadDir);
```

**2.3 在 QML 设置页面新增上传路径配置**

SettingsPage.qml 当前**没有**上传路径配置 UI，需要新增：
- 在 `SettingsPage.qml` 中添加上传路径选择控件（文本输入框 + 文件夹选择按钮）
- 绑定到 `settingsManager.getUploadPath()` 和 `settingsManager.setUploadPath()`
- 参考现有 TLS 配置项的 UI 风格（SectionHeader + SettingItem 模式）

### 涉及文件
- 修改：`src/main.cpp`
- 修改：`src/core/common/SettingsManager.cpp` / `.h`
- 修改：`src/gui/qml/SettingsPage.qml`

### 风险
- 低。仅影响配置读取逻辑，不影响现有功能。

---

## 问题 3：配置目录策略优化

### 现状
[main.cpp](file:///d:/qt6cmake/NetShare/src/main.cpp#L585-L610) 已使用 `QStandardPaths::AppLocalDataLocation`，跨平台兼容性良好。

**但存在一个小问题**：
- 当前配置、数据库都在同一目录（`AppLocalDataLocation`）
- 按 XDG 规范，配置文件应使用 `AppConfigLocation`（Linux 下为 `~/.config/`，而非 `~/.local/share/`）

### 目标
将配置文件目录独立为 `QStandardPaths::AppConfigLocation`，数据库和日志保留在 `AppLocalDataLocation`。

### 执行计划

**3.1 修改 `main.cpp` 中的路径函数**

注意：当前三个函数是**链式调用**关系：
- `getAppDataDirectory()` → `QStandardPaths::AppLocalDataLocation`
- `getConfigDirectory()` → 调用 `getAppDataDirectory()`
- `getDatabaseDirectory()` → 调用 `getConfigDirectory()`

重构时保持链式关系，仅修改 `getConfigDirectory()` 的底层调用：

```cpp
QString getAppDataDirectory() const
{
    // 保持不变：应用数据目录
    return QDir::toNativeSeparators(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
}

QString getConfigDirectory() const
{
    // 改为 AppConfigLocation（Linux: ~/.config/NetShare）
    QString dir = QDir::toNativeSeparators(
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    QDir().mkpath(dir);
    return dir;
}

QString getDatabaseDirectory() const
{
    // 保持链式调用：数据库目录 = 配置目录（Windows 下相同，Linux 下会到 ~/.config/NetShare）
    // 注意：如果希望数据库和配置分离，可改为直接调用 getAppDataDirectory()
    QString dir = getConfigDirectory();
    QDir().mkpath(dir);
    return dir;
}

QString getLogDirectory() const
{
    QString dir = getAppDataDirectory() + "/logs";
    QDir().mkpath(dir);
    return dir;
}
```

**3.2 旧配置迁移（可选但推荐）**

首次启动时检查旧配置路径是否存在，如存在则迁移：
```cpp
// 旧路径：AppLocalDataLocation/config.json
// 新路径：AppConfigLocation/config.json
QString oldConfig = getDatabaseDirectory() + "/config.json";
QString newConfig = getConfigDirectory() + "/config.json";
if (QFileInfo::exists(oldConfig) && !QFileInfo::exists(newConfig)) {
    QFile::copy(oldConfig, newConfig);
    LOG_INFO("Migrated config from old path to new path");
}
```

### 涉及文件
- 修改：`src/main.cpp`

### 风险
- **低**。`AppConfigLocation` 在 Windows 下与 `AppLocalDataLocation` 相同（都是 `%LOCALAPPDATA%`），仅影响 Linux/macOS。
- 旧配置迁移确保用户无感知。

---

## 问题 4：防火墙规则累积

### 现状
`main.cpp` 中 `configureWindowsFirewall()` 每次启动都执行 `netsh ... add rule`，不会检查规则是否已存在，导致规则累积。

### 目标
启动时先检查规则是否存在，不存在才添加。

### 执行计划

**4.1 修改 `configureWindowsFirewall()` 函数**

使用 PowerShell 检查（比 netsh 更可靠）：
```cpp
static void configureWindowsFirewall(quint16 port)
{
    QString ruleName = "NetShare HTTP Server";

    // 使用 PowerShell 检查规则是否存在
    int checkRet = QProcess::execute("powershell", {
        "-NoProfile", "-Command",
        QString("if (Get-NetFirewallRule -DisplayName '%1' -ErrorAction SilentlyContinue) { exit 0 } else { exit 1 }").arg(ruleName)
    });

    if (checkRet == 0) {
        LOG_INFO("Windows Firewall rule already exists for '%s'", qPrintable(ruleName));
        return;
    }

    // 规则不存在，添加新规则
    int ret = QProcess::execute("netsh", {
        "advfirewall", "firewall", "add", "rule",
        QString("name=%1").arg(ruleName),
        "dir=in", "action=allow", "protocol=TCP",
        QString("localport=%1").arg(port)
    });

    if (ret == 0) {
        LOG_INFO("Windows Firewall rule added for port %d", port);
    } else {
        LOG_WARN("Failed to add Windows Firewall rule for port %d (need admin)", port);
    }
}
```

**4.2（可选）添加退出时清理函数**

不建议在退出时清理，因为用户可能希望保留规则以便下次快速启动。

### 涉及文件
- 修改：`src/main.cpp`

### 风险
- 低。仅影响 Windows 平台防火墙规则管理。
- PowerShell 在 Windows 7+ 均内置，无兼容性问题。

---

## 问题 5：TLS 未实现

### 现状
`main.cpp` 中 TLS 配置仅打印日志：
```cpp
if (settings->value("server.tlsEnabled", false).toBool()) {
    LOG_INFO("TLS is enabled, configure certificates in settings");
}
```
实际 TLS 功能未实现。

**配置键不一致问题**：
- `main.cpp` 使用 `server.tlsEnabled`（**点号分隔**）
- `SettingsPage.qml` 使用 `server/tlsEnabled`（**斜杠分隔**）

QSettings 在 `IniFormat` 下，点号和斜杠被视为**不同的键**，导致 GUI 设置的值无法被 `main.cpp` 读取。这是一个现有 bug。

### 目标
1. 统一 TLS 配置键为斜杠分隔（`server/tlsEnabled`）
2. 实现 HttpServer 和 WebSocketHandler 的 TLS 支持

### 执行计划

**5.1 修复 `main.cpp` 配置键不一致（点号→斜杠）**

`main.cpp` 中有 4 处 `settings->value()` 调用，其中 2 处使用点号分隔，需统一为斜杠：

```cpp
// 修改前（点号分隔）
settings->value("server.tlsEnabled", false).toBool()
settings->value("advanced.mDNSServiceName", "NetShare").toString()

// 修改后（斜杠分隔，与 SettingsPage.qml 和其余配置一致）
settings->getBool("server/tlsEnabled", false)
settings->getString("advanced/mDNSServiceName", "NetShare")
```

同时改用类型安全方法（`getBool`/`getString`）替代 `.toBool()`/`.toString()`。

**5.2 确认现有 TLS 配置项（无需新增）**

`SettingsPage.qml` 已定义以下配置键，保持不变：
- `server/tlsEnabled` — TLS 开关
- `server/httpsPort` — HTTPS 端口（默认 8443）
- `server/tlsCertPath` — 证书文件路径
- `server/tlsKeyPath` — 私钥文件路径

**5.3 修改 `HttpServer` 支持 TLS**

- 添加 `QSslConfiguration` 支持
- 添加 `startTls(quint16 port, const QString& certPath, const QString& keyPath)` 方法
- 内部使用 `QSslSocket` 处理连接
- 保留原有 `start()` 方法不变

**5.4 修改 `WebSocketHandler` 支持 WSS**

- 类似 HttpServer，添加 TLS 支持
- WSS 端口 = HTTPS 端口 + 1

**5.5 修改 `main.cpp` 集成 TLS 启动逻辑**

```cpp
if (settings->getBool("server/tlsEnabled", false)) {
    QString certPath = settings->getString("server/tlsCertPath");
    QString keyPath = settings->getString("server/tlsKeyPath");
    quint16 tlsPort = settings->getInt("server/httpsPort", 8443);

    if (!certPath.isEmpty() && !keyPath.isEmpty()) {
        httpServer->startTls(tlsPort, certPath, keyPath);
        LOG_INFO("HTTPS server started on port %d", tlsPort);

        // WebSocket over TLS
        quint16 wssPort = tlsPort + 1;
        if (wsHandler->startTls(wssPort, certPath, keyPath)) {
            LOG_INFO("WSS server started on port %d", wssPort);
        }
    } else {
        LOG_WARN("TLS enabled but certificate/key paths not configured");
    }
}
```

**5.6 SettingsPage.qml 无需修改**

TLS 设置 UI 已完整存在（第 900-1081 行），仅需确保配置键与 C++ 端一致（已使用斜杠）。

### 涉及文件
- 修改：`src/main.cpp`（修复配置键不一致 + 集成 TLS 启动）
- 修改：`src/network/HttpServer.h` / `.cpp`
- 修改：`src/network/WebSocketHandler.h` / `.cpp`

### 风险
- **较高**。TLS 实现涉及网络层改动，需要充分测试。
- **建议分阶段**：先实现 HttpServer TLS，验证后再做 WebSocket TLS。
- **证书管理**：用户需要自行准备证书，建议提供自签名证书生成指引。

---

## 问题 6：.gitignore 补充

### 现状
[.gitignore](file:///d:/qt6cmake/NetShare/.gitignore) 已包含大部分规则（`build/`、`*.obj`、`*.lib`、`*.user`、`Thumbs.db`、`.DS_Store` 等）。

**实际缺失项**：
- `dist/`（发布包）
- `*.db`（数据库文件）
- `*.ninja` / `build.ninja`（Ninja 构建文件）
- `CMakeCache.txt` / `CMakeFiles/`（CMake 缓存）
- `*_autogen/`（Qt 自动生成目录）
- `logs/`（日志目录）
- `compile_commands.json`（编译数据库）
- `.qtcreator/`（Qt Creator 用户配置，已有 `*.user` 但目录未忽略）

### 目标
补充缺失的忽略规则。

### 执行计划

**6.1 补充 `.gitignore` 规则**

在现有文件末尾追加：
```gitignore
# CMake cache
CMakeCache.txt
CMakeFiles/
cmake_install.cmake
compile_commands.json

# Ninja
*.ninja
build.ninja

# Qt autogen
*_autogen/

# Distribution
dist/

# Database
*.db

# Logs
logs/

# Qt Creator user data
.qtcreator/
```

### 涉及文件
- 修改：`.gitignore`

### 风险
- 低。仅影响 Git 忽略规则。
- 注意：如果 `build/` 或 `dist/` 已被 Git 跟踪，需要额外执行 `git rm -r --cached build/ dist/`。

---

## 问题 7：SettingsManager::load() 悬空指针风险（新增）

### 现状
[SettingsManager.cpp](file:///d:/qt6cmake/NetShare/src/core/common/SettingsManager.cpp#L22-L36) 中 `load()` 方法：
```cpp
bool SettingsManager::load(const QString& filePath)
{
    QSettings* newSettings = new QSettings(filePath, QSettings::IniFormat);
    if (newSettings->status() != QSettings::NoError) {
        delete newSettings;
        return false;  // m_settings 未被删除，安全
    }

    delete m_settings;      // 先删除旧对象
    m_settings = newSettings;  // 再赋新值
    return true;
}
```

**分析**：当前实现实际上是安全的——只有在新对象创建成功（status == NoError）后才会 delete 旧对象。但代码可读性差，容易引起误解。

### 目标
改进代码可读性，消除潜在的维护风险。

### 执行计划

**7.1 重构 `load()` 方法**

```cpp
bool SettingsManager::load(const QString& filePath)
{
    if (filePath.isEmpty()) {
        return false;
    }

    // 先创建并验证新对象
    QSettings* newSettings = new QSettings(filePath, QSettings::IniFormat);
    if (newSettings->status() != QSettings::NoError) {
        delete newSettings;
        return false;
    }

    // 验证成功后替换
    QSettings* oldSettings = m_settings;
    m_settings = newSettings;
    delete oldSettings;

    return true;
}
```

### 涉及文件
- 修改：`src/core/common/SettingsManager.cpp`

### 风险
- 极低。仅改进代码结构，不改变行为。

---

## 执行优先级与依赖关系

```
优先级 1（立即执行，无依赖）:
├── 问题 1: 创建 README.md
├── 问题 6: 补充 .gitignore
├── 问题 4: 防火墙规则优化
└── 问题 7: SettingsManager::load() 重构

优先级 2（低依赖，独立修改）:
├── 问题 2: 上传目录配置化
└── 问题 3: 配置目录策略优化

优先级 3（高复杂度，需充分测试）:
└── 问题 5: TLS 实现
```

## 详细任务清单

| # | 任务 | 涉及文件 | 优先级 | 预估改动量 |
|---|------|----------|--------|-----------|
| 1.1 | 创建 README.md | 新建 `README.md` | P1 | 中 |
| 4.1 | 防火墙规则先检查后添加（PowerShell） | `main.cpp` | P1 | 小 |
| 6.1 | 补充 .gitignore 缺失规则 | `.gitignore` | P1 | 小 |
| 7.1 | 重构 SettingsManager::load() | `SettingsManager.cpp` | P1 | 小 |
| 2.1 | 添加 getUploadPath()/setUploadPath() | `SettingsManager.cpp/.h` | P2 | 小 |
| 2.2 | main.cpp 使用 getUploadPath() | `main.cpp` | P2 | 小 |
| 2.3 | 设置页面新增上传路径配置 UI | `SettingsPage.qml` | P2 | 小 |
| 3.1 | 配置目录改用 AppConfigLocation | `main.cpp` | P2 | 小 |
| 3.2 | （可选）旧配置迁移逻辑 | `main.cpp` | P2 | 小 |
| 5.1 | 修复 main.cpp 配置键不一致（点号→斜杠） | `main.cpp` | P3 | 小 |
| 5.2 | HttpServer 添加 TLS 支持 | `HttpServer.h/.cpp` | P3 | 大 |
| 5.3 | WebSocketHandler 添加 TLS 支持 | `WebSocketHandler.h/.cpp` | P3 | 大 |
| 5.4 | main.cpp 集成 TLS 启动逻辑 | `main.cpp` | P3 | 中 |

---

## 注意事项

1. **遵循断点续传规则**：所有涉及上传/传输的修改必须遵守 `.comate/rules/breakpoint-resume-progress.mdr` 中的规范
2. **配置键命名统一**：全部使用斜杠分隔（如 `Network/Port`、`server/tlsEnabled`），不使用点号
3. **TLS 配置键沿用现有**：使用 `server/` 前缀（`server/tlsEnabled`、`server/tlsCertPath` 等），与 SettingsPage.qml 保持一致
4. **TLS 分阶段**：问题 5 建议先完成 HttpServer TLS，验证后再做 WebSocket TLS
5. **测试覆盖**：每个问题修改后应运行现有测试确保无回归
6. **向后兼容**：问题 3 的路径变更在 Windows 下无影响（AppConfigLocation == AppLocalDataLocation），仅优化 Linux/macOS
7. **SettingsPage.qml TLS UI 已存在**：无需修改，仅需确保 C++ 端配置键与其一致

---

## 修订记录

| 版本 | 日期 | 说明 |
|------|------|------|
| v1 | - | 初始版本 |
| v2 | 2026-05-23 | 根据代码审查修正：问题 3 已使用 QStandardPaths、问题 2 已有 getDefaultUploadPath()、.gitignore 大部分已有、新增问题 7、防火墙改用 PowerShell 检查 |
| v3 | 2026-05-23 | 修正 TLS 配置键冲突：沿用 `server/` 前缀（非 `Network/`）、删除 5.5 任务（TLS UI 已存在）、新增 5.1 修复点号/斜杠不一致 bug、任务清单从 14 项减至 13 项 |
| v4 | 2026-05-23 | 修正 4 处冲突：① 问题 3 路径函数链式关系补充说明 ② 任务 2.3 明确为"新增"上传路径 UI ③ 任务 5.1 范围扩展至包含 `advanced.mDNSServiceName` ④ 顶部版本号改为 v3 |
