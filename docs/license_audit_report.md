# NetShare 项目模块及开源协议审计报告

**审计日期：** 2026-06-12  
**项目版本：** 1.0.0  
**分析范围：** 所有自研模块 + 第三方依赖

---

## 一、项目模块清单

NetShare 使用 CMake 构建，由以下 **7 个自研模块** 构成，并依赖 **多个第三方库**。

### 1.1 自研模块总览

| 模块名称 | 目录 | 类型 | 功能说明 | 文件数 |
|---------|------|------|---------|-------|
| **NetshareCore** | `src/core/` | 静态库 | 业务核心：文件分享、分块传输、聊天、认证授权、通知、日志、设置 | 27 |
| **NetshareDatabase** | `src/database/` | 静态库 | 数据库管理（SQLite 封装） | 2 |
| **NetshareNetwork** | `src/network/` | 静态库 | HTTP 服务器（基于 CivetWeb）、mDNS 服务发现 | 5 |
| **NetshareQrcode** | `src/qrcode/` | 静态库 | 二维码生成（包装 Project Nayuki 库） | 2 |
| **NetshareGui** | `src/gui/` | 静态库 | Qt Quick/QML 桌面 GUI 界面 | 11 QML + 2 C++ |
| **Web** | `web/` | 资源 | Web 前端 HTML/CSS/JS 界面 | 5 HTML + 1 JS |
| **NetShareLicense** | `dist/keygen/` | 可执行程序 | 许可证密钥生成工具 | 1 C++ |

### 1.2 自研模块架构依赖关系

```
NetShare (主可执行程序)
 ├─ NetshareGui (GUI 层)
 │   ├─ NetshareCore (业务逻辑层)
 │   └─ NetshareQrcode (二维码库)
 ├─ NetshareNetwork (网络层)
 │   ├─ NetshareCore
 │   └─ CivetWeb (第三方 HTTP 服务器)
 ├─ NetshareDatabase (数据层)
 │   ├─ NetshareCore
 │   └─ Qt6::Sql → SQLite3 (数据库引擎)
 ├─ NetshareCore (核心层)
 │   └─ Boost.DI (依赖注入框架)
 └─ Qt6 (基础框架)
```

---

## 二、第三方依赖开源协议详情

### 2.1 Qt 6.8 — LGPL v3 / GPL v3

| 项目 | 值 |
|-----|-----|
| **组件** | Qt6::Core, Gui, Network, Sql, Qml, Quick, QuickControls2, QuickDialogs2, QuickEffectsPrivate, Widgets, Concurrent, LinguistTools, Test |
| **许可证** | **GNU LGPL v3**（基础模块）+ **GNU GPL v3**（部分高级模块） |
| **版权方** | The Qt Company |

**NetShare 使用的 Qt 子模块许可分类：**

| Qt 模块 | 许可证 | NetShare 是否使用 |
|---------|--------|:----:|
| Qt6::Core | LGPL v3 | ✅ |
| Qt6::Gui | LGPL v3 | ✅ |
| Qt6::Network | LGPL v3 | ✅ |
| Qt6::Sql | LGPL v3 | ✅ |
| Qt6::Qml | LGPL v3 | ✅ |
| Qt6::Quick | LGPL v3 | ✅ |
| Qt6::QuickControls2 | LGPL v3 | ✅ |
| Qt6::QuickDialogs2 | LGPL v3 | ✅ |
| Qt6::QuickEffectsPrivate | LGPL v3 | ✅ |
| Qt6::Widgets | LGPL v3 | ✅ |
| Qt6::Concurrent | LGPL v3 | ✅ |
| Qt6::LinguistTools | LGPL v3 | ✅（构建工具） |
| Qt6::Test | LGPL v3 | ✅（测试） |

**结论：** NetShare 使用的均为 LGPL v3 模块，未使用 GPL v3 专有的模块。

### 2.2 CivetWeb — MIT License

| 项目 | 值 |
|-----|-----|
| **目录** | `third_party/civetweb_src/` |
| **许可证** | **MIT License** |
| **版权方** | CivetWeb developers (2013-2021), Sergey Lyubka (2004-2013) |
| **用途** | 嵌入式 HTTP/HTTPS 服务器，处理文件分享的 HTTP 请求和 WebSocket 通信 |

**CivetWeb 捆绑的子组件：**

