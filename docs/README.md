# NetShare

**局域网文件分享工具** - 简单、快速、安全

[![Platform](https://img.shields.io/badge/Platform-Windows-blue.svg)](https://github.com/netshare)
[![Qt](https://img.shields.io/badge/Qt-6.8.3-purple.svg)](https://www.qt.io/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

---

## 📖 目录

- [特性](#-特性)
- [快速开始](#-快速开始)
- [安装](#-安装)
- [使用方法](#-使用方法)
- [系统要求](#-系统要求)
- [技术架构](#-技术架构)
- [目录结构](#-目录结构)
- [常见问题](#-常见问题)
- [贡献](#-贡献)
- [许可证](#-许可证)

---

## ✨ 特性

### 核心功能
- 📱 **扫码分享** - 电脑端生成二维码，手机扫码即可下载
- 📁 **文件夹分享** - 支持文件夹打包下载 (ZIP)
- ⏸️ **断点续传** - 网络中断后可继续下载
- 📥 **大文件支持** - 支持 30GB+ 单文件传输
- 🔢 **多线程下载** - 单文件多线程并行下载
- 📤 **手机上传** - 手机浏览器直接上传文件到电脑
- 🌐 **多设备支持** - 电脑、手机、平板均可访问

### 性能特性
- ⚡ **高速传输** - 多线程并行下载，充分利用带宽
- 🔄 **并行任务** - 支持多个文件同时下载
- 📊 **带宽控制** - 可设置上传/下载速度限制
- ⏰ **时段限速** - 支持夜间自动限速

### 安全特性
- 🔐 **密码保护** - 分享链接可设置访问密码
- 🔒 **TLS 加密** - 支持 HTTPS 安全传输
- ⏱️ **有效期控制** - 分享链接可设置过期时间
- 📊 **下载限制** - 可限制分享链接的下载次数

### 网络特性
- 🔍 **自动发现** - 支持 mDNS 局域网自动发现电脑
- 📡 **跨平台访问** - 支持 HTTP/HTTPS 访问
- 📝 **传输日志** - 完整的传输记录和统计

---

## 🚀 快速开始

### 1. 下载安装

从 [Releases](https://github.com/netshare/releases) 下载最新版本：

```
NetShare-1.0.0-Windows-x64.exe
```

### 2. 启动应用

双击运行 `NetShare.exe`，启动后会在系统托盘显示图标。

### 3. 创建分享

1. 点击桌面客户端的「创建分享」按钮
2. 选择要分享的文件或文件夹
3. 设置分享选项（有效期、密码等）
4. 点击「创建」，显示二维码

### 4. 手机扫码下载

1. 打开手机相机或微信/支付宝扫码
2. 点击打开链接，进入下载页面
3. 点击下载按钮，保存文件

---

## 📥 安装

### Windows 安装

#### 方式一：安装包 (推荐)

1. 下载 `NetShare-Setup-x.x.x.exe`
2. 双击运行安装向导
3. 选择安装目录
4. 完成安装

#### 方式二：便携版

1. 下载 `NetShare-x.x.x-Portable.zip`
2. 解压到任意目录
3. 直接运行 `NetShare.exe`

### 从源码构建

详见 [BUILD.md](BUILD.md)

---

## 📖 使用方法

### 桌面客户端

#### 创建文件分享

```
1. 点击「文件列表」页面的文件
2. 点击「分享」按钮
3. 设置有效期和密码（可选）
4. 点击「创建分享」
5. 扫描显示的二维码
```

#### 创建文件夹分享

```
1. 选择要分享的文件夹
2. 点击「分享文件夹」
3. 选择打包格式（ZIP 或分卷压缩）
4. 创建分享并扫描二维码
```

#### 查看传输状态

```
1. 切换到「传输」页面
2. 查看下载/上传任务列表
3. 支持暂停、继续、取消操作
```

### 手机浏览器端

#### 下载文件

```
1. 使用相机扫码或点击分享链接
2. 进入下载页面
3. 点击「开始下载」或「继续下载」
4. 等待下载完成
```

#### 上传文件

```
1. 点击「上传」按钮
2. 选择文件或拖拽到上传区域
3. 选择上传目标文件夹
4. 等待上传完成
```

#### 浏览共享文件

```
1. 点击「文件浏览」
2. 进入共享文件夹目录
3. 点击文件可直接预览
4. 点击文件夹可进入子目录
```

---

## 💻 系统要求

### 最低要求

| 组件 | 要求 |
|------|------|
| 操作系统 | Windows 10 (1809+) |
| 内存 | 4 GB |
| 磁盘空间 | 200 MB |
| 网络 | 100 Mbps 以太网 |

### 推荐配置

| 组件 | 推荐 |
|------|------|
| 操作系统 | Windows 11 |
| 内存 | 8 GB+ |
| 磁盘空间 | 1 GB+ |
| 网络 | 1 Gbps 以太网 |

### 手机端要求

| 组件 | 要求 |
|------|------|
| 浏览器 | Chrome 90+ / Safari 14+ / Edge 90+ |
| 网络 | 与电脑在同一局域网 |

---

## 🏗️ 技术架构

### 技术栈

| 组件 | 技术 |
|------|------|
| 框架 | Qt 6.8.3 |
| 构建系统 | CMake 3.30.5+ |
| UI | Qt Quick (QML) |
| 网络 | Qt Network (TCP/HTTP/WebSocket) |
| 数据库 | SQLite 3 |
| 安全 | TLS 1.2+, SHA256 |

### 架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                         NetShare                                 │
├─────────────────────────────────────────────────────────────────┤
│                                                                   │
│  ┌──────────────────┐      ┌──────────────────┐                │
│  │   QML UI Layer   │      │   Web Frontend   │                │
│  │                  │      │                  │                │
│  │  - HomePage     │      │  - Browse        │                │
│  │  - FileList     │      │  - Download      │                │
│  │  - Transfer     │      │  - Upload        │                │
│  │  - Settings     │      │  - Preview       │                │
│  └────────┬─────────┘      └────────┬─────────┘                │
│           │                           │                           │
│           └──────────┬────────────────┘                           │
│                      ▼                                            │
│  ┌──────────────────────────────────────────┐                    │
│  │            Business Logic (C++)           │                    │
│  │                                            │                    │
│  │  - ShareManager (分享管理)                 │                    │
│  │  - FileTransferEngine (传输引擎)           │                    │
│  │  - ChunkManager (分块管理)                  │                    │
│  │  - BandwidthManager (带宽控制)              │                    │
│  │  - TransferLogService (日志服务)            │                    │
│  └──────────────────────────────────────────┘                    │
│                      │                                            │
│                      ▼                                            │
│  ┌──────────────────────────────────────────┐                    │
│  │            Network Layer (C++)             │                    │
│  │                                            │                    │
│  │  - HttpServer (HTTP 服务器)               │                    │
│  │  - HttpsServer (HTTPS 服务器)             │                    │
│  │  - WebSocketHandler (WebSocket)            │                    │
│  │  - mDNSService (服务发现)                   │                    │
│  └──────────────────────────────────────────┘                    │
│                      │                                            │
│                      ▼                                            │
│  ┌──────────────────────────────────────────┐                    │
│  │            Data Layer (C++)               │                    │
│  │                                            │                    │
│  │  - DatabaseManager (数据库)               │                    │
│  │  - FileBrowser (文件浏览)                  │                    │
│  │  - QRGenerator (二维码生成)                │                    │
│  └──────────────────────────────────────────┘                    │
│                                                                   │
└─────────────────────────────────────────────────────────────────┘
```

---

## 📁 目录结构

```
NetShare/
├── src/
│   ├── main.cpp                    # 应用程序入口
│   ├── core/                       # 核心业务逻辑
│   │   ├── ShareManager.h/cpp       # 分享管理
│   │   ├── FileTransferEngine.h/cpp # 文件传输引擎
│   │   ├── ChunkManager.h/cpp       # 分块管理
│   │   ├── ResumeManager.h/cpp     # 断点续传
│   │   ├── UploadHandler.h/cpp     # 上传处理
│   │   ├── FolderPacker.h/cpp      # 文件夹打包
│   │   ├── BandwidthManager.h/cpp  # 带宽管理
│   │   ├── TransferLogService.h/cpp # 日志服务
│   │   └── FileBrowser.h/cpp       # 文件浏览
│   │
│   ├── network/                    # 网络层
│   │   ├── HttpServer.h/cpp         # HTTP 服务器
│   │   ├── HttpsServer.h/cpp       # HTTPS 服务器
│   │   ├── WebSocketHandler.h/cpp  # WebSocket 处理
│   │   └── mDNSService.h/cpp       # mDNS 服务
│   │
│   ├── database/                   # 数据库层
│   │   └── DatabaseManager.h/cpp   # 数据库管理
│   │
│   ├── qrcode/                    # 二维码模块
│   │   └── QRGenerator.h/cpp      # 二维码生成
│   │
│   └── qml/                       # QML 界面
│       ├── main.qml                # 主界面
│       ├── HomePage.qml            # 首页
│       ├── FileListPage.qml       # 文件列表
│       ├── TransferPage.qml       # 传输页面
│       ├── SharePage.qml          # 分享页面
│       └── SettingsPage.qml       # 设置页面
│
├── web/                           # Web 前端
│   ├── index.html                 # 主页
│   ├── css/                       # 样式表
│   ├── js/                        # JavaScript
│   └── pages/                     # 页面
│
├── tests/                         # 测试
│   ├── tst_sharemanager.cpp       # 分享管理测试
│   ├── tst_transfer.cpp           # 传输测试
│   └── tst_network.cpp            # 网络测试
│
├── CMakeLists.txt                 # CMake 配置
├── BUILD.md                       # 构建指南
├── CONFIG.md                      # 配置说明
├── ERROR_CODES.md                 # 错误码定义
├── API.md                         # API 文档
└── LICENSE                        # 许可证
```

---

## ❓ 常见问题

### Q: 手机无法扫码访问？

**A:** 请确保：
1. 手机和电脑在同一 WiFi 网络
2. 电脑防火墙允许 NetShare 通过
3. 尝试手动输入电脑 IP 地址访问

### Q: 下载速度慢？

**A:** 可以尝试：
1. 增加并行下载线程数
2. 检查网络环境（建议使用有线网络）
3. 调整带宽限制设置

### Q: 大文件下载失败？

**A:** NetShare 支持 30GB+ 文件，但请确保：
1. 磁盘空间充足
2. 网络稳定
3. 开启断点续传功能

### Q: 如何设置访问密码？

**A:**
1. 创建分享时勾选「密码保护」
2. 输入密码
3. 分享创建后，扫码需要输入密码才能访问

### Q: 二维码过期了怎么办？

**A:**
1. 重新创建分享即可
2. 可设置永不过期避免此问题

---

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

### 开发环境

- Qt 6.8.3+
- CMake 3.30.5+
- C++17 编译器

### 构建

```bash
# 克隆代码
git clone https://github.com/netshare/netshare.git
cd netshare

# 创建构建目录
mkdir build && cd build

# 配置
cmake .. -G "MinGW Makefiles"  # Windows
cmake .. -G "Ninja"            # Linux

# 构建
cmake --build . --parallel

# 运行
./NetShare
```

---

## 📄 许可证

本项目基于 MIT 许可证开源，详见 [LICENSE](LICENSE) 文件。

---

## 📞 联系方式

- GitHub Issues: [https://github.com/netshare/netshare/issues](https://github.com/netshare/netshare/issues)
- 邮箱: support@netshare.example.com

---

**NetShare** - 让局域网文件分享变得简单 🚀
