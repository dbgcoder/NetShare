# NetShare 代码规范

**版本**: 1.0
**日期**: 2026-04-25
**适用**: NetShare 项目所有代码

---

## 目录

1. [命名规范](#1-命名规范)
2. [架构分层](#2-架构分层)
3. [QML 规范](#3-qml-规范)
4. [C++ 规范](#4-c-规范)
5. [页面加载规范](#5-页面加载规范)
6. [资源管理](#6-资源管理)
7. [错误处理](#7-错误处理)
8. [日志规范](#8-日志规范)
9. [数据库规范](#9-数据库规范)
10. [网络通信规范](#10-网络通信规范)
11. [安全规范](#11-安全规范)
12. [测试规范](#12-测试规范)
13. [Git 提交规范](#13-git-提交规范)

---

## 1. 命名规范

### 1.1 总体原则

| 类型 | 命名方式 | 示例 |
|------|---------|------|
| 类名 | PascalCase | `FileTransferEngine` |
| 接口名 | PascalCase + I前缀 | `IFileHandler` |
| 枚举类型 | PascalCase | `TransferStatus` |
| 枚举值 | PascalCase | `StatusCompleted` |
| 函数名 | PascalCase | `startDownload()` |
| 变量名 | camelCase | `filePath`, `downloadSpeed` |
| 常量 | kConstantName 或 CONSTANT_NAME | `kMaxRetryCount`, `MAX_BUFFER_SIZE` |
| 私有成员 | m_ + camelCase | `m_fileList`, `m_downloadTask` |
| 信号名 | PascalCase | `progressChanged` |
| 槽函数 | PascalCase | `onDownloadFinished` |
| QML id | snake_case | `file_list_view`, `download_progress` |
| QML 文件名 | PascalCase | `HomePage.qml`, `FileListPage.qml` |
| 文件夹名 | kebab-case | `core/`, `network/`, `qml/` |

### 1.2 C++ 命名

```cpp
// 类名: PascalCase
class FileTransferEngine {};
class DownloadSession {};
class ShareManager {};

// 成员变量: m_ 前缀 + camelCase
class ShareManager {
private:
    QString m_shareToken;
    QMap<QString, ShareInfo> m_shareCache;
    quint32 m_maxParallelTasks;
};

// 常量: k 前缀
static const int kDefaultChunkSize = 4 * 1024 * 1024;
static const QString kDefaultPort = "8080";

// 枚举
enum class TransferStatus {
    Pending,
    Downloading,
    Paused,
    Completed,
    Failed,
    Cancelled
};

// 枚举值: PascalCase
enum class FileType {
    Unknown,
    Image,
    Video,
    Audio,
    Document
};

// 全局函数: PascalCase
QString generateShareToken();
QByteArray calculateFileHash(const QString& filePath);
```

### 1.3 QML 命名

```qml
// QML 文件名: PascalCase
HomePage.qml
FileListPage.qml
TransferPage.qml

// QML id: snake_case
Rectangle {
    id: main_container

    ListView {
        id: file_list_view

        delegate: Item {
            id: file_item_delegate
        }
    }

    ProgressBar {
        id: download_progress_bar
    }
}

// 信号: PascalCase
signal fileSelected(string filePath)
signal downloadStarted(string taskId)

// 属性: camelCase
property int maxParallelTasks: 3
property alias progress: download_progress_bar.value
```

### 1.4 数据库命名

```sql
-- 表名: snake_case, 复数形式
CREATE TABLE shares ();
CREATE TABLE download_sessions ();
CREATE TABLE transfer_logs ();

-- 列名: snake_case
CREATE TABLE shares (
    token TEXT PRIMARY KEY,
    share_type TEXT NOT NULL,
    file_path TEXT NOT NULL,
    created_at INTEGER NOT NULL
);

-- 索引名: idx_ + 表名_列名
CREATE INDEX idx_shares_token ON shares(token);
CREATE INDEX idx_logs_created_at ON transfer_logs(created_at);
```

---

## 2. 架构分层

### 2.1 分层原则

```
┌─────────────────────────────────────────────────────────────┐
│                    Presentation Layer (QML)                  │
│  - 界面展示                                                   │
│  - 用户交互                                                   │
│  - 数据绑定                                                   │
│  - 状态管理                                                   │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    Business Logic (C++)                       │
│  - 核心业务逻辑                                               │
│  - 数据处理                                                   │
│  - 算法实现                                                   │
│  - 跨平台兼容                                                │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    Data Access Layer (C++)                  │
│  - 数据库操作                                                 │
│  - 文件系统操作                                               │
│  - 缓存管理                                                  │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    Network Layer (C++)                       │
│  - TCP/UDP 通信                                              │
│  - HTTP/HTTPS 处理                                           │
│  - WebSocket                                                 │
│  - mDNS                                                      │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 QML 与 C++ 职责划分

#### 2.2.1 性能阈值（商用标准）

| 指标 | QML 处理上限 | 超过阈值处理方式 |
|------|-------------|----------------|
| **列表项数量** | ≤ 500 项 | C++ 分页模型 |
| **单文件大小** | ≤ 1MB (内存) | C++ 流式处理 |
| **图片尺寸** | ≤ 1920×1080 | C++ 缩略图生成 |
| **数据计算** | ≤ 10ms | C++ QtConcurrent |
| **文件遍历** | ≤ 100 文件 | C++ QThreadPool |
| **UI 响应** | ≤ 16ms (60fps) | 必须异步 |
| **内存占用** | ≤ 100MB 峰值 | C++ 分块处理 |

#### 2.2.2 QML 职责范围

| 适合 QML 处理 | 不适合 QML 处理 |
|-------------|---------------|
| ✅ UI 布局和动画 | ❌ 大文件 I/O ( > 1MB) |
| ✅ 用户输入验证 | ❌ 数据库查询 (> 100行) |
| ✅ 简单数据格式化 | ❌ 复杂计算 ( > 10ms) |
| ✅ 小列表展示 ( < 500) | ❌ 网络请求 |
| ✅ 页面导航和状态 | ❌ 文件系统操作 |
| ✅ 实时进度展示 | ❌ SHA256/压缩/加密 |

#### 2.2.3 C++ 职责范围

| 适合 C++ 处理 | QML 可直接调用 |
|-------------|--------------|
| ✅ 所有网络通信 | ✅ 通过信号获取结果 |
| ✅ 数据库 CRUD | ✅ 通过回调接收数据 |
| ✅ 文件压缩/解压 | ✅ QML 显示进度 |
| ✅ 大文件流式读写 | ✅ 分块加载展示 |
| ✅ 加密/解密 | ✅ 文件信息展示 |
| ✅ SHA256/MD5 | ✅ 日期格式化 |
| ✅ mDNS 服务 | ✅ 简单数学计算 |

#### 2.2.4 C++ 暴露给 QML 的规则

```cpp
// ✅ 推荐: C++ 类通过 QML_TYPE 暴露
class FileListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY loadingChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        PathRole,
        SizeRole,
        TypeRole
    };

    // 数据获取必须异步
    Q_INVOKABLE void loadFiles(const QString& path);
    Q_INVOKABLE void refresh();

signals:
    void countChanged();
    void loadingChanged();
    void loadFinished(bool success);
    void errorOccurred(const QString& message);

private:
    // C++ 内部处理，不暴露给 QML
    void loadFilesFromDisk(const QString& path);
    void sortAndFilter();
    QList<FileInfo> m_files;
};

// ✅ 推荐: 大数据使用分页
class TransferLogModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int page READ currentPage NOTIFY pageChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY hasMoreChanged)

public:
    Q_INVOKABLE void loadMore() {
        // C++ 分页加载，每页 50 条
        auto page = loadPageFromDb(++m_currentPage);
        beginInsertRows(QModelIndex(), rowCount(), rowCount() + page.size() - 1);
        m_logs.append(page);
        endInsertRows();
    }

    Q_INVOKABLE void refresh() {
        beginResetModel();
        m_logs.clear();
        m_currentPage = 0;
        endResetModel();
        loadMore();
    }
};

// ❌ 避免: 同步返回大数据
Q_INVOKABLE QList<FileInfo> getAllFiles() {
    // 危险！可能阻塞 UI
    return m_database.getAllFiles();  // 可能数万条
}

// ❌ 避免: 暴露内部实现
Q_PROPERTY(QMap<QString, ShareInfo> m_shareCache)  // 不要暴露容器
```

### 2.3 模块依赖规则

```
src/
├── core/           # 核心业务，不依赖其他业务模块
│   ├── FileTransferEngine.h/cpp
│   ├── ChunkManager.h/cpp
│   └── ShareManager.h/cpp
│
├── network/        # 依赖 core
│   ├── HttpServer.h/cpp
│   └── TcpServer.h/cpp
│
├── database/       # 依赖 core
│   └── DatabaseManager.h/cpp
│
├── qrcode/         # 独立工具模块
│   └── QRGenerator.h/cpp
│
└── qml/           # 依赖 core (通过 QML 绑定)
    ├── HomePage.qml
    └── FileListPage.qml
```

**依赖规则:**
- core 可被所有模块依赖
- network 依赖 core，不依赖 qml
- database 依赖 core
- qml 依赖 core 和 network

---

## 3. QML 规范

### 3.1 文件组织

```qml
// main.qml - 应用程序入口
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root

    // 1. 属性定义
    property string currentPage: "Home"
    property int taskCount: 0

    // 2. 信号定义
    signal pageChanged(string page)

    // 3. 状态组
    state: "default"
    states: [
        State { name: "default" },
        State { name: "loading" }
    ]

    // 4. 过渡动画
    transitions: [
        Transition {
            to: "loading"
            PropertyAnimation { target: loadingIndicator; property: "visible"; to: true }
        }
    ]

    // 5. 组件
    header: ToolBar { }
    StackView {
        id: mainStack
        initialItem: "qrc:/qml/HomePage.qml"
    }
    footer: TabBar { }
}
```

### 3.2 组件结构

```qml
// HomePage.qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/*
 * HomePage.qml
 * 首页组件，提供快速入口和状态展示
 */
Item {
    id: root

    // 2. 属性
    property alias computerName: nameLabel.text
    property bool isLoading: false
    property var sharedFolders: []

    // 3. 信号
    signal createShareClicked()
    signal folderSelected(string folderPath)

    // 4. 子组件属性
    width: 800
    height: 600

    // 5. 布局
    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        // 6. 组件
        Label {
            id: nameLabel
            text: qsTr("NetShare")
            font.pixelSize: 24
        }

        ListView {
            id: folderListView
            Layout.fillWidth: true
            Layout.fillHeight: true

            model: ListModel {
                id: folderModel
            }

            delegate: folderDelegate
        }

        Button {
            text: qsTr("Create Share")
            onClicked: root.createShareClicked()
        }
    }

    // 7. 组件定义 (放在文件末尾或单独文件)
    Component {
        id: folderDelegate
        Item {
            width: ListView.view.width
            height: 60

            Label { text: model.name }
        }
    }

    // 8. 状态
    states: [
        State {
            name: "loading"
            PropertyChanges { target: loadingIndicator; visible: true }
        }
    ]

    // 9. 动画
    transitions: [
        Transition {
            from: "loading"
            to: ""
            NumberAnimation { target: loadingIndicator; property: "opacity"; duration: 300 }
        }
    ]
}
```

### 3.3 性能要求（商用标准）

#### 3.3.1 响应时间要求

| 操作类型 | 最大响应时间 | 说明 |
|---------|------------|------|
| 页面首次加载 | ≤ 300ms | 首屏可见 |
| 列表滚动 | ≤ 16ms (60fps) | 每帧 |
| 按钮点击响应 | ≤ 50ms | 视觉反馈 |
| 数据加载完成 | ≤ 500ms | 数据可见 |
| 网络请求完成 | ≤ 2000ms | 超时提示 |

#### 3.3.2 内存限制

| 场景 | 最大内存 | 超过处理方式 |
|------|---------|------------|
| 单个页面 | ≤ 50MB | 组件卸载 |
| 列表项 | ≤ 1KB/项 | 只保存可见项 |
| 图片缓存 | ≤ 100MB | LRU 淘汰 |
| 总体 QML | ≤ 200MB | 分页加载 |

#### 3.3.3 QML 线程模型

```qml
/*
 * 线程安全原则:
 * 1. QML 主线程处理所有 UI 操作
 * 2. C++ 后台线程处理耗时操作
 * 3. 通过信号槽跨线程通信
 */

// ✅ 推荐: 耗时操作在 C++ 后台执行
Connections {
    target: transferEngine

    // 接收后台线程信号 (自动切换到主线程)
    onProgressChanged: {
        // ✅ 安全: 在主线程更新 UI
        progressBar.value = progress
    }

    onDownloadCompleted: {
        // ✅ 显示结果
        showNotification("Download completed")
    }

    onErrorOccurred: {
        // ✅ 显示错误
        errorDialog.show(error)
    }
}

// ✅ 推荐: WorkerScript 处理纯计算
WorkerScript {
    id: fileProcessor
    source: "file_processor.js"

    function processFiles(files) {
        // 在后台线程执行
        fileProcessor.sendMessage({ files: files })
    }

    onMessage: {
        // 回到主线程，更新 UI
        listModel.append(messageObject.results)
    }
}

// file_processor.js (后台线程)
WorkerScript.onMessage = function(message) {
    var results = [];
    for (var i = 0; i < message.files.length; i++) {
        // 处理文件 (不能操作 UI)
        results.push(computeFileInfo(message.files[i]))
    }
    WorkerScript.sendMessage({ results: results })
}

// ❌ 避免: 在 QML 中直接执行耗时操作
Button {
    onClicked: {
        // 危险! 会阻塞 UI
        var result = computeHash(largeFile)  // 可能需要几秒
    }
}

// ❌ 避免: 在 QML 中同步读取大文件
function loadLargeFile() {
    var xhr = new XMLHttpRequest()
    xhr.open("GET", "largefile.zip", false)  // 同步请求，阻塞 UI
    xhr.send()
}

// ✅ 推荐: 异步加载大文件
function loadLargeFileAsync() {
    var xhr = new XMLHttpRequest()
    xhr.open("GET", "largefile.zip", true)  // 异步
    xhr.onprogress: {
        // 显示进度，不阻塞 UI
        progressBar.value = event.loaded / event.total
    }
    xhr.onload: {
        // 处理完成
    }
    xhr.send()
}
```

### 3.4 QML 最佳实践

```qml
// ✅ 推荐: 使用布局
ColumnLayout {
    anchors.fill: parent
    spacing: 10

    Label { text: "Title" }
    ListView { Layout.fillWidth: true; Layout.fillHeight: true }
    Button { Layout.alignment: Qt.AlignHCenter }
}

// ❌ 避免: 固定坐标
Rectangle {
    x: 100; y: 200; width: 300; height: 400
}

// ✅ 推荐: 使用 Loader 延迟加载
Loader {
    id: settingsLoader
    source: "SettingsPage.qml"
    active: false  // 初始不加载
    onLoaded: item.visible = true
}

// 需要时再加载
Button {
    text: "Settings"
    onClicked: settingsLoader.active = true
}

// ❌ 避免: 深度嵌套
Rectangle {
    Rectangle {
        Rectangle {
            Rectangle {
                // 太深，难以维护
            }
        }
    }
}

// ✅ 推荐: 扁平化结构，使用组件分离
Item {
    Header { }
    Content { }
    Footer { }
}

// ✅ 推荐: 使用 ListModel 而非 Repeater 处理大量数据
ListView {
    model: ListModel {
        id: largeModel
        // 动态加载
    }
    delegate: Text { text: model.display }
}

// ❌ 避免: Repeater 处理大量数据
Repeater {
    model: 10000  // 性能问题
    delegate: Text { text: index }
}
```

### 3.5 QML vs C++ 决策树

```
┌─────────────────────────────────────────────────────────────────┐
│                    QML/C++ 职责决策流程                           │
└─────────────────────────────────────────────────────────────────┘

问题1: 这个功能涉及 UI 展示吗？
    │
    ├── 否 → 交给 C++ 处理
    │
    └── 是 → 问题2: 这个功能会阻塞 UI 吗？
                │
                ├── 计算量 > 10ms?
                │       │
                │       ├── 是 → C++ 后台线程 + QML 信号更新
                │       │
                │       └── 否 → 问题3
                │
                └── 数据量 > 阈值?
                        │
                        ├── 列表 > 500 项? → C++ 分页模型
                        ├── 文件 > 1MB? → C++ 流式处理
                        ├── 图片 > 1920px? → C++ 缩略图
                        │
                        └── 都在阈值内 → QML 处理

─────────────────────────────────────────────────────────────────

决策速查表:

| 功能场景 | 推荐实现 | 原因 |
|---------|---------|------|
| 列表展示 < 500 项 | QML ListView | 原生支持，流畅 |
| 列表展示 > 500 项 | C++ QAbstractListModel | 虚拟化，性能好 |
| 文件上传进度 | C++ 计算，QML 显示 | 需要后台 I/O |
| 文件下载进度 | C++ 计算，QML 显示 | 需要网络 I/O |
| SHA256 计算 | C++ QtConcurrent | CPU 密集 |
| 路径格式化 | QML 直接处理 | 简单计算 |
| 日期格式化 | C++ 或 QML 皆可 | - |
| JSON 解析 | C++ QJson | 大量数据更高效 |
| HTTP 请求 | C++ QNetworkAccessManager | 必须异步 |
| WebSocket | C++ QWebSocket | 需要长连接 |
| 数据库 CRUD | C++ QSql | 避免阻塞 UI |
| 文件系统扫描 | C++ QThreadPool | 耗时操作 |
| UI 动画 | QML PropertyAnimation | 声明式，更简单 |
| 正则表达式 | C++ QRegularExpression | 性能更好 |
| 压缩/解压 | C++ QZipWriter/Reader | CPU 密集 |
| 二维码生成 | C++ | 需要图像处理 |
| mDNS 发现 | C++ | 网络操作 |
```

### 3.6 C++ 后台任务模式

```cpp
// ✅ 推荐: 使用 QtConcurrent 处理后台任务
#include <QtConcurrent>

class FileHasher : public QObject {
    Q_OBJECT

public:
    static QFuture<QByteArray> hashFileAsync(const QString& filePath) {
        return QtConcurrent::run([filePath]() -> QByteArray {
            return hashFile(filePath);
        });
    }

    static QByteArray hashFile(const QString& filePath) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            return QByteArray();
        }

        QCryptographicHash hash(QCryptographicHash::Sha256);
        const int bufferSize = 64 * 1024;
        char buffer[bufferSize];

        while (!file.atEnd()) {
            qint64 bytesRead = file.read(buffer, bufferSize);
            hash.addData(buffer, bytesRead);
        }

        return hash.result();
    }
};

// ✅ 推荐: 使用 QThreadPool 处理文件遍历
class FolderScanner : public QObject {
    Q_OBJECT

public:
    explicit FolderScanner(QObject* parent = nullptr)
        : QObject(parent)
        , m_pool(new QThreadPool(this))
    {
        m_pool->setMaxThreadCount(4);
    }

    Q_INVOKABLE void scanFolderAsync(const QString& folderPath) {
        auto task = new ScanTask(folderPath);
        connect(task, &ScanTask::progress, this, &FolderScanner::onScanProgress);
        connect(task, &ScanTask::finished, this, &FolderScanner::onScanFinished);
        m_pool->start(task);
    }

signals:
    void progress(int filesFound);
    void finished(const QList<FileInfo>& files);

private:
    QThreadPool* m_pool;
};

// ✅ 推荐: 使用 QObject + QThread 处理网络请求
class NetworkRequestWorker : public QObject {
    Q_OBJECT

public:
    explicit NetworkRequestWorker(QObject* parent = nullptr)
        : QObject(parent)
        , m_thread(new QThread(this))
    {
        m_thread->start();
        moveToThread(m_thread);
    }

    ~NetworkRequestWorker() {
        m_thread->quit();
        m_thread->wait();
    }

public slots:
    void downloadFile(const QString& url, const QString& savePath);

signals:
    void downloadProgress(qint64 received, qint64 total);
    void downloadComplete(const QString& path);
    void downloadFailed(const QString& error);

private:
    QThread* m_thread;
};
```

### 4. C++ 规范

#### 4.1 类设计

```cpp
// ShareManager.h
#ifndef SHAREMANAGER_H
#define SHAREMANAGER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QSet>

class ShareInfo {
public:
    QString token;
    QString filePath;
    qint64 fileSize;
    QDateTime expiresAt;

    bool isValid() const;
    bool isExpired() const;
};

class ShareManager : public QObject
{
    Q_OBJECT

public:
    // 单例
    static ShareManager* instance();

    // 复制构造和赋值删除
    ShareManager(const ShareManager&) = delete;
    ShareManager& operator=(const ShareManager&) = delete;

    // 公开接口
    QString shareFile(const QString& filePath,
                     int expireHours = 24,
                     int maxDownloads = 0);

    ShareInfo getShareInfo(const QString& token) const;
    bool validateShare(const QString& token,
                      const QString& password = QString()) const;

    QList<ShareInfo> getActiveShares() const;

public slots:
    void cancelShare(const QString& token);
    void cleanupExpiredShares();

signals:
    void shareCreated(const QString& token);
    void shareCancelled(const QString& token);
    void shareAccessed(const QString& token);

private:
    explicit ShareManager(QObject* parent = nullptr);
    ~ShareManager() = default;

    QString generateToken() const;
    void saveToDatabase(const ShareInfo& info);
    ShareInfo loadFromDatabase(const QString& token) const;

    QMap<QString, ShareInfo> m_shareCache;
    QSet<QString> m_activeTokens;
};
#endif // SHAREMANAGER_H
```

### 4.2 信号与槽

```cpp
// ✅ 推荐: 使用 Q_SIGNALS 和 Q_SLOTS
class FileTransferEngine : public QObject
{
    Q_OBJECT

public:
    explicit FileTransferEngine(QObject* parent = nullptr);

    // 公开接口不使用 signals/slots
    void startDownload(const QString& taskId);
    void pauseDownload(const QString& taskId);
    void cancelDownload(const QString& taskId);

public slots:
    void onChunkCompleted(quint32 chunkIndex);
    void onTransferFinished();
    void onErrorOccurred(const QString& error);

signals:
    void progressChanged(const QString& taskId, qreal progress);
    void speedChanged(const QString& taskId, qint64 bytesPerSecond);
    void statusChanged(const QString& taskId, TransferStatus status);
    void downloadCompleted(const QString& taskId);
    void errorOccurred(const QString& taskId, const QString& error);

private:
    void processNextChunk();
    void verifyFileIntegrity();

    QString m_currentTaskId;
    qint64 m_totalBytes;
    qint64 m_downloadedBytes;
};

// ❌ 避免: 信号槽中使用复杂参数
signals:
    void dataReceived(QList<Map<String, QVariant>> data);  // 太复杂

// ✅ 推荐: 定义数据结构
struct TransferChunk {
    quint32 index;
    qint64 start;
    qint64 end;
    QByteArray data;
};

signals:
    void chunkReceived(const TransferChunk& chunk);
```

### 4.3 内存管理

```cpp
// ✅ 推荐: 使用智能指针管理堆对象
#include <QSharedPointer>
#include <QScopedPointer>

class FileCache : public QObject
{
public:
    using Ptr = QSharedPointer<FileCache>;

    static Ptr create() {
        return Ptr(new FileCache());
    }

private:
    explicit FileCache(QObject* parent = nullptr)
        : QObject(parent)
    {}

    QByteArray m_data;
};

// 使用
auto cache = FileCache::create();

// ✅ 推荐: QObject 使用 parent 所有权
class NetworkManager : public QObject
{
    Q_OBJECT

public:
    explicit NetworkManager(QObject* parent = nullptr)
        : QObject(parent)
        , m_tcpServer(new TcpServer(this))  // 父对象管理内存
        , m_httpServer(new HttpServer(this))
    {}

private:
    TcpServer* m_tcpServer;
    HttpServer* m_httpServer;
};

// ❌ 避免: 在堆上创建 QObject 但不设置父对象
NetworkManager* manager = new NetworkManager();  // 内存泄漏风险

// ✅ 推荐: 使用 QScopedPointer 处理局部堆对象
void processFile(const QString& path) {
    QScopedPointer<QFile> file(new QFile(path));
    if (!file->open(QIODevice::ReadOnly)) {
        return;
    }
    // 自动删除
}

// ✅ 推荐: 容器存储值而非指针
QList<ShareInfo> ShareManager::getActiveShares() const {
    return m_shareCache.values();  // 拷贝
}

// 如需指针，使用 QList<QSharedPointer<T>>
QList<FileCache::Ptr> getCaches() const;
```

### 4.4 错误处理

```cpp
// ✅ 推荐: 使用 Q_UNUSED 处理未使用参数
void ShareManager::cancelShare(const QString& token) {
    Q_UNUSED(token);
    // 实现
}

// ✅ 推荐: 使用 noexcept
class FileHasher {
public:
    static QByteArray hash(const QString& filePath) noexcept {
        try {
            // 计算 hash
            return result;
        } catch (...) {
            return QByteArray();
        }
    }
};

// ✅ 推荐: 返回 bool + 输出参数，而非异常
bool ShareManager::validateShare(const QString& token,
                                 const QString& password,
                                 ShareInfo* info) const {
    auto it = m_shareCache.find(token);
    if (it == m_shareCache.end()) {
        return false;
    }

    if (it->isExpired()) {
        return false;
    }

    if (!it->passwordHash.isEmpty() && it->passwordHash != password) {
        return false;
    }

    if (info) {
        *info = *it;
    }
    return true;
}

// ❌ 避免: 抛出异常
void processFile(const QString& path) {
    throw std::runtime_error("File not found");  // Qt 风格不使用异常
}
```

---

## 5. 页面加载规范

### 5.1 页面加载原则

| 原则 | 说明 | 阈值 |
|------|------|------|
| **延迟加载** | 非首屏组件使用 Loader | 首屏外组件 |
| **懒加载** | 数据按需加载，使用分页 | > 500 项 |
| **异步操作** | 耗时操作放在 Worker 线程 | > 10ms |
| **骨架屏** | 大数据列表显示骨架占位 | > 100 项 |
| **缓存** | 频繁访问的数据缓存 | - |
| **虚拟化** | 大量数据使用虚拟列表 | > 1000 项 |

### 5.2 数据量阈值（商用标准）

```cpp
// 数据处理阈值常量
namespace PerformanceThresholds {
    // 列表阈值
    static const int kMaxSyncListItems = 500;          // 同步加载最大项数
    static const int kPageSize = 50;                   // 分页每页大小
    static const int kPrefetchThreshold = 10;           // 预加载触发阈值

    // 文件阈值
    static const qint64 kMaxMemoryFileSize = 1 * 1024 * 1024;      // 1MB
    static const qint64 kMaxThumbnailSize = 5 * 1024 * 1024;       // 5MB
    static const int kMaxImageDimension = 1920;                     // 最大图片尺寸

    // 时间阈值
    static const int kMaxSyncOperationTime = 10;       // 10ms，超过必须异步
    static const int kMaxUIResponseTime = 16;          // 16ms (60fps)
    static const int kPageLoadTimeout = 3000;          // 页面加载超时 3s

    // 内存阈值
    static const int kMaxListItemMemory = 1024;        // 单项最大内存 1KB
    static const int kMaxImageCache = 100 * 1024 * 1024; // 图片缓存 100MB
    static const int kMaxQMLMemoryTotal = 200 * 1024 * 1024; // QML 总体 200MB

    // 网络阈值
    static const int kRequestTimeout = 30000;         // 请求超时 30s
    static const int kChunkSize = 4 * 1024 * 1024;    // 4MB chunk
}
```

### 5.3 QML 页面加载

```qml
// ✅ 推荐: 使用 Loader 延迟加载页面
StackView {
    id: mainStack

    // 首页立即加载
    initialItem: "qrc:/qml/HomePage.qml"

    // 其他页面延迟加载
    function loadTransferPage() {
        if (!transferLoader.item) {
            transferLoader.active = true;
        }
        mainStack.push(transferLoader);
    }

    function loadSettingsPage() {
        if (!settingsLoader.item) {
            settingsLoader.active = true;
        }
        mainStack.push(settingsLoader);
    }

    Loader {
        id: transferLoader
        source: "qrc:/qml/TransferPage.qml"
        active: false
        onLoaded: console.log("Transfer page loaded")
    }

    Loader {
        id: settingsLoader
        source: "qrc:/qml/SettingsPage.qml"
        active: false
    }
}

// ✅ 推荐: 大列表使用 ListView + 分页
ListView {
    id: fileListView
    model: fileListModel

    // 分页加载
    visibleArea.onAtEndChanged: {
        if (visibleArea.atEnd && model.hasMore) {
            model.loadMore();
        }
    }

    // 缓存
    cacheBuffer: 200  // 缓存 200 像素的内容
}

// ✅ 推荐: 模型分页
ListModel {
    id: fileListModel

    property int currentPage: 0
    property int pageSize: 50
    property bool hasMore: true
    property bool isLoading: false

    function loadMore() {
        if (isLoading || !hasMore) return;
        isLoading = true;

        // 异步加载
        CppBridge.loadFiles(currentPage, pageSize, function(files) {
            if (files.length < pageSize) {
                hasMore = false;
            }
            for (var i = 0; i < files.length; i++) {
                append(files[i]);
            }
            currentPage++;
            isLoading = false;
        });
    }

    function refresh() {
        clear();
        currentPage = 0;
        hasMore = true;
        loadMore();
    }
}
```

### 5.3 防止 UI 卡顿

```qml
// ✅ 推荐: 大量数据使用 ListView 而非 Grid/Column
ListView {
    // ListView 只渲染可见项
    // 适合大量数据
}

// ❌ 避免: Repeater 用于大量数据
Column {
    Repeater {
        model: 10000  // 性能问题
        delegate: Rectangle { }
    }
}

// ✅ 推荐: 使用 Item + positioner 显示固定数量
Grid {
    // 适合小数量固定布局
    rows: 3
    columns: 3
    spacing: 10

    Repeater {
        model: 9  // 固定数量
        delegate: Rectangle { }
    }
}

// ✅ 推荐: 图片懒加载
Image {
    source: model.thumbnailUrl
    asynchronous: true  // 异步加载
    visible: status === Image.Ready
}

PlaceholderImage {
    visible: parent.status !== Image.Ready
}

// ✅ 推荐: 动画使用属性动画而非脚本
Rectangle {
    id: animatedRect

    // ❌ 避免: 在 Timer 中直接修改位置
    Timer {
        interval: 16
        running: true
        onTriggered: animatedRect.x += 5  // 阻塞 UI
    }

    // ✅ 推荐: 使用 PropertyAnimation
    NumberAnimation {
        id: moveAnimation
        target: animatedRect
        property: "x"
        from: 0
        to: 400
        duration: 1000
        easing.type: Easing.InOutQuad
    }

    MouseArea {
        onClicked: moveAnimation.running = true
    }
}
```

### 5.4 Worker 线程

```cpp
// ✅ 推荐: 使用 QThreadPool + QtConcurrent 处理耗时任务
#include <QtConcurrent>

class FileHasher : public QObject
{
    Q_OBJECT

public:
    static QFuture<QByteArray> hashFileAsync(const QString& filePath) {
        return QtConcurrent::run([filePath]() -> QByteArray {
            return hashFile(filePath);
        });
    }

    static QByteArray hashFile(const QString& filePath) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            return QByteArray();
        }

        QCryptographicHash hash(QCryptographicHash::Sha256);
        const int bufferSize = 64 * 1024;
        char buffer[bufferSize];

        while (!file.atEnd()) {
            qint64 bytesRead = file.read(buffer, bufferSize);
            hash.addData(buffer, bytesRead);
        }

        return hash.result();
    }

private:
    explicit FileHasher() = default;
};

// QML 调用
Button {
    onClicked: {
        var future = FileHasher.hashFileAsync(filePath);
        future.then(function(hash) {
            console.log("Hash:", hash);
        });
    }
}

// ✅ 推荐: 大数据处理使用 QObject + QThread
class FileCompressor : public QObject
{
    Q_OBJECT

public:
    explicit FileCompressor(QObject* parent = nullptr)
        : QObject(parent)
        , m_thread(new QThread(this))
    {
        moveToThread(m_thread);
        m_thread->start();
    }

    ~FileCompressor() {
        m_thread->quit();
        m_thread->wait();
    }

public slots:
    void compressFolder(const QString& folderPath, const QString& outputPath) {
        // 在子线程执行
        QZipWriter writer(outputPath);

        QDir dir(folderPath);
        for (const QFileInfo& info : dir.entryInfoList()) {
            if (info.isFile()) {
                compressFile(info.filePath(), writer);
                emit progressChanged(info.filePath());
            }
        }

        writer.close();
        emit finished();
    }

signals:
    void progressChanged(const QString& currentFile);
    void finished();

private:
    void compressFile(const QString& filePath, QZipWriter& writer) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) return;

        QZipWriter::Entry entry;
        entry.filePath = filePath;
        writer.addEntry(entry, &file);

        file.close();
    }

    QThread* m_thread;
};
```

---

## 6. 资源管理

### 6.1 QML 资源路径

```qml
// ✅ 推荐: 使用 qrc:// 前缀
Image {
    source: "qrc:///icons/app.png"
}

Component.onCompleted: {
    console.log("qrc:///icons/app.png");
}

// ❌ 避免: 相对路径
Image {
    source: "icons/app.png"  // 可能找不到
}

// ✅ 推荐: 使用别名
// resources.qrc
// <qresource prefix="/">
//     <file alias="icons/app.png">images/app.png</file>
// </qresource>

Image {
    source: "qrc:/icons/app.png"  // 始终可找到
}
```

### 6.2 图像资源

```qml
// ✅ 推荐: 使用异步加载
Image {
    source: model.imageUrl
    asynchronous: true
    cache: true
}

// ✅ 推荐: 调整图像大小
Image {
    source: "large_image.jpg"
    sourceSize.width: 200
    sourceSize.height: 200
}

// ✅ 推荐: 占位图
Image {
    id: profileImage
    source: user.avatarUrl
    asynchronous: true

    Rectangle {
        anchors.fill: parent
        color: "#f0f0f0"
        visible: profileImage.status === Image.Loading
    }

    Rectangle {
        anchors.fill: parent
        color: "#e0e0e0"
        visible: profileImage.status === Image.Error
    }
}
```

### 6.3 字体资源

```qml
// ✅ 推荐: 使用系统字体
Label {
    font.family: "Microsoft YaHei"
    font.pixelSize: 14
}

// ✅ 推荐: 字体大小使用固定像素或 pt
Label {
    font.pixelSize: 14  // 推荐
    // 或
    font.pointSize: 10
}
```

---

## 7. 错误处理

### 7.1 错误码定义

```cpp
// ErrorCode.h
namespace ErrorCode {

enum Code {
    Success = 0,

    // 通用错误 (1000-1999)
    InvalidParameter = 1001,
    FileNotFound = 1002,
    PermissionDenied = 1003,
    DiskFull = 1004,
    NetworkError = 1005,
    Timeout = 1006,
    UnknownError = 1999,

    // 分享错误 (2000-2999)
    ShareNotFound = 2001,
    ShareExpired = 2002,
    ShareMaxDownloadsReached = 2003,
    SharePasswordRequired = 2004,
    SharePasswordWrong = 2005,

    // 传输错误 (3000-3999)
    TransferNotFound = 3001,
    TransferCancelled = 3002,
    TransferFailed = 3003,
    ChunkVerificationFailed = 3004,
    InsufficientSpace = 3005,

    // 上传错误 (4000-4999)
    UploadNotFound = 4001,
    UploadIncomplete = 4002,
    InvalidFileFormat = 4003
};

} // namespace ErrorCode

// ErrorResult.h
struct ErrorResult {
    ErrorCode::Code code;
    QString message;
    QString detail;

    bool isSuccess() const { return code == ErrorCode::Success; }

    static ErrorResult success() {
        return {ErrorCode::Success, QString(), QString()};
    }

    static ErrorResult fromCode(ErrorCode::Code code,
                               const QString& detail = QString()) {
        return {code, errorMessage(code), detail};
    }

private:
    static QString errorMessage(ErrorCode::Code code) {
        switch (code) {
            case ErrorCode::FileNotFound:
                return QStringLiteral("文件不存在");
            case ErrorCode::ShareExpired:
                return QStringLiteral("分享已过期");
            case ErrorCode::NetworkError:
                return QStringLiteral("网络错误");
            default:
                return QStringLiteral("未知错误");
        }
    }
};
```

### 7.2 C++ 错误处理

```cpp
// ✅ 推荐: 返回 ErrorResult
class ShareManager {
public:
    ErrorResult createShare(const QString& filePath,
                           const ShareOptions& options,
                           QString* token) {
        // 验证参数
        if (filePath.isEmpty()) {
            return ErrorResult::fromCode(ErrorCode::InvalidParameter,
                                         "filePath is empty");
        }

        // 检查文件存在
        if (!QFile::exists(filePath)) {
            return ErrorResult::fromCode(ErrorCode::FileNotFound,
                                         filePath);
        }

        // 创建分享
        QString newToken = generateToken();
        if (token) {
            *token = newToken;
        }

        return ErrorResult::success();
    }

    // 或使用异常 (Qt 风格不推荐，但可以使用)
    ErrorResult getShareInfo(const QString& token) const {
        auto it = m_shareCache.find(token);
        if (it == m_shareCache.end()) {
            return ErrorResult::fromCode(ErrorCode::ShareNotFound);
        }
        return ErrorResult::success(*it);
    }
};

// ✅ 推荐: QML 使用错误信号
signals:
    void errorOccurred(int code, const QString& message)

Connections {
    target: shareManager
    onErrorOccurred: {
        console.error("Error:", code, message);
        errorDialog.show(message);
    }
}
```

### 7.3 QML 错误展示

```qml
// ✅ 推荐: 统一的错误处理组件
Item {
    id: root

    // 错误提示
    Popup {
        id: errorPopup
        modal: true

        Column {
            spacing: 10
            Label {
                text: errorPopup.errorMessage
                wrapMode: Text.WordWrap
            }
            Button {
                text: "OK"
                onClicked: errorPopup.close()
            }
        }
    }

    function showError(code, message) {
        errorPopup.errorMessage = message;
        errorPopup.open();
    }

    // 使用
    Button {
        onClicked: {
            var result = shareManager.createShare(path);
            if (!result.success) {
                root.showError(result.code, result.message);
            }
        }
    }
}
```

---

## 8. 日志规范

### 8.1 日志级别

| 级别 | 用途 | 示例 |
|------|------|------|
| DEBUG | 开发调试 | `qDebug() << "Processing file:" << path;` |
| INFO | 一般信息 | `qInfo() << "Server started on port" << port;` |
| WARNING | 警告 | `qWarning() << "File not found, using default";` |
| CRITICAL | 严重错误 | `qCritical() << "Database connection failed";` |
| FATAL | 致命错误 | `qFatal() << "Unrecoverable error";` |

### 8.2 日志格式

```cpp
// ✅ 推荐: 结构化日志
qInfo() << "Share created"
        << "token:" << token
        << "file:" << filePath
        << "expires:" << expiresAt.toString();

qDebug() << "Chunk downloaded"
         << "taskId:" << taskId
         << "chunkIndex:" << chunkIndex
         << "speed:" << bytesPerSecond << "bytes/s";

qWarning() << "Retry attempt"
           << "attempt:" << attempt
           << "maxAttempts:" << maxAttempts;

qCritical() << "Transfer failed"
            << "taskId:" << taskId
            << "error:" << errorMessage;
```

### 8.3 日志宏定义

```cpp
// Logger.h
#ifndef LOGGER_H
#define LOGGER_H

#include <QLoggingCategory>
#include <QFile>
#include <QTextStream>
#include <QMutex>

class FileLogger {
public:
    static FileLogger& instance() {
        static FileLogger instance;
        return instance;
    }

    void init(const QString& logFilePath) {
        QMutexLocker locker(&m_mutex);
        m_file.setFileName(logFilePath);
        if (m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            m_stream.setDevice(&m_file);
        }
    }

    void log(QtMsgType type, const char* msg) {
        QMutexLocker locker(&m_mutex);
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        m_stream << timestamp << " [" << levelName(type) << "] " << msg << "\n";
        m_stream.flush();
    }

private:
    FileLogger() = default;

    QString levelName(QtMsgType type) {
        switch (type) {
            case QtDebugMsg: return "DEBUG";
            case QtInfoMsg: return "INFO";
            case QtWarningMsg: return "WARN";
            case QtCriticalMsg: return "ERROR";
            case QtFatalMsg: return "FATAL";
        }
        return "UNKNOWN";
    }

    QFile m_file;
    QTextStream m_stream;
    QMutex m_mutex;
};

// 使用
#define INIT_LOGGER(path) FileLogger::instance().init(path)
#define LOG_DEBUG(msg) qDebug() << msg
#define LOG_INFO(msg) qInfo() << msg
#define LOG_WARN(msg) qWarning() << msg
#define LOG_ERROR(msg) qCritical() << msg

#endif // LOGGER_H
```

### 8.4 QML 日志

```qml
// ✅ 推荐: 使用 console
console.log("User clicked button")
console.debug("File loaded:", filePath)
console.warn("Retry attempt:", attempt)
console.error("Network error:", errorCode)

// ❌ 避免: alert 用于调试
alert("Debug message")  // 会阻塞 UI
```

---

## 9. 数据库规范

### 9.1 数据库操作

```cpp
// ✅ 推荐: 使用 QSqlQuery 绑定参数
bool ShareManager::insertShare(const ShareInfo& info) {
    QSqlQuery query;
    query.prepare(
        "INSERT INTO shares (token, file_path, file_size, created_at, expires_at) "
        "VALUES (:token, :file_path, :file_size, :created_at, :expires_at)"
    );

    query.bindValue(":token", info.token);
    query.bindValue(":file_path", info.filePath);
    query.bindValue(":file_size", info.fileSize);
    query.bindValue(":created_at", info.createdAt.toSecsSinceEpoch());
    query.bindValue(":expires_at", info.expiresAt.toSecsSinceEpoch());

    return query.exec();
}

// ❌ 避免: 字符串拼接 SQL
QSqlQuery query;
query.exec("INSERT INTO shares VALUES ('" + token + "', ...)");  // SQL 注入风险

// ✅ 推荐: 事务操作
bool ShareManager::transactionalOperation() {
    db.transaction();

    try {
        // 操作1
        if (!operation1()) {
            db.rollback();
            return false;
        }

        // 操作2
        if (!operation2()) {
            db.rollback();
            return false;
        }

        db.commit();
        return true;
    } catch (...) {
        db.rollback();
        return false;
    }
}
```

### 9.2 数据模型

```cpp
// ✅ 推荐: 数据模型与数据库表对应
class ShareRecord {
public:
    QString token;
    QString filePath;
    qint64 fileSize;
    QDateTime createdAt;
    QDateTime expiresAt;

    static ShareRecord fromQuery(const QSqlQuery& query) {
        ShareRecord record;
        record.token = query.value("token").toString();
        record.filePath = query.value("file_path").toString();
        record.fileSize = query.value("file_size").toLongLong();
        record.createdAt = QDateTime::fromSecsSinceEpoch(
            query.value("created_at").toLongLong());
        record.expiresAt = QDateTime::fromSecsSinceEpoch(
            query.value("expires_at").toLongLong());
        return record;
    }

    bool isExpired() const {
        return QDateTime::currentDateTime() > expiresAt;
    }
};
```

---

## 10. 网络通信规范

### 10.1 请求处理

```cpp
// ✅ 推荐: 异步处理请求
class HttpConnection : public QObject {
    Q_OBJECT

public:
    explicit HttpConnection(qintptr socketDescriptor, QObject* parent = nullptr)
        : QObject(parent)
        , m_socket(new QTcpSocket(this))
    {
        m_socket->setSocketDescriptor(socketDescriptor);
        connect(m_socket, &QTcpSocket::readyRead, this, &HttpConnection::onReadyRead);
        connect(m_socket, &QTcpSocket::disconnected, this, &HttpConnection::deleteLater);
    }

private slots:
    void onReadyRead() {
        QByteArray data = m_socket->readAll();
        HttpRequest request = parseRequest(data);

        // 异步处理
        QtConcurrent::run([this, request]() {
            HttpResponse response = processRequest(request);
            sendResponse(response);
        });
    }

private:
    HttpResponse processRequest(const HttpRequest& request);
    void sendResponse(const HttpResponse& response);

    QTcpSocket* m_socket;
};
```

### 10.2 连接管理

```cpp
// ✅ 推荐: 使用连接池
class ConnectionPool {
public:
    static ConnectionPool& instance() {
        static ConnectionPool pool;
        return pool;
    }

    QSqlDatabase getConnection() {
        QMutexLocker locker(&m_mutex);
        if (!m_connections.isEmpty()) {
            return m_connections.dequeue();
        }
        return QSqlDatabase::addDatabase("QSQLITE", QUuid::createUuid().toString());
    }

    void returnConnection(QSqlDatabase db) {
        QMutexLocker locker(&m_mutex);
        m_connections.enqueue(db);
    }

private:
    ConnectionPool() = default;
    QMutex m_mutex;
    QQueue<QSqlDatabase> m_connections;
};

// ✅ 推荐: 心跳保活
class TcpConnection : public QObject {
    Q_OBJECT

public:
    explicit TcpConnection(qintptr descriptor, QObject* parent = nullptr)
        : QObject(parent)
    {
        m_socket = new QTcpSocket(this);
        m_socket->setSocketDescriptor(descriptor);

        // 心跳
        m_heartbeatTimer = new QTimer(this);
        connect(m_heartbeatTimer, &QTimer::timeout, this, &TcpConnection::sendHeartbeat);
        m_heartbeatTimer->start(30000);  // 30秒
    }

private slots:
    void sendHeartbeat() {
        if (m_socket->state() == QAbstractSocket::ConnectedState) {
            m_socket->write("PING\r\n");
        }
    }

private:
    QTcpSocket* m_socket;
    QTimer* m_heartbeatTimer;
};
```

### 10.3 协议设计

```cpp
// ✅ 推荐: 使用结构化协议
struct TransferHeader {
    quint32 magic;        // 0x4E53 ("NS")
    quint8 version;      // 协议版本
    quint8 type;         // 消息类型
    quint32 length;      // 载荷长度
    quint32 checksum;    // 校验和

    static const quint32 kMagic = 0x4E53;
};

enum class MessageType : quint8 {
    Handshake = 0x01,
    FileInfo = 0x02,
    ChunkRequest = 0x03,
    ChunkData = 0x04,
    ChunkComplete = 0x05,
    ResumeQuery = 0x06,
    ResumeResponse = 0x07,
    TransferComplete = 0x08,
    Cancel = 0x09,
    Error = 0xFF
};

// 序列化
QByteArray serialize(const TransferHeader& header) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << header.magic
           << header.version
           << header.type
           << header.length
           << header.checksum;
    return data;
}
```

---

## 11. 安全规范

### 11.1 输入验证

```cpp
// ✅ 推荐: 验证所有输入
class ShareManager {
public:
    ErrorResult createShare(const QString& inputPath,
                           int expireHours,
                           int maxDownloads) {
        // 验证路径
        if (inputPath.isEmpty()) {
            return ErrorResult::fromCode(ErrorCode::InvalidParameter,
                                         "Path is empty");
        }

        // 验证路径格式
        QFileInfo fileInfo(inputPath);
        if (!fileInfo.exists()) {
            return ErrorResult::fromCode(ErrorCode::FileNotFound);
        }

        // 防止路径遍历
        if (inputPath.contains("..")) {
            return ErrorResult::fromCode(ErrorCode::PermissionDenied,
                                         "Invalid path");
        }

        // 验证数值范围
        if (expireHours < 0 || expireHours > 8760) {  // 最大1年
            return ErrorResult::fromCode(ErrorCode::InvalidParameter,
                                         "Invalid expire hours");
        }

        // 验证文件名
        if (!isValidFileName(fileInfo.fileName())) {
            return ErrorResult::fromCode(ErrorCode::InvalidFileFormat);
        }

        return success;
    }

private:
    bool isValidFileName(const QString& name) {
        // 检查非法字符
        static const QSet<QChar> invalidChars = {'<', '>', ':', '"', '|', '?', '*'};
        for (const QChar& c : name) {
            if (invalidChars.contains(c)) {
                return false;
            }
        }
        return true;
    }
};
```

### 11.2 密码处理

```cpp
// ✅ 推荐: 使用密码哈希
#include <QCryptographicHash>