| 子组件 | 许可证 | 说明 |
|--------|--------|------|
| CivetWeb 核心 | MIT | HTTP/HTTPS 服务器核心 |
| **SQLite3** | **Public Domain（SQLite Blessing）** | 嵌入式数据库引擎 |
| Lua 5.4.3 | MIT | 嵌入式脚本语言（未使用） |
| LuaXML | MIT | XML 解析（未使用） |
| Duktape 1.5.2 / 1.8.0 | MIT | 嵌入式 JavaScript 引擎（未使用） |
| cJSON | MIT | JSON 解析（示例，未使用） |

### 2.3 Boost.DI — Boost Software License 1.0

| 项目 | 值 |
|-----|-----|
| **目录** | `third_party/boost-di/` |
| **许可证** | **Boost Software License 1.0** |
| **版权方** | Kris Jusiak (2012-2020) |
| **用途** | C++ 依赖注入（DI）框架，实现控制反转（IoC） |

### 2.4 QR Code Generator (Project Nayuki) — MIT License

| 项目 | 值 |
|-----|-----|
| **文件** | `src/qrcode/qrcodegen.hpp` + `qrcodegen.cpp` |
| **许可证** | **MIT License** |
| **版权方** | Project Nayuki |
| **来源** | https://www.nayuki.io/page/qr-code-generator-library |
| **用途** | 生成分享链接的二维码图片 |

### 2.5 Windows 系统库（无开源义务）

| 库名 | 说明 |
|-----|------|
| `bcrypt` | Windows 加密 API（机器指纹、认证） |
| `ole32` / `oleaut32` | COM 组件支持 |
| `wbemuuid` | WMI 查询（机器指纹） |
| `iphlpapi` | 网络接口信息 |
| `dwmapi` | DWM 窗口特效 |
| `comctl32` | 通用控件（密钥生成工具） |

这些都是 Windows SDK 系统库，随操作系统提供，不附带开源义务。

---

## 三、各开源协议优缺点分析

### 3.1 MIT License（最宽松）

**适用模块：** CivetWeb、QR Code Generator、Qt 示例代码、CivetWeb 子组件（Lua, Duktape, cJSON, LuaXML）

**优点：**
- ✅ **极其宽松**：允许任意使用、修改、复制、分发、再许可
- ✅ **商业友好**：可在闭源商业软件中使用，无需公开源代码
- ✅ **专利友好**：无需担心专利授权问题
- ✅ **兼容性极佳**：几乎与所有其他开源许可兼容
- ✅ **低合规负担**：只需保留版权声明和免责声明即可

**缺点：**
- ❌ **无专利授权**：未明确授予专利使用权（相比 Apache 2.0）
- ❌ **无明文商标限制**：未禁止使用原作者名称进行推广
- ❌ **无条款变更控制**：未规定"如果起诉专利侵权则许可终止"的保护条款
- ❌ **担保免责较弱**：虽然是"按原样"提供，但法律保护力度不如 Apache 2.0 全面

**合规要求：** 在再分发时保留原始版权声明和 MIT 许可文本。

### 3.2 Boost Software License 1.0（宽松类）

**适用模块：** Boost.DI

**优点：**
- ✅ **极其宽松**：允许在商业和非商业项目中自由使用、修改和分发
- ✅ **无传染性**：不要求衍生作品使用相同许可
- ✅ **明确专利让步**：明确排除专利主张（与 BSD/MIT 类似但更规范）
- ✅ **简化文本**：许可文本短小精悍，法律负担低
- ✅ **C++ 生态标准**：Boost 库的官方许可，被广泛接受

**缺点：**
- ❌ **无明确的专利授权条款**：相比 Apache 2.0，专利保护不够明确
- ❌ **无商标保护**：未包含商标使用限制
- ❌ **知名度较低**：法律先例不如 MIT、Apache 丰富
- ❌ **不兼容 GPL v2**：Boost 1.0 与 GPL v2 存在兼容性问题（但与 GPL v3 兼容）

**合规要求：** 在再分发时保留版权声明，附上许可文本或链接。

### 3.3 GNU LGPL v3（弱著佐权）

**适用模块：** Qt 6.8 基础模块（Core, Gui, Network, Qml, Quick 等）

**优点：**
- ✅ **允许闭源商业使用**：动态链接专有代码时无需公开应用程序源码
- ✅ **修改透明**：最终用户可以替换/修改 Qt 库版本
- ✅ **专利保护**：包含明确的专利授权条款
- ✅ **兼容 GPL v3**：可与 GPL v3 代码结合使用
- ✅ **生态成熟**：Qt 使用 LGPL 已数十年，法律实践丰富
- ✅ **防止 TiVo 化**：LGPL v3 禁止硬件锁定

