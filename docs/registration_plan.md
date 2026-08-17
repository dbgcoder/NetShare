# 用户注册功能执行计划

## 0. 问题概述

当前 NetShare 软件启动后直接进入主界面，无任何用户身份验证机制。需要新增用户注册页面及注册码验证系统，实现：

1. 软件未注册时，启动弹窗显示注册页面，页面包含"试用"按钮可直接进入软件
2. 软件已注册时，启动直接进入主界面，注册信息在"关于"页面显示
3. 注册页面收集用户名、邮箱、手机号，并验证合法性
4. 用户点击注册时，自动将用户名、手机号、邮箱及硬件信息发送到指定邮箱
5. 采用非对称加密（RSA）注册码机制：软件内仅嵌入公钥，私钥由管理员离线保管，防止注册码被伪造
6. 加入反调试和防破解机制，增加逆向分析、动态调试、绕过注册弹窗的难度

**解决方法**：在应用启动流程中增加注册状态判断，未注册时弹窗注册页面（含试用入口），已注册时直接进入主界面。新增用户数据库表、硬件指纹采集、注册码验证服务、邮件发送服务、RSA 密钥对管理，以及完整的输入合法性校验。注册码绑定硬件指纹，防止注册码在不同机器上复用。同时加入多层反调试检测和防篡改机制，提高破解门槛。

---

## 1. 整体架构设计

### 1.1 启动流程变更

```
当前流程：App启动 → 直接显示主窗口 → 启动网络服务
目标流程：App启动 → 初始化基础服务（数据库/设置/核心服务）→ 初始化认证服务 → 显示主窗口
  ├─ 已注册 → 校验机器码匹配度
  │    ├─ 匹配度 ≥ 60 → 启动网络服务 → 直接显示主窗口（注册信息在"关于"页显示）
  │    └─ 匹配度 < 60 → 提示"硬件信息变更较大，需重新注册" → 弹窗注册页面
  └─ 未注册 → 弹窗注册页面（网络服务尚未启动）
       ├─ 点击"试用" → 关闭弹窗 → 启动网络服务 → 进入主窗口（试用模式）
       ├─ 点击"注册" → 发送注册信息到指定邮箱 → 输入注册码 → 验证通过 → 启动网络服务 → 进入主窗口（正式模式）
       └─ 关闭弹窗 → 退出软件（网络服务未启动，无安全风险）

注意：网络服务（HTTP 文件共享）仅在用户选择试用或注册成功后才启动，确保未注册状态下无法通过网络访问共享文件
```

### 1.1.1 试用模式与正式模式

| 特性 | 试用模式 | 正式模式 |
|------|----------|----------|
| 进入方式 | 点击注册页面"试用"按钮 | 输入注册码验证通过 |
| 功能限制 | 无功能限制（本期不限制功能） | 无限制 |
| 试用时长 | 每次启动均可试用（本期不限次数） | 永久有效 |
| 注册提示 | 主界面"关于"页显示"试用版"标识 | 主界面"关于"页显示注册信息 |
| 注册入口 | "关于"页提供"注册"按钮，可随时打开注册页面 | "关于"页显示注册详情 |

> **设计说明**：试用模式不限制功能，仅通过"试用版"标识引导用户注册。后续版本可增加试用天数限制或功能限制。

### 1.2 注册码验证流程（参考成熟商业软件离线激活模式）

```
管理员侧（离线）：
  1. 管理员收到用户邮件（包含用户名+邮箱+手机号+机器码）
  2. 管理员持有 RSA 私钥（2048位或4096位）
  3. 管理员使用私钥对「注册信息+机器码」摘要（SHA-256）进行签名
  4. 将签名结果编码为注册码（Base64），通过邮件或其他方式交付用户

软件侧（在线）：
  1. 软件自动采集硬件信息，生成机器码（Machine ID）
  2. 用户填写注册信息（用户名+邮箱+手机号）
  3. 点击"注册"按钮 → 软件自动将用户名+邮箱+手机号+机器码发送到指定邮箱
  4. 用户收到管理员提供的注册码后，在注册页面输入注册码
  5. 软件使用内嵌公钥验证注册码签名（验证载荷包含当前机器码）
  6. 验证通过 → 注册信息+机器码存入本地数据库 → 进入正式模式
  7. 验证失败 → 提示注册码无效或与本机不匹配
  8. 每次启动时校验当前机器码与注册时一致，防止注册码迁移到其他机器
```

### 1.2.1 硬件指纹采集方案

软件采集以下硬件信息，经哈希运算后生成机器码：

| 采集项 | Windows API / 方法 | 说明 |
|--------|-------------------|------|
| 主板序列号 | WMI `Win32_BaseBoard::SerialNumber` | 主板唯一标识，更换主板后机器码变化 |
| BIOS 序列号 | WMI `Win32_BIOS::SerialNumber` | BIOS 固件标识，与主板强关联 |
| CPU ID | `__cpuid` 内联函数（`<intrin.h>`） | 处理器特征，取 ProcessorId 部分 |
| 网卡 MAC 地址 | `GetAdaptersAddresses` API（`<iphlpapi.h>`） | 取第一个活跃物理网卡的 MAC，虚拟网卡过滤 |

**机器码生成算法**：

```
原始信息 = 主板序列号 + "|" + BIOS序列号 + "|" + CPU_ID + "|" + 首个物理网卡MAC
机器码 = SHA-256(原始信息) 的前16字节 → Base64 编码 → 约24字符
```

**容错策略**（硬件变更处理）：

| 变更场景 | 处理方式 |
|----------|----------|
| 更换网卡 | 机器码变化，需重新注册 |
| 更换主板 | 机器码变化，需重新注册 |
| 重装系统 | 硬件信息不变，机器码不变（硬件信息不依赖操作系统） |
| 虚拟机迁移 | 硬件信息全部变化，需重新注册 |

**推荐权重容错方案**（可选，增强用户体验）：

```
匹配度计算：
  主板序列号匹配 → +40分
  BIOS序列号匹配 → +20分
  CPU ID匹配 → +20分
  网卡MAC匹配 → +20分
  总分 ≥ 60分 → 允许使用（容忍一项硬件变更）
  总分 < 60分 → 需重新注册
```

此方案在步骤 2 的 `MachineFingerprint` 类中实现，通过 `Q_INVOKABLE` 暴露给 QML。

### 1.3 密钥管理策略

| 项目 | 说明 |
|------|------|
| 密钥算法 | RSA 2048位（兼顾安全性与性能） |
| 公钥存储 | 编译时嵌入 C++ 代码中（硬编码常量），不作为外部文件分发 |
| 私钥存储 | 管理员离线保管，**绝不出现在软件代码或安装包中** |
| 签名内容 | `SHA-256(用户名 + "|" + 邮箱 + "|" + 手机号 + "|" + 机器码)` |
| 注册码格式 | Base64 编码的 RSA 签名，约 344 字符 |

### 1.4 反调试与防破解架构

**设计目标**：提高逆向工程和绕过注册弹窗的难度，使攻击者需要付出远超购买正版的时间成本。

**防护层次**：

```
┌─────────────────────────────────────────────────────┐
│ 第1层：编译期防护                                      │
│   - Release 构建，禁用调试符号                          │
│   - 开启 MSVC /O2 优化 + /GL 全局优化                  │
│   - 关键函数标记 __declspec(noinline) 防止内联优化删除    │
│   - 链接期代码生成（LTCG）/LTCG                        │
├─────────────────────────────────────────────────────┤
│ 第2层：运行时反调试检测                                 │
│   - IsDebuggerPresent / CheckRemoteDebuggerPresent    │
│   - NtQueryInformationProcess 检测调试器               │
│   - 硬件断点检测（Dr0-Dr3 寄存器）                      │
│   - 定时器检测（QueryPerformanceCounter 反调试）        │
│   - 父进程检测（非 explorer.exe 启动则可疑）            │
├─────────────────────────────────────────────────────┤
│ 第3层：反篡改检测                                      │
│   - 关键函数入口校验（内存 CRC32 / SHA-256）            │
│   - 注册状态变量分散存储 + 多处交叉校验                  │
│   - 注册弹窗显示逻辑与核心功能绑定                       │
│   - 二进制完整性自校验（PE 文件哈希）                    │
├─────────────────────────────────────────────────────┤
│ 第4层：反响应（检测到攻击后的行为）                       │
│   - 静默降级：不弹窗提示，而是随机制造功能异常            │
│   - 延迟触发：检测到调试后不立即响应，随机时间后生效       │
│   - 数据污染：注册码验证返回伪造的"通过"但内部标记无效     │
└─────────────────────────────────────────────────────┘
```

**关键原则**：
- **不提示"检测到调试器"**：攻击者可据此定位检测代码。应静默降级或延迟触发
- **多处冗余检测**：同一检测逻辑在不同位置重复执行，删除一处不影响其他
- **检测逻辑混淆**：反调试代码不集中在 AntiDebug 类中，而是分散嵌入到 AuthService、RegistrationKey、MachineFingerprint 等关键路径中
- **注册弹窗不可简单跳过**：注册状态不仅由一个布尔值控制，而是与核心功能深度绑定