class PasswordHasher {
public:
    static QString hash(const QString& password, const QString& salt) {
        // 使用 PBKDF2 或 Argon2
        QByteArray data = password.toUtf8() + salt.toUtf8();
        QByteArray hash = QCryptographicHash::hash(
            data, QCryptographicHash::Sha256);

        // 多次迭代
        for (int i = 0; i < 10000; i++) {
            hash = QCryptographicHash::hash(hash + data,
                                           QCryptographicHash::Sha256);
        }

        return hash.toBase64();
    }

    static QString generateSalt() {
        return QString::fromLatin1(
            QCryptographicHash::hash(
                QByteArray::number(QDateTime::currentMSecsSinceEpoch()),
                QCryptographicHash::Md5
            ).toHex()
        );
    }

    static bool verify(const QString& password,
                       const QString& hash,
                       const QString& salt) {
        return hash(password, salt) == hash;
    }
};
```

### 11.3 TLS 配置

```cpp
// ✅ 推荐: 安全 TLS 配置
QSslConfiguration getSecureConfiguration() {
    QSslConfiguration config = QSslConfiguration::defaultConfiguration();

    // 设置协议版本
    config.setProtocol(QSsl::TlsV1_2OrLater);

    // 设置加密套件
    QStringList cipherSuites = {
        "ECDHE-RSA-AES256-GCM-SHA384",
        "ECDHE-RSA-AES128-GCM-SHA256",
        "DHE-RSA-AES256-GCM-SHA384"
    };
    config.setCiphers(cipherSuites);

    // 验证证书
    config.setPeerVerifyMode(QSslSocket::VerifyPeer);

    return config;
}
```

---

## 12. 测试规范

### 12.1 单元测试

```cpp
// tests/TestShareManager.cpp
#include <QtTest>
#include "ShareManager.h"