**缺点：**
- ❌ **需提供目标文件**：必须向用户提供足够的构建信息，使其能链接修改后的 Qt 库
- ❌ **修改必须开源**：如果修改了 Qt 库本身，必须公开发布修改后的源代码
- ❌ **静态链接受限**：如果静态链接 Qt，需要额外提供对象文件供用户重新链接
- ❌ **合规成本**：需要维护版权声明、许可文本等合规文档
- ❌ **兼容性约束**：与部分严格许可（如 GPL v2）不兼容

**合规要求：**
1. 随软件分发 LGPL v3 许可文本
2. 保留 Qt 的版权声明
3. 提供获取 Qt 源代码的方式（如链接到 Qt 官方源码仓库）
4. 如果使用了修改版 Qt，需提供修改后的源代码
5. 提供适当的构建信息（如使用的工具链、链接方式）

### 3.4 Public Domain (SQLite Blessing) — 无限制

**适用模块：** SQLite3（CivetWeb 捆绑版本）

**优点：**
- ✅ **完全无限制**：可以任意使用、修改、复制、分发
- ✅ **无需任何合规操作**：不需要保留版权声明或许可文本
- ✅ **商业友好度最高**：不存在任何许可证合规风险
- ✅ **专利风险最低**（取决于具体实现）

**缺点：**
- ❌ **法律地位不明确**：并非所有司法管辖区都承认"公有领域"概念
- ❌ **无担保条款**：为使用者提供的法律保护最少
- ❌ **无专利授权**：没有明确的专利保护机制
- ❌ **可能被他人专利包围**：无法防御专利侵权诉讼

**合规要求：** 无需额外操作（建议保留原始的 SQLite 祝福文本以示尊重）。

---

## 四、NetShare 项目自身代码许可状态

### 状态：⚠️ 未明确声明

NetShare 项目自身的源代码（`src/`、`web/`、`CMakeLists.txt`、测试代码等）**目前没有在任何文件中声明开源许可证**，项目根目录也未放置 `LICENSE` 文件。

### 建议

根据项目的第三方依赖推测，NetShare 可考虑以下许可方案：

| 建议方案 | 适用场景 | 与依赖的兼容性 |
|---------|---------|:-----------:|
| **GPL v3** | 希望开源且能接受强著佐权 | ✅ 兼容所有依赖 |
| **MIT** | 希望最宽松的商业友好许可 | ✅ 兼容所有依赖 |
| **LGPL v3** | 闭源商业应用，且动态链接 Qt | ✅ 兼容 LGPL Qt + MIT 依赖 |
| **Apache 2.0** | 需要专利保护条款 | ✅ 兼容所有依赖 |

**推荐：** 如果 NetShare 是开源项目，建议在根目录添加 `LICENSE` 文件明确许可条款。如果项目仅供学习或个人使用，暂无紧急风险。

---

## 五、许可证兼容性矩阵

| NetShare 自选 ↓ \ 依赖 → | Qt (LGPL v3) | CivetWeb (MIT) | Boost.DI (BSL-1.0) | QR Code Gen (MIT) | SQLite3 (Public Domain) |
|:---:|:---:|:---:|:---:|:---:|:---:|
| **MIT** | ✅ 兼容 | ✅ 兼容 | ✅ 兼容 | ✅ 兼容 | ✅ 兼容 |
| **LGPL v3** | ✅ 兼容 | ✅ 兼容 | ✅ 兼容 | ✅ 兼容 | ✅ 兼容 |
| **GPL v3** | ✅ 兼容 | ✅ 兼容 | ✅ 兼容 | ✅ 兼容 | ✅ 兼容 |
| **Apache 2.0** | ⚠️ 需注意 | ✅ 兼容 | ✅ 兼容 | ✅ 兼容 | ✅ 兼容 |

---

## 六、合规风险总结

| 风险等级 | 问题描述 | 建议行动 |
|:-------:|---------|---------|
| 🔴 高 | 项目自身无 LICENSE 文件 | 添加 `LICENSE` 文件明确许可条款 |
| 🟡 中 | 所有自研源文件缺少版权头部 | 在源文件中添加简短版权声明 |
| 🟢 低 | Qt LGPL 合规要求（提供源码信息） | 在文档中注明 Qt 源码获取方式 |
| 🟢 低 | MIT 组件要求保留版权声明 | 创建 `NOTICE` 或 `LICENSE-3RD-PARTY` 文件汇总第三方声明 |
| 🟢 低 | Windows 系统库无合规风险 | 无需操作 |
