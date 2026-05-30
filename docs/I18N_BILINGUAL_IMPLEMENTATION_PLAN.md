# 中英双语国际化实现计划

## 0. 问题概述

NetShare 当前所有界面文本均为中文硬编码，需要实现中英双语支持。

**语言切换策略：重启/刷新生效**（经对比分析，实时切换的复杂度和 bug 风险远高于收益，详见第 5 节）

**涉及三个层面的国际化**：
1. **PC 端 QML 界面** — 使用 Qt 官方国际化方案（`qsTr` + `.ts` 翻译文件），启动时根据设置加载翻译
2. **PC 端 C++ 代码** — 使用 `tr()` + 同一套 `.ts` 翻译文件，启动时加载
3. **移动端 HTML 页面** — 使用 JavaScript 国际化方案（JSON 翻译字典），页面加载时根据 URL 参数或服务端 API 获取语言

**切换流程**：
- **桌面端**：用户在设置页面选择语言 → 保存到 `SettingsManager` → 弹窗提示"语言设置将在重启后生效" → 用户重启应用后加载新语言
- **移动端**：页面加载时从 URL 参数 `?lang=en` 或服务端 API 读取语言设置，切换语言后刷新页面即可

---

## 1. 当前项目中文文本清单

### 1.1 QML 文件中文文本（共 10 个文件）

#### Main.qml
| 序号 | 中文文本 | 位置 | 上下文 |
|------|---------|------|--------|
| 1 | `NetShare - 局域网文件共享` | title | 窗口标题 |
| 2 | `IP：` / `IP：--` | 状态栏 | 状态栏左侧 |
| 3 | `分享：` + ` 个活跃` / `分享：0 个活跃` | 状态栏 | 状态栏中间 |
| 4 | `就绪` | 状态栏 | 状态栏右侧 |
| 5 | `分享管理` | 导航 | 左侧导航项 |
| 6 | `接收管理` | 导航 | 左侧导航项 |
| 7 | `传输列表` | 导航 | 左侧导航项 |
| 8 | `设备发现` | 导航 | 左侧导航项 |

#### SettingsPage.qml
| 序号 | 中文文本 | 位置 | 上下文 |
|------|---------|------|--------|
| 1 | `设置` | 侧栏标题 | 设置页左侧 |
| 2 | `常规` | 侧栏项 | 设置分类 |
| 3 | `网络` | 侧栏项 | 设置分类 |
| 4 | `安全` | 侧栏项 | 设置分类 |
| 5 | `传输日志` | 侧栏项 | 设置分类 |
| 6 | `带宽控制` | 侧栏项 | 设置分类 |
| 7 | `TLS/HTTPS` | 侧栏项 | 设置分类 |
| 8 | `常规设置` | 页面标题 | 常规页 |
| 9 | `设备名称` | 标签 | 常规页 |
| 10 | `输入设备名称` | 占位文本 | 常规页 |
| 11 | `语言` | 标签 | 常规页 |
| 12 | `开机自启` | 标签 | 常规页 |
| 13 | `最小化到托盘` | 标签 | 常规页 |
| 14 | `显示通知` | 标签 | 常规页 |
| 15 | `网络设置` | 页面标题 | 网络页 |
| 16 | `服务端口` | 标签 | 网络页 |
| 17 | `最大连接数` | 标签 | 网络页 |
| 18 | `带宽限制 (KB/s)` | 标签 | 网络页 |
| 19 | `0 = 不限制` | 占位文本 | 网络页 |
| 20 | `自动检测IP` | 标签 | 网络页 |
| 21 | `手动IP地址` | 标签 | 网络页 |
| 22 | `安全设置` | 页面标题 | 安全页 |
| 23 | `访问密码` | 标签 | 安全页 |
| 24 | `密码` | 标签 | 安全页 |
| 25 | `设置访问密码` | 占位文本 | 安全页 |
| 26 | `允许上传` | 标签 | 安全页 |
| 27 | `允许删除` | 标签 | 安全页 |
| 28 | `访问日志` | 标签 | 安全页 |
| 29 | `上传路径` | 标签 | 路径页 |
| 30 | `传输日志` | 页面标题 | 日志页 |
| 31 | `全部` / `下载` / `上传` | 过滤标签 | 日志页 |
| 32 | `搜索日志...` | 占位文本 | 日志页 |
| 33 | `下载 %1 次` / `上传 %1 次` | 统计 | 日志页 |
| 34 | `总传输: %1` | 统计 | 日志页 |
| 35 | `带宽控制` | 页面标题 | 带宽页 |
| 36 | `当前速度` / `限速设置` | 标签 | 带宽页 |
| 37 | `TLS/HTTPS 设置` | 页面标题 | TLS页 |
| 38 | `启用TLS` / `HTTPS端口` / `证书路径` / `密钥路径` | 标签 | TLS页 |

#### MessagePage.qml
| 序号 | 中文文本 | 位置 | 上下文 |
|------|---------|------|--------|
| 1 | `消息` | 标题 | 左侧标题 |
| 2 | `(移动端用户)` | 标签 | 聊天头部 |
| 3 | `选择一个用户开始聊天` | 提示 | 空状态 |
| 4 | `输入消息...` | 占位文本 | 输入框 |
| 5 | `发送` | 按钮 | 发送按钮 |
| 6 | `发送失败` | 状态 | 消息时间戳后 |

#### ShareManagement.qml
| 序号 | 中文文本 | 位置 | 上下文 |
|------|---------|------|--------|
| 1 | `分享管理` | 页面标题 | 页头 |
| 2 | `新建分享` | 按钮 | 页头 |
| 3 | `全部` / `进行中` / `已过期` | 过滤标签 | 过滤栏 |
| 4 | `共 0 个分享` | 统计 | 过滤栏 |
| 5 | `暂无分享\n点击右上角「新建分享」开始` | 空状态 | 列表空 |
| 6 | `已过期` / `进行中` | 状态 | 列表项 |
| 7 | `访问 %1 次` | 统计 | 列表项 |
| 8 | `复制链接` | 提示 | 按钮 Tooltip |
| 9 | `二维码` | 提示 | 按钮 Tooltip |
| 10 | `取消分享` | 提示 | 按钮 Tooltip |
| 11 | `分享详情` | 对话框标题 | 详情弹窗 |
| 12 | `分享路径` / `过期时间` / `访问次数` / `下载限制` / `访问密码` / `分享令牌` / `分享链接` | 标签 | 详情弹窗 |
| 13 | `次` / `无限制` / `已设置` / `无` | 值 | 详情弹窗 |
| 14 | `(文件夹)` | 后缀 | 详情弹窗 |
| 15 | `永不过期` / `已过期` / `X小时后过期` / `X天后过期` | 过期时间 | 列表项 |
| 16 | `新建分享` | 对话框标题 | 分享弹窗 |
| 17 | `选择文件` / `选择文件夹` | 按钮 | 分享弹窗 |
| 18 | `过期时间` / `访问密码` / `最大下载次数` | 标签 | 分享弹窗 |
| 19 | `永不过期` / `1小时` / `6小时` / `1天` / `7天` / `30天` | 选项 | 分享弹窗 |
| 20 | `无限制` | 选项 | 分享弹窗 |
| 21 | `创建分享` | 按钮 | 分享弹窗 |