### 1.5 新增文件清单

| 文件 | 类型 | 所属模块 | 说明 |
|------|------|----------|------|
| `src/core/auth/AuthService.h` | 头文件 | core/auth | 认证服务类声明 |
| `src/core/auth/AuthService.cpp` | 源文件 | core/auth | 认证服务实现（注册码验证、用户管理、试用模式） |
| `src/core/auth/RegistrationKey.h` | 头文件 | core/auth | 公钥常量及注册码编解码工具 |
| `src/core/auth/RegistrationKey.cpp` | 源文件 | core/auth | 注册码验证逻辑实现 |
| `src/core/auth/MachineFingerprint.h` | 头文件 | core/auth | 硬件指纹采集类声明 |
| `src/core/auth/MachineFingerprint.cpp` | 源文件 | core/auth | 硬件指纹采集实现（WMI、Windows API） |
| `src/core/auth/EmailService.h` | 头文件 | core/auth | 邮件发送服务类声明 |
| `src/core/auth/EmailService.cpp` | 源文件 | core/auth | 邮件发送实现（SMTP，将注册信息+机器码发送到指定邮箱） |
| `src/core/auth/AntiDebug.h` | 头文件 | core/auth | 反调试检测工具函数声明（仅静态方法，不暴露为 QML 对象） |
| `src/core/auth/AntiDebug.cpp` | 源文件 | core/auth | 反调试检测实现（多维度检测 + 静默降级） |
| `src/gui/qml/RegisterPage.qml` | QML | gui/qml | 注册页面（弹窗形式，含试用按钮） |

### 1.6 需修改的现有文件

| 文件 | 修改内容 |
|------|----------|
| `src/database/DatabaseManager.h` | 新增 `createUsersTable()` 方法声明 |
| `src/database/DatabaseManager.cpp` | 新增 `createUsersTable()` 实现，创建 users 表；在 `createTables()` 中追加调用 |
| `src/main.cpp` | 新增 `initializeAuth()` 方法；启动顺序异步化改造（网络服务延迟到注册完成后启动）；注册 QML 上下文属性；新增 `onRegistrationCompleted()` 槽 |
| `src/core/common/DiContainer.h` | 新增 `AuthModule`（绑定 AuthService、MachineFingerprint、EmailService） |
| `src/gui/qml/Main.qml` | 新增 RegisterPage 弹窗组件（Popup）；启动时判断注册状态；试用模式标识 |
| `src/gui/qml/SettingsPage.qml` | "关于"页面（currentSection=3）新增注册信息展示区和"注册"按钮；试用模式显示机器码 |
| `src/core/CMakeLists.txt` | 新增 auth 子目录源文件；新增 `Qt6::Network` 链接（EmailService 需要）；新增 Win32 链接库 |
| `src/gui/CMakeLists.txt` | 新增 RegisterPage.qml 到 QML_FILES |
| `src/CMakeLists.txt` | （可选）新增 tools/keygen 子目录 |
| `src/network/RequestHandler.cpp` 或 `src/core/share/ShareManager.cpp` | 服务启动前检查 `authService->hasValidSecurityToken()`，无效则拒绝启动（securityToken 集成点） |
| `CMakeLists.txt`（根） | `find_package` 中补充 `Concurrent` 组件（EmailService 异步发送需要） |
| `src/gui/translations/netshare_zh_CN.ts` | 新增翻译条目 |
| `src/gui/translations/netshare_en.ts` | 新增翻译条目 |

---

## 2. 执行步骤

### 步骤 1：创建 users 数据库表

- **修改内容**：
  - 在 `DatabaseManager.h` 中新增 `bool createUsersTable()` 私有方法声明
  - 在 `DatabaseManager.cpp` 中实现 `createUsersTable()`，SQL 如下：
    ```sql
    CREATE TABLE IF NOT EXISTS users (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        username TEXT UNIQUE NOT NULL,
        email TEXT,
        phone TEXT,
        machine_id TEXT NOT NULL,
        hardware_components TEXT,
        registration_code TEXT,
        is_trial INTEGER DEFAULT 1,
        registered_at TEXT,
        created_at TEXT DEFAULT CURRENT_TIMESTAMP,
        updated_at TEXT DEFAULT CURRENT_TIMESTAMP
    );
    ```
  - 注意：`email` 和 `phone` 允许为空（试用模式下不填写）；正式注册时由 `AuthService` 校验必填
  - 注意：`is_trial` 列标记当前是否为试用模式（1=试用，0=已注册）
  - 注意：`machine_id` 列存储注册时的机器码（硬件指纹哈希），用于启动时校验机器一致性
  - 注意：`hardware_components` 列存储 JSON 格式的各硬件项哈希（如 `{"baseboard":"abc...","bios":"def...","cpu":"ghi...","mac":"jkl..."}`），用于硬件变更时的权重匹配计算
  - 注意：`registered_at` 列记录注册时间，试用模式下为空
  - 在 `createTables()` 中追加调用 `createUsersTable()`，调用顺序：`createSharesTable() && createTransferLogsTable() && createUsersTable() && createSettingsTable()`（users 表在 settings 表之前创建）
- **难易程度**：低
- **完成状态**：未开始
- **验证方式**：编译通过，启动后检查数据库中是否存在 users 表

### 步骤 2：实现硬件指纹采集（MachineFingerprint）

- **修改内容**：
  - 新建 `src/core/auth/MachineFingerprint.h`，声明：
    ```cpp
    class MachineFingerprint : public QObject {
        Q_OBJECT
    public:
        explicit MachineFingerprint(QObject* parent = nullptr);

        // 获取当前机器码（SHA-256 哈希前16字节 Base64）
        Q_INVOKABLE QString machineId() const;

        // 获取各硬件项原始值（用于调试/展示）
        Q_INVOKABLE QString baseboardSerial() const;
        Q_INVOKABLE QString biosSerial() const;
        Q_INVOKABLE QString cpuId() const;
        Q_INVOKABLE QString primaryMacAddress() const;

        // 硬件变更容错：计算当前硬件与注册时的匹配度（0-100分）
        // registeredComponents: 数据库中存储的 hardware_components JSON
        Q_INVOKABLE int matchScore(const QString& registeredComponents) const;

    private:
        // 采集各硬件信息
        QString queryWmi(const QString& wmiClass, const QString& property) const;
        QString getCpuId() const;
        QString getPrimaryMacAddress() const;

        // 分项哈希（用于权重容错匹配）
        QString hashComponent(const QString& componentValue) const;
    };
    ```
  - 新建 `src/core/auth/MachineFingerprint.cpp`，实现：
    - `queryWmi()`：通过 COM 接口查询 WMI（`IWbemServices`），获取 `Win32_BaseBoard::SerialNumber`、`Win32_BIOS::SerialNumber`
    - `getCpuId()`：使用 `__cpuid` 内联函数（`<intrin.h>`）获取 CPU 特征，取 EAX/EBX/ECX/EDX 拼接
    - `getPrimaryMacAddress()`：使用 `GetAdaptersAddresses` API（`<iphlpapi.h>`），过滤虚拟网卡（VMware/VirtualBox/Hyper-V），取第一个物理网卡 MAC
    - `machineId()`：拼接 `主板序列号|BIOS序列号|CPU_ID|首个物理网卡MAC` → SHA-256 → 取前16字节 → Base64
    - `matchScore()`：分别比对各硬件项哈希，按权重（主板40、BIOS 20、CPU 20、网卡20）计算总分
  - **技术细节**：
    - WMI COM 初始化：`CoInitializeEx` + `CoInitializeSecurity` + `IWbemLocator::ConnectServer`
    - 需链接 `ole32.lib`、`oleaut32.lib`、`wbemuuid.lib`（WMI COM 库）
    - `iphlpapi.lib`（网卡信息 API）
    - `__cpuid` 为 MSVC 内联函数，无需额外库
- **难易程度**：高
- **完成状态**：未开始
- **验证方式**：编译运行，`machineId()` 在同一机器多次调用返回相同值；不同机器返回不同值；`matchScore()` 在硬件不变时返回100

### 步骤 3：实现注册码验证核心（RegistrationKey）

- **修改内容**：
  - 新建 `src/core/auth/RegistrationKey.h`，声明：
    - `namespace RegistrationKey` 包含公钥 PEM 常量字符串
    - `bool verifyRegistrationCode(const QString& username, const QString& email, const QString& phone, const QString& machineId, const QString& code)` —— 使用公钥验证注册码签名
    - `QString generatePayload(const QString& username, const QString& email, const QString& phone, const QString& machineId)` —— 生成待签名的载荷字符串
  - 新建 `src/core/auth/RegistrationKey.cpp`，实现：
    - 公钥 PEM 常量（RSA 2048位公钥，后续由密钥生成工具产出后填入）
    - `verifyRegistrationCode()` 使用 Qt 的 `QSslKey` 加载公钥，`QCryptographicHash::Sha256` 计算摘要，使用 OpenSSL EVP API 或 Qt 底层接口进行 RSA 签名验证
    - 载荷格式：`SHA-256(username|email|phone|machineId)`
  - **技术细节**：
    - **重要**：Qt 6 on Windows 捆绑了 OpenSSL DLL 运行时，但**不一定提供 OpenSSL 开发头文件**。因此不能直接 `#include <openssl/evp.h>`。
    - **推荐方案**：使用 Qt 自带的 `QSslKey` + `QCryptographicHash` + 底层 OpenSSL 函数指针加载方式，或者通过 `QProcess` 调用 `openssl dgst -verify` 命令行（与现有 `TlsCertificateGenerator` 的调用方式一致）。
    - **最优方案**：使用 Qt 6.8 的 `QMessageAuthenticationCode` 或直接使用 Windows CryptoAPI（`BCryptVerifySignature`），无需额外依赖。Windows 10+ 原生支持 RSA 签名验证，无需 OpenSSL 头文件。
