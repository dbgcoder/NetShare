# NetShare

基于 Qt6 的局域网文件共享工具，支持文件/文件夹分享、分块传输、断点续传和 Web 端访问。

## 功能特性

- **文件/文件夹分享** — 创建分享链接，局域网内任意设备通过浏览器即可访问
- **分块传输** — 大文件自动分块并发传输，充分利用带宽
- **断点续传** — 支持下载中断后从断点续传，不重复下载
- **mDNS 服务发现** — 自动注册 mDNS 服务，无需记忆 IP 地址
- **实时进度推送** — WebSocket 实时推送传输进度到浏览器
- **双端界面** — 提供 QML 桌面 GUI 和 Web 浏览器双界面
- **二维码分享** — 生成的分享链接自动生成二维码，扫码即访

## 技术栈

| 类别 | 技术 |
|------|------|
| 语言 | C++17 |
| GUI 框架 | Qt 6.8 (Quick/QML + Widgets) |
| 构建系统 | CMake 3.30+ |
| 网络 | QTcpServer, QWebSocket, mDNS |
| 数据库 | SQLite |
| 二维码 | qrcodegen + QZXing |

## 快速开始

### 环境要求

- Windows 10+ / Linux / macOS
- Qt 6.8+
- CMake 3.30+
- 编译器：MSVC 2022 (Windows) / GCC 11+ (Linux) / Clang 14+ (macOS)

### 构建

```bash
# 克隆项目
git clone https://github.com/your/NetShare.git
cd NetShare

# CMake 配置
cmake -S . -B build -G Ninja

# 编译
cmake --build build

# 运行
./build/bin/NetShare.exe
```

### 使用说明

1. 启动 NetShare，程序自动在系统托盘运行
2. 点击"创建分享"，选择要分享的文件或文件夹
3. 局域网内其他设备打开浏览器，访问显示的 URL 或扫码二维码
4. 在浏览器中浏览、下载分享的文件
5. 如需接收文件，在浏览器上传页面选择文件上传

### 配置

配置文件位于 `%LOCALAPPDATA%/NetShare/config.ini`（Windows）或 `~/.config/NetShare/config.ini`（Linux/macOS）。

| 配置键 | 默认值 | 说明 |
|--------|--------|------|
| `Network/Port` | 8080 | HTTP 服务器端口 |
| `Network/BindAddress` | 0.0.0.0 | 绑定地址 |
| `Network/MaxConnections` | 10 | 最大并发连接数 |
| `Network/MaxBandwidth` | 0 | 带宽限制（字节/秒，0 表示不限） |
| `Security/AllowUpload` | true | 是否允许浏览器端上传 |
| `server/tlsEnabled` | false | 是否启用 TLS |
| `server/httpsPort` | 8443 | HTTPS 端口 |
| `advanced/mDNSServiceName` | NetShare | mDNS 服务名称 |

## 目录结构

```
NetShare/
├── CMakeLists.txt          # 顶层构建配置
├── cmake/                  # CMake 模块
├── src/                    # 源代码
│   ├── main.cpp            # 应用入口
│   ├── core/               # 核心业务逻辑
│   │   ├── common/         # 通用工具
│   │   ├── share/          # 分享管理
│   │   ├── transfer/       # 传输引擎
│   │   └── notification/   # 通知系统
│   ├── database/           # 数据库层
│   ├── network/            # 网络层（HTTP/WebSocket/mDNS）
│   ├── qrcode/             # 二维码生成
│   └── gui/                # GUI 层
│       └── qml/            # QML 界面文件
├── tests/                  # 测试模块
├── web/                    # Web 端静态资源
└── .gitignore
```

## 开发规范

本项目遵循 `.comate/rules/breakpoint-resume-progress.mdr` 中定义的断点续传开发规范。

## 许可

MIT License