#### ReceiveManagement.qml
| 序号 | 中文文本 | 位置 | 上下文 |
|------|---------|------|--------|
| 1 | `接收管理` | 页面标题 | 页头 |
| 2 | `接收二维码` | 按钮/对话框标题 | 页头/弹窗 |
| 3 | `存储路径：%1` | 标签 | 页头 |
| 4 | `扫描二维码发送文件` | 提示 | 二维码弹窗 |
| 5 | `复制链接` | 按钮 | 二维码弹窗 |
| 6 | `分享二维码` | 对话框标题 | 转发弹窗 |
| 7 | `分享成功 - 扫描二维码下载文件` | 对话框标题 | 转发弹窗 |
| 8 | `扫描二维码下载文件` | 提示 | 转发弹窗 |
| 9 | `全部` / `今日` / `本周` | 过滤标签 | 过滤栏 |
| 10 | `搜索文件...` | 占位文本 | 搜索框 |
| 11 | `已下载 %1 次` / `未下载` | 状态 | 文件列表 |
| 12 | `打开文件夹` | 提示 | 按钮 Tooltip |
| 13 | `分享二维码` | 提示 | 按钮 Tooltip |
| 14 | `删除` | 提示 | 按钮 Tooltip |
| 15 | `还没有接收到文件` | 空状态 | 列表空 |
| 16 | `已复制到剪贴板` | Toast | 提示 |

#### TransferList.qml
| 序号 | 中文文本 | 位置 | 上下文 |
|------|---------|------|--------|
| 1 | `传输列表` | 页面标题 | 页头 |
| 2 | `全部暂停` / `清空已完成` | 按钮 | 页头 |
| 3 | `全部` / `下载` / `上传` / `进行中` / `已完成` / `失败` | 过滤标签 | 过滤栏 |
| 4 | `共 0 个任务` | 统计 | 过滤栏 |
| 5 | `暂无传输任务` | 空状态 | 列表空 |
| 6 | `下载` / `上传` | 类型 | 列表项 |
| 7 | `等待中` / `准备中` / `下载中` / `上传中` / `已暂停` / `已完成` / `失败` / `已取消` / `未知` | 状态 | 列表项 |
| 8 | `暂停` / `继续` / `删除` | 提示 | 按钮 Tooltip |
| 9 | `未知文件` | 默认值 | 列表项 |

#### DeviceDiscovery.qml
| 序号 | 中文文本 | 位置 | 上下文 |
|------|---------|------|--------|
| 1 | `发现设备` | 页面标题 | 页头 |
| 2 | `停止扫描` / `扫描设备` | 按钮 | 页头 |
| 3 | `全部` | 过滤标签 | 过滤栏 |
| 4 | `发现 X 个设备` | 统计 | 过滤栏 |
| 5 | `暂未发现其他设备\n点击"扫描设备"搜索局域网` | 空状态 | 列表空 |
| 6 | `本机` / `在线` / `未知设备` | 状态 | 列表项 |
| 7 | `在浏览器中打开` | 提示 | 按钮 Tooltip |
| 8 | `💡 提示：确保设备在同一局域网内，且对方已启动 NetShare` | 提示 | 底部栏 |
| 9 | `扫描中...` / `就绪` | 状态 | 底部栏 |

### 1.2 C++ 文件中文文本

#### main.cpp（系统托盘）
| 序号 | 中文文本 | 上下文 |
|------|---------|--------|
| 1 | `NetShare - 局域网文件共享` | 托盘图标提示 |
| 2 | `打开主窗口` | 托盘菜单 |
| 3 | `我的分享` | 托盘菜单 |
| 4 | `传输列表` | 托盘菜单 |
| 5 | `设置` | 托盘菜单 |
| 6 | `退出` | 托盘菜单 |

#### NotificationManager.cpp（系统通知）
| 序号 | 中文文本 | 上下文 |
|------|---------|--------|
| 1 | `下载完成` / `文件 %1 已下载完成` | 通知标题/内容 |
| 2 | `上传完成` / `文件 %1 已上传完成` | 通知标题/内容 |
| 3 | `分享已创建` / `已分享: %1` | 通知标题/内容 |
| 4 | `分享被访问` / `来自 %1 的访问` | 通知标题/内容 |

#### ChatService.cpp
| 序号 | 中文文本 | 上下文 |
|------|---------|--------|
| 1 | `移动端-%1` | 移动端用户默认名称 |

#### RequestHandler.cpp（服务端生成 HTML）
| 序号 | 中文文本 | 上下文 |
|------|---------|--------|
| 1 | `局域网文件分享服务` | 首页 |
| 2 | `请使用分享链接访问具体分享内容` | 首页 |
| 3 | `%1个文件` | 文件计数 |
| 4 | `下载` / `打包下载 (ZIP)` / `下载文件` / `直接下载` | 下载页 |
| 5 | `文件列表` / `名称` / `大小` / `操作` | 文件列表表头 |
| 6 | `准备中...` / `获取文件信息...` / `下载中...` / `续传中...` / `下载完成!` / `下载失败` / `下载中断` / `已取消` / `保存中...` | 下载状态 |
| 7 | `文件夹分享` / `文件分享` | 分享类型 |
| 8 | `该分享需要密码` / `请输入访问密码` / `输入密码` / `验证` | 密码验证页 |
| 9 | `上传文件` / `选择文件或文件夹上传到局域网分享` | 上传页 |
| 10 | `将文件或文件夹拖拽到此处` / `选择文件` / `选择文件夹` | 上传页 |
| 11 | `上传中...` / `开始上传` / `上传完成` / `上传成功！` / `上传失败` / `准备上传...` / `正在上传` / `网络错误` | 上传状态 |
| 12 | `查看分享页面` | 上传成功后链接 |
| 13 | `由 NetShare 提供` | 页脚 |

### 1.3 HTML 文件中文文本

#### web/receive.html（移动端发送页）
| 序号 | 中文文本 | 上下文 |
|------|---------|--------|
| 1 | `NetShare - 发送文件` | 页面标题 |
| 2 | `📤 发送文件` | 标题 |
| 3 | `选择文件发送到对方设备` | 副标题 |
| 4 | `将文件或文件夹拖拽到此处` | 拖拽区 |
| 5 | `📄 选择文件` / `📁 选择文件夹` | 按钮 |
| 6 | `发送中...` / `开始发送` | 上传状态 |
| 7 | `重试失败文件` | 重试按钮 |
| 8 | `文件发送成功！` | 成功提示 |
| 9 | `💬 发送消息` | 聊天标题 |
| 10 | `输入消息内容...` / `发送` | 聊天输入 |
| 11 | `由 NetShare 提供` / `实时连接` / `连接断开` | 页脚 |
| 12 | `与服务端断开连接，请重试` | 错误提示 |
| 13 | `JS错误` / `Promise错误` | 错误弹窗 |

#### web/index.html（移动端下载页）
| 序号 | 中文文本 | 上下文 |
|------|---------|--------|
| 1 | `NetShare - 文件分享` | 页面标题 |
| 2 | `加载中...` / `正在获取分享信息` | 加载状态 |
| 3 | `文件夹分享` / `文件分享` | 分享类型 |
| 4 | `打包下载 (ZIP)` / `下载文件` | 下载按钮 |
| 5 | `名称` / `大小` / `操作` | 表头 |
| 6 | `输入访问密码` / `验证` | 密码验证 |
| 7 | `加载失败` | 错误状态 |
| 8 | `由 NetShare 提供` | 页脚 |