- **难易程度**：高
- **完成状态**：未开始
- **验证方式**：编写测试用例——用私钥签名一段载荷，用 `verifyRegistrationCode()` 验证通过；篡改载荷后验证失败

### 步骤 4：实现认证服务（AuthService）

- **修改内容**：
  - 新建 `src/core/auth/AuthService.h`，声明：
    ```cpp
    class AuthService : public QObject {
        Q_OBJECT
    public:
        explicit AuthService(DatabaseManager* db, MachineFingerprint* fingerprint,
                             EmailService* emailService, QObject* parent = nullptr);

        // 注册：验证输入 + 发送邮件 + 验证注册码 + 存入数据库
        Q_INVOKABLE bool registerUser(const QString& username,
                                       const QString& email, const QString& phone,
                                       const QString& registrationCode);
        // 发送注册信息到指定邮箱（用户点击"注册"时调用）
        Q_INVOKABLE bool sendRegistrationEmail(const QString& username,
                                                const QString& email, const QString& phone);
        // 试用模式：标记为试用用户，直接进入软件
        Q_INVOKABLE bool enterTrialMode();
        // 输入合法性校验
        Q_INVOKABLE bool validateEmail(const QString& email) const;
        Q_INVOKABLE bool validatePhone(const QString& phone) const;
        Q_INVOKABLE bool validateUsername(const QString& username) const;
        // 检查是否已注册（非试用模式）
        Q_INVOKABLE bool isRegistered() const;
        // 检查是否为试用模式
        Q_INVOKABLE bool isTrialMode() const;
        // 获取当前机器码（供 QML 展示）
        Q_INVOKABLE QString currentMachineId() const;
        // 获取注册用户信息（供"关于"页展示，仅用户名、注册日期、机器码）
        Q_INVOKABLE QString registeredUsername() const;
        Q_INVOKABLE QString registeredDate() const;
        // 校验当前机器码与注册时是否匹配（启动时调用）
        Q_INVOKABLE bool verifyMachineMatch() const;
        // 检查 securityToken 是否有效（核心功能启动前调用）
        Q_INVOKABLE bool hasValidSecurityToken() const;
    signals:
        void registrationSuccess();
        void registrationFailed(const QString& reason);
        void emailSent();
        void emailSendFailed(const QString& reason);
        void registrationCompleted();  // 注册弹窗关闭时发射，通知 C++ 层启动网络服务
    private:
        DatabaseManager* m_db;
        MachineFingerprint* m_fingerprint;
        EmailService* m_emailService;
    };
    ```
  - 新建 `src/core/auth/AuthService.cpp`，实现上述方法：
    - `validateEmail()`：正则 `^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$`
    - `validatePhone()`：正则 `^1[3-9]\d{9}$`（中国大陆手机号）
    - `validateUsername()`：4-20位字母数字下划线，`^[A-Za-z][A-Za-z0-9_]{3,19}$`
    - `sendRegistrationEmail()`：调用 `m_emailService->sendRegistrationInfo(username, email, phone, machineId)`，将用户名+邮箱+手机号+机器码发送到管理员指定邮箱
    - `registerUser()`：先校验输入合法性 → 检查用户名是否已存在 → 获取当前机器码 → 验证注册码（载荷包含机器码）→ 若当前为试用用户则 UPDATE 现有记录（is_trial=0，填入注册码和注册信息），否则 INSERT 新记录（含 machine_id，is_trial=0，registered_at=当前时间）
    - `enterTrialMode()`：在数据库中创建试用用户记录（is_trial=1，无注册码，username 为 "TrialUser"，email/phone 为空占位符，machine_id 为当前机器码）
    - `isRegistered()`：查询数据库是否存在 is_trial=0 的用户记录
    - `isTrialMode()`：查询数据库是否存在 is_trial=1 的用户记录且无 is_trial=0 的记录
    - `verifyMachineMatch()`：获取当前各硬件项哈希，与数据库中 `hardware_components` JSON 逐项比对，匹配度 ≥ 60 分返回 true
    - `registeredUsername/Date()`：从数据库读取注册用户信息（邮箱和手机号不对外展示，保护隐私）
- **难易程度**：高
- **完成状态**：未开始
- **验证方式**：编译通过，单元测试覆盖注册/试用/校验各路径

### 步骤 5：实现反调试与防破解（AntiDebug）

- **修改内容**：
  - 新建 `src/core/auth/AntiDebug.h`，声明：
    ```cpp
    namespace AntiDebug {
        // 运行时反调试检测（多维度）
        bool isDebuggerPresent();           // IsDebuggerPresent + CheckRemoteDebuggerPresent
        bool isNtDebugged();                // NtQueryInformationProcess(ProcessDebugPort)
        bool isHardwareBreakpointSet();     // 读取 Dr0-Dr3 寄存器
        bool isTimingAnomaly();             // QueryPerformanceCounter 时间差检测
        bool isParentSuspicious();          // 父进程非 explorer.exe

        // 综合检测：调用以上所有方法，任一命中返回 true
        bool isDebugEnvironment();

        // 反篡改检测
        bool verifyFunctionIntegrity(const void* funcPtr, size_t funcSize,
                                      const QByteArray& expectedHash);  // 内存 CRC32/SHA-256
        bool verifyPeIntegrity();           // PE 文件自身哈希校验

        // 静默降级：检测到调试环境时，不弹窗，而是返回伪造结果
        // 由调用方（AuthService、RegistrationKey）决定降级行为
        // 例如：注册码验证返回 true 但内部标记为无效
    };
    ```
  - 新建 `src/core/auth/AntiDebug.cpp`，实现：
    - `isDebuggerPresent()`：调用 Windows API `IsDebuggerPresent()` + `CheckRemoteDebuggerPresent(GetCurrentProcess(), &debugged)`
    - `isNtDebugged()`：动态加载 `ntdll.dll`，调用 `NtQueryInformationProcess(ProcessDebugPort)`，检查调试端口是否非零。**动态加载**避免静态链接 ntdll 引起注意
    - `isHardwareBreakpointSet()`：使用 `GetThreadContext()` 读取当前线程的 `CONTEXT_DEBUG_REGISTERS`，检查 Dr0-Dr3 是否非零
    - `isTimingAnomaly()`：使用 `QueryPerformanceCounter` 测量一段空循环的耗时，若显著超过正常值（被调试器单步执行时），返回 true。阈值设为正常耗时的 5 倍
    - `isParentSuspicious()`：获取当前进程 PID → `CreateToolhelp32Snapshot` 遍历进程列表 → 找到父进程 → 检查父进程名是否为 `explorer.exe`。非 explorer 启动（如从调试器启动）视为可疑
    - `isDebugEnvironment()`：依次调用上述 5 个检测，任一返回 true 则返回 true
    - `verifyFunctionIntegrity()`：对指定内存区域计算 SHA-256，与预存的哈希值比对。用于检测函数被下断点（INT3 覆写）或补丁
    - `verifyPeIntegrity()`：读取自身 EXE 文件，计算 `.text` 段的 SHA-256，与编译时嵌入的预存哈希比对。**注意**：每次编译后哈希值会变化，需在发布构建后手动更新或通过脚本自动计算
  - **在 AuthService 中嵌入反调试调用**：
    - `isRegistered()` 内部调用 `AntiDebug::isDebugEnvironment()`，若检测到调试则返回伪造结果（始终返回 false，强制显示注册弹窗）
    - `registerUser()` 内部调用 `AntiDebug::isDebugEnvironment()`，若检测到调试则静默返回 false（注册码验证"通过"但实际未注册）
    - `verifyMachineMatch()` 内部调用 `AntiDebug::isDebugEnvironment()`，若检测到调试则返回 false
  - **在 RegistrationKey 中嵌入反调试调用**：
    - `verifyRegistrationCode()` 内部调用 `AntiDebug::isDebugEnvironment()`，若检测到调试则返回 false（即使签名验证通过也返回 false）
  - **在 MachineFingerprint 中嵌入反调试调用**：
    - `machineId()` 内部调用 `AntiDebug::isDebugEnvironment()`，若检测到调试则返回伪造的机器码（与真实机器码不同，导致注册码验证必然失败）
  - **注册弹窗防跳过机制**：
    - 注册状态不使用单一布尔值，而是由 `AuthService` 内部的一个 `m_securityToken`（随机生成的 QByteArray）控制
    - `isRegistered()` 返回 true 时同时设置 `m_securityToken`，主窗口核心功能（如文件共享服务启动）依赖此 token
    - 攻击者无法通过简单修改 QML 属性跳过注册弹窗，因为 C++ 层的核心功能需要有效 token
  - **定时检测**：使用 `QTimer` 每 30 秒执行一次 `AntiDebug::isDebugEnvironment()`，若检测到调试器附加则触发降级（如随机禁用某项功能）
