# NetShare 配置说明

本文档详细说明 NetShare 的所有配置项。

---

## 📋 目录

- [配置文件](#配置文件)
- [配置项详解](#配置项详解)
- [配置文件格式](#配置文件格式)
- [默认值](#默认值)
- [配置示例](#配置示例)

---

## 配置文件

### 配置文件位置

| 平台 | 路径 |
|------|------|
| Windows | `%APPDATA%\NetShare\config.json` |
| Linux | `~/.config/NetShare/config.json` |
| macOS | `~/Library/Application Support/NetShare/config.json` |

### 配置文件格式

```json
{
    "version": "1.0.0",
    "server": {
        "port": 8080,
        "httpsPort": 8443,
        "tlsEnabled": false
    },
    "sharedFolders": [
        "D:\\Shared",
        "E:\\Downloads"
    ],
    "transfer": {
        "maxParallelTasks": 2,
        "maxThreadsPerTask": 3,
        "chunkSize": 4194304,
        "maxBandwidthUpload": 0,
        "maxBandwidthDownload": 0
    },
    "security": {
        "requirePassword": false,
        "defaultPassword": ""
    },
    "logging": {
        "level": "info",
        "maxFileSize": 10485760,
        "maxFiles": 5
    }
}
```

---

## 配置项详解

### 1. 服务器配置 (server)

#### `port`

| 属性 | 值 |
|------|---|
| 类型 | `integer` |
| 默认值 | `8080` |
| 范围 | 1024 - 65535 |
| 说明 | HTTP 服务端口 |

```json
"port": 8080
```

#### `httpsPort`

| 属性 | 值 |
|------|---|
| 类型 | `integer` |
| 默认值 | `8443` |
| 范围 | 1024 - 65535 |
| 说明 | HTTPS 服务端口 |

```json
"httpsPort": 8443
```

#### `tlsEnabled`

| 属性 | 值 |
|------|---|
| 类型 | `boolean` |
| 默认值 | `false` |
| 说明 | 是否启用 TLS 加密 |

```json
"tlsEnabled": false
```

#### `bindAddress`

| 属性 | 值 |
|------|---|
| 类型 | `string` |
| 默认值 | `"0.0.0.0"` |
| 说明 | 绑定地址，`0.0.0.0` 表示所有网卡 |

```json
"bindAddress": "0.0.0.0"
```

#### `maxConnections`

| 属性 | 值 |
|------|---|
| 类型 | `integer` |
| 默认值 | `100` |
| 说明 | 最大并发连接数 |

```json
"maxConnections": 100
```

---

### 2. 共享文件夹配置 (sharedFolders)

#### `sharedFolders`

| 属性 | 值 |
|------|---|
| 类型 | `array<string>` |
| 默认值 | `[]` |
| 说明 | 共享文件夹路径列表 |

```json
"sharedFolders": [
    "D:\\Shared",
    "E:\\Downloads",
    "\\\\192.168.1.100\\Public"
]
```

#### 路径格式

| 类型 | 示例 | 说明 |
|------|------|------|
| 本地路径 (Windows) | `D:\\Shared` 或 `D:/Shared` | 本地文件夹 |
| 本地路径 (Linux) | `/home/user/shared` | 本地文件夹 |
| 网络路径 (Windows) | `\\\\server\\share` | UNC 路径 |

---

### 3. 传输配置 (transfer)

#### `maxParallelTasks`

| 属性 | 值 |
|------|---|
| 类型 | `integer` |
| 默认值 | `2` |
| 范围 | 1 - 10 |
| 说明 | 最大并行下载任务数 |

```json
"maxParallelTasks": 2
```

#### `maxThreadsPerTask`

| 属性 | 值 |
|------|---|
| 类型 | `integer` |
| 默认值 | `3` |
| 范围 | 1 - 10 |
| 说明 | 单个任务的最大下载线程数 |

```json
"maxThreadsPerTask": 3
```

#### `chunkSize`

| 属性 | 值 |
|------|---|
| 类型 | `integer` |
| 默认值 | `4194304` (4MB) |
| 说明 | 分块大小 (字节) |

```json
"chunkSize": 4194304
```

**自动调整策略:**

| 文件大小 | 分块大小 |
|---------|---------|
| < 100MB | 1MB |
| 100MB - 1GB | 4MB |
| 1GB - 5GB | 8MB |
| > 5GB | 16MB |

#### `maxBandwidthUpload`

| 属性 | 值 |
|------|---|
| 类型 | `integer` |
| 默认值 | `0` (不限制) |
| 单位 | 字节/秒 |
| 说明 | 最大上传速度，0 表示不限制 |

```json
"maxBandwidthUpload": 10485760  // 10 MB/s
```

#### `maxBandwidthDownload`

| 属性 | 值 |
|------|---|
| 类型 | `integer` |
| 默认值 | `0` (不限制) |
| 单位 | 字节/秒 |
| 说明 | 最大下载速度，0 表示不限制 |

```json
"maxBandwidthDownload": 20971520  // 20 MB/s
```

#### `scheduleEnabled`

| 属性 | 值 |
|------|---|
| 类型 | `boolean` |
| 默认值 | `false` |
| 说明 | 是否启用时段限速 |

```json
"scheduleEnabled": true
```

#### `scheduleStart`

| 属性 | 值 |
|------|---|
| 类型 | `string` |
| 默认值 | `"22:00"` |
| 格式 | HH:MM (24小时制) |
| 说明 | 限速时段开始时间 |

```json
"scheduleStart": "22:00"
```

#### `scheduleEnd`

| 属性 | 值 |
|------|---|
| 类型 | `string` |
| 默认值 | `"08:00"` |
| 格式 | HH:MM (24小时制) |
| 说明 | 限速时段结束时间 |

```json
"scheduleEnd": "08:00"
```

#### `scheduledBandwidth`

| 属性 | 值 |
|------|---|
| 类型 | `integer` |
| 默认值 | `1048576` (1MB/s) |
| 单位 | 字节/秒 |
| 说明 | 时段内的最大带宽 |

```json
"scheduledBandwidth": 1048576  // 1 MB/s
```

---

### 4. 安全配置 (security)

#### `requirePassword`

| 属性 | 值 |
|------|---|
| 类型 | `boolean` |
| 默认值 | `false` |
| 说明 | 是否要求密码访问 |

```json
"requirePassword": false
```

#### `defaultPassword`

| 属性 | 值 |
|------|---|
| 类型 | `string` |
| 默认值 | `""` |
| 说明 | 默认访问密码 (SHA256 哈希) |

```json
"defaultPassword": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
```

#### `passwordSalt`

| 属性 | 值 |
|------|---|
| 类型 | `string` |
| 说明 | 密码盐值 |

#### `tlsCertificatePath`

| 属性 | 值 |
|------|---|
| 类型 | `string` |
| 说明 | TLS 证书路径 |

```json
"tlsCertificatePath": "C:\\NetShare\\certs\\server.crt"
```

#### `tlsPrivateKeyPath`

| 属性 | 值 |
|------|---|
| 类型 | `string` |
| 说明 | TLS 私钥路径 |

```json
"tlsPrivateKeyPath": "C:\\NetShare\\certs\\server.key"
```

---

### 5. 日志配置 (logging)

#### `level`

| 属性 | 值 |
|------|---|
| 类型 | `string` |
| 默认值 | `"info"` |
| 可选值 | `debug`, `info`, `warning`, `error`, `critical` |
| 说明 | 日志级别 |

```json
"level": "info"
```

#### `maxFileSize`

| 属性 | 值 |
|------|---|
| 类型 | `integer` |
| 默认值 | `10485760` (10MB) |
| 单位 | 字节 |
| 说明 | 单个日志文件最大大小 |

```json
"maxFileSize": 10485760
```

#### `maxFiles`

| 属性 | 值 |
|------|---|
| 类型 | `integer` |
| 默认值 | `5` |
| 说明 | 保留的日志文件数量 |

```json
"maxFiles": 5
```

#### `logPath`

| 属性 | 值 |
|------|---|
| 类型 | `string` |
| 说明 | 日志文件目录 |

```json
"logPath": "C:\\ProgramData\\NetShare\\logs"
```

---

### 6. 界面配置 (ui)

#### `language`

| 属性 | 值 |
|------|---|
| 类型 | `string` |
| 默认值 | `"system"` |
| 可选值 | `system`, `zh_CN`, `en_US` |
| 说明 | 界面语言 |

```json
"language": "system"
```

#### `theme`

| 属性 | 值 |
|------|---|
| 类型 | `string` |
| 默认值 | `"system"` |
| 可选值 | `system`, `light`, `dark` |
| 说明 | 界面主题 |

```json
"theme": "system"
```

#### `startMinimized`

| 属性 | 值 |
|------|---|
| 类型 | `boolean` |
| 默认值 | `false` |
| 说明 | 启动时最小化到托盘 |

```json
"startMinimized": false
```

#### `minimizeToTray`

| 属性 | 值 |
|------|---|
| 类型 | `boolean` |
| 默认值 | `true` |
| 说明 | 关闭时最小化到托盘 |

```json
"minimizeToTray": true
```

---

### 7. 高级配置 (advanced)

#### `downloadPath`

| 属性 | 值 |
|------|---|
| 类型 | `string` |
| 说明 | 默认下载保存路径 |

```json
"downloadPath": "C:\\Users\\Public\\Downloads"
```

#### `tempPath`

| 属性 | 值 |
|------|---|
| 类型 | `string` |
| 说明 | 临时文件目录 (用于分块传输) |

```json
"tempPath": "C:\\ProgramData\\NetShare\\temp"
```

#### `databasePath`

| 属性 | 值 |
|------|---|
| 类型 | `string` |
| 说明 | 数据库文件路径 |

```json
"databasePath": "C:\\ProgramData\\NetShare\\netshare.db"
```

#### `mDNSEnabled`

| 属性 | 值 |
|------|---|
| 类型 | `boolean` |
| 默认值 | `true` |
| 说明 | 是否启用 mDNS 服务发现 |

```json
"mDNSEnabled": true
```

#### `mDNSServiceName`

| 属性 | 值 |
|------|---|
| 类型 | `string` |
| 默认值 | `"NetShare"` |
| 说明 | mDNS 服务名称 |

```json
"mDNSServiceName": "My-PC-NetShare"
```

---

## 配置文件格式

### JSON Schema

```json
{
    "$schema": "https://json-schema.org/draft/2020-12/schema",
    "type": "object",
    "required": ["version", "server", "transfer"],
    "properties": {
        "version": {
            "type": "string",
            "pattern": "^\\d+\\.\\d+\\.\\d+$"
        },
        "server": {
            "type": "object",
            "properties": {
                "port": { "type": "integer", "minimum": 1024, "maximum": 65535 },
                "httpsPort": { "type": "integer", "minimum": 1024, "maximum": 65535 },
                "tlsEnabled": { "type": "boolean" },
                "bindAddress": { "type": "string" },
                "maxConnections": { "type": "integer", "minimum": 1 }
            }
        },
        "sharedFolders": {
            "type": "array",
            "items": { "type": "string" }
        },
        "transfer": {
            "type": "object",
            "properties": {
                "maxParallelTasks": { "type": "integer", "minimum": 1, "maximum": 10 },
                "maxThreadsPerTask": { "type": "integer", "minimum": 1, "maximum": 10 },
                "chunkSize": { "type": "integer", "minimum": 1048576 },
                "maxBandwidthUpload": { "type": "integer", "minimum": 0 },
                "maxBandwidthDownload": { "type": "integer", "minimum": 0 }
            }
        },
        "security": {
            "type": "object"
        },
        "logging": {
            "type": "object"
        },
        "ui": {
            "type": "object"
        },
        "advanced": {
            "type": "object"
        }
    }
}
```

---

## 默认值

### 完整默认值配置

```json
{
    "version": "1.0.0",

    "server": {
        "port": 8080,
        "httpsPort": 8443,
        "tlsEnabled": false,
        "bindAddress": "0.0.0.0",
        "maxConnections": 100
    },

    "sharedFolders": [],

    "transfer": {
        "maxParallelTasks": 2,
        "maxThreadsPerTask": 3,
        "chunkSize": 4194304,
        "maxBandwidthUpload": 0,
        "maxBandwidthDownload": 0,
        "scheduleEnabled": false,
        "scheduleStart": "22:00",
        "scheduleEnd": "08:00",
        "scheduledBandwidth": 1048576
    },

    "security": {
        "requirePassword": false,
        "defaultPassword": "",
        "passwordSalt": "",
        "tlsCertificatePath": "",
        "tlsPrivateKeyPath": ""
    },

    "logging": {
        "level": "info",
        "maxFileSize": 10485760,
        "maxFiles": 5,
        "logPath": ""
    },

    "ui": {
        "language": "system",
        "theme": "system",
        "startMinimized": false,
        "minimizeToTray": true
    },

    "advanced": {
        "downloadPath": "",
        "tempPath": "",
        "databasePath": "",
        "mDNSEnabled": true,
        "mDNSServiceName": "NetShare"
    }
}
```

---

## 配置示例

### 1. 基本配置 (家庭网络)

```json
{
    "version": "1.0.0",
    "server": {
        "port": 8080,
        "tlsEnabled": false
    },
    "sharedFolders": [
        "D:\\Shared",
        "E:\\Downloads"
    ],
    "transfer": {
        "maxParallelTasks": 2,
        "maxThreadsPerTask": 3
    }
}
```

### 2. 办公环境配置 (需要密码)

```json
{
    "version": "1.0.0",
    "server": {
        "port": 8080,
        "tlsEnabled": false
    },
    "sharedFolders": [
        "D:\\WorkDocs",
        "\\\\FileServer\\Public"
    ],
    "security": {
        "requirePassword": true,
        "defaultPassword": "hashed_password_here"
    },
    "transfer": {
        "maxParallelTasks": 1,
        "maxThreadsPerTask": 2
    }
}
```

### 3. 高安全配置 (启用 TLS)

```json
{
    "version": "1.0.0",
    "server": {
        "port": 8080,
        "httpsPort": 8443,
        "tlsEnabled": true
    },
    "security": {
        "tlsCertificatePath": "C:\\NetShare\\certs\\server.crt",
        "tlsPrivateKeyPath": "C:\\NetShare\\certs\\server.key",
        "requirePassword": true
    },
    "sharedFolders": [
        "D:\\SecureShare"
    ],
    "logging": {
        "level": "debug"
    }
}
```

### 4. 带宽限制配置

```json
{
    "version": "1.0.0",
    "server": {
        "port": 8080
    },
    "transfer": {
        "maxParallelTasks": 3,
        "maxThreadsPerTask": 4,
        "maxBandwidthUpload": 5242880,
        "maxBandwidthDownload": 10485760,
        "scheduleEnabled": true,
        "scheduleStart": "22:00",
        "scheduleEnd": "08:00",
        "scheduledBandwidth": 1048576
    }
}
```

---

## 配置管理 API

配置也可以通过 HTTP API 获取和修改:

### 获取配置

```http
GET /api/settings
```

### 更新配置

```http
PUT /api/settings
Content-Type: application/json

{
    "server.port": 9090
}
```

### 重置为默认值

```http
POST /api/settings/reset
```

---

## 带宽单位说明

| 值 | 单位 | 实际速度 |
|----|------|---------|
| `1048576` | 1 MiB/s | ~1 MB/s |
| `5242880` | 5 MiB/s | ~5 MB/s |
| `10485760` | 10 MiB/s | ~10 MB/s |
| `20971520` | 20 MiB/s | ~20 MB/s |
| `52428800` | 50 MiB/s | ~50 MB/s |
| `0` | - | 不限制 |