---

## 2. 技术方案

### 2.1 PC 端（QML + C++）国际化方案

**方案：Qt 官方国际化 + 启动时加载翻译文件**

1. **QML 中**：将所有中文硬编码字符串用 `qsTr()` 包裹
2. **C++ 中**：将所有中文硬编码字符串用 `tr()` 包裹（main.cpp 已使用 `tr()`，NotificationManager 和 ChatService 未使用）
3. **CMake 集成**：使用 `qt_add_translations()` 生成 `.ts` 文件并编译为 `.qm` 文件
4. **启动加载**：在 `main()` 中根据 `SettingsManager` 保存的语言设置加载对应的 `.qm` 翻译文件
5. **切换提示**：设置页面切换语言后弹窗提示"语言设置将在重启后生效"

**关键实现细节**：

```
项目结构变更：
src/gui/
  translations/
    netshare_zh_CN.ts    ← 中文翻译文件
    netshare_en.ts       ← 英文翻译文件
```

**启动加载流程**：
1. `main()` 中创建 `QApplication` 后，读取 `SettingsManager` 的 `General/Language` 值
2. 根据语言值加载对应的 `.qm` 文件：`QTranslator::load()` + `QCoreApplication::installTranslator()`
3. 初始化 QML 引擎，所有 `qsTr()` 和 `tr()` 自动使用已安装的翻译

**启动加载代码**：
```cpp
// NetShareApplication::initialize() 中，initializeSettings() 之后
QTranslator* translator = new QTranslator(this);
int langIndex = m_settings->value("General/Language", 0).toInt();
if (langIndex == 1) { // English
    QString qmFile = ":/i18n/netshare_en.qm";  // 路径需构建验证
    if (translator->load(qmFile)) {
        QCoreApplication::installTranslator(translator);
    } else {
        delete translator;
    }
} else {
    delete translator;
}
```

**设置页面切换逻辑**：
```qml
// SettingsPage.qml
ThemedComboBox {
    id: generalLangCombo
    model: ["简体中文", "English"]
    onCurrentIndexChanged: {
        saveSetting("General/Language", currentIndex)
        if (currentIndex !== originalLangIndex) {
            restartHintDialog.open()
        }
    }
}

Dialog {
    id: restartHintDialog
    title: qsTr("语言设置")
    contentItem: Label {
        text: qsTr("语言设置将在重启后生效")
    }
    standardButtons: Dialog.Ok
}
```

**动态拼接文本规范**：
统一使用 `qsTr("模板 %1").arg(value)` 格式，避免字符串拼接：
- ✅ `qsTr("共 %1 条").arg(count)`
- ❌ `qsTr("共 ") + count + qsTr(" 条")`

### 2.2 移动端（HTML）国际化方案

**方案：JavaScript i18n 字典 + URL 语言参数**

1. **创建翻译字典**：在 HTML 的 `<script>` 中定义 `i18n` 对象，包含中英文键值对
2. **语言检测**：从 URL 参数 `?lang=en` 或服务端 API `/api/language` 获取语言设置
3. **动态渲染**：所有文本通过 `t('key')` 函数获取，根据当前语言返回对应文本
4. **服务端 HTML**（RequestHandler 生成的页面）：从请求中读取语言参数，生成对应语言的 HTML

**receive.html 和 index.html 的改造方式**：
```javascript
var i18n = {
  zh: {
    'send_files': '发送文件',
    'select_files': '选择文件发送到对方设备',
    'drag_here': '将文件或文件夹拖拽到此处',
  },
  en: {
    'send_files': 'Send Files',
    'select_files': 'Select files to send to the target device',
    'drag_here': 'Drag files or folders here',
  }
};
var currentLang = 'zh';
function t(key) { return (i18n[currentLang] && i18n[currentLang][key]) || key; }

// 页面加载时检测语言
(function() {
    var params = new URLSearchParams(window.location.search);
    var lang = params.get('lang');
    if (lang === 'en') currentLang = 'en';
    applyTranslations();
})();

function applyTranslations() {
    document.querySelectorAll('[data-i18n]').forEach(function(el) {
        el.textContent = t(el.getAttribute('data-i18n'));
    });
}
```

**HTML 元素标记方式**：
```html
<h1 data-i18n="send_files">📤 发送文件</h1>
<p class="subtitle" data-i18n="select_files">选择文件发送到对方设备</p>
```

**RequestHandler.cpp 中动态 HTML 的改造方式**：
- 从请求的 URL 参数 `?lang=en` 读取语言偏好
- 使用独立翻译字典 `trHtml(key, lang)` 替换中文文本，**不使用 Qt 的 `tr()`**（因为服务端需根据每个请求的语言参数动态返回不同语言 HTML）
- 通过 `QString::arg()` 占位符在生成 HTML 时替换翻译文本

### 2.3 语言同步机制

PC 端和移动端的语言设置通过 `SettingsManager` 统一管理：

1. **PC 端**：语言设置保存到 `SettingsManager`，键为 `General/Language`（0=中文，1=英文）
2. **移动端**：页面加载时通过 `/api/language` API 读取 PC 端当前语言设置，或通过 URL 参数 `?lang=en` 指定
3. **无需 WebSocket 同步**：移动端用户刷新页面即可获取最新语言设置

---

## 3. 执行步骤

### 步骤 1：QML 文件 — 所有硬编码中文改用 `qsTr()`

- **修改内容**：将所有 QML 文件中的中文硬编码字符串用 `qsTr()` 包裹
- **涉及文件**：
  - `Main.qml` — 导航项、状态栏文本
  - `SettingsPage.qml` — 所有标签、标题、占位文本
  - `MessagePage.qml` — 标题、提示、按钮文本
  - `ShareManagement.qml` — 标题、按钮、状态、对话框文本
  - `ReceiveManagement.qml` — 标题、按钮、提示、对话框文本
  - `TransferList.qml` — 标题、状态、过滤标签、Tooltip
  - `DeviceDiscovery.qml` — 标题、按钮、提示、状态
- **规范**：
  - QML 属性绑定中的中文 → 直接 `qsTr()` 包裹
  - JS 函数体内的中文 → 直接 `qsTr()` 包裹（重启生效模式下无需特殊处理）
  - 动态拼接文本 → 统一用 `qsTr("模板 %1").arg(value)` 格式
- **难易程度**：中（工作量大但机械性操作）
- **完成状态**：未开始
- **验证方式**：编译通过，界面显示与修改前完全一致

### 步骤 2：C++ 文件 — 所有硬编码中文改用 `tr()`

- **修改内容**：将 C++ 中用户可见的中文硬编码字符串用 `tr()` 包裹
- **涉及文件**：
  - `main.cpp` — 托盘菜单项（已使用 `tr()`，确认无遗漏）
  - `NotificationManager.cpp` — 通知标题和内容
  - `ChatService.cpp` — 将 `QStringLiteral("移动端-%1")` 改为 `tr("Mobile-%1")`
- **注意**：`RequestHandler.cpp` 中的内嵌 HTML 中文文本**不使用 `tr()`**，改用独立翻译字典（详见步骤 9）
- **难易程度**：低
- **完成状态**：未开始
- **验证方式**：编译通过

### 步骤 3：CMake 集成翻译系统