- **难易程度**：高
- **完成状态**：未开始
- **验证方式**：正常启动无异常；使用 x64dbg 附加后功能静默异常；使用 Cheat Engine 修改内存后功能异常

### 步骤 6：实现管理员密钥生成与签名工具

- **修改内容**：
  - 新建 `tools/keygen/KeyGenTool.cpp`，独立命令行程序：
    - `--generate`：生成 RSA 2048 密钥对，输出私钥文件和公钥 PEM（公钥 PEM 将复制到步骤 3 的代码中）
    - `--sign <username> <email> <phone> <machineId>`：使用私钥对载荷签名，输出注册码（Base64）。**machineId 由用户提供，用户从软件注册页面获取机器码**
    - `--verify <username> <email> <phone> <machineId> <code>`：使用公钥验证注册码
  - 该工具**不纳入 NetShare 主程序构建**，单独编译或脚本运行
  - 可选：在项目根 CMakeLists.txt 中添加 `add_subdirectory(tools/keygen)` 作为可选目标
  - **工作流程**：用户在注册页面看到机器码 → 将机器码+注册信息发给管理员 → 管理员用签名工具生成注册码 → 用户输入注册码完成注册
- **难易程度**：中
- **完成状态**：未开始
- **验证方式**：运行 `--generate` 生成密钥对 → 用 `--sign` 生成注册码 → 用 `--verify` 验证通过

### 步骤 7：实现邮件发送服务（EmailService）

- **修改内容**：
  - 新建 `src/core/auth/EmailService.h`，声明：
    ```cpp
    class EmailService : public QObject {
        Q_OBJECT
    public:
        explicit EmailService(QObject* parent = nullptr);

        // 发送注册信息到管理员邮箱
        Q_INVOKABLE bool sendRegistrationInfo(const QString& username,
                                               const QString& userEmail,
                                               const QString& phone,
                                               const QString& machineId);

        // 配置 SMTP 服务器（从配置文件读取）
        void loadSmtpConfig();

    signals:
        void sendSuccess();
        void sendFailed(const QString& reason);

    private:
        QString m_smtpServer;
        int m_smtpPort;
        QString m_senderEmail;
        QString m_senderPassword;
        QString m_recipientEmail;  // 管理员接收邮箱
        bool m_useSsl;
    };
    ```
  - 新建 `src/core/auth/EmailService.cpp`，实现：
    - `loadSmtpConfig()`：从配置文件或硬编码常量加载 SMTP 服务器配置
    - `sendRegistrationInfo()`：构建邮件内容（包含用户名、邮箱、手机号、机器码），通过 SMTP 协议发送到管理员指定邮箱
    - **技术方案选择**：
      - **方案 A（推荐）**：使用 Qt 的 `QSslSocket` 直接实现 SMTP 协议（SMTP AUTH + STARTTLS），无需额外依赖。邮件内容格式：
        ```
        主题：NetShare 注册申请 - {username}
        正文：
          用户名：{username}
          邮箱：{email}
          手机号：{phone}
          机器码：{machineId}
          申请时间：{timestamp}
        ```
      - **方案 B**：调用系统默认邮件客户端（`QDesktopServices::openUrl` with `mailto:` 协议），用户手动发送。优点：无需 SMTP 配置；缺点：用户体验差，无法自动填充机器码
      - **方案 C**：使用第三方 SMTP 库（如 `SmtpMime`），功能完善但增加依赖
    - **SMTP 配置**：管理员邮箱地址和 SMTP 服务器信息编译时硬编码或存储在配置文件中。发送邮箱使用专用noreply邮箱，密码加密存储
  - **注意**：邮件发送为异步操作，需在后台线程执行，避免阻塞 UI。发送期间注册页面显示"正在发送..."状态
- **难易程度**：高
- **完成状态**：未开始
- **验证方式**：调用 `sendRegistrationInfo()` 后管理员邮箱收到包含完整注册信息的邮件

### 步骤 8：创建注册页面 QML（弹窗形式）

- **修改内容**：
  - 新建 `src/gui/qml/RegisterPage.qml`：
    - **弹窗形式**（非全屏页面），居中显示在主窗口上方
    - 标题："NetShare 注册" 或 "欢迎使用 NetShare"
    - 输入区域：
      - 用户名输入框
      - 邮箱输入框
      - 手机号输入框
      - 注册码输入框（初始隐藏，点击"输入注册码"后展开）
    - **机器码展示区**：自动显示当前机器码（只读，`authService.currentMachineId()`），附带"复制机器码"按钮
    - 每个字段旁实时显示校验状态（绿色对勾/红色提示）
    - 操作按钮区：
      - **"试用"按钮**（醒目位置）：调用 `authService.enterTrialMode()` → 关闭弹窗 → 进入主界面（试用模式）
      - **"注册"按钮**：先校验输入 → 调用 `authService.sendRegistrationEmail()` 发送注册信息到管理员邮箱 → 提示"注册信息已发送，请等待注册码" → 展开注册码输入区
      - **"验证注册码"按钮**（注册码输入区展开后显示）：调用 `authService.registerUser()` → 验证通过则关闭弹窗进入主界面（正式模式）
    - 注册/发送邮件失败显示具体错误原因
    - 弹窗不可通过点击外部关闭（必须选择试用或注册）
- **难易程度**：中
- **完成状态**：未开始
- **验证方式**：编译运行，弹窗布局正确，试用/注册/验证注册码流程完整

### 步骤 9：修改主窗口启动流程与"关于"页面

- **修改内容**：
  - 修改 `src/gui/qml/Main.qml`：
    - 新增 `property bool isRegistered: authService.isRegistered()` 状态属性
    - 新增 `property bool isTrialMode: authService.isTrialMode()` 状态属性
    - 新增 `RegisterPage` 弹窗组件（使用 `Popup { modal: true; closePolicy: Popup.NoAutoClose }`，叠加在 Main.qml 上，不加入 pageStack）
    - 启动逻辑：`Component.onCompleted` 中判断 `isRegistered`，若未注册则显示注册弹窗
    - 试用模式下标题栏显示"试用版"标识
    - 注册弹窗关闭后（试用或注册成功）正常显示主界面
  - 修改 `src/gui/qml/SettingsPage.qml`（"关于"页面部分，currentSection=3）：
    - **注册信息展示区**（在现有版本号标签下方新增）：
      - 已注册时显示：用户名、注册日期（不显示邮箱、手机号、机器码，保护隐私）
      - 试用模式时显示："试用版"标识 + 机器码（只读 TextField + "复制"按钮） + "注册"按钮（机器码仅在未注册时展示，方便用户复制后发送给管理员）
    - **条件显示逻辑**：
      ```qml
      // 伪代码
      if (authService.isRegistered()) {
          // 显示：用户名标签 + authService.registeredUsername()
          // 显示：注册日期标签 + authService.registeredDate()
      } else if (authService.isTrialMode()) {
          // 显示："试用版"标识（红色/橙色高亮）
          // 显示：机器码 + "复制"按钮
          // 显示："注册"按钮 → 打开 RegisterPage 弹窗
      }
      ```
    - **"注销注册"按钮**（可选，已注册时显示）：清除注册信息，恢复试用模式
  - 修改 `src/main.cpp`：
    - **启动顺序异步化改造**（见 C-49，这是本步骤最关键的改动）：
      - 当前 `initialize()` 是同步顺序调用：`initializeDatabase → initializeSettings → loadTranslator → initializeCoreServices → initializeNetworkServer → initializeTrayIcon → buildInjector → initializeQml`
      - 改造后：`initializeDatabase → initializeSettings → loadTranslator → initializeCoreServices → initializeAuth → initializeTrayIcon → buildInjector → initializeQml`（**跳过 `initializeNetworkServer`**）
      - 新增 `initializeAuth()` 方法：创建 `MachineFingerprint`、`EmailService`、`AuthService` 实例
      - 在 `initializeQml()` 中将这三个对象注册到 QML 上下文：
        - `m_engine->rootContext()->setContextProperty("authService", m_authService)`
        - `m_engine->rootContext()->setContextProperty("machineFingerprint", m_fingerprint)`
      - QML 层注册弹窗关闭后，通过信号通知 C++ 层启动网络服务：
        - 在 `NetShareApplication` 中新增 `onRegistrationCompleted()` 槽函数
        - QML 中 `RegisterPage` 弹窗关闭时调用 `authService.registrationCompleted()` 信号
        - `NetShareApplication` 连接此信号到 `onRegistrationCompleted()` 槽
        - `onRegistrationCompleted()` 中调用 `initializeNetworkServer()` + `buildInjector()`
      - **重要**：注册检查必须在网络服务启动前完成，否则未注册用户也能通过 HTTP 访问共享文件
  - 修改 `src/core/common/DiContainer.h`：
    - 新增 `AuthModule`（绑定 AuthService、MachineFingerprint、EmailService）
    - 在 `NetShareApplication::buildInjector()` 中注册 `AuthModule`
  - 修改 `src/network/RequestHandler.cpp` 或 `src/core/share/ShareManager.cpp`：
    - 在文件共享服务启动前检查 `authService->hasValidSecurityToken()`，无效则拒绝启动服务
    - 此为 `securityToken` 机制的跨模块集成点（见 C-31、C-43）
