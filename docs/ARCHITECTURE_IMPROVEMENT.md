# NetShare 架构改进执行文档

> 最后更新：2026-04-28
> 状态标记：⏳待开始 | 🔄进行中 | ✅已完成 | ❌阻塞

---

## 总览

| # | 优先级 | 难易 | 内容 | 状态 | 验证方法 |
|---|--------|------|------|------|----------|
| 1 | P0 | ⭐⭐⭐ | ShareManager 数据持久化 | ✅ | 创建共享→重启→共享仍存在 |
| 2 | P0 | ⭐⭐ | FileTransferEngine chunk 失败错误传播 | ✅ | chunk 失败时不 merge，taskFailed 信号触发 |
| 3 | P1 | ⭐⭐⭐ | Core 服务定义接口抽象 | ✅ | RequestHandler 依赖接口指针 |
| 4 | P1 | ⭐ | Logger 单例改 Meyers 模式 | ✅ | static local variable + m_initialized |
| 5 | P1 | ⭐ | 统一编译警告级别 | ✅ | 全局 /W4 或 -Wall |
| 6 | P1 | ⭐ | web/ 纳入构建系统 | ✅ | add_subdirectory(web) |
| 7 | P1 | ⭐ | 清理根目录 + .gitignore | ✅ | 根目录整洁 |
| 8 | P1 | ⭐ | 移除冗余 FindQt.cmake | ✅ | 已删除 |
| 9 | P1 | ⭐ | 统一错误处理 Result\<T\> | ✅ | NetShareError.h + ErrorCode + Result\<T\> |
| 10 | P1 | ⭐ | 补充单元测试 | ✅ | test_sharemanager.cpp |
| 11 | P1 | ⭐ | 修复编译错误/警告 | ✅ | 0 error, 第三方警告已抑制 |
| 12 | P2 | ⭐⭐ | ServiceLocator 依赖注入容器 | ✅ | ServiceLocator.h 模板类 |
| 13 | P2 | ⭐ | 构建脚本移至 scripts/ | ✅ | 使用相对路径 |
| 14 | P2 | ⭐ | 集成 Version.cmake | ✅ | 版本号集中管理 |
| 15 | P2 | ⭐⭐ | TransferLogService 数据库持久化 | ✅ | setDatabase + saveLogToDb |
| 16 | P2 | ⭐⭐ | DatabaseManager 高层 API | ✅ | DbRow + queryRows + queryValue |
| 17 | P2 | ⭐⭐ | QML 资源嵌入 (.qrc) | ✅ | qrc 优先，文件系统回退 |
| 18 | P2 | ⭐⭐ | 核心类添加 QML_ELEMENT | ✅ | ShareManager/FileTransferEngine/SettingsManager/BandwidthManager |
| 19 | P2 | ⭐ | .clang-format 代码风格 | ✅ | 项目根目录 |
| 20 | P3 | ⭐⭐⭐ | 拆分 Core 模块 | ✅ | 子目录 common/share/transfer/notification |
| 21 | P3 | ⭐⭐⭐⭐ | main.cpp DI 重构 | ✅ | ServiceLocator 替代 15+ 成员变量 |
| 22 | P3 | ⭐⭐⭐⭐ | QML 完整迁移 qt_add_qml_module | ✅ | loadFromModule + QML_ELEMENT |
| 23 | P3 | ⭐⭐ | i18n 国际化 | ✅ | tr()/qsTr() 提取 |
| 24 | P4 | ⭐ | CMake QML 策略警告清除 | ✅ | QTP0001/0003/0004/0005 + OUTPUT_DIRECTORY |

**进度：24/24 已完成 ✅**

---

## 第一轮改进（P0+P1 快速修复）详细记录

### #1: ShareManager 数据持久化 ✅
- 新增 `setDatabase(DatabaseManager*)` 注入
- `createShare()` 持久化到 DB
- `cancelShare()` / `cleanupExpiredShares()` 删除 DB 记录
- `shareAccessed()` 更新 download_count，达到 maxDownloads 自动取消
- 新增 `loadSharesFromDb()` 启动时恢复共享
- 改为 Meyers 单例 `static ShareManager& instance()`

### #2: FileTransferEngine chunk 失败错误传播 ✅
- `QAtomicInt failedChunks` 跟踪失败 chunk 数
- 失败时清理临时目录，设置 task Failed，emit taskFailed
- 仅在全部 chunk 成功时执行 merge
- `(void)QtConcurrent::run` 抑制 C4858 警告