- **修改内容**：
  1. 创建 `src/gui/translations/` 目录
  2. 在 `src/CMakeLists.txt` 中添加 `qt_add_translations()` 配置（作用于 `NetShare` 可执行目标，而非 `NetshareGui` 库）
  3. 生成初始 `.ts` 翻译文件（中文和英文）
- **关键代码**：
  ```cmake
  # src/CMakeLists.txt 中添加（在 qt_add_executable 之后）
  qt_add_translations(NetShare
      TS_FILES
          ${CMAKE_CURRENT_SOURCE_DIR}/gui/translations/netshare_zh_CN.ts
          ${CMAKE_CURRENT_SOURCE_DIR}/gui/translations/netshare_en.ts
      LUPDATE_OPTIONS -no-obsolete
      LRELEASE_OPTIONS -compress -removeidentical
  )
  ```
- **难易程度**：中
- **完成状态**：未开始
- **验证方式**：执行 `cmake --build` 后在 build 目录生成 `.ts` 和 `.qm` 文件

### 步骤 4：生成并填写翻译文件

- **修改内容**：
  1. 运行 `lupdate` 提取所有 `qsTr()` 和 `tr()` 字符串到 `.ts` 文件
  2. 在 `netshare_zh_CN.ts` 中填写中文翻译（源文本已是中文，翻译与源文本相同）
  3. 在 `netshare_en.ts` 中填写英文翻译
- **难易程度**：中（英文翻译需要准确）
- **完成状态**：未开始
- **验证方式**：`.ts` 文件中无未翻译条目

### 步骤 5：C++ 启动加载翻译文件

- **修改内容**：
  1. 在 `NetShareApplication::initialize()` 方法中，`initializeSettings()` 之后、`initializeTrayIcon()` 之前，加载翻译文件
  2. 从 `SettingsManager` 读取 `General/Language` 值（0=中文，1=英文）
  3. 根据语言值加载对应的 `.qm` 文件
  4. 中文为默认语言，可不加载翻译文件（源文本本身就是中文）；英文需加载 `netshare_en.qm`
  5. 翻译加载在 `initializeTrayIcon()` 之前，确保托盘菜单使用正确语言
- **关键代码逻辑**：
  ```cpp
  // NetShareApplication::initialize() 中
  // initializeSettings() 之后添加：
  void loadTranslator() {
      m_translator = new QTranslator(this);
      int langIndex = m_settings->value("General/Language", 0).toInt();
      if (langIndex == 1) { // English
          QString qmFile = ":/i18n/netshare_en.qm"; // 路径需构建验证
          if (m_translator->load(qmFile)) {
              QCoreApplication::installTranslator(m_translator);
          }
      }
      // 中文（默认）不需要加载翻译文件
  }
  ```
- **难易程度**：低
- **完成状态**：未开始
- **验证方式**：设置语言为英文后重启应用，界面显示英文

### 步骤 6：设置页面语言切换提示

- **修改内容**：
  1. `SettingsPage.qml` 中 `generalLangCombo` 的 `onCurrentIndexChanged` 保存语言设置
  2. 当语言设置发生变化时，弹窗提示"语言设置将在重启后生效"
  3. 记录打开设置页时的原始语言索引，仅在真正切换时弹窗
- **难易程度**：低
- **完成状态**：未开始
- **验证方式**：切换语言后弹出提示对话框

### 步骤 7：HTML 页面国际化 — receive.html

- **修改内容**：
  1. 在 `<script>` 中添加 `i18n` 翻译字典（中英文）
  2. 所有中文硬编码文本替换为 `t('key')` 调用，HTML 元素添加 `data-i18n` 属性
  3. 页面加载时从 URL 参数 `?lang=en` 检测语言
  4. 无 URL 参数时，通过 `/api/language` API 获取 PC 端当前语言设置
- **难易程度**：中
- **完成状态**：未开始
- **验证方式**：访问 `http://IP:8080/receive?lang=en` 显示英文界面

### 步骤 8：HTML 页面国际化 — index.html

- **修改内容**：
  1. 在 `<script>` 中添加 `i18n` 翻译字典
  2. 所有中文硬编码文本替换为 `t('key')` 调用
  3. 从 URL 参数或 API 获取语言设置
- **难易程度**：中
- **完成状态**：未开始
- **验证方式**：访问 `http://IP:8080/s/TOKEN?lang=en` 显示英文界面

### 步骤 9：RequestHandler.cpp 动态 HTML 国际化

- **修改内容**：
  1. 在 `RequestHandler` 类中添加翻译字典 `trHtml(key, lang)` 方法和 `getLangFromRequest(conn)` 方法
  2. 从 HTTP 请求的 URL 参数 `?lang=en` 或 Cookie 读取语言偏好
  3. 将所有动态生成 HTML 中的中文文本替换为 `trHtml("key", lang)` 调用
  4. 内嵌 JS 代码中的中文文本，通过 `trHtml()` 翻译后用 `%1` 占位符注入到 HTML 字符串中
- **注意**：RequestHandler **不使用 Qt 的 `tr()` 翻译系统**，因为服务端需要根据每个 HTTP 请求的语言参数动态返回不同语言的 HTML，而 `tr()` 的翻译在 `QTranslator` 安装时就固定了
- **实现方式**：
  ```cpp
  // RequestHandler 中添加翻译字典
  QString trHtml(const QString& key, const QString& lang = "zh") const {
      static QMap<QString, QMap<QString, QString>> dict = {
          {"download", {{"zh", "下载"}, {"en", "Download"}}},
          {"file_list", {{"zh", "文件列表"}, {"en", "File List"}}},
          {"name", {{"zh", "名称"}, {"en", "Name"}}},
          {"size", {{"zh", "大小"}, {"en", "Size"}}},
          {"action", {{"zh", "操作"}, {"en", "Action"}}},
          // ... 所有翻译条目
      };
      auto it = dict.find(key);
      if (it != dict.end()) {
          auto langIt = it->find(lang);
          if (langIt != it->end()) return langIt.value();
      }
      return key;
  }

  QString getLangFromRequest(mg_connection* conn) const {
      // 从 query string 读取 ?lang=en
      // 默认返回 "zh"
  }
  ```
- **难易程度**：高（内嵌 JS 文本量大，需逐一替换）
- **完成状态**：未开始
- **验证方式**：移动端浏览器访问 `?lang=en` 时显示英文界面

### 步骤 10：添加 `/api/language` API

- **修改内容**：
  1. 在 `RequestHandler` 中添加 `/api/language` 路由
  2. 返回当前 `SettingsManager` 中的 `General/Language` 值
  3. 移动端 HTML 页面在无 URL 参数时调用此 API 获取语言设置
- **难易程度**：低
- **完成状态**：未开始
- **验证方式**：访问 `http://IP:8080/api/language` 返回 `{"language": 0}` 或 `{"language": 1}`

### 步骤 11：打包脚本更新

- **修改内容**：
  1. 在 `package.ps1` 中添加 `.qm` 翻译文件的复制逻辑
  2. 确保 `windeployqt` 不会遗漏翻译资源
- **难易程度**：低
- **完成状态**：未开始
- **验证方式**：打包后的程序能正常加载翻译文件

### 步骤 12：全流程测试