- **难易程度**：中
- **完成状态**：未开始
- **验证方式**：启动软件未注册时弹窗注册页；已注册时直接进入主界面；"关于"页正确显示注册信息

### 步骤 10：更新 CMakeLists.txt 构建配置

- **修改内容**：
  - 修改 `src/core/CMakeLists.txt`：
    - 新增 `auth/AuthService.h`、`auth/AuthService.cpp`、`auth/RegistrationKey.h`、`auth/RegistrationKey.cpp`、`auth/MachineFingerprint.h`、`auth/MachineFingerprint.cpp`、`auth/EmailService.h`、`auth/EmailService.cpp`、`auth/AntiDebug.h`、`auth/AntiDebug.cpp` 到源文件列表
    - 新增 `${CMAKE_CURRENT_SOURCE_DIR}/auth` 到 include 目录
    - 新增链接库（仅 WIN32 条件下）：
      - `bcrypt`（Windows CryptoAPI，用于 RSA 签名验证）
      - `ole32`、`oleaut32`、`wbemuuid`（WMI COM，用于硬件信息采集）
      - `iphlpapi`（网卡信息 API）
    - **重要**：`NetshareCore` 的 `target_link_libraries` 中需新增 `Qt6::Network`（EmailService 使用 `QSslSocket` 需要）。当前 `NetshareCore` 已链接 `Qt6::Network`，无需额外修改
  - 修改 `src/gui/CMakeLists.txt`：
    - 在 `qt_add_qml_module` 的 `QML_FILES` 中新增 `qml/RegisterPage.qml`
    - **注意**：`RegisterPage.qml` 文件顶部必须包含 `import NetShare` 声明（与项目其他 QML 文件一致，因为使用了 `qt_add_qml_module` 注册的 URI）
  - 修改根 `CMakeLists.txt`：
    - `find_package(Qt6 ... COMPONENTS ...)` 中补充 `Concurrent` 组件（EmailService 异步发送需要）。当前根 CMakeLists.txt 未包含此组件，需补充
- **难易程度**：低
- **完成状态**：未开始
- **验证方式**：完整编译通过，无链接错误

### 步骤 11：更新翻译文件

- **修改内容**：
  - 在 `src/gui/translations/netshare_zh_CN.ts` 和 `netshare_en.ts` 中添加注册页面和"关于"页注册信息区所有用户可见文本的翻译条目
  - 可通过 `lupdate` 工具自动提取新增 QML 中的 `qsTr()` 字符串
- **难易程度**：低
- **完成状态**：未开始
- **验证方式**：切换语言后注册页面和"关于"页文本正确显示

### 步骤 12：集成测试与安全审查

- **修改内容**：
  - 端到端测试完整试用→注册→使用流程
  - 反调试专项测试：
    - 正常启动软件，功能完全正常
    - 使用 x64dbg 附加到进程，30秒内功能静默异常
    - 使用 x64dbg 启动软件，注册弹窗仍显示（无法跳过）
    - 使用 Cheat Engine 搜索并修改 `isRegistered` 内存值，核心功能仍不可用
    - 在 `verifyRegistrationCode()` 下断点，函数返回 false
    - 使用 API Monitor 监控 `IsDebuggerPresent` 调用，软件不崩溃
  - 安全审查项：
    - 确认私钥未出现在任何源码、构建产物中
    - 确认公钥为常量硬编码，非外部可替换文件
    - 确认注册码无法被篡改或伪造（无私钥情况下）
    - 确认注册码重放攻击无效（绑定用户名+邮箱+手机号+机器码）
    - 确认注册码不可跨机器使用（机器码绑定）
    - 确认硬件指纹采集不泄露敏感信息（仅存储哈希值，不存储原始序列号）
    - 确认"关于"页不泄露邮箱、手机号、机器码（已注册时）
    - 确认 SMTP 授权码不以明文存储
    - 确认反调试检测不影响正常用户体验（无调试器时零误判）
    - 确认反调试检测被绕过后，核心功能仍受 securityToken 保护
    - 确认 PE 完整性校验的预存哈希与发布版本一致
  - 硬件指纹专项测试：
    - 同一机器多次启动，机器码一致
    - 模拟硬件变更（如禁用网卡），验证 `matchScore()` 返回正确分数
    - 匹配度 ≥ 60 分允许使用，< 60 分需重新注册
  - 边界测试：空输入、超长输入、特殊字符、SQL 注入尝试
- **难易程度**：中
- **完成状态**：未开始
- **验证方式**：用户确认功能完整、安全审查通过

---

## 3. 冲突与遗漏检查

### 3.1 已识别并整合的问题