### #3: 接口抽象 ✅
新增 `IShareManager.h`, `IFileBrowser.h`, `IFolderPacker.h`
- 前向声明使用 `class ShareInfo` / `class FileEntry`（匹配 Q_GADGET 定义）

### #4: Logger Meyers 单例 ✅
`static Logger& instance()` + `m_initialized` 标志 + `shutdown()` 重置

### #5-8: 构建系统清理 ✅
- 全局 `/W4` 或 `-Wall -Wextra`
- `add_subdirectory(web)`
- `.gitignore` + 删除 8 个构建日志
- 删除 `cmake/FindQt.cmake`

### #9: 统一错误处理 ✅
`NetShareError.h` — 30+ ErrorCode（7 分类）+ `Result<T>` + `Result<void>` 特化

### #10: 单元测试 ✅
`test_sharemanager.h/.cpp` — 9 个测试用例（localIp、createShare、cancelShare、Result 等）

### #11: 编译错误修复 ✅
- DatabaseManager.h 路径 → 添加 `../database` include
- QSystemTrayIcon → 恢复 Qt6::Widgets
- C4099 struct/class 前向声明 → 改为 `class`
- C4244 qrcodegen → `/wd4244`
- C4996 AA_EnableHighDpiScaling → 移除已弃用 API
- LNK2019 DatabaseManager → 链接 NetshareDatabase
- LNK2005 moc 冲突 → 重构 test .h/.cpp 分离
- AUTOMOC 失败 → 清理缓存，匹配声明

---

## 第二轮改进（Step 1-10）详细记录

### Step 1: ServiceLocator ✅
新增 `src/core/common/ServiceLocator.h` — 轻量级服务定位器，基于 `typeid` 的类型注册/查找。模板方法 `registerService<T>()` / `service<T>()`。为后续 main.cpp DI 重构铺路。

### Step 2: 构建脚本移至 scripts/ ✅
`build_release.bat` / `build_only.bat` → `scripts/`，使用 `%~dp0..` 相对路径，不再硬编码项目根目录。

### Step 3: 集成 Version.cmake ✅
根 CMakeLists.txt 通过 `include(cmake/Version.cmake)` 加载版本号，`project()` 使用 `${NETSHARE_VERSION}`。移除重复的组织名设置。

### Step 4: TransferLogService 持久化 ✅
- 新增 `setDatabase(DatabaseManager*)` 注入
- `logTransfer()` 调用 `saveLogToDb()`
- `updateLogEntry()` 调用 `updateLogInDb()`
- 新增 `loadLogsFromDb()` 启动时加载最近 500 条
- main.cpp 中调用 `m_transferLogService->setDatabase(m_database)`

### Step 5: DatabaseManager 高层 API ✅
- 新增 `DbRow` 结构体（列名→值映射），提供 `stringValue()/int64Value()/boolValue()` 类型化访问
- 新增 `queryRows()` 返回 `QList<DbRow>`，不暴露 QSqlQuery
- 新增 `queryValue()` 返回单个值，用于 COUNT/SUM 查询
- 保留原有 `QSqlQuery query()` 方法保持向后兼容

### Step 6: QML 资源嵌入 ✅
- 新增 `src/gui/resources.qrc` 嵌入 main.qml / Theme.qml / qmldir
- main.cpp 加载策略改为：qrc 优先 → 文件系统回退
- context property 在 load 之前设置（修复了原来先 load 后 setContextProperty 的潜在问题）

### Step 7+8: QML_ELEMENT 宏 ✅
为以下类添加 `QML_ELEMENT`/`QML_SINGLETON`：
- `ShareManager` — QML_SINGLETON
- `FileTransferEngine` — QML_ELEMENT
- `SettingsManager` — QML_ELEMENT
- `BandwidthManager` — QML_ELEMENT

添加 `#include <QtQml/qqml.h>` 和 `Qt6::Qml` 到 NetshareCore 链接。

### Step 9: .clang-format ✅
基于 LLVM 风格，C++17，120 列宽，Allman 大括号，左对齐指针。

### Step 10: 执行文档更新 ✅

---

## 第三轮改进（Step 11-16）详细记录

### Step 11: 拆分 Core 模块 ✅
`src/core/` 从扁平结构拆分为子目录：
```
src/core/common/      — Logger, SettingsManager, NetShareError, ServiceLocator, 接口
src/core/share/       — ShareManager, FileBrowser, FolderPacker
src/core/transfer/    — FileTransferEngine, ChunkManager, ResumeManager, BandwidthManager, TransferLogService
src/core/notification/ — NotificationManager
```
CMakeLists.txt 按子目录组织源文件列表，include 路径添加所有子目录。