- **修改内容**：
  1. 测试 PC 端中文界面所有文本显示正确（默认启动）
  2. 测试 PC 端英文界面所有文本显示正确（设置语言为英文后重启）
  3. 测试设置页面切换语言后弹出重启提示
  4. 测试系统托盘菜单中英文切换
  5. 测试系统通知中英文切换
  6. 测试移动端中文页面显示正确
  7. 测试移动端英文页面显示正确（`?lang=en`）
  8. 测试移动端无 URL 参数时通过 API 获取语言设置
- **难易程度**：低
- **完成状态**：未开始
- **验证方式**：所有测试项通过

---

## 4. 英文翻译对照表（核心词汇）

| 中文 | English |
|------|---------|
| 分享管理 | Share Management |
| 接收管理 | Receive Management |
| 传输列表 | Transfer List |
| 设备发现 | Device Discovery |
| 设置 | Settings |
| 消息 | Messages |
| 常规 | General |
| 网络 | Network |
| 安全 | Security |
| 传输日志 | Transfer Log |
| 带宽控制 | Bandwidth Control |
| 设备名称 | Device Name |
| 语言 | Language |
| 开机自启 | Auto Start |
| 最小化到托盘 | Minimize to Tray |
| 显示通知 | Show Notifications |
| 服务端口 | Service Port |
| 最大连接数 | Max Connections |
| 带宽限制 | Bandwidth Limit |
| 不限制 | Unlimited |
| 自动检测IP | Auto Detect IP |
| 手动IP地址 | Manual IP Address |
| 访问密码 | Access Password |
| 允许上传 | Allow Upload |
| 允许删除 | Allow Delete |
| 访问日志 | Access Log |
| 上传路径 | Upload Path |
| 新建分享 | New Share |
| 进行中 | Active |
| 已过期 | Expired |
| 分享详情 | Share Details |
| 分享路径 | Share Path |
| 过期时间 | Expiration |
| 访问次数 | Visits |
| 下载限制 | Download Limit |
| 分享令牌 | Share Token |
| 分享链接 | Share Link |
| 复制链接 | Copy Link |
| 二维码 | QR Code |
| 取消分享 | Cancel Share |
| 永不过期 | Never Expire |
| 接收二维码 | Receive QR Code |
| 存储路径 | Storage Path |
| 扫描二维码发送文件 | Scan QR code to send files |
| 扫描二维码下载文件 | Scan QR code to download |
| 已下载 X 次 | Downloaded %1 times |
| 未下载 | Not downloaded |
| 打开文件夹 | Open Folder |
| 删除 | Delete |
| 全部暂停 | Pause All |
| 清空已完成 | Clear Completed |
| 等待中 | Pending |
| 准备中 | Preparing |
| 下载中 | Downloading |
| 上传中 | Uploading |
| 已暂停 | Paused |
| 已完成 | Completed |
| 失败 | Failed |
| 已取消 | Cancelled |
| 暂停 | Pause |
| 继续 | Resume |
| 扫描设备 | Scan Devices |
| 停止扫描 | Stop Scanning |
| 本机 | This PC |
| 在线 | Online |
| 就绪 | Ready |
| 局域网文件共享 | LAN File Sharing |
| 打开主窗口 | Open Main Window |
| 我的分享 | My Shares |
| 退出 | Quit |
| 下载完成 | Download Complete |
| 上传完成 | Upload Complete |
| 分享已创建 | Share Created |
| 分享被访问 | Share Accessed |
| 移动端用户 | Mobile User |
| 发送文件 | Send Files |
| 选择文件发送到对方设备 | Select files to send to the target device |
| 将文件或文件夹拖拽到此处 | Drag files or folders here |
| 选择文件 | Select Files |
| 选择文件夹 | Select Folder |
| 开始发送 | Start Sending |
| 发送 | Send |
| 发送中... | Sending... |
| 文件发送成功！ | Files sent successfully! |
| 重试失败文件 | Retry Failed Files |
| 发送消息 | Send Message |
| 输入消息内容... | Type a message... |
| 输入消息... | Type a message... |
| 由 NetShare 提供 | Powered by NetShare |
| 实时连接 | Live Connection |
| 连接断开 | Disconnected |
| 文件分享 | File Sharing |
| 文件夹分享 | Folder Sharing |
| 打包下载 (ZIP) | Download as ZIP |
| 下载文件 | Download File |
| 直接下载 | Direct Download |
| 文件列表 | File List |
| 名称 | Name |
| 大小 | Size |
| 操作 | Action |
| 该分享需要密码 | This share requires a password |
| 请输入访问密码 | Please enter the access password |
| 输入密码 | Enter password |
| 验证 | Verify |
| 上传文件 | Upload Files |
| 上传成功！ | Upload successful! |
| 查看分享页面 | View share page |
| 上传中... | Uploading... |
| 开始上传 | Start Upload |
| 上传完成 | Upload Complete |
| 上传失败 | Upload Failed |
| 准备上传... | Preparing to upload... |
| 正在上传 | Uploading |
| 网络错误 | Network Error |

---

## 5. 风险与注意事项

1. **翻译文件路径**：`.qm` 文件作为 Qt 资源系统嵌入到可执行文件中，需要确保 CMake 的资源前缀配置正确。实际路径需构建后验证。
2. **首次加载语言**：应用启动时需要根据 `SettingsManager` 中保存的语言设置加载对应的翻译文件，默认为中文。翻译加载必须在 `initializeSettings()` 之后、`initializeQml()` 之前执行。
3. **RequestHandler 内嵌 JS**：服务端生成的 HTML 中包含大量内嵌 JS 中文文本，需逐一用 `tr()` + 占位符替换，工作量较大。
4. **日期和时间格式**：英文环境下日期格式应调整为 MM/DD/YYYY 或 DD/MM/YYYY，时间格式保持 HH:mm:ss。
5. **数字格式**：英文环境下千位分隔符使用逗号（如 1,000），中文环境不需要。
6. **语言切换策略说明**：采用重启/刷新生效而非实时切换，原因如下：
   - 桌面端实时切换需处理 6+ 处 ListModel 手动刷新、托盘菜单重建、JS 函数体内文本不自动更新等问题，bug 风险高
   - 移动端实时切换需 WebSocket 同步 + 浏览器缓存处理，复杂度高
   - 语言切换是低频操作，重启/刷新的代价完全可接受
   - 重启生效模式下，所有 `qsTr()` / `tr()` 在启动时就是正确语言，无需任何手动刷新逻辑

---

## 6. 冲突和遗漏检查结果

### 6.1 遗漏的中文文本（计划文档第 1 节未收录）

#### SettingsPage.qml 遗漏（共 22 条）

