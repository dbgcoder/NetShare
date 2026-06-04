# 移动端页面实现方式比较：内嵌 C++ vs Web 文件

## 0. 问题概述

当前移动端页面实现方式混乱：
- **下载页**（sharePage/passwordPage/errorPage）：内嵌在 C++ `RequestHandler.cpp` 中，约 450 行
- **上传页**（uploadPage）：内嵌在 C++ 中，约 120 行
- **接收页**（receive.html）：独立 Web 文件，1415 行
- **首页**（index.html）：独立 Web 文件，402 行 — **死文件，未被任何路由引用**

导致的问题：
1. 内嵌页面修改需重新编译，调试困难
2. `QString::arg()` 与 JS 中的 `%` 冲突，已多次引发乱码 bug
3. i18n 实现方式不统一（C++ `i18nTable()` + `htmlI18nDict()` + JS `data-i18n` 共 3 套）
4. 代码可读性差，HTML/CSS/JS 混在 C++ 字符串中

---

## 1. 两种方案对比

### 1.1 内嵌 C++ 方案（当前下载页/上传页的方式）

| 维度 | 评价 |
|------|------|
| **编译依赖** | 每次修改 HTML/CSS/JS 都需重新编译 C++（约 30 秒） |
| **调试难度** | 极高。无法用浏览器 DevTools 调试源码，只能看运行时输出 |
| **代码可读性** | 差。HTML/CSS/JS 混在 C++ 字符串中，无语法高亮、无缩进 |
| **i18n 复杂度** | 高。需维护 C++ `i18nTable()` + `htmlI18nDict()` 两套翻译表 |
| **`%` 转义问题** | 频繁触发。JS 中的 `%`（如 `pct+'%'`）必须转义为 `%%`，遗漏即出 bug |
| **扩展性** | 差。添加新页面需修改 C++ 代码，增加编译时间 |
| **部署** | 无额外文件，单 exe 部署 |

### 1.2 Web 文件方案（当前 receive.html 的方式）

| 维度 | 评价 |
|------|------|
| **编译依赖** | 修改 HTML/CSS/JS 无需编译，刷新浏览器即可看到效果 |
| **调试难度** | 低。浏览器 DevTools 直接调试，源码映射清晰 |
| **代码可读性** | 好。独立 HTML 文件，完整语法高亮和格式化 |
| **i18n 复杂度** | 低。JS 端统一用 `data-i18n` 属性 + `t()` 函数，一套翻译表 |
| **`%` 转义问题** | 无。HTML/JS 中的 `%` 不经过 `QString::arg()` |
| **扩展性** | 好。添加新页面只需新增 HTML 文件，注册路由即可 |
| **部署** | 需附带 web 目录，但 CMake 已自动复制到构建目录 |

---

## 2. 冲突和遗漏

### 2.1 冲突：sharePage 有服务端动态数据

`generateSharePage()` 中文件列表由 `m_fileBrowser->listDirectory()` 动态生成，不能纯静态 HTML。
**解决**：share.html 通过 `/api/files/{token}` API 获取文件列表，由 JS 动态渲染（已有 `/api/files/*` 路由）。

### 2.2 遗漏：index.html 是死文件

`web/index.html` 未被任何路由引用，与内嵌的 sharePage 功能重复。
**解决**：迁移完成后删除 index.html，由 share.html 替代。

### 2.3 遗漏：i18n 翻译表实际有 3 套

- C++ `i18nTable()`：用于 `trHtml()` 服务端翻译（约 40 个 key）
- C++ `htmlI18nDict()`：注入到 JS 的翻译字典（约 50 个 key，比 i18nTable 多下载相关 key）
- JS `data-i18n`：receive.html/index.html 的翻译表（约 40 个 key）

三者有大量重叠 key（如 powered_by、select_file 等），但值不完全一致。
**解决**：合并为 common.js 一套翻译表，统一 key 命名。

### 2.4 遗漏：首页 `/` 路由也是内嵌 HTML

`GET /` 路由（第 135 行）也用 C++ 拼接了一个简单首页，需一并迁移。
**解决**：创建 web/home.html 替代。

### 2.5 遗漏：sharePage 中子目录链接 `?sub=` 未被处理

`generateSharePage()` 生成了 `/s/{token}?sub={name}&lang={lang}` 链接，但 `handleSharePage()` 没有解析 `sub` 参数，点击子目录链接实际无效。
**解决**：迁移时在 share.html 中通过 `/api/files/{token}?sub=xxx` API 支持子目录浏览。

### 2.6 遗漏：CMakeLists.txt 需更新

`web/CMakeLists.txt` 中 `WEB_HTML_FILES` 列表需添加新 HTML 文件，`common.js` 需加入复制列表。

### 2.7 遗漏：C++ 端需添加 loadWebFile() 工具函数

当前 receive.html 的加载逻辑是内联在路由中的，需提取为通用函数，支持 `{{变量}}` 替换。

---

## 3. 最优方案：Web 文件 + 模板替换 + API 动态数据