### Step 12: 更新 include 路径 ✅
main.cpp 和 test_sharemanager.cpp 中的 include 路径统一为子路径格式（如 `core/common/Logger.h`），修复 14 处断引用。

### Step 13: main.cpp DI 重构 ✅
- 15+ 成员变量 → `ServiceLocator m_locator` + 3 个直接成员
- 所有 `new Type()` → `m_locator.registerService(ptr)`
- 所有 `m_xxx->method()` → `m_locator.service<Type>()->method()`
- `initializeNetworkServer()` 使用 `m_locator.service<IShareManager>()` 接口指针
- QML 加载：`loadFromModule("NetShare", "main")` → qrc 回退 → 文件系统回退

### Step 14: QML 完整迁移 ✅
- `qt_add_qml_module` 替代 resources.qrc + 手写 qmldir
- 删除 `resources.qrc` 和 `qmldir`（自动生成）
- `loadFromModule("NetShare", "main")` 替代 `load(qmlPath)`
- 添加 `qt_policy(SET QTP0004 NEW)` 解决 qmldir 策略警告
- 添加 `OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/NetShare` 解决 qmllint 路径警告

### Step 15: i18n 国际化 ✅
- main.cpp 托盘菜单字符串改为 `tr()`
- QML 字符串改为 `qsTr()` 包裹
- 为后续 Qt Linguist .ts 文件生成做准备

### Step 16: CMake QML 策略警告清除 ✅
- `qt_policy(SET QTP0001 NEW)` — 资源前缀默认 `/qt/qml`
- `qt_policy(SET QTP0003 NEW)` — RESOURCE_PREFIX 默认模块 URI
- `qt_policy(SET QTP0004 NEW)` — qmldir 自动生成
- `qt_policy(SET QTP0005 NEW)` — qmllint 自动启用
- `OUTPUT_DIRECTORY` 与模块 target path 对齐

---

## 变更汇总

| 类别 | 新增文件 | 修改文件 | 删除文件 |
|------|---------|---------|---------|
| 核心 | IShareManager.h, IFileBrowser.h, IFolderPacker.h, NetShareError.h, ServiceLocator.h | ShareManager.h/.cpp, Logger.h/.cpp, FileBrowser.h, FolderPacker.h, FileTransferEngine.h/.cpp, TransferLogService.h/.cpp, SettingsManager.h, BandwidthManager.h | - |
| 数据库 | - | DatabaseManager.h/.cpp | - |
| 网络 | - | RequestHandler.h/.cpp | - |
| GUI | - | gui/CMakeLists.txt, main.qml, Theme.qml 等 | resources.qrc, qmldir |
| 入口 | - | main.cpp (DI+QML+i18n+持久化) | - |
| 构建 | scripts/build_release.bat, scripts/build_only.bat, NetShareVersion.h.in | CMakeLists.txt (root+core+qrcode+tests+web+gui) | build_release.bat, build_only.bat, cmake/FindQt.cmake |
| 测试 | test_sharemanager.cpp, test_sharemanager.h | tests/CMakeLists.txt | (旧)test_sharemanager.h |
| 项目 | .gitignore, .clang-format | - | 8个构建日志 |

---

## 架构改进前后对比

| 维度 | 改进前 | 改进后 |
|------|--------|--------|
| 持久化 | ShareManager 数据不落盘 | DB 持久化，重启恢复 |
| 传输可靠性 | chunk 失败仍 merge | QAtomicInt 追踪，失败清理 |
| 依赖管理 | 15+ 手动 new/delete | ServiceLocator 注入 |
| 接口抽象 | 无 | IShareManager/IFileBrowser/IFolderPacker |
| 单例模式 | 原始指针 + delete | Meyers singleton |
| 错误处理 | 散落各处 | NetShareError + Result\<T\> |
| QML 绑定 | context property + qmldir | qt_add_qml_module + QML_ELEMENT |
| 代码组织 | 扁平 src/core/ | 子目录分类 |
| 构建系统 | 零警告级别、脚本散落 | /W4、scripts/、Version.cmake |
| 国际化 | 硬编码字符串 | tr()/qsTr() 提取 |
| CMake 警告 | QML 策略未设置 | QTP0001-0005 全部 NEW |