| 序号 | 遗漏文本 | 行号 | 上下文 |
|------|---------|------|--------|
| 1 | `恢复默认设置` | 567 | 安全页底部按钮 |
| 2 | `共 X 条` | 606 | 传输日志页统计 |
| 3 | `刷新` | 612 | 传输日志页按钮 |
| 4 | `导出` | 617 | 传输日志页按钮 |
| 5 | `清空` | 625 | 传输日志页按钮 |
| 6 | `搜索文件名或地址...` | 675 | 传输日志页搜索框（原文写的是"搜索日志..."不准确） |
| 7 | `📊 统计:` | 701 | 传输日志页统计标签 |
| 8 | `下载 X 次` | 706 | 传输日志统计（原文写的是"下载 %1 次"不准确，实际无 %1） |
| 9 | `上传 X 次` | 711 | 传输日志统计 |
| 10 | `总传输 X` | 716 | 传输日志统计（原文写的是"总传输: %1"不准确） |
| 11 | `开始` / `完成` / `失败` / `取消` / `未知` | 773-775 | 传输日志状态 |
| 12 | `当前全局速度` | 827 | 带宽控制页 |
| 13 | `带宽限制` | 839 | 带宽控制页 |
| 14 | `0 = 不限制` | 842 | 带宽限制标签（动态拼接） |
| 15 | `(不限制)` | 890 | 带宽限制动态文本 |
| 16 | `全局速度限制 (KB/s)` | 899 | 带宽控制页 |
| 17 | `传输统计` | 915 | 带宽控制页 |
| 18 | `总传输: X \| 记录数: X` | 919 | 传输统计动态文本 |
| 19 | `上传目录` | 960 | 上传路径页 |
| 20 | `TLS / HTTPS 设置` | 989 | TLS 页标题（原文写的是"TLS/HTTPS 设置"格式不一致） |
| 21 | `当前未启用 TLS 加密，所有传输均为明文` | 981 | TLS 警告 |
| 22 | `TLS 加密已启用，传输数据受保护` | 1003 | TLS 状态 |
| 23 | `启用 TLS` | 1018 | TLS 开关 |
| 24 | `HTTPS 端口` | 1029 | TLS 页 |
| 25 | `证书文件` | 1056 | TLS 页（原文写的是"证书路径"不准确） |
| 26 | `选择 .pem 或 .crt 证书文件` | 1063 | TLS 页占位文本 |
| 27 | `私钥文件` | 1080 | TLS 页（原文写的是"密钥路径"不准确） |
| 28 | `选择 .pem 或 .key 私钥文件` | 1087 | TLS 页占位文本 |
| 29 | `提示: 更改 TLS 设置后需要重启服务才能生效` | 1119 | TLS 页提示 |

#### ShareManagement.qml 遗漏（共 8 条）

| 序号 | 遗漏文本 | 行号 | 上下文 |
|------|---------|------|--------|
| 1 | `选择文件或文件夹` | 631 | 分享弹窗提示 |
| 2 | `请选择文件或文件夹` | 637 | 分享弹窗占位文本 |
| 3 | `文件` | 643 | 分享弹窗按钮 |
| 4 | `文件夹` | 648 | 分享弹窗按钮 |
| 5 | `有效期` | 656 | 分享弹窗标签（原文写的是"过期时间"不准确） |
| 6 | `24小时` / `7天` / `30天` / `永不过期` | 660 | 分享弹窗有效期选项（原文写的是"1小时/6小时/1天/7天/30天"不准确） |
| 7 | `留空则无需密码` | 669 | 分享弹窗密码占位文本 |
| 8 | `确定` / `取消` | 690/698 | 分享弹窗按钮（原文写的是"创建分享"不准确） |
| 9 | `选择文件` / `选择文件夹` | 704/713 | FileDialog/FolderDialog 标题 |

#### TransferList.qml 遗漏（共 5 条）

| 序号 | 遗漏文本 | 行号 | 上下文 |
|------|---------|------|--------|
| 1 | `共 X 个任务` | 142 | 动态统计文本（原文只写了"共 0 个任务"静态文本） |
| 2 | `下载` / `上传` | 57 | JS 函数中硬编码（原文已收录但未标注是 JS 函数内硬编码） |
| 3 | `未知文件` | 63/122 | JS 函数中默认值（原文已收录但未标注是 JS 函数内硬编码） |

#### ReceiveManagement.qml 遗漏（共 3 条）

| 序号 | 遗漏文本 | 行号 | 上下文 |
|------|---------|------|--------|
| 1 | `共 %1 个文件` | 607 | 文件统计 |
| 2 | `总计 %1` | 618 | 大小统计 |

#### MessagePage.qml 遗漏（共 1 条）

| 序号 | 遗漏文本 | 行号 | 上下文 |
|------|---------|------|--------|
| 1 | `⚠ 发送失败` | 393 | 消息时间戳后缀（注意：原文写的是"发送失败"但实际代码中是 `⚠ 发送失败`，含 emoji 前缀） |

#### receive.html 遗漏（共 20+ 条）

| 序号 | 遗漏文本 | 行号 | 上下文 |
|------|---------|------|--------|
| 1 | `未知大小` | 607 | 文件大小默认值 |
| 2 | `共 X 个文件，总大小 X` | 617 | 文件统计 |
| 3 | `检测到未完成的上传，` | 740 | 断点续传提示 |
| 4 | `跳过X个已完成文件` | 742 | 断点续传提示 |
| 5 | `续传X个部分文件` | 743 | 断点续传提示 |
| 6 | `检查接口返回错误: HTTP X` | 748 | 错误弹窗 |
| 7 | `检查上传状态失败: X` | 751 | 错误弹窗 |
| 8 | `上传过程出错: X` | 757 | 错误弹窗 |
| 9 | `上传已暂停` | 764/1048/1138 | 上传状态 |
| 10 | `X 个文件发送失败` | 767/1141 | 上传状态 |
| 11 | `发送中...` | 783/1009 | 上传状态 |
| 12 | `已完成` | 788 | 文件状态 |
| 13 | `失败: X` | 795 | 文件状态 |
| 14 | `上传已暂停` | 870/986 | 错误消息 |
| 15 | `续传失败，服务端连续重启超过X次，请重新上传` | 921 | 续传错误 |
| 16 | `网络错误: X` | 995 | 网络错误 |
| 17 | `超时: X` | 996 | 超时错误 |
| 18 | `连接断开` | 1031 | 文件状态 |
| 19 | `已暂停` | 1045 | 文件状态 |
| 20 | `检测到 X 个未完成的上传任务，请选择相同文件后点击开始发送续传` | 1059 | 续传提示 |
| 21 | `有未完成的任务，请选择文件后续传` | 1072 | 续传提示 |
| 22 | `重试检查失败: X` | 1128 | 错误弹窗 |
| 23 | `正在完成...` | 1149 | 完成状态 |
| 24 | `成功发送 X 个文件！` | 1173 | 成功提示 |
| 25 | `完成操作失败` | 1176/1180 | 失败提示 |
| 26 | `消息发送失败` | 1245 | 错误弹窗 |
| 27 | `消息发送失败: X` | 1249 | 错误弹窗 |

#### RequestHandler.cpp 遗漏（共 10+ 条）

| 序号 | 遗漏文本 | 行号 | 上下文 |
|------|---------|------|--------|
| 1 | `获取文件信息失败` | 1877 | 下载页 JS |
| 2 | `服务器不支持断点续传，使用直接下载` | 1880 | 下载页 JS |
| 3 | `文件较大(X)，建议使用下载管理器` | 1881 | 下载页 JS |
| 4 | `文件大小: X` | 1882 | 下载页 JS |
| 5 | `错误: X` | 1884 | 下载页 JS |
| 6 | `下载失败: HTTP X` | 1895 | 下载页 JS |
| 7 | `下载中断，点击下载按钮继续` | 1909 | 下载页 JS |
| 8 | `已取消，点击下载按钮继续` | 1910/1912 | 下载页 JS |
| 9 | `下载中断: X，点击下载按钮继续` | 1913 | 下载页 JS |
| 10 | `上传 X 个文件 (X)` | 2043 | 上传页 JS |
| 11 | `取消` | 1840 | 下载页按钮 |