### 3.1 架构设计

```
web/
├── home.html         # 首页（替代 GET / 内嵌 HTML）
├── share.html        # 下载页（替代 generateSharePage，JS 通过 API 获取文件列表）
├── password.html     # 密码验证页（替代 generatePasswordPage）
├── error.html        # 错误页（替代 generateErrorPage）
├── upload.html       # 上传页（替代 generateUploadPage）
├── receive.html      # 接收页（已有，重构引用 common.js）
└── common.js         # 公共 i18n + 工具函数（新增）
```

删除：`index.html`（死文件，功能由 share.html 覆盖）

### 3.2 模板替换机制

C++ 端用 `QString::replace()` 替换 `{{变量}}`，避免 `QString::arg()` 的 `%` 冲突：

```cpp
QString html = loadWebFile("share.html");
html.replace("{{TOKEN}}", token);
html.replace("{{LANG}}", lang);
```

### 3.3 动态数据方案

- **token/lang**：`{{TOKEN}}`/`{{LANG}}` 模板替换
- **文件列表**：JS 通过 `/api/files/{token}` API 获取并渲染
- **i18n**：common.js 统一翻译，通过 `/api/language` 获取语言设置

---

## 4. 执行步骤

### 步骤 1：创建 common.js 公共模块

- **修改内容**：新建 `web/common.js`，合并 3 套 i18n 翻译表为 1 套，提取 `t()`/`tf()`/`applyI18n()`/`doApplyI18n()`/`formatSize()` 函数
- **难易程度**：低
- **完成状态**：完成
- **验证方式**：receive.html 引用 common.js 后功能不变

### 步骤 2：重构 receive.html 引用 common.js

- **修改内容**：删除 receive.html 中内联的 i18n 翻译表和 `t()`/`tf()`/`applyI18n()`/`doApplyI18n()` 函数，改为引用 common.js；保留页面特有的 i18n key 在页面内
- **难易程度**：低
- **完成状态**：完成
- **验证方式**：接收页功能不变，中英文切换正常

### 步骤 3：添加 C++ loadWebFile() 工具函数

- **修改内容**：在 RequestHandler 中添加 `loadWebFile()` 函数，支持从 web 目录读取 HTML 文件并做 `{{变量}}` 替换
- **难易程度**：低
- **完成状态**：完成
- **验证方式**：编译通过

### 步骤 4：创建 home.html + 迁移首页路由

- **修改内容**：创建 `web/home.html`，修改 `GET /` 路由使用 loadWebFile() 加载
- **难易程度**：低
- **完成状态**：完成
- **验证方式**：访问首页显示正常，中英文切换正常

### 步骤 5：创建 error.html + password.html + 迁移路由

- **修改内容**：创建 `web/error.html` 和 `web/password.html`，修改 handleSharePage 中对应调用
- **难易程度**：低
- **完成状态**：完成
- **验证方式**：无效链接显示错误页，密码验证页功能正常

### 步骤 6：创建 share.html + 迁移下载页路由

- **修改内容**：创建 `web/share.html`，文件列表通过 `/api/files/{token}` API 动态获取；修改 handleSharePage 使用 loadWebFile() 加载；修复 `?sub=` 子目录浏览
- **难易程度**：高（页面最复杂，含断点续传、进度显示、文件列表动态渲染）
- **完成状态**：完成
- **验证方式**：移动端下载页功能正常，文件列表显示正确，断点续传正常，中英文切换正常

### 步骤 7：创建 upload.html + 迁移上传页路由

- **修改内容**：创建 `web/upload.html`，修改 handleUploadPage 使用 loadWebFile() 加载
- **难易程度**：中
- **完成状态**：完成
- **验证方式**：上传文件功能正常，中英文切换正常

### 步骤 8：清理 C++ 代码 + 更新 CMakeLists + 删除死文件

- **修改内容**：删除 `generateSharePage()`/`generateUploadPage()`/`generatePasswordPage()`/`generateErrorPage()` 函数（约 500 行），删除 `i18nTable()`/`trHtml()`/`htmlI18nDict()` 函数（约 250 行），更新 `web/CMakeLists.txt`，删除 `web/index.html`
- **难易程度**：低
- **完成状态**：完成
- **验证方式**：编译通过，所有页面功能正常

---

## 5. 迁移收益

| 指标 | 迁移前 | 迁移后 |
|------|--------|--------|
| 修改页面需编译 | 是（~30秒） | 否（刷新即可） |
| `%` 转义 bug 风险 | 高 | 无 |
| i18n 翻译表 | 3 套（C++ i18nTable + C++ htmlI18nDict + JS） | 1 套（JS common.js） |
| 浏览器 DevTools 调试 | 不可用 | 可用 |
| C++ 代码行数 | ~2474 行（含 ~750 行 HTML+i18n） | ~1927 行 |
| 新增页面流程 | 修改 C++ + 编译 | 新建 HTML + 注册路由 |
| 死文件 | index.html 存在 | 已清理 |
