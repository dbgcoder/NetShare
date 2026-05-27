# NetShare 构建指南

本文档详细说明如何从源码构建 NetShare 项目。

---

## 📋 目录

- [环境要求](#环境要求)
- [Windows 构建](#windows-构建)
- [依赖安装](#依赖安装)
- [获取源码](#获取源码)
- [构建步骤](#构建步骤)
- [常见问题](#常见问题)
- [发布版本](#发布版本)

---

## 环境要求

### 必需组件

| 组件 | 版本要求 | 说明 |
|------|---------|------|
| **Qt** | 6.8.3+ | 跨平台 GUI 框架 |
| **Qt Creator** | 18.0.2+ | IDE |
| **CDB Debugger** | 最新版 | 调试器 |
| **Debugging Tools** | for Windows | 调试工具 |
| **CMake** | 3.30.5+ | 构建系统 |
| **Ninja** | 1.12.1+ | 构建工具 |

### 可选组件

| 组件 | 版本要求 | 说明 |
|------|---------|------|
| **OpenSSL** | 1.1.1+ | TLS/SSL 支持（自动生成自签证书） |
| **SQLite** | 3.x | 数据库 (Qt 自带) |

### 内置第三方库

| 库 | 说明 | 位置 |
|----|------|------|
| **CivetWeb** | 嵌入式 HTTP/HTTPS/WebSocket 服务器 | `third_party/civetweb_src/` |
| **Boost.DI** | 编译期依赖注入框架 (header-only) | `third_party/boost-di/` |

---

## Windows 构建

### 方式一：使用 Qt Creator (推荐)

#### 1. 安装 Qt

1. 下载 [Qt Online Installer](https://www.qt.io/download-qt-installer)
2. 运行安装程序
3. 选择组件：
   - Qt 6.8.3
   - Qt Creator (IDE)
   - MSVC 2022 64-bit 或 MinGW 11 64-bit
   - Additional Libraries:
     - Qt Network
     - Qt QML
     - Qt Quick
     - Qt SQL

#### 2. 打开项目

1. 启动 Qt Creator
2. 点击「文件」→「打开文件或项目」
3. 选择 `NetShare/CMakeLists.txt`
4. 选择 kit (MSVC 2022 或 MinGW)

#### 3. 配置构建

1. 点击「项目」→「构建设置」
2. 构建目录: `build/release`
3. CMake 变量:
   - `CMAKE_BUILD_TYPE`: Release
   - `CMAKE_PREFIX_PATH`: `<Qt 安装目录>/lib/cmake`

#### 4. 构建运行

1. 点击「构建」→「构建项目」
2. 或按 `Ctrl+B`
3. 构建完成后，点击「运行`

---

### 方式二：命令行构建

#### 1. 安装依赖

```powershell
# 使用 winget 安装 (Windows 10/11)
winget install Qt.Qt6.8.3
winget install Ninja-build.Ninja
winget install Microsoft.VisualStudio.2022.BuildTools

# 或下载安装包
# Qt: https://www.qt.io/download
# CMake: https://cmake.org/download
# Ninja: https://ninja-build.org
```

#### 2. 配置环境变量

```powershell
# 设置 Qt 路径 (根据实际安装位置调整)
$env:QTDIR = "C:\Qt\6.8.3\msvc2022_64"
$env:PATH = "$env:QTDIR\bin;$env:PATH"
$env:CMAKE_PREFIX_PATH = "$env:QTDIR\lib\cmake"
```

---

## 依赖安装

### Qt 安装 (详细)

1. **下载 Qt 安装程序**
   ```
   https://www.qt.io/download-qt-installer
   ```

2. **运行安装程序**
   ```powershell
   qt-unified-windows-x64-4.6.0-online.exe
   ```

3. **选择安装组件**
   ```
   Qt 6.8.3
   ├── Qt Core
   ├── Qt Network
   ├── Qt QML
   ├── Qt Quick
   ├── Qt Quick Controls
   ├── Qt SQL
   └── Qt 6.8.3 Src (可选，查看源码)

   Tools
   ├── Qt Creator 18.0.2
   ├── MinGW 11.2.0 64-bit
   └── CMake 3.30.5
   ```

4. **验证安装**
   ```powershell
   qmake --version
   # 输出: QMake version 3.1 ... using Qt version 6.8.3
   ```

### CMake 安装

```powershell
# 使用 winget
winget install Kitware.CMake

# 或下载安装包
# https://cmake.org/download/
# 选择 Windows x64 Installer
```

验证:
```powershell
cmake --version
# 输出: cmake version 3.28.x
```

### Ninja 安装 (可选)

```powershell
# 使用 winget
winget install Ninja-build.Ninja

# 验证
ninja --version
```

### Visual Studio Build Tools

如果使用 MSVC 编译器:

```powershell
# 下载 Visual Studio Build Tools
https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022

# 安装时选择
# - "使用 C++ 的桌面开发"
# - MSVC v143 - VS 2022 C++ x64/x86 生成工具
# - Windows 11 SDK
```

---

## 获取源码

### 克隆仓库

```bash
git clone https://github.com/netshare/netshare.git
cd netshare
```

### 切换分支

```bash
# 查看所有分支
git branch -a

# 切换到稳定版本
git checkout v1.0.0

# 或使用最新开发版
git checkout main
```

---

## 构建步骤

### 标准构建流程

```bash
# 1. 创建构建目录
mkdir build
cd build

# 2. 配置 CMake
cmake .. ^
    -G "Ninja" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64" ^
    -DQT_VERSION=6.8.3

# 3. 构建
cmake --build . --parallel

# 4. 安装 (可选)
cmake --install .
```

### 使用 Qt Creator 构建

```
1. 打开 Qt Creator
2. 文件 → 打开文件/项目
3. 选择 NetShare/CMakeLists.txt
4. Kit 选择: Desktop Qt 6.8.3 MSVC2022 64bit
5. 构建目录: build
6. 点击「配置项目」
7. 左下角点击「运行」按钮构建并运行
```

### 构建配置说明

| CMake 选项 | 说明 | 默认值 |
|-----------|------|--------|
| `CMAKE_BUILD_TYPE` | 构建类型 | Release |
| `CMAKE_PREFIX_PATH` | Qt 安装路径 | 自动检测 |
| `QT_VERSION` | Qt 版本 | 6.8.3 |
| `NETSHARE_ENABLE_TLS` | 启用 TLS 支持 | ON |
| `NETSHARE_ENABLE_TESTS` | 启用单元测试 | ON |
| `NETSHARE_ENABLE_DOCS` | 生成文档 | OFF |

### 环境变量

| 变量 | 说明 | 示例 |
|------|------|------|
| `NETSHARE_QML_PATH` | QML 入口文件路径覆盖（开发调试用） | `D:/qt6cmake/NetShare/src/gui/Main.qml` |

开发时设置 `NETSHARE_QML_PATH` 可跳过 QML 模块编译，直接加载源码中的 QML 文件，方便热重载调试。

```powershell
$env:NETSHARE_QML_PATH = "D:\qt6cmake\NetShare\src\gui\Main.qml"
```

### CivetWeb 服务器

NetShare 使用 CivetWeb 作为嵌入式 HTTP/HTTPS/WebSocket 服务器，源码位于 `third_party/civetweb_src/`。
CMake 构建时自动编译，无需手动安装。HTTP 和 WebSocket 共用同一端口（默认 8080），WebSocket 端点为 `/ws`。

### 完整构建示例

```powershell
# PowerShell 脚本
$BUILD_DIR = "C:\Projects\NetShare\build"
$QT_PREFIX = "C:\Qt\6.8.3\msvc2022_64"

# 清理旧构建 (可选)
if (Test-Path $BUILD_DIR) {
    Remove-Item -Recurse -Force $BUILD_DIR
}

# 创建目录
New-Item -ItemType Directory -Path $BUILD_DIR -Force
Set-Location $BUILD_DIR

# 配置
cmake .. `
    -G "Ninja" `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_PREFIX_PATH=$QT_PREFIX `
    -DNETSHARE_ENABLE_TLS=ON `
    -DNETSHARE_ENABLE_TESTS=ON

# 构建 (4 核并行)
cmake --build . --parallel 4

# 验证
if (Test-Path "bin\NetShare.exe") {
    Write-Host "构建成功: bin\NetShare.exe"
}
```

---

## 常见问题

### Q: CMake 无法找到 Qt

**错误信息:**
```
CMake Error at CMakeLists.txt:10 (find_package):
  By not providing "FindQt6.cmake" in CMAKE_MODULE_PATH this project has
  asked CMake for a component list and didn't find it.
```

**解决方案:**
```bash
# 显式指定 Qt 路径
cmake .. -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64/lib/cmake
```

### Q: MSVC 编译器报错

**错误信息:**
```
cl : Command line error D8022 : cannot open '*.obj'
```

**解决方案:**
```powershell
# 使用 Developer Command Prompt
call "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake --build .
```

### Q: 缺少 OpenSSL

**错误信息:**
```
Could not find OpenSSL, please install it or configure NETSHARE_ENABLE_TLS=OFF
```

**解决方案:**
```bash
# 方案1: 安装 OpenSSL
winget install OpenSSL.OpenSSL.Light

# 方案2: 禁用 TLS
cmake .. -DNETSHARE_ENABLE_TLS=OFF
```

### Q: 构建太慢

**优化方案:**
```bash
# 1. 使用 Ninja
cmake .. -G Ninja

# 2. 并行构建
cmake --build . --parallel 8

# 3. 编译缓存
cmake --build . --parallel 4 --target PreCompile
```

---

## 发布版本

### Windows 发布

#### 打包步骤

```bash
# 1. 安装 windeployqt 和 macdeployqt (macOS)
# Windows:
$env:QTDIR = "C:\Qt\6.8.3\msvc2022_64"
& "$env:QTDIR\bin\windeployqt.exe" build\NetShare.exe

# 2. 创建压缩包
cd build
7z a NetShare-1.0.0-Windows-x64.7z NetShare.exe
```

#### NSIS 安装包 (可选)

```bash
# 安装 NSIS
winget install NSIS.NSIS

# 创建安装包
makensis installer.nsi
```

### 文件清单

发布时需要包含:

```
NetShare/
├── NetShare.exe                    # 主程序
├── Qt6Core.dll                    # Qt 运行时
├── Qt6Network.dll
├── Qt6Qml.dll
├── Qt6Quick.dll
├── Qt6Sql.dll
├── platforms/                     # Qt 平台插件
│   └── qwindows.dll
├── tls/                          # TLS 证书 (如启用)
│   └── openssl.dll
└── resources/                     # 资源文件
    └── web/                       # Web 前端
```

---

## 持续集成

### GitHub Actions

```yaml
# .github/workflows/build.yml
name: Build

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  build-windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4

      - name: Install Qt
        uses: jurplel/install-qt-action@v4
        with:
          version: 6.8.3
          dir: C:\Qt

      - name: Configure
        run: cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

      - name: Build
        run: cmake --build build --parallel

      - name: Deploy
        run: |
          windeployqt build/NetShare.exe
          7z a NetShare.zip build/NetShare.exe

      - name: Upload
        uses: actions/upload-artifact@v4
        with:
          name: NetShare-Windows
          path: NetShare.zip
```

---

## 联系方式

构建问题请联系: build@netshare.example.com