class TestShareManager : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        m_manager = ShareManager::instance();
    }

    void testCreateShare() {
        QString token = m_manager->shareFile("/test/file.txt");
        QVERIFY(!token.isEmpty());
        QVERIFY(token.length() == 36);  // UUID 长度
    }

    void testGetShareInfo() {
        QString token = m_manager->shareFile("/test/file.txt", 24);

        ShareInfo info = m_manager->getShareInfo(token);
        QVERIFY(info.isValid());
        QVERIFY(info.token == token);
        QVERIFY(info.filePath == "/test/file.txt");
    }

    void testExpiredShare() {
        QString token = m_manager->shareFile("/test/file.txt", 0);  // 已过期

        QTest::qWait(100);  // 等待过期

        ShareInfo info = m_manager->getShareInfo(token);
        QVERIFY(info.isExpired());
    }

    void testCancelShare() {
        QString token = m_manager->shareFile("/test/file.txt");

        m_manager->cancelShare(token);

        ShareInfo info = m_manager->getShareInfo(token);
        QVERIFY(!info.isValid());
    }

private:
    ShareManager* m_manager;
};

QTEST_MAIN(TestShareManager)
#include "TestShareManager.moc"
```

### 12.2 QML 测试

```qml
// tests/TestHomePage.qml
import QtQuick
import QtTest