| 编号 | 问题 | 整合位置 |
|------|------|----------|
| C-1 | 现有 `Security/RequireAuth` 设置是 HTTP 访问密码，与用户注册系统是不同概念，需明确区分 | 步骤 9 中需确保两套认证互不干扰；HTTP 访问密码保持现有逻辑不变 |
| C-2 | 现有 `TlsCertificateGenerator` 使用外部 `openssl` 命令行工具，注册码验证不应依赖用户机器安装 openssl | 步骤 3 中推荐使用 Windows CryptoAPI（`BCryptVerifySignature`），无需外部依赖；或使用 QProcess 调用 openssl 命令行作为备选方案 |
| C-3 | 原设计包含密码和登录功能，新需求改为试用/注册模式，无需密码和登录 | 已移除密码相关字段（password_hash、password_salt）和登录页面（LoginPage），改为试用/注册弹窗模式 |
| C-4 | 手机号正则仅匹配中国大陆号码，若需国际化需扩展 | 步骤 4 中注明当前为中国大陆规则，后续可扩展 |
| C-5 | 注册码绑定用户信息后，用户无法修改邮箱/手机号（否则注册码失效） | 此为设计意图，修改个人信息需重新申请注册码；可在设置页面增加"更新注册码"入口（不在本期范围） |
| C-6 | 多用户场景：当前设计支持多用户注册，但同一设备多用户是否有意义需确认 | 步骤 1 的 users 表支持多用户，但同一台机器通常只有一个注册用户 |
| C-7 | （已废弃）原 C-7 关于 password_salt 列的问题已不适用 | 新需求移除了密码机制，users 表不再需要 password_hash/password_salt 列 |
| C-8 | OpenSSL 开发头文件在 Qt 安装中可能不可用，无法直接 `#include <openssl/evp.h>` | 步骤 3 中已明确推荐使用 Windows CryptoAPI 或 QProcess 调用 openssl 命令行，避免依赖 OpenSSL 头文件 |
| C-9 | （已废弃）原 C-9 关于登录页隐藏标题栏的问题已不适用 | 新需求改为弹窗注册页，不存在登录页遮挡标题栏的问题 |
| C-10 | `AuthService` 使用 `QML_ELEMENT` 宏但项目统一使用 `setContextProperty` 注册，应保持一致 | `AuthService` 应去掉 `QML_ELEMENT`，改为在 `main.cpp` 中通过 `setContextProperty("authService", ...)` 注册，与项目现有模式一致 |
| C-11 | 签名工具生成的注册码较长（约344字符），用户手动输入容易出错 | 可考虑在注册码中加入连字符分隔（每32字符一组），或在注册页面支持粘贴方式输入，步骤 8 中需注意 |
| C-12 | 硬件指纹采集依赖 WMI COM，需确保 COM 环境正确初始化和清理 | `MachineFingerprint` 中需在构造时 `CoInitializeEx`，析构时 `CoUninitialize`；或使用 `CoInitializeSecurity` 设置安全级别 |
| C-13 | 部分主板/BIOS 可能返回空序列号，导致机器码不稳定 | `machineId()` 中需对空值做降级处理：若某项为空则跳过该项，仅使用非空项计算哈希；若全部为空则回退到使用 CPU ID + 网卡 MAC（这两项几乎不会同时为空） |
| C-14 | 虚拟网卡（VMware/VirtualBox/Hyper-V）的 MAC 地址不稳定，不应纳入机器码 | `getPrimaryMacAddress()` 中需过滤虚拟网卡，通过适配器描述或厂商前缀识别并排除 |
| C-15 | 启动时硬件匹配度校验需存储各硬件项的分项哈希，而非仅存总机器码 | users 表需新增 `hardware_components TEXT` 列，存储 JSON 格式的各硬件项哈希，用于 `matchScore()` 逐项比对 |
| C-16 | WMI 查询可能较慢（首次 100-500ms），影响启动体验 | `MachineFingerprint` 应在后台线程采集硬件信息，或首次采集后缓存到本地配置文件，后续启动直接读取缓存（定期刷新） |
| C-17 | SMTP 发送邮件需要网络连接，离线环境下无法发送注册信息 | `EmailService` 需处理网络异常，发送失败时提示用户检查网络连接，并提供"复制注册信息到剪贴板"的备选方案，用户可手动发送 |
| C-18 | SMTP 发送邮箱的密码存储在软件中存在泄露风险 | 发送邮箱密码应加密存储（使用 `QSettings` + 简单混淆），或使用应用专用密码（如 QQ邮箱的授权码）。更安全的方案：使用 OAuth2 认证（实现复杂，本期不采用） |
| C-19 | 试用模式无任何限制可能导致用户无限试用而不注册 | 本期不限制试用功能，仅通过"试用版"标识引导注册。后续版本可增加试用天数限制（如30天）或功能限制 |
| C-20 | 注册弹窗不可关闭可能导致用户强制结束进程 | 弹窗必须提供"试用"按钮作为退出路径，用户不会被困在弹窗中。不提供关闭按钮（X）是合理的，因为有试用选项 |
| C-21 | "关于"页面需要读取注册信息，需确保 AuthService 在 SettingsPage 之前初始化 | `AuthService` 在 `main.cpp` 中创建并注册到 QML 上下文，早于任何 QML 页面加载，无此问题 |
| C-22 | 邮件发送包含机器码，机器码本身是哈希值，管理员无法从中还原硬件信息用于签名工具 | 管理员签名工具需要的是用户名+邮箱+手机号+机器码，机器码已包含在邮件中，管理员可直接使用。签名工具的 `--sign` 参数接受机器码原文 |
| C-23 | 试用用户后续注册时，数据库已有 "TrialUser" 记录，直接 INSERT 会违反 `username UNIQUE` 约束 | `registerUser()` 中需判断：若当前为试用用户则 UPDATE 现有记录（更新 username/email/phone/registration_code/is_trial），否则 INSERT 新记录 |
| C-24 | 已注册用户启动时机器码校验失败（匹配度 < 60），需要明确处理方式 | 启动流程中增加校验分支：匹配度 < 60 时提示用户并弹窗注册页面，用户需重新申请注册码 |
| C-25 | `email` 和 `phone` 列在试用模式下为空，但原表定义 `NOT NULL`，会导致 INSERT 失败 | 已修正表定义，`email TEXT` 和 `phone TEXT` 允许为空，正式注册时由 `AuthService` 校验必填 |
| C-26 | `matchScore()` 参数原设计为 `registeredMachineId`（总机器码），但权重匹配需逐项比对各硬件哈希 | 已修正为 `matchScore(const QString& registeredComponents)`，接收 `hardware_components` JSON 参数 |
| C-27 | 反调试检测可能产生误判（如杀毒软件注入 DLL 触发 `IsDebuggerPresent`） | `isDebugEnvironment()` 不应仅依赖单一检测，需多维度综合判断。若仅 `IsDebuggerPresent` 命中但其他检测均正常，可视为杀毒软件误报，不触发降级。建议：至少 2 项检测命中才触发降级 |
| C-28 | `NtQueryInformationProcess` 为未公开 API，未来 Windows 版本可能移除 | 使用 `GetProcAddress` 动态加载，若加载失败则跳过此项检测，不影响其他检测 |
| C-29 | PE 完整性校验的预存哈希在每次编译后都会变化，维护成本高 | 发布构建后通过脚本自动计算 `.text` 段哈希并写入源码。或改为运行时计算首次哈希并缓存，后续比对缓存值（防篡改而非防重编译） |
| C-30 | 反调试定时检测（30秒间隔）可能影响性能 | 检测逻辑本身耗时极短（<1ms），30秒间隔对性能无影响。但应避免在 UI 线程执行耗时操作 |
| C-31 | `securityToken` 机制需要核心功能（文件共享服务）配合改造 | 核心功能启动时需检查 `authService` 的 `securityToken` 是否有效，无效则拒绝启动服务。此为跨模块改动，需在步骤 9 中明确 |
| C-32 | 攻击者可 patch 掉 `AntiDebug::isDebugEnvironment()` 使其始终返回 false | 反调试代码分散嵌入各关键函数中，不集中调用 `isDebugEnvironment()`，而是各函数内联检测逻辑。同时 `verifyFunctionIntegrity()` 可检测函数被篡改 |
| C-33 | 反调试检测在开发/调试阶段会干扰正常开发 | 使用编译宏 `#ifdef NDEBUG` 包裹反调试代码，Debug 构建时完全禁用，Release 构建时启用 |
| C-34 | 计划文档 C-1 原引用"步骤 8"但步骤 8 是注册页 QML，C-1 涉及的是主窗口集成 | 已修正为引用步骤 9（修改主窗口启动流程与"关于"页面） |
| C-35 | 计划文档 C-11 原引用"步骤 7"但步骤 7 是邮件服务，注册码输入在注册页面 | 已修正为引用步骤 8（创建注册页面 QML） |
| C-36 | `src/core/CMakeLists.txt` 中 `NetshareCore` 未链接 `Qt6::Network`，但 `EmailService` 使用 `QSslSocket` 需要此模块 | 步骤 10 中需在 `NetshareCore` 的 `target_link_libraries` 中新增 `Qt6::Network` |
| C-37 | `src/gui/CMakeLists.txt` 使用 `qt_add_qml_module` 注册 QML 模块（URI NetShare），新增 `RegisterPage.qml` 必须加入 `QML_FILES` 列表 | 步骤 10 已覆盖，但需注意 QML 文件的 import 声明必须包含 `import NetShare` |
| C-38 | `main.cpp` 中 `initializeQml()` 在 `initializeNetworkServer()` 之后调用，但注册检查应在网络服务启动前执行 | 步骤 9 中需调整启动顺序：先检查注册状态 → 未注册时弹窗 → 用户选择试用/注册后 → 再启动网络服务。否则未注册用户也能通过 HTTP 访问共享文件 |
| C-39 | `DatabaseManager::createTables()` 当前调用链为 `createSharesTable() && createTransferLogsTable() && createSettingsTable()`，新增 `createUsersTable()` 需追加 | 步骤 1 已说明"在 `createTables()` 中追加调用"，但需注意 `createUsersTable()` 应在 `createSettingsTable()` 之前调用（users 表可能被 settings 引用） |
| C-40 | `SettingsManager` 使用 `QML_ELEMENT` 宏（见 SettingsManager.h），但计划文档 C-10 说项目统一使用 `setContextProperty` | 实际项目存在两种模式并存：`SettingsManager` 用 `QML_ELEMENT`，`ShareManager` 等用 `setContextProperty`。`AuthService` 等新类应使用 `setContextProperty`（与 main.cpp 中其他服务一致），C-10 的建议仍然正确 |
| C-41 | `Main.qml` 使用 `StackView` + `Component` 管理页面切换，注册弹窗应使用 `Popup` 或 `Dialog` 而非新的 StackView 页面 | 步骤 8 和 9 中需明确：注册弹窗使用 `Popup { modal: true; closePolicy: Popup.NoAutoClose }` 叠加在 Main.qml 上，不加入 pageStack |
| C-42 | `SettingsPage.qml` 的"关于"区域（currentSection=3）当前仅有版本号标签，需新增注册信息展示区 | 步骤 9 中需详细说明"关于"区域的 QML 布局改动，包括条件显示逻辑（已注册 vs 试用模式） |
| C-43 | `securityToken` 机制需要 `CivetWebServer` 和 `ShareManager` 等核心服务配合，但计划文档未列出这些文件的修改 | 步骤 9 的修改文件清单中需新增 `src/network/CivetWebServer.cpp`（或 `RequestHandler.cpp`）和 `src/core/share/ShareManager.cpp`，在服务启动前检查 token |
| C-44 | `main.cpp` 中 `NetShareApplication` 类的 `initialize()` 方法按固定顺序初始化各模块，注册检查需插入到 `initializeQml()` 之前 | 步骤 9 中需明确：在 `initializeQml()` 之前新增 `initializeAuth()` 方法，创建 AuthService/MachineFingerprint/EmailService 并注册到 QML 上下文。注册弹窗在 QML 层通过 `Component.onCompleted` 触发 |
| C-45 | `src/gui/CMakeLists.txt` 中 `NetshareGui` 链接了 `NetshareCore`，新增的 auth 模块在 `NetshareCore` 中，QML 可通过 context property 访问 | 链接关系正确，无需额外修改。但 `EmailService` 使用的 `QSslSocket` 需要 `Qt6::Network`，需确保 `NetshareCore` 链接了此模块（见 C-36） |
| C-46 | 根 `CMakeLists.txt` 中 `find_package` 未包含 `Concurrent` 组件，但 `EmailService` 异步发送可能需要 `Qt6::Concurrent` | `NetshareCore` 已链接 `Qt6::Concurrent`，根 CMakeLists.txt 的 `find_package` 需确认包含 `Concurrent` 组件。当前根 CMakeLists.txt 未列出 `Concurrent`，需补充 |
| C-47 | `DiContainer.h` 使用 Boost.DI 注入器，新增 AuthService 等需要更新注入模块，否则其他模块无法通过 DI 获取 AuthService | 步骤 9 中需在 `DiContainer.h` 新增 `AuthModule`（绑定 AuthService、MachineFingerprint、EmailService），并在 `main.cpp` 的 `buildInjector()` 中注册 |
| C-48 | `ShareManager` 同时使用 `QML_ELEMENT` + `QML_SINGLETON`（声明式注册）和 `setContextProperty`（命令式注册），存在双重注册 | 项目实际存在双重注册模式：`QML_ELEMENT`/`QML_SINGLETON` 的类通过 QML 模块系统自动注册，`setContextProperty` 作为备用。`AuthService` 等新类仅使用 `setContextProperty` 即可（与 ChatService、NotificationManager 等一致），无需 `QML_ELEMENT`。C-40 描述已更新 |
| C-49 | **启动顺序异步化**：当前 `initialize()` 是同步顺序调用，`initializeNetworkServer()` 在 `initializeQml()` 之前执行。注册检查在 QML 层（异步），用户选择试用/注册后才应启动网络服务。需要将 `initializeNetworkServer()` 延迟到注册完成后 | 步骤 9 中需改造启动流程：`initializeAuth()` → `initializeQml()`（含注册弹窗）→ 用户选择试用/注册后通过信号通知 C++ 层 → C++ 层收到信号后才调用 `initializeNetworkServer()`。需在 `NetShareApplication` 中新增 `onRegistrationCompleted()` 槽函数，由 QML 的注册弹窗关闭信号触发 |