### 6.2 方案冲突

> **注意**：采用重启/刷新生效方案后，以下冲突大部分已自动解决。

#### 冲突 1：`qt_add_translations()` 目标选择 — ✅ 已解决

**问题**：原计划将 `qt_add_translations()` 添加到 `src/gui/CMakeLists.txt` 作用于 `NetshareGui` 库目标，但应作用于 `NetShare` 可执行目标。

**解决**：已在步骤 3 中修正，改为在 `src/CMakeLists.txt` 中作用于 `NetShare` 可执行目标。

#### 冲突 2：QML 中 `qsTr()` 与 JS 函数内字符串 — ✅ 已解决（重启生效模式下无需特殊处理）

**问题**：实时切换模式下，JS 函数体内的 `qsTr()` 在 `retranslate()` 时不会自动重新求值。

**解决**：重启生效模式下，应用启动时 `qsTr()` 就会求值为正确语言，JS 函数体内可直接使用 `qsTr()`，无需任何特殊处理。

#### 冲突 3：`SettingsPage.qml` 的设置侧栏 `ListModel` 也需要刷新 — ✅ 已解决

**问题**：实时切换模式下，设置侧栏 ListModel 需手动刷新。

**解决**：重启生效模式下，启动时 ListModel 就会以正确语言创建，无需手动刷新。

#### 冲突 4：`DeviceDiscovery.qml` 的 `deviceModel` 动态数据 — ✅ 已解决

**解决**：重启生效模式下，`refreshDevices()` 中的 `qsTr()` 在启动时就是正确语言。

#### 冲突 5：`TransferList.qml` 的 `taskListModel` 动态数据 — ✅ 已解决

**解决**：同上。

#### 冲突 6：`SettingsPage.qml` 中的动态拼接文本 — ⚠️ 仍需处理

**问题**：`"共 " + count + " 条"` 等拼接文本需改为 `qsTr("共 %1 条").arg(count)` 格式。

**解决**：在步骤 1 中统一改为 `qsTr("模板 %1").arg(value)` 格式。这是代码规范要求，与切换策略无关。

#### 冲突 7：`SettingsPage.qml` 语言下拉框选项文本 — ✅ 已解决

**问题**：实时切换模式下，"简体中文"需变为 "Simplified Chinese"。

**解决**：重启生效模式下，语言下拉框在启动时就是正确语言，无需动态更新。

#### 冲突 8：`NotificationManager` 中 `tr()` 的类上下文 — ✅ 已确认

**问题**：需确认 `NotificationManager` 类有 `Q_OBJECT` 宏且继承自 `QObject`，否则 `tr()` 不可用。

**解决**：已确认 `NotificationManager` 继承 `QObject` 并有 `Q_OBJECT` 宏，`tr()` 可用。

#### 冲突 9：`ChatService` 中 `QStringLiteral` 与 `tr()` 不兼容 — ⚠️ 仍需处理

**问题**：`QStringLiteral("移动端-%1")` 是编译期宏，不支持运行时翻译。

**解决**：在步骤 2 中将 `QStringLiteral("移动端-%1")` 改为 `tr("Mobile-%1")`。

#### 冲突 10：`RequestHandler.cpp` 中内嵌 JS 代码的国际化 — ⚠️ 方案已修正

**问题**：RequestHandler 中内嵌 JS 代码的中文文本需国际化。

**解决**：原方案使用 `tr()` + `%1` 占位符，已修正为使用独立翻译字典 `trHtml(key, lang)`。原因：服务端需根据每个 HTTP 请求的语言参数动态返回不同语言 HTML，而 `tr()` 的翻译在 `QTranslator` 安装时就固定了。详见 6.5 节错误 1。

#### 冲突 11：`.qm` 文件资源路径 — ⚠️ 仍需验证

**问题**：`.qm` 文件的实际资源路径取决于 CMake 配置，需构建后验证。

**解决**：在步骤 5 执行时通过构建验证实际路径。

### 6.3 遗漏的执行步骤

> **注意**：采用重启/刷新生效方案后，原遗漏步骤大部分已简化或消除。

#### 遗漏步骤 A：JS 函数内中文文本的 qsTr() 改造 — ✅ 已简化

**原问题**：需区分 QML 属性绑定和 JS 函数体内的文本，分别处理。

**解决**：重启生效模式下，JS 函数体内可直接使用 `qsTr()`，无需特殊处理。已在步骤 1 的规范中说明。

#### 遗漏步骤 B：语言切换后所有动态数据刷新 — ✅ 已消除

**原问题**：实时切换模式下需手动刷新 6 个组件。

**解决**：重启生效模式下无需任何手动刷新。

#### 遗漏步骤 C：`NotificationManager` 类声明检查 — ✅ 已确认

**解决**：已确认 `NotificationManager` 有 `Q_OBJECT` 宏，`tr()` 可用。

#### 遗漏步骤 D：`RequestHandler` 内嵌 JS 的国际化策略 — ✅ 已确定

**解决**：采用 `tr()` + `%1` 占位符方式，已在步骤 9 中明确。

#### 遗漏步骤 E：应用启动时加载语言设置 — ✅ 已纳入

**解决**：已作为步骤 5 独立列出。

#### 遗漏步骤 F：系统托盘菜单的语言切换 — ✅ 已消除

**原问题**：实时切换模式下需重建托盘菜单。

**解决**：重启生效模式下，托盘菜单在启动时以正确语言创建，无需重建。

### 6.4 遗漏的英文翻译对照表