Item {
    id: root

    HomePage {
        id: homePage
    }

    SignalSpy {
        id: spy
        target: homePage
        signalName: "createShareClicked"
    }

    TestCase {
        name: "HomePage Test"

        function test_createShareButton() {
            compare(spy.count, 0)
            homePage.createShareClicked()
            compare(spy.count, 1)
        }
    }
}
```

---

## 13. Git 提交规范

### 13.1 提交信息格式

```
<type>(<scope>): <subject>

<body>

<footer>
```

### 13.2 Type 类型

| Type | 说明 |
|------|------|
| feat | 新功能 |
| fix | Bug 修复 |
| docs | 文档更新 |
| style | 代码格式 (不影响功能) |
| refactor | 重构 |
| perf | 性能优化 |
| test | 测试相关 |
| chore | 构建/工具 |

### 13.3 示例

```
feat(share): 添加二维码分享功能

- 添加 ShareManager 类
- 实现二维码生成
- 添加分享页面 UI

Closes #123
```

```
fix(transfer): 修复大文件下载时内存溢出

- 使用流式读取替代一次性加载
- 添加分块下载机制

Fixes #456
```

### 13.4 分支命名

```
feature/share-qrcode
feature/multi-thread-download
bugfix/transfer-memory
hotfix/security-token
release/v1.0.0
```

---

## 附录: 速查表

### 命名速查

| 元素 | 格式 | 示例 |
|------|------|------|
| 类名 | PascalCase | `ShareManager` |
| 函数 | PascalCase | `createShare()` |
| 变量 | camelCase | `filePath` |
| 常量 | kName | `kMaxRetries` |
| 私有成员 | m_name | `m_token` |
| QML id | snake_case | `file_list_view` |
| QML 文件 | PascalCase | `HomePage.qml` |
| 数据库表 | snake_case | `share_tokens` |
| 数据库列 | snake_case | `created_at` |

### 文件结构速查

```
src/
├── main.cpp              # 入口
├── core/                 # 业务逻辑
├── network/              # 网络
├── database/            # 数据库
├── qrcode/               # 工具
└── qml/                  # 界面

tests/
├── tst_*.cpp            # C++ 测试
└── Test*.qml            # QML 测试
```

---

**规范版本**: 1.0
**最后更新**: 2026-04-25