### 3.2 需用户确认的决策点

| 编号 | 问题 | 建议选项 |
|------|------|----------|
| D-1 | 试用模式是否需要限制功能或天数？ | 建议本期不限制，仅显示"试用版"标识；后续可增加30天试用限制 |
| D-2 | 注册码是否需要设置有效期？ | 建议本期不设有效期，注册码永久有效 |
| D-3 | 管理员签名工具是否需要集成到项目中构建？ | 建议作为独立项目，不纳入 NetShare 主构建 |
| D-4 | 邮件发送方案选择？ | 建议方案 A（QSslSocket 直接实现 SMTP），无需额外依赖；备选方案 B（调用系统邮件客户端） |
| D-5 | 手机号是否仅限中国大陆？ | 建议本期仅支持中国大陆，后续可扩展国际号码 |
| D-6 | 硬件变更容错阈值（匹配度）设为多少？ | 建议 ≥ 60 分允许使用（容忍一项硬件变更），可由用户确认 |
| D-7 | 硬件指纹是否需要缓存到本地文件以加速启动？ | 建议缓存，首次采集后写入配置文件，每次启动校验缓存有效性（若硬件变更则更新缓存） |
| D-8 | 注册页面是否需要展示详细硬件信息（主板型号等）供用户确认？ | 建议仅展示机器码，不展示原始硬件信息（避免信息泄露）；可选增加"查看详细硬件信息"折叠面板 |
| D-9 | SMTP 发送邮箱使用哪个服务商？ | 建议使用 QQ 邮箱（需申请授权码）或 163 邮箱，国内 SMTP 服务器稳定可靠 |
| D-10 | 管理员接收邮箱地址是否需要可配置？ | 建议编译时硬编码管理员邮箱地址，避免用户误改。若需灵活性可存储在配置文件中 |
| D-11 | 反调试检测触发降级时，是否需要"至少2项命中"的容错机制？ | 建议启用，避免杀毒软件等合法软件注入导致误判。仅1项命中时记录日志但不降级 |
| D-12 | PE 完整性校验采用哪种方案？ | 建议方案 B（运行时计算首次哈希并缓存），维护成本低；方案 A（编译时嵌入预存哈希）安全性更高但维护成本高 |
| D-13 | 反调试检测是否需要在 Debug 构建中完全禁用？ | 建议是，使用 `#ifdef NDEBUG` 包裹，Debug 构建完全禁用，方便开发调试 |
| D-14 | 启动流程异步化：网络服务延迟到注册完成后启动，是否可接受？ | 建议接受。当前 `initializeNetworkServer()` 在 `initializeQml()` 之前同步执行，改造后需延迟到注册弹窗关闭后异步执行。这意味着已注册用户启动时会先显示主窗口再启动网络服务（有短暂延迟），但安全性显著提升 |

### 3.3 安全性复核

| 检查项 | 结果 |
|--------|------|
| 私钥是否可能泄露到软件包中？ | 否。私钥仅在管理员签名工具中使用，不参与软件构建 |
| 公钥是否可被替换？ | 硬编码在 C++ 常量中，替换需重新编译。若需更高安全性，可考虑对二进制做签名验证（超出本期范围） |
| 注册码是否可被逆向分析？ | RSA 签名无法在无私钥情况下伪造。但若攻击者反编译替换公钥，则可绕过。这是所有客户端验证的固有局限 |
| 是否存在 SQL 注入风险？ | 所有数据库操作使用参数化查询（`QVariantList` 绑定），无拼接 SQL |
| 注册码是否可重放？ | 注册码绑定特定用户名+邮箱+手机号+机器码，不同信息或不同机器无法使用同一注册码 |
| 注册码是否可跨机器使用？ | 否。注册码签名载荷包含机器码，且启动时校验当前硬件与注册时的匹配度 |
| 硬件信息是否泄露隐私？ | 数据库仅存储各硬件项的 SHA-256 哈希值，不存储原始序列号。机器码为哈希摘要，不可逆推原始硬件信息 |
| 硬件信息是否可伪造？ | 攻击者可尝试伪造 WMI 返回值，但需修改系统底层，门槛较高。若需更高安全性，可改用内核级硬件信息采集（超出本期范围） |
| SMTP 密码是否安全？ | 发送邮箱密码使用授权码（非登录密码），存储时简单混淆。OAuth2 方案更安全但实现复杂，本期不采用 |
| 邮件传输是否安全？ | 使用 SMTP over TLS（STARTTLS），邮件内容加密传输 |
| 试用模式是否可被滥用？ | 本期试用无限制，后续可增加天数或功能限制。试用模式不影响已注册用户 |
| 反调试检测是否影响正常用户？ | 不影响。无调试器环境下所有检测返回 false，零误判（启用至少2项命中容错机制后） |
| 攻击者能否简单 patch 跳过注册弹窗？ | 不能简单跳过。注册状态由 C++ 层 securityToken 控制，QML 属性修改无效。核心功能依赖有效 token |
| 攻击者能否通过调试器绕过注册码验证？ | 难度较高。多维度反调试检测（5种方法），检测到后静默降级（伪造结果），不提示攻击者 |
| 攻击者能否通过内存修改绕过？ | 难度较高。securityToken 为随机 QByteArray，无法通过简单搜索修改。定时检测可发现调试器附加 |
| 反调试代码是否可被一次性移除？ | 难度较高。反调试代码分散嵌入 AuthService、RegistrationKey、MachineFingerprint 等多处，需逐个定位和 patch |

---

## 4. 依赖关系图

```
步骤1 (数据库表) ──→ 步骤4 (AuthService)
步骤2 (硬件指纹) ──→ 步骤4 (AuthService)
步骤3 (注册码验证) ──→ 步骤4 (AuthService)
步骤5 (反调试) ──→ 步骤4 (AuthService) [嵌入调用]
步骤5 (反调试) ──→ 步骤3 (RegistrationKey) [嵌入调用]
步骤5 (反调试) ──→ 步骤2 (MachineFingerprint) [嵌入调用]
步骤6 (签名工具) ──→ 步骤3 (提供公钥PEM)
步骤7 (邮件服务) ──→ 步骤4 (AuthService)
步骤4 (AuthService) ──→ 步骤8 (注册页QML)
步骤8 (注册页) ──→ 步骤9 (主窗口+关于页)
步骤10 (CMake) ──→ 全部步骤编译依赖
步骤11 (翻译) ──→ 步骤8+9完成后执行
步骤12 (测试) ──→ 全部步骤完成后执行
```

**推荐执行顺序**：6 → 2 → 3 → 5 → 7 → 1 → 4 → 10 → 8 → 9 → 11 → 12

---

## 5. 技术选型说明

### 5.1 为什么选择 RSA 签名而非加密？

- **RSA 签名**：私钥签名，公钥验证。软件只需公钥即可验证注册码合法性，无私钥则无法伪造。
- **RSA 加密**：公钥加密，私钥解密。若软件需要解密注册码内容则需私钥，与"私钥不在软件中"的要求矛盾。
- 因此选择**签名方案**，这也是 JetBrains、Adobe 等商业软件离线激活的通用做法。

### 5.2 RSA 签名验证技术方案

**方案对比**：