| 中文 | English | 备注 |
|------|---------|------|
| 恢复默认设置 | Reset to Defaults | 计划文档第 4 节遗漏 |
| 刷新 | Refresh | 遗漏 |
| 导出 | Export | 遗漏 |
| 清空 | Clear | 遗漏 |
| 搜索文件名或地址... | Search filename or address... | 遗漏 |
| 统计 | Statistics | 遗漏 |
| 当前全局速度 | Current Global Speed | 遗漏 |
| 带宽限制 | Bandwidth Limit | 遗漏 |
| 全局速度限制 (KB/s) | Global Speed Limit (KB/s) | 遗漏 |
| 传输统计 | Transfer Statistics | 遗漏 |
| 总传输 | Total Transferred | 遗漏 |
| 记录数 | Records | 遗漏 |
| 上传目录 | Upload Directory | 遗漏 |
| 证书文件 | Certificate File | 遗漏 |
| 私钥文件 | Private Key File | 遗漏 |
| 选择 .pem 或 .crt 证书文件 | Select .pem or .crt certificate file | 遗漏 |
| 选择 .pem 或 .key 私钥文件 | Select .pem or .key private key file | 遗漏 |
| 当前未启用 TLS 加密，所有传输均为明文 | TLS encryption is not enabled, all transfers are in plaintext | 遗漏 |
| TLS 加密已启用，传输数据受保护 | TLS encryption is enabled, transfer data is protected | 遗漏 |
| 提示: 更改 TLS 设置后需要重启服务才能生效 | Note: TLS settings require service restart to take effect | 遗漏 |
| 选择文件或文件夹 | Select file or folder | 遗漏 |
| 请选择文件或文件夹 | Please select a file or folder | 遗漏 |
| 文件 / 文件夹 | File / Folder | 分享弹窗按钮 |
| 有效期 | Validity Period | 遗漏（原文写的是"过期时间"不准确） |
| 24小时 / 7天 / 30天 | 24 Hours / 7 Days / 30 Days | 遗漏 |
| 留空则无需密码 | Leave empty for no password | 遗漏 |
| 确定 / 取消 | OK / Cancel | 遗漏 |
| 共 X 条 | Total %1 records | 遗漏 |
| 开始 / 完成 / 失败 / 取消 | Started / Completed / Failed / Cancelled | 日志状态遗漏 |
| 未知大小 | Unknown size | 遗漏 |
| 共 X 个文件，总大小 X | Total %1 files, %2 total | 遗漏 |
| 上传已暂停 | Upload paused | 遗漏 |
| X 个文件发送失败 | %1 file(s) failed to send | 遗漏 |
| 已完成 | Completed | 文件状态遗漏 |
| 失败: X | Failed: %1 | 遗漏 |
| 连接断开 | Connection lost | 遗漏 |
| 已暂停 | Paused | 文件状态遗漏 |
| 续传失败，服务端连续重启超过X次，请重新上传 | Resume failed, server restarted more than %1 times, please re-upload | 遗漏 |
| 正在完成... | Finalizing... | 遗漏 |
| 成功发送 X 个文件！ | Successfully sent %1 file(s)! | 遗漏 |
| 完成操作失败 | Failed to finalize | 遗漏 |
| 消息发送失败 | Failed to send message | 遗漏 |
| 获取文件信息失败 | Failed to get file info | 遗漏 |
| 服务器不支持断点续传，使用直接下载 | Server does not support resume, using direct download | 遗漏 |
| 文件较大(X)，建议使用下载管理器 | Large file (%1), recommended to use download manager | 遗漏 |
| 下载中断，点击下载按钮继续 | Download interrupted, click download button to continue | 遗漏 |
| 已取消，点击下载按钮继续 | Cancelled, click download button to continue | 遗漏 |
| 上传 X 个文件 (X) | Upload %1 files (%2) | 遗漏 |

### 6.5 新发现的遗漏和错误（执行前最终检查）

#### 错误 1：RequestHandler.cpp 的 `tr()` 方案不可行 — ⚠️ 需修正

**问题**：计划文档步骤 9 中提出"将 RequestHandler 内嵌 JS 中的中文文本用 `tr()` + `%1` 占位符替换"。但此方案在重启生效模式下**无法工作**，原因：
- `RequestHandler` 继承自 `QObject`，有 `Q_OBJECT` 宏，`tr()` 可用
- 但 `tr()` 的翻译在 `QTranslator` 安装时就固定了，服务端启动后不会因每个 HTTP 请求的语言偏好而改变
- 移动端用户可能使用不同语言访问同一个服务端，服务端无法根据请求的语言参数动态切换 `tr()` 的翻译结果

**修正方案**：RequestHandler 中的动态 HTML 应采用**独立的翻译字典方案**，与 `tr()` 无关：
1. 在 `RequestHandler` 类中添加一个 `QMap<QString, QMap<QString, QString>>` 类型的翻译字典
2. 从请求的 URL 参数 `?lang=en` 读取语言偏好
3. 根据语言偏好从字典中获取对应文本，用 `QString::arg()` 替换占位符
4. 首页、密码页、下载页、上传页的 HTML 生成函数都需传入语言参数

```cpp
// RequestHandler 中添加翻译字典
QString trHtml(const QString& key, const QString& lang = "zh") const {
    static QMap<QString, QMap<QString, QString>> dict = {
        {"download", {{"zh", "下载"}, {"en", "Download"}}},
        {"file_list", {{"zh", "文件列表"}, {"en", "File List"}}},
        // ... 所有翻译
    };
    auto it = dict.find(key);
    if (it != dict.end()) {
        auto langIt = it->find(lang);
        if (langIt != it->end()) return langIt.value();
    }
    return key;
}

// 从请求中读取语言参数
QString getLangFromRequest(mg_connection* conn) const {
    // 从 query string 读取 ?lang=en
    // 或从 Cookie 读取
    // 默认返回 "zh"
}
```

**影响**：步骤 9 需修正，步骤 2 中 RequestHandler 不再使用 `tr()`，改用翻译字典。

#### 错误 2：步骤 5 翻译加载时机描述不准确 — ⚠️ 需修正

**问题**：步骤 5 写"在 `main()` 函数中，`QApplication` 创建后、`NetShareApplication` 初始化前"，但 `SettingsManager` 在 `NetShareApplication::initializeSettings()` 中才初始化，在此之前无法读取语言设置。

**修正**：翻译加载应在 `NetShareApplication::initialize()` 方法中，`initializeSettings()` 之后、`initializeTrayIcon()` 之前执行。具体位置：
```
initializeLogger() → initializeDatabase() → initializeSettings() → 【加载翻译】 → initializeCoreServices() → initializeNetworkServer() → initializeTrayIcon() → initializeQml()
```

**影响**：托盘菜单（`initializeTrayIcon()`）和 QML 界面（`initializeQml()`）都能使用正确的翻译。

#### 遗漏 1：package.ps1 使用 `--no-translations` 标志 — ⚠️ 需修正

**问题**：`package.ps1` 第 39 行使用 `--no-translations` 参数调用 `windeployqt`，这会跳过 Qt 自带的翻译文件。虽然我们的翻译文件作为资源嵌入可执行文件（不需要 windeployqt 复制），但此标志可能导致 Qt 自带控件（如 QFontDialog、QFileDialog）的翻译缺失。

**修正**：移除 `--no-translations` 标志，或在步骤 11 中明确说明保留此标志的原因（减小包体积，且我们使用自定义 UI 不依赖 Qt 标准对话框）。

#### 遗漏 2：SettingsPage.qml 带宽控制页遗漏文本

**问题**：6.1 节遗漏列表中已列出 `当前全局速度`、`带宽限制`、`0 = 不限制`、`(不限制)` 等，但原文第 1 节 SettingsPage.qml 表格中第 36 行写的是 `当前速度 / 限速设置`，与实际代码 `当前全局速度` / `带宽限制` 不一致。

**修正**：第 1 节 SettingsPage.qml 表格第 36 行应更正为 `当前全局速度 / 带宽限制`。

#### 遗漏 3：SettingsPage.qml 上传路径页遗漏

**问题**：第 1 节 SettingsPage.qml 表格第 29 行写的是 `上传路径` 标签，但实际代码中还有 `上传目录` 标签（第 960 行）和 `上传路径` 页面标题（第 941 行）。

**修正**：已在 6.1 节遗漏列表中列出，无需额外修改。

#### 确认项：NotificationManager 有 Q_OBJECT 宏 — ✅ 已确认

**结果**：`NotificationManager` 继承自 `QObject`，有 `Q_OBJECT` 宏，`tr()` 可用。冲突 8 已解决。

#### 确认项：ChatService 有 Q_OBJECT 宏 — ✅ 已确认

**结果**：`ChatService` 继承自 `QObject`，有 `Q_OBJECT` 宏，`tr()` 可用。

#### 确认项：RequestHandler 有 Q_OBJECT 宏 — ✅ 已确认

**结果**：`RequestHandler` 继承自 `QObject`，有 `Q_OBJECT` 宏，`tr()` 技术上可用，但因错误 1 的原因不应使用 `tr()` 处理动态 HTML。