| 方案 | 优点 | 缺点 | 推荐度 |
|------|------|------|--------|
| Windows CryptoAPI (`BCryptVerifySignature`) | 零外部依赖，Windows 10+ 原生支持，性能好 | 仅限 Windows 平台 | 推荐（本项目仅支持 Windows） |
| QProcess 调用 `openssl dgst -verify` | 与现有 `TlsCertificateGenerator` 风格一致，实现简单 | 依赖用户机器安装 openssl，启动进程开销大 | 备选 |
| 直接使用 OpenSSL 库 API | 性能好，跨平台 | 需 OpenSSL 开发头文件，Qt 安装可能不包含 | 不推荐 |

**推荐方案**：使用 Windows CryptoAPI，核心 API：
- `BCryptOpenAlgorithmProvider` —— 打开 RSA 算法提供者
- `BCryptImportKeyPair` —— 导入公钥
- `BCryptVerifySignature` —— 验证签名
- 头文件：`<bcrypt.h>`，链接库：`bcrypt.lib`

Qt 辅助 API（用于 PEM 解析和哈希计算）：
- `QSslKey` —— 解析 PEM 格式公钥，提取原始 RSA 公钥字节
- `QCryptographicHash` —— 计算 SHA-256 摘要

### 5.3 注册码格式示例

```
机器码：SHA-256("MB-12345|BIOS-67890|CPUID-AABBCCDD|MAC-001122334455") 前16字节 Base64 → "Xk9mP3qR7sT2vW5y"
载荷：SHA-256("zhangsan|zhangsan@example.com|13800138000|Xk9mP3qR7sT2vW5y")
签名：RSA-2048-Sign(载荷) → 256字节二进制
注册码：Base64(签名) → 约344字符的ASCII字符串
```

用户收到的注册码形如：
```
A3Bf9x2K...（约344字符）...mQ7pR==
```

### 5.4 硬件指纹采集技术方案

**Windows API 调用链**：

| 采集项 | API 调用 | 返回值示例 |
|--------|----------|-----------|
| 主板序列号 | WMI `Win32_BaseBoard::SerialNumber` | `"PF1RKQ4R"` |
| BIOS 序列号 | WMI `Win32_BIOS::SerialNumber` | `"H1CK421002Y"` |
| CPU ID | `__cpuid` leaf 1 EAX/EBX/ECX/EDX | `"0x0A0671..."` |
| 网卡 MAC | `GetAdaptersAddresses` → `PhysicalAddress` | `"00:1A:2B:3C:4D:5E"` |

**WMI COM 调用流程**：
```
CoInitializeEx → CoInitializeSecurity → CoCreateInstance(IWbemLocator)
→ IWbemLocator::ConnectServer("ROOT\\CIMV2") → IWbemServices::ExecQuery("SELECT SerialNumber FROM Win32_BaseBoard")
→ IEnumWbemClassObject::Next → IWbemClassObject::Get("SerialNumber")
```

**虚拟网卡过滤规则**：
- 排除描述中包含 `Virtual`、`VMware`、`VirtualBox`、`Hyper-V`、`Tunnel`、`Loopback` 的适配器
- 排除 `IfType` 为 `IF_TYPE_SOFTWARE_LOOPBACK`（24）或 `IF_TYPE_TUNNEL`（131）的适配器
- 仅取 `OperStatus` 为 `IfOperStatusUp`（1）的适配器

### 5.5 邮件发送技术方案

**SMTP 协议实现**（使用 `QSslSocket`）：

```
连接服务器 → EHLO → STARTTLS → AUTH LOGIN → MAIL FROM → RCPT TO → DATA → 邮件内容 → QUIT
```

**核心流程**：
1. `QSslSocket::connectToHostEncrypted(smtpServer, port)` 建立 TLS 连接
2. 发送 `EHLO` 握手
3. 发送 `STARTTLS` 升级为加密连接
4. 发送 `AUTH LOGIN` + Base64 编码的用户名/密码（授权码）
5. 发送 `MAIL FROM` + `RCPT TO` 指定发件人和收件人
6. 发送 `DATA` + 邮件头（From/To/Subject/Date/Content-Type）+ 邮件正文
7. 发送 `QUIT` 断开连接

**常用 SMTP 服务器配置**：

| 服务商 | 服务器 | 端口 | 说明 |
|--------|--------|------|------|
| QQ 邮箱 | smtp.qq.com | 465(SSL)/587(STARTTLS) | 需申请授权码，非QQ密码 |
| 163 邮箱 | smtp.163.com | 465(SSL)/994(SSL) | 需开启 SMTP 并获取授权码 |
| 阿里邮箱 | smtp.aliyun.com | 465(SSL) | 企业邮箱推荐 |

**邮件内容编码**：
- 邮件头使用 UTF-8 + Base64 编码（`Subject: =?UTF-8?B?...?=`）
- 邮件正文使用 UTF-8 + quoted-printable 编码
- Content-Type: `text/plain; charset=UTF-8`

**异步发送**：
- 使用 `QThread` + `QObject::moveToThread` 在后台线程执行 SMTP 通信
- 通过信号 `sendSuccess()` / `sendFailed(reason)` 通知 UI 层

### 5.6 反调试与防破解技术方案

**Windows 反调试 API 详解**：

| 检测方法 | API / 技术 | 检测原理 | 绕过难度 |
|----------|-----------|----------|----------|
| `IsDebuggerPresent` | `kernel32.dll` 导出函数 | 检查 PEB 的 `BeingDebugged` 标志 | 低（可 patch PEB） |
| `CheckRemoteDebuggerPresent` | `kernel32.dll` 导出函数 | 检查进程是否被远程调试器附加 | 低 |
| `NtQueryInformationProcess` | `ntdll.dll` 未公开 API | 查询 `ProcessDebugPort`（0x7）或 `ProcessDebugObjectHandle`（0x1E） | 中（需 hook ntdll） |
| 硬件断点检测 | `GetThreadContext` | 读取调试寄存器 Dr0-Dr3，非零表示有硬件断点 | 中（需修改 CONTEXT 结构） |
| 定时器检测 | `QueryPerformanceCounter` | 测量代码段执行时间，被调试时显著变慢 | 中（需 hook QPC） |
| 父进程检测 | `CreateToolhelp32Snapshot` | 检查父进程是否为 explorer.exe | 低（可修改进程名） |
| 内存完整性校验 | `QCryptographicHash` | 计算关键函数内存区域的哈希，与预存值比对 | 高（需同时 patch 校验逻辑和预存哈希） |
| PE 完整性校验 | 读取自身 EXE 文件 | 计算 .text 段哈希，与首次运行缓存值比对 | 高 |

**反调试代码分散嵌入策略**：

```
AuthService::isRegistered()
  ├── 调用 AntiDebug::isDebugEnvironment()  [入口1]
  ├── 检查 m_securityToken 有效性
  └── 返回结果

AuthService::registerUser()
  ├── 内联反调试检测（不调用 AntiDebug 命名空间）  [入口2]
  │   ├── 直接调用 IsDebuggerPresent()
  │   └── 直接调用 GetThreadContext() 检查 Dr0
  ├── 验证注册码
  └── 返回结果

RegistrationKey::verifyRegistrationCode()
  ├── 内联反调试检测  [入口3]
  │   ├── 直接调用 NtQueryInformationProcess (动态加载)
  │   └── 定时器检测
  ├── 验证签名
  └── 返回结果

MachineFingerprint::machineId()
  ├── 内联反调试检测  [入口4]
  │   └── 父进程检测
  ├── 采集硬件信息
  └── 返回机器码（调试环境下返回伪造值）
```

**securityToken 机制**：

```cpp
// AuthService 内部
QByteArray m_securityToken;  // 注册成功时随机生成，64字节

// 注册成功时
m_securityToken = QCryptographicHash::hash(
    QUuid::createUuid().toByteArray() + QByteArray::number(QDateTime::currentMSecsSinceEpoch()),
    QCryptographicHash::Sha256
);

// 暴露给核心功能模块
Q_INVOKABLE bool hasValidSecurityToken() const;

// 核心功能（文件共享服务）启动前检查
if (!authService->hasValidSecurityToken()) {
    // 拒绝启动服务，不提示原因
    return;
}
```

**编译期防护配置**（CMake Release 构建）：

```cmake
if(MSVC)
    # Release 构建优化选项
    add_compile_options(/O2 /GL /DNDEBUG)       # 全局优化 + 链接期代码生成
    add_link_options(/LTCG)                       # 链接期代码生成
    # 禁用调试符号
    add_compile_options(/Zi-)
    # 去除 RTTI（可选，减小二进制体积）
    add_compile_options(/GR-)
endif()
```

**Debug 构建条件编译**：

```cpp
// AntiDebug.cpp 中所有检测函数
bool AntiDebug::isDebugEnvironment() {
#ifdef NDEBUG
    // Release 构建：执行完整反调试检测
    int hitCount = 0;
    if (isDebuggerPresent()) hitCount++;
    if (isNtDebugged()) hitCount++;
    if (isHardwareBreakpointSet()) hitCount++;
    if (isTimingAnomaly()) hitCount++;
    if (isParentSuspicious()) hitCount++;
    return hitCount >= 2;  // 至少2项命中才判定
#else
    // Debug 构建：跳过所有检测
    return false;
#endif
}
```
