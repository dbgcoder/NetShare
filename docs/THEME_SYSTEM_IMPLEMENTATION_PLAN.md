# 主题系统实现计划

## 0. 问题概述

NetShare 当前只有暗色主题，颜色硬编码在 C++ `ThemeProvider` 和 QML 文件中（73处硬编码颜色值）。需要：
1. 在设置页语言上方添加主题选择（亮色/暗色）
2. 切换主题后所有页面统一更换颜色，消除硬编码
3. 预留扩展能力，通过配置文件加入新主题无需编译代码

---

## 1. 当前主题系统分析

### 1.1 现有 ThemeProvider 结构

文件：`src/gui/ThemeProvider.h` / `ThemeProvider.cpp`

- C++ 单例，QML 中通过 `Theme.xxx` 访问
- 所有属性标记为 `CONSTANT`（不支持动态切换）
- 10个颜色属性 + 6个尺寸属性

| 属性 | 暗色值 | 用途 |
|------|--------|------|
| backgroundColor | #202020 | 页面背景 |
| surfaceColor | #2d2d30 | 卡片/输入框背景 |
| sidebarColor | #2b2b2d | 侧边栏背景 |
| accentColor | #007acc | 强调色/选中色 |
| textColor | #ffffff | 主文字颜色 |
| textSecondary | #cccccc | 次要文字颜色 |
| borderColor | #3e3e42 | 边框颜色 |
| successColor | #4caf50 | 成功/安全色 |
| warningColor | #ff9800 | 警告色 |
| errorColor | #f44336 | 错误/危险色 |

### 1.2 硬编码颜色完整统计

QML 文件中共 **68处** 硬编码颜色值，需全部迁移到 Theme 属性：

| 硬编码值 | 含义 | 出现次数 | 出现位置 | 需新增Theme属性 |
|----------|------|----------|----------|-----------------|
| `#ffffff` | 强调色上的白色文字 | 24 | Main(3)/Share(5)/Settings(1)/Message(4)/Transfer(2)/Receive(4)/DeviceDiscovery(3)/ThemedButton/ThemedComboBox | `textOnAccentColor` |
| `#ffffff` | Switch滑块颜色 | 1 | ThemedSwitch | `switchThumbColor` **[新增]** |
| `#ffffff` | QR码容器背景 | 3 | ShareManagement(1)/ReceiveManagement(2) | `qrCodeBgColor` **[新增]** |
| `#cccccc` | 强调色上的次要文字 | 2 | MessagePage | `textOnAccentSecondaryColor` **[新增]** |
| `#3e3e42` | 悬停背景色 | 27 | Main(4)/Settings(3)/Share(5)/Message(2)/Transfer(5)/Receive(4)/DeviceDiscovery(2)/ThemedButton | `hoverColor` |
| `#333336` | 列表项悬停背景 | 3 | DeviceDiscovery/ShareManagement/TransferList | `itemHoverColor` |
| `#252528` | 交替行背景 | 1 | SettingsPage | `alternateRowColor` |
| `#555555` | Switch未选中轨道 | 1 | ThemedSwitch | `switchTrackColor` |
| `#005a9e` | 按钮按下色 | 1 | ThemedButton | `accentPressedColor` |
| `#e81123` | 关闭按钮悬停 | 1 | Main | `closeHoverColor` |
| `#3e2a1a` | TLS警告背景 | 1 | SettingsPage | `warningBgColor` |
| `#1a3e2a` | TLS安全背景 | 1 | SettingsPage | `successBgColor` |
| `#80000000` | 半透明遮罩层 | 2 | Main/ShareManagement | `overlayColor` **[新增]** |

### 1.3 SVG 图标硬编码

4个SVG图标中 `fill="#ffffff"` 硬编码白色，亮色主题下需改为主题色：

| 文件 | 当前 fill |
|------|-----------|
| `icons/send.svg` | `#ffffff` |
| `icons/receive.svg` | `#ffffff` |
| `icons/transfer.svg` | `#ffffff` |
| `icons/lan.svg` | `#ffffff` |

### 1.4 Qt 控件主题适配遗漏

以下 Qt 控件在亮色主题下会出现配色问题，原计划未覆盖：

| 控件 | 问题 | 影响 |
|------|------|------|
| Dialog (SettingsPage) | 无自定义 background，亮色主题下背景为白色系统默认 | 弹窗颜色不匹配 |
| TextField | 无 placeholderTextColor，亮色主题下占位文字可能不可见 | 输入框占位文字颜色不匹配 |
| TextField | 无 selectionColor/selectedTextColor，亮色主题下选中文字颜色不匹配 | 文字选中高亮不匹配 |
| ScrollBar | 无自定义样式，使用 Qt Basic 默认 | 滚动条颜色不匹配 |
| ToolTip | 无自定义样式，使用 Qt Basic 默认 | 提示框颜色不匹配 |
| Overlay.modal | 无自定义遮罩颜色 | 弹窗遮罩层不匹配 |
| QMenu (托盘) | 使用 Qt Widgets，跟随系统主题 | 无法适配应用主题（已知限制） |

---

## 2. 设计方案

### 2.1 主题配置文件格式（JSON）

采用 JSON 格式作为主题配置文件，放在 `src/gui/themes/` 目录下，编译时嵌入 QRC 资源，运行时也可从外部文件加载。

**文件：`src/gui/themes/dark.json`**

```json
{
    "name": "Dark",
    "id": "dark",
    "colors": {
        "backgroundColor": "#202020",
        "surfaceColor": "#2d2d30",
        "sidebarColor": "#2b2b2d",
        "accentColor": "#007acc",
        "textColor": "#ffffff",
        "textSecondary": "#cccccc",
        "borderColor": "#3e3e42",
        "successColor": "#4caf50",
        "warningColor": "#ff9800",
        "errorColor": "#f44336",
        "textOnAccentColor": "#ffffff",
        "hoverColor": "#3e3e42",
        "itemHoverColor": "#333336",
        "alternateRowColor": "#252528",
        "switchTrackColor": "#555555",
        "accentPressedColor": "#005a9e",
        "closeHoverColor": "#e81123",
        "warningBgColor": "#3e2a1a",
        "successBgColor": "#1a3e2a",
        "switchThumbColor": "#ffffff",
        "qrCodeBgColor": "#ffffff",
        "textOnAccentSecondaryColor": "#cccccc",
        "overlayColor": "#80000000"
    },
    "fontSizes": {
        "large": 24,
        "medium": 16,
        "small": 12
    },
    "metrics": {
        "spacing": 8,
        "padding": 16,
        "radius": 4
    }
}
```

**文件：`src/gui/themes/light.json`**

```json
{
    "name": "Light",
    "id": "light",
    "colors": {
        "backgroundColor": "#f5f5f5",
        "surfaceColor": "#ffffff",
        "sidebarColor": "#e8e8e8",
        "accentColor": "#0078d4",
        "textColor": "#1a1a1a",
        "textSecondary": "#666666",
        "borderColor": "#d0d0d0",
        "successColor": "#107c10",
        "warningColor": "#d48c00",
        "errorColor": "#d13438",
        "textOnAccentColor": "#ffffff",
        "hoverColor": "#e5e5e5",
        "itemHoverColor": "#ebebeb",
        "alternateRowColor": "#f0f0f0",
        "switchTrackColor": "#b0b0b0",
        "accentPressedColor": "#005a9e",
        "closeHoverColor": "#e81123",
        "warningBgColor": "#fff4ce",
        "successBgColor": "#dff6dd",
        "switchThumbColor": "#ffffff",
        "qrCodeBgColor": "#ffffff",
        "textOnAccentSecondaryColor": "#c0d8ec",
        "overlayColor": "#80000000"
    },
    "fontSizes": {
        "large": 24,
        "medium": 16,
        "small": 12
    },
    "metrics": {
        "spacing": 8,
        "padding": 16,
        "radius": 4
    }
}
```

### 2.2 ThemeProvider 改造

将 `ThemeProvider` 从硬编码常量改为可动态切换的主题管理器：

**关键改动：**

1. 所有颜色属性从 `CONSTANT` 改为带 `NOTIFY` 信号，支持动态切换
2. 添加 `currentTheme` 属性，存储当前主题 ID
3. 添加 `availableThemes` 属性，返回可用主题列表
4. 添加 `switchTheme(themeId)` 方法，切换主题
5. 主题加载优先级：QRC → 外部文件 → 内置默认
6. **[新增]** 添加4个新颜色属性：`switchThumbColor`、`qrCodeBgColor`、`textOnAccentSecondaryColor`、`overlayColor`
7. **[新增]** `overlayColor` 支持 ARGB 格式（`#AARRGGBB`），用于半透明遮罩

**ThemeProvider.h 改造后核心结构：**

```cpp
class ThemeProvider : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Theme)
    QML_SINGLETON

    Q_PROPERTY(QString currentTheme READ currentTheme WRITE setCurrentTheme NOTIFY themeChanged)
    Q_PROPERTY(QStringList availableThemes READ availableThemes CONSTANT)

    Q_PROPERTY(QColor backgroundColor READ backgroundColor NOTIFY themeChanged)
    Q_PROPERTY(QColor surfaceColor READ surfaceColor NOTIFY themeChanged)
    Q_PROPERTY(QColor sidebarColor READ sidebarColor NOTIFY themeChanged)
    Q_PROPERTY(QColor accentColor READ accentColor NOTIFY themeChanged)
    Q_PROPERTY(QColor textColor READ textColor NOTIFY themeChanged)
    Q_PROPERTY(QColor textSecondary READ textSecondary NOTIFY themeChanged)
    Q_PROPERTY(QColor borderColor READ borderColor NOTIFY themeChanged)
    Q_PROPERTY(QColor successColor READ successColor NOTIFY themeChanged)
    Q_PROPERTY(QColor warningColor READ warningColor NOTIFY themeChanged)
    Q_PROPERTY(QColor errorColor READ errorColor NOTIFY themeChanged)
    Q_PROPERTY(QColor textOnAccentColor READ textOnAccentColor NOTIFY themeChanged)
    Q_PROPERTY(QColor hoverColor READ hoverColor NOTIFY themeChanged)
    Q_PROPERTY(QColor itemHoverColor READ itemHoverColor NOTIFY themeChanged)
    Q_PROPERTY(QColor alternateRowColor READ alternateRowColor NOTIFY themeChanged)
    Q_PROPERTY(QColor switchTrackColor READ switchTrackColor NOTIFY themeChanged)
    Q_PROPERTY(QColor accentPressedColor READ accentPressedColor NOTIFY themeChanged)
    Q_PROPERTY(QColor closeHoverColor READ closeHoverColor NOTIFY themeChanged)
    Q_PROPERTY(QColor warningBgColor READ warningBgColor NOTIFY themeChanged)
    Q_PROPERTY(QColor successBgColor READ successBgColor NOTIFY themeChanged)
    Q_PROPERTY(QColor switchThumbColor READ switchThumbColor NOTIFY themeChanged)
    Q_PROPERTY(QColor qrCodeBgColor READ qrCodeBgColor NOTIFY themeChanged)
    Q_PROPERTY(QColor textOnAccentSecondaryColor READ textOnAccentSecondaryColor NOTIFY themeChanged)
    Q_PROPERTY(QColor overlayColor READ overlayColor NOTIFY themeChanged)

    Q_PROPERTY(int fontSizeLarge READ fontSizeLarge NOTIFY themeChanged)
    Q_PROPERTY(int fontSizeMedium READ fontSizeMedium NOTIFY themeChanged)
    Q_PROPERTY(int fontSizeSmall READ fontSizeSmall NOTIFY themeChanged)
    Q_PROPERTY(int spacing READ spacing NOTIFY themeChanged)
    Q_PROPERTY(int padding READ padding NOTIFY themeChanged)
    Q_PROPERTY(int radius READ radius NOTIFY themeChanged)

public:
    static ThemeProvider* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    QString currentTheme() const;
    void setCurrentTheme(const QString& themeId);
    QStringList availableThemes() const;

    Q_INVOKABLE void switchTheme(const QString& themeId);

    QColor backgroundColor() const;
    QColor surfaceColor() const;
    QColor sidebarColor() const;
    QColor accentColor() const;
    QColor textColor() const;
    QColor textSecondary() const;
    QColor borderColor() const;
    QColor successColor() const;
    QColor warningColor() const;
    QColor errorColor() const;
    QColor textOnAccentColor() const;
    QColor hoverColor() const;
    QColor itemHoverColor() const;
    QColor alternateRowColor() const;
    QColor switchTrackColor() const;
    QColor accentPressedColor() const;
    QColor closeHoverColor() const;
    QColor warningBgColor() const;
    QColor successBgColor() const;
    QColor switchThumbColor() const;
    QColor qrCodeBgColor() const;
    QColor textOnAccentSecondaryColor() const;
    QColor overlayColor() const;

    int fontSizeLarge() const;
    int fontSizeMedium() const;
    int fontSizeSmall() const;
    int spacing() const;
    int padding() const;
    int radius() const;

signals:
    void themeChanged();

private:
    void loadBuiltinThemes();
    bool loadThemeFromJson(const QString& themeId);
    void applyTheme(const QString& themeId);
    QColor parseColor(const QString& colorStr) const;

    struct ThemeData {
        QString id;
        QString name;
        QMap<QString, QColor> colors;
        QMap<QString, int> fontSizes;
        QMap<QString, int> metrics;
    };

    QMap<QString, ThemeData> m_themes;
    QString m_currentTheme;
    ThemeData m_activeData;
};
```

**ThemeProvider.cpp 关键实现：**

```cpp
#include "ThemeProvider.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QGuiApplication>
#include <QQmlEngine>

QColor ThemeProvider::parseColor(const QString& colorStr) const
{
    QString c = colorStr.trimmed();
    if (c.startsWith("#") && c.length() == 9) {
        // ARGB format: #AARRGGBB -> convert to QColor
        bool ok;
        uint argb = c.mid(1).toUInt(&ok, 16);
        if (ok) {
            return QColor::fromRgba(argb);
        }
    }
    return QColor(c);
}

void ThemeProvider::loadBuiltinThemes()
{
    // Load from QRC
    QStringList builtinThemes = {"dark", "light"};
    for (const auto& id : builtinThemes) {
        QString qrcPath = QString(":/themes/%1.json").arg(id);
        QFile f(qrcPath);
        if (f.open(QIODevice::ReadOnly)) {
            loadThemeFromJson(QString::fromUtf8(f.readAll()));
        }
    }

    // Load from external directory
    QDir themesDir(QGuiApplication::applicationDirPath() + "/themes");
    if (themesDir.exists()) {
        for (const auto& entry : themesDir.entryList({"*.json"})) {
            QFile f(themesDir.filePath(entry));
            if (f.open(QIODevice::ReadOnly)) {
                loadThemeFromJson(QString::fromUtf8(f.readAll()));
            }
        }
    }
}

bool ThemeProvider::loadThemeFromJson(const QString& jsonData)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) return false;

    QJsonObject root = doc.object();
    ThemeData data;
    data.id = root["id"].toString();
    data.name = root["name"].toString();

    QJsonObject colors = root["colors"].toObject();
    for (auto it = colors.begin(); it != colors.end(); ++it) {
        data.colors[it.key()] = parseColor(it.value().toString());
    }

    QJsonObject fontSizes = root["fontSizes"].toObject();
    for (auto it = fontSizes.begin(); it != fontSizes.end(); ++it) {
        data.fontSizes[it.key()] = it.value().toInt();
    }

    QJsonObject metrics = root["metrics"].toObject();
    for (auto it = metrics.begin(); it != metrics.end(); ++it) {
        data.metrics[it.key()] = it.value().toInt();
    }

    m_themes[data.id] = data;
    return true;
}

void ThemeProvider::applyTheme(const QString& themeId)
{
    if (!m_themes.contains(themeId)) return;
    m_currentTheme = themeId;
    m_activeData = m_themes[themeId];
    emit themeChanged();
}

void ThemeProvider::switchTheme(const QString& themeId)
{
    applyTheme(themeId);
    // Persist to QSettings
    QSettings settings;
    settings.setValue("General/Theme", themeId);
}
```

### 2.3 SVG 图标主题适配

**原方案问题**：计划使用 `Icon` 组件的 `color` 属性，但 Qt 6 中没有 `Icon` QML 类型。`IconImage` 曾导致 "IconImage is not a type" 错误。

**修正方案（推荐）**：使用 `Image` + `MultiEffect`（Qt 6.5+ 内置模块 `QtQuick.Effects`）

1. 将 SVG 中的 `fill="#ffffff"` 改为 `fill="currentColor"`
2. 在 QML 中使用 `Image` + `MultiEffect` 实现颜色替换：

```qml
import QtQuick.Effects

// 在导航栏图标组件中
Item {
    id: iconContainer
    width: 18
    height: 18

    Image {
        id: iconImage
        source: iconSource
        sourceSize.width: 18
        sourceSize.height: 18
        visible: false
    }

    MultiEffect {
        source: iconImage
        anchors.fill: iconImage
        colorizationColor: isSelected ? Theme.textColor : Theme.textSecondary
        colorization: 1.0
    }
}
```

**备选方案**：如果 `MultiEffect` 不可用，使用 `Image` + `layer` + `ColorOverlay`（需要 `Qt5Compat.GraphicalEffects`）：

```qml
import Qt5Compat.GraphicalEffects

Image {
    source: iconSource
    sourceSize.width: 18
    sourceSize.height: 18
    layer.enabled: true
    layer.effect: ColorOverlay {
        color: isSelected ? Theme.textColor : Theme.textSecondary
    }
}
```

**需要在 CMakeLists.txt 中添加对应模块依赖**：
- 主方案：`Qt6::QuickEffects`（对应 `QtQuick.Effects`）
- 备选方案：`Qt6::5Compat`（对应 `Qt5Compat.GraphicalEffects`）

### 2.4 设置页主题选择 UI

在语言选择上方添加主题选择：

```
┌─────────────────────────────────────┐
│ 主题          [  暗色  ▼ ]          │  ← 新增
│ 语言          [ 简体中文 ▼ ]        │
│ 开机自启      [  ○  ]              │
│ 最小化到托盘   [  ●  ]              │
└─────────────────────────────────────┘
```

主题下拉框选项从 `Theme.availableThemes` 动态加载，显示主题的 `name` 字段。

### 2.5 主题持久化

- 主题设置存储在 `QSettings` 的 `General/Theme` 键中
- 默认值为 `"dark"`
- 启动时从设置读取并应用主题
- 切换主题立即生效（无需重启）

### 2.6 Qt 控件主题适配方案

**[新增]** 以下控件需要额外适配才能在亮色主题下正确显示：

#### 2.6.1 Dialog 适配

所有 Dialog 必须添加自定义 `background`：

```qml
Dialog {
    background: Rectangle {
        color: Theme.surfaceColor
        radius: 8
        border.color: Theme.borderColor
    }
}
```

**需要修复的 Dialog**：
- `SettingsPage.qml:1134` - `restartHintDialog`（当前无自定义 background）

已正确设置的 Dialog（无需修改）：
- `ShareManagement.qml:358` - `detailDialog` ✓
- `ShareManagement.qml:612` - `shareDialog` ✓
- `ShareManagement.qml:786` - `qrCodeDialog` ✓
- `ReceiveManagement.qml:126` - `receiveQrDialog` ✓
- `ReceiveManagement.qml:207` - `forwardQrDialog` ✓

#### 2.6.2 TextField 适配

所有 TextField 添加 `placeholderTextColor`、`selectionColor`、`selectedTextColor`：

```qml
TextField {
    placeholderTextColor: Theme.textSecondary
    selectionColor: Theme.accentColor
    selectedTextColor: Theme.textOnAccentColor
}
```

**涉及文件**：SettingsPage.qml、ShareManagement.qml、DeviceDiscovery.qml、MessagePage.qml、ReceiveManagement.qml（共16个 TextField）

#### 2.6.3 ScrollBar 适配

自定义 ScrollBar 样式：

```qml
ScrollBar.vertical: ScrollBar {
    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 100
        radius: 3
        color: parent.active ? Theme.textSecondary : Theme.borderColor
        opacity: parent.active ? 0.8 : 0.4
    }
}
```

**涉及文件**：DeviceDiscovery.qml、ShareManagement.qml、TransferList.qml（共3个 ScrollBar）

#### 2.6.4 ToolTip 适配

在 ApplicationWindow 中设置全局 ToolTip 样式：

```qml
// 在 Main.qml 的 ApplicationWindow 中添加
ToolTip.palette.window: Theme.surfaceColor
ToolTip.palette.windowText: Theme.textColor
ToolTip.palette.mid: Theme.borderColor
```

或为每个 ToolTip 单独设置（不推荐，太多）。

#### 2.6.5 Overlay.modal 适配

在 ApplicationWindow 中设置模态遮罩：

```qml
// 在 Main.qml 的 ApplicationWindow 中添加
Overlay.modal: Rectangle {
    color: Theme.overlayColor
}
```

#### 2.6.6 QMenu (托盘菜单) - 已知限制

QMenu 是 Qt Widgets 组件，无法通过 QML 主题系统控制。托盘菜单始终跟随操作系统主题。这是 Qt 框架的固有限制，暂不处理。

### 2.7 扩展新主题的流程

添加新主题只需3步，**无需修改任何代码文件**：

1. 创建新的 JSON 文件，如 `src/gui/themes/blue.json`
2. 在 `CMakeLists.txt` 的 `RESOURCES` 中添加该文件
3. 重新编译（仅资源更新，不修改代码）

如果需要运行时加载外部主题文件（完全不编译）：
1. 将 JSON 文件放到 `<exe_dir>/themes/` 目录
2. ThemeProvider 自动扫描该目录并加载

---

## 3. 执行步骤

### 步骤1：创建主题配置文件

- **修改内容**：
  - 创建 `src/gui/qml/themes/dark.json`（暗色主题配置，23个颜色属性）
  - 创建 `src/gui/qml/themes/light.json`（亮色主题配置，23个颜色属性）
  - 在 `src/gui/CMakeLists.txt` 的 `RESOURCES` 中添加 `qml/themes/dark.json` 和 `qml/themes/light.json`
- **难易程度**：低
- **完成状态**：完成
- **验证方式**：JSON 文件格式正确，CMake 配置后编译通过

### 步骤2：改造 ThemeProvider

- **修改内容**：
  - 将 `ThemeProvider.h` 中所有 `CONSTANT` 改为 `NOTIFY themeChanged`
  - 添加13个新颜色属性（原9个 + 新增4个：switchThumbColor, qrCodeBgColor, textOnAccentSecondaryColor, overlayColor）
  - 添加 `currentTheme`、`availableThemes` 属性
  - 添加 `switchTheme()` 方法
  - 添加 `loadBuiltinThemes()`、`loadThemeFromJson()`、`applyTheme()`、`parseColor()` 方法
  - 添加 `ThemeData` 内部结构体
  - 改造 `ThemeProvider.cpp` 实现 JSON 加载和主题切换逻辑
  - `parseColor()` 需支持 ARGB 格式（`#AARRGGBB`）用于 `overlayColor`
- **难易程度**：高
- **完成状态**：完成
- **验证方式**：编译通过，QML 中 `Theme.switchTheme("light")` 可切换颜色

### 步骤3：消除 QML 硬编码颜色

- **修改内容**：
  - 将68处硬编码颜色值替换为 `Theme.xxx` 属性引用
  - 涉及文件：Main.qml, SettingsPage.qml, ShareManagement.qml, DeviceDiscovery.qml, MessagePage.qml, TransferList.qml, ReceiveManagement.qml, ThemedButton.qml, ThemedComboBox.qml, ThemedSwitch.qml
  - **精确替换映射表**：

    | 硬编码值 | 上下文 | 替换为 | 出现次数 |
    |----------|--------|--------|----------|
    | `"#ffffff"` | 强调色上的文字（按钮、选中项、状态徽章、Toast、未读徽章、发送消息文字） | `Theme.textOnAccentColor` | 24 |
    | `"#ffffff"` | Switch 滑块 | `Theme.switchThumbColor` | 1 |
    | `"#ffffff"` | QR码容器背景 | `Theme.qrCodeBgColor` | 3 |
    | `"#cccccc"` | 强调色上的次要文字（消息时间、用户列表次要信息） | `Theme.textOnAccentSecondaryColor` | 2 |
    | `"#3e3e42"` | 悬停背景 | `Theme.hoverColor` | 27 |
    | `"#333336"` | 列表项悬停背景 | `Theme.itemHoverColor` | 3 |
    | `"#252528"` | 交替行背景 | `Theme.alternateRowColor` | 1 |
    | `"#555555"` | Switch未选中轨道 | `Theme.switchTrackColor` | 1 |
    | `"#005a9e"` | 按钮按下 | `Theme.accentPressedColor` | 1 |
    | `"#e81123"` | 关闭按钮悬停 | `Theme.closeHoverColor` | 1 |
    | `"#3e2a1a"` | TLS警告背景 | `Theme.warningBgColor` | 1 |
    | `"#1a3e2a"` | TLS安全背景 | `Theme.successBgColor` | 1 |
    | `"#80000000"` | 半透明遮罩 | `Theme.overlayColor` | 2 |

  - **逐文件替换明细**：

    **Main.qml**（10处）：
    - L99: `"#80000000"` → `Theme.overlayColor`
    - L178: `"#3e3e42"` → `Theme.hoverColor`（消息按钮悬停）
    - L206: `"#ffffff"` → `Theme.textOnAccentColor`（未读徽章文字）
    - L230: `"#3e3e42"` → `Theme.hoverColor`（设置按钮悬停）
    - L259: `"#3e3e42"` → `Theme.hoverColor`（最小化按钮悬停）
    - L283: `"#3e3e42"` → `Theme.hoverColor`（最大化按钮悬停）
    - L312: `"#e81123"` → `Theme.closeHoverColor`（关闭按钮悬停）
    - L446: `"#3e3e42"` → `Theme.hoverColor`（导航项悬停）
    - L490: `"#ffffff"` → `Theme.textOnAccentColor`（选中导航图标）
    - L497: `"#ffffff"` → `Theme.textOnAccentColor`（选中导航文字）

    **SettingsPage.qml**（7处）：
    - L144: `"#3e3e42"` → `Theme.hoverColor`（设置项悬停）
    - L161: `"#ffffff"` → `Theme.textOnAccentColor`（选中设置项文字）
    - L201: `"#3e3e42"` → `Theme.hoverColor`（关闭设置按钮悬停）
    - L583: `"#3e3e42"` → `Theme.hoverColor`（下拉框悬停）
    - L746: `"#252528"` → `Theme.alternateRowColor`（交替行背景）
    - L977: `"#3e2a1a"` → `Theme.warningBgColor`（TLS警告背景）
    - L1000: `"#1a3e2a"` → `Theme.successBgColor`（TLS安全背景）

    **ShareManagement.qml**（13处）：
    - L122: `"#ffffff"` → `Theme.textOnAccentColor`（新建分享按钮文字）
    - L149: `"#ffffff"` → `Theme.textOnAccentColor`（选中项文字）
    - L214: `"#333336"` → `Theme.itemHoverColor`（列表项悬停）
    - L305: `"#3e3e42"` → `Theme.hoverColor`（复制链接悬停）
    - L322: `"#3e3e42"` → `Theme.hoverColor`（二维码悬停）
    - L343: `"#3e3e42"` → `Theme.hoverColor`（取消分享悬停）
    - L446: `"#ffffff"` → `Theme.textOnAccentColor`（对话框状态徽章）
    - L576: `"#3e3e42"` → `Theme.hoverColor`（复制链接悬停）
    - L593: `"#3e3e42"` → `Theme.hoverColor`（二维码悬停）
    - L820: `"#ffffff"` → `Theme.qrCodeBgColor`（QR码背景）
    - L847: `"#ffffff"` → `Theme.textOnAccentColor`（复制链接按钮文字）
    - L868: `"#80000000"` → `Theme.overlayColor`（遮罩层）
    - L881: `"#ffffff"` → `Theme.textOnAccentColor`（Toast通知文字）

    **DeviceDiscovery.qml**（6处）：
    - L141: `"#ffffff"` → `Theme.textOnAccentColor`（扫描按钮文字）
    - L175: `"#ffffff"` → `Theme.textOnAccentColor`（筛选标签文字）
    - L224: `"#333336"` → `Theme.itemHoverColor`（列表项悬停）
    - L315: `"#3e3e42"` → `Theme.hoverColor`（重命名按钮悬停）
    - L346: `"#ffffff"` → `Theme.textOnAccentColor`（状态徽章文字）
    - L361: `"#3e3e42"` → `Theme.hoverColor`（发送消息按钮悬停）

    **MessagePage.qml**（8处）：
    - L19: `"#3e3e42"` → `Theme.hoverColor`（关闭按钮悬停）
    - L215: `"#3e3e42"` → `Theme.hoverColor`（用户项悬停）
    - L248: `"#ffffff"` → `Theme.textOnAccentColor`（选中用户文字）
    - L263: `"#ffffff"` → `Theme.textOnAccentColor`（未读徽章文字）
    - L271: `"#cccccc"` → `Theme.textOnAccentSecondaryColor`（选中用户次要文字）
    - L379: `"#ffffff"` → `Theme.textOnAccentColor`（已发送消息文字）
    - L397: `"#cccccc"` → `Theme.textOnAccentSecondaryColor`（已发送消息次要文字）
    - L442: `"#ffffff"` → `Theme.textOnAccentColor`（发送按钮文字）

    **TransferList.qml**（8处）：
    - L240: `"#3e3e42"` → `Theme.hoverColor`（操作按钮悬停）
    - L263: `"#3e3e42"` → `Theme.hoverColor`（操作按钮悬停）
    - L298: `"#ffffff"` → `Theme.textOnAccentColor`（选中项文字）
    - L363: `"#333336"` → `Theme.itemHoverColor`（列表项悬停）
    - L403: `"#ffffff"` → `Theme.textOnAccentColor`（文件图标）
    - L421: `"#3e3e42"` → `Theme.hoverColor`（暂停按钮悬停）
    - L439: `"#3e3e42"` → `Theme.hoverColor`（恢复按钮悬停）
    - L457: `"#3e3e42"` → `Theme.hoverColor`（删除按钮悬停）

    **ReceiveManagement.qml**（10处）：
    - L160: `"#ffffff"` → `Theme.qrCodeBgColor`（接收QR码背景）
    - L187: `"#ffffff"` → `Theme.textOnAccentColor`（复制链接按钮文字）
    - L243: `"#ffffff"` → `Theme.qrCodeBgColor`（转发QR码背景）
    - L270: `"#ffffff"` → `Theme.textOnAccentColor`（复制链接按钮文字）
    - L367: `"#ffffff"` → `Theme.textOnAccentColor`（QR码按钮悬停文字）
    - L385: `"#3e3e42"` → `Theme.hoverColor`（路径按钮悬停）
    - L439: `"#ffffff"` → `Theme.textOnAccentColor`（选中筛选文字）
    - L534: `"#3e3e42"` → `Theme.hoverColor`（文件夹按钮悬停）
    - L547: `"#3e3e42"` → `Theme.hoverColor`（二维码按钮悬停）
    - L569: `"#3e3e42"` → `Theme.hoverColor`（删除按钮悬停）

    **ThemedButton.qml**（3处）：
    - L12: `"#ffffff"` → `Theme.textOnAccentColor`
    - L20: `"#005a9e"` → `Theme.accentPressedColor`
    - L21: `"#3e3e42"` → `Theme.hoverColor`

    **ThemedComboBox.qml**（1处）：
    - L64: `"#ffffff"` → `Theme.textOnAccentColor`

    **ThemedSwitch.qml**（2处）：
    - L14: `"#555555"` → `Theme.switchTrackColor`
    - L26: `"#ffffff"` → `Theme.switchThumbColor`

- **难易程度**：中
- **完成状态**：完成
- **验证方式**：`grep -r '"#[0-9a-fA-F]\{6\}"' src/gui/qml/` 仅返回SVG文件结果

### 步骤4：SVG 图标主题适配

- **修改内容**：
  - 将4个SVG图标中的 `fill="#ffffff"` 改为 `fill="currentColor"`
  - 在 `src/gui/CMakeLists.txt` 的 `target_link_libraries` 中添加 `Qt6::QuickEffectsPrivate`
  - 在顶层 `CMakeLists.txt` 的 `find_package` 中将 `QuickEffects` 改为 `QuickEffectsPrivate`
  - 在 Main.qml 导航栏图标组件中，将 `Image` 替换为 `Image` + `MultiEffect`：
    ```qml
    Image {
        id: iconImage
        source: iconSource
        sourceSize.width: 18
        sourceSize.height: 18
        visible: false
    }
    MultiEffect {
        source: iconImage
        anchors.fill: iconImage
        colorizationColor: isSelected ? Theme.textColor : Theme.textSecondary
        colorization: 1.0
    }
    ```
  - 如果 `MultiEffect` 不可用，备选使用 `Qt5Compat.GraphicalEffects` 的 `ColorOverlay`
- **难易程度**：中
- **完成状态**：完成
- **验证方式**：切换亮色主题后图标颜色跟随变化

### 步骤5：Qt 控件主题适配

- **修改内容**：
  - **5a. Dialog 适配**：
    - 为 `SettingsPage.qml` 的 `restartHintDialog` 添加 `background: Rectangle { color: Theme.surfaceColor; radius: 8; border.color: Theme.borderColor }`
    - 统一所有Dialog的 `border.color` 从 `Theme.sidebarColor` 改为 `Theme.borderColor`（涉及5个Dialog）：
      - ShareManagement.qml: detailDialog (L370)
      - ShareManagement.qml: shareDialog (L622)
      - ShareManagement.qml: qrCodeDialog (L799)
      - ReceiveManagement.qml: receiveQrDialog (L138)
      - ReceiveManagement.qml: forwardQrDialog (L219)
  - **5b. TextField 适配**：为所有 TextField 添加 `placeholderTextColor: Theme.textSecondary`、`selectionColor: Theme.accentColor`、`selectedTextColor: Theme.textOnAccentColor`
    - SettingsPage.qml：12个 TextField
    - ShareManagement.qml：2个 TextField
    - DeviceDiscovery.qml：1个 TextField
    - MessagePage.qml：1个 TextField
    - ReceiveManagement.qml：1个 TextField
  - **5c. ScrollBar 适配**：为3个 ScrollBar 添加自定义 `contentItem`
    - DeviceDiscovery.qml:1个
    - ShareManagement.qml:1个
    - TransferList.qml:1个
  - **5d. ToolTip 适配**：在 Main.qml 的 ApplicationWindow 中添加全局 ToolTip palette 设置
  - **5e. Overlay.modal 适配**：在 Main.qml 的 ApplicationWindow 中添加 `Overlay.modal: Rectangle { color: Theme.overlayColor }`
- **难易程度**：中
- **完成状态**：完成
- **验证方式**：亮色主题下所有控件颜色正确

### 步骤6：设置页添加主题选择 UI

- **修改内容**：
  - 在 SettingsPage.qml 的语言下拉框上方添加主题标签和下拉框
  - 主题下拉框的 model 绑定 `Theme.availableThemes`
  - 切换主题时调用 `Theme.switchTheme(themeId)`
  - 保存主题设置到 `General/Theme`
  - 加载设置时读取 `General/Theme` 并应用
- **难易程度**：中
- **完成状态**：完成
- **验证方式**：设置页显示主题选择，切换后所有页面颜色立即变化

### 步骤7：启动时加载主题设置

- **修改内容**：
  - 在 `main.cpp` 的 `initialize()` 中，`loadTranslator()` 之后添加主题初始化
  - 从 QSettings 读取 `General/Theme`，调用 `ThemeProvider::switchTheme()`
  - 或在 QML 的 `Main.qml` 的 `Component.onCompleted` 中初始化
- **难易程度**：低
- **完成状态**：完成
- **验证方式**：重启应用后主题设置保持

### 步骤8：添加翻译条目

- **修改内容**：
  - 在 `netshare_zh_CN.ts` 和 `netshare_en.ts` 中添加主题相关翻译
  - "Theme" → "主题"
  - "Dark" → "暗色"
  - "Light" → "亮色"
- **难易程度**：低
- **完成状态**：完成
- **验证方式**：切换语言后主题标签显示正确

### 步骤9：deploy.bat 添加主题文件复制

- **修改内容**：
  - 在 `deploy.bat` 中添加复制 `themes/` 目录到 `dist/netshare/themes/` 的步骤
  - 添加验证主题文件是否存在的检查
- **难易程度**：低
- **完成状态**：完成
- **验证方式**：部署目录包含主题文件

### 步骤10：编译验证

- **修改内容**：编译项目，确认无错误和警告
- **难易程度**：低
- **完成状态**：完成
- **验证方式**：编译成功，运行后暗色/亮色主题切换正常，所有页面颜色正确

---

## 4. 亮色主题颜色设计参考

亮色主题颜色参考 VS Code Light+ 和 Windows 11 浅色模式：

| 属性 | 暗色值 | 亮色值 | 说明 |
|------|--------|--------|------|
| backgroundColor | #202020 | #f5f5f5 | 页面背景 |
| surfaceColor | #2d2d30 | #ffffff | 卡片/输入框背景 |
| sidebarColor | #2b2b2d | #e8e8e8 | 侧边栏背景 |
| accentColor | #007acc | #0078d4 | 强调色（Windows蓝） |
| textColor | #ffffff | #1a1a1a | 主文字 |
| textSecondary | #cccccc | #666666 | 次要文字 |
| borderColor | #3e3e42 | #d0d0d0 | 边框 |
| successColor | #4caf50 | #107c10 | 成功色 |
| warningColor | #ff9800 | #d48c00 | 警告色 |
| errorColor | #f44336 | #d13438 | 错误色 |
| textOnAccentColor | #ffffff | #ffffff | 强调色上的文字（保持白色） |
| hoverColor | #3e3e42 | #e5e5e5 | 悬停背景 |
| itemHoverColor | #333336 | #ebebeb | 列表项悬停 |
| alternateRowColor | #252528 | #f0f0f0 | 交替行 |
| switchTrackColor | #555555 | #b0b0b0 | Switch未选中轨道 |
| accentPressedColor | #005a9e | #005a9e | 按钮按下（保持深蓝） |
| closeHoverColor | #e81123 | #e81123 | 关闭按钮悬停（保持红色） |
| warningBgColor | #3e2a1a | #fff4ce | TLS警告背景 |
| successBgColor | #1a3e2a | #dff6dd | TLS安全背景 |
| switchThumbColor | #ffffff | #ffffff | Switch滑块（保持白色） |
| qrCodeBgColor | #ffffff | #ffffff | QR码背景（保持白色，扫描需要） |
| textOnAccentSecondaryColor | #cccccc | #c0d8ec | 强调色上的次要文字 |
| overlayColor | #80000000 | #80000000 | 半透明遮罩（暗亮相同） |

---

## 5. 冲突、遗漏、缺失、错误检查结果

### 5.1 原计划错误

| 编号 | 错误描述 | 修正方案 |
|------|----------|----------|
| E1 | 硬编码颜色统计不准确：原计划73处，表格加总仅56处，实际扫描为68处。主要差异：#3e3e42计划17处实际27处，#ffffff textOnAccentColor计划20处实际24处 | 更新为68处，修正各颜色出现次数，补充逐文件明细 |
| E2 | 所有 `#ffffff` 统一映射为 `textOnAccentColor`，但 Switch 滑块和 QR 码背景的 `#ffffff` 语义不同 | 新增 `switchThumbColor`、`qrCodeBgColor` 属性 |
| E3 | SVG 图标方案使用 `Icon` 组件，但 Qt 6 中不存在 `Icon` QML 类型 | 改用 `Image` + `MultiEffect` 方案 |
| E4 | 未覆盖 `#80000000` 半透明遮罩颜色 | 新增 `overlayColor` 属性，支持 ARGB 格式 |
| E5 | 未覆盖 `#cccccc` 在强调色背景上的次要文字 | 新增 `textOnAccentSecondaryColor` 属性 |

### 5.2 原计划遗漏

| 编号 | 遗漏描述 | 补充方案 |
|------|----------|----------|
| M1 | SettingsPage 的 `restartHintDialog` 无自定义 background | 步骤5a：添加 background |
| M2 | 所有 TextField 缺少 `placeholderTextColor` | 步骤5b：添加 placeholderTextColor |
| M3 | 所有 TextField 缺少 `selectionColor`/`selectedTextColor` | 步骤5b：添加选中颜色 |
| M4 | ScrollBar 无自定义样式 | 步骤5c：添加自定义 contentItem |
| M5 | ToolTip 无自定义样式 | 步骤5d：添加全局 ToolTip palette |
| M6 | Overlay.modal 无自定义遮罩颜色 | 步骤5e：添加 Overlay.modal |
| M7 | deploy.bat 缺少主题文件复制步骤 | 步骤9：添加 themes 复制 |
| M8 | CMakeLists.txt 缺少主题 JSON 资源注册 | 步骤1：添加 RESOURCES |
| M9 | main.cpp 缺少主题初始化代码 | 步骤7：添加主题加载 |
| M10 | SVG 图标适配需要 `QtQuick.Effects` 模块依赖 | 步骤4：添加 CMake 模块依赖 |
| M11 | QMenu（托盘菜单）无法适配应用主题 | 已知限制，记录在文档中 |
| M12 | Dialog 边框颜色不一致：现有5个Dialog使用 `Theme.sidebarColor` 作为 border.color，但步骤5a建议使用 `Theme.borderColor` | 统一为 `Theme.borderColor`，在步骤5a中一并修改所有Dialog的border.color |
| M13 | 步骤5a仅提到 `restartHintDialog` 需添加background，但应同时统一所有Dialog的border.color | 扩展步骤5a范围，修改所有6个Dialog的border.color为 `Theme.borderColor` |

### 5.3 颜色属性完整性验证

| 属性 | dark.json | light.json | QML 引用 | 状态 |
|------|-----------|------------|----------|------|
| backgroundColor | ✓ | ✓ | ✓ | 完整 |
| surfaceColor | ✓ | ✓ | ✓ | 完整 |
| sidebarColor | ✓ | ✓ | ✓ | 完整 |
| accentColor | ✓ | ✓ | ✓ | 完整 |
| textColor | ✓ | ✓ | ✓ | 完整 |
| textSecondary | ✓ | ✓ | ✓ | 完整 |
| borderColor | ✓ | ✓ | ✓ | 完整 |
| successColor | ✓ | ✓ | ✓ | 完整 |
| warningColor | ✓ | ✓ | ✓ | 完整 |
| errorColor | ✓ | ✓ | ✓ | 完整 |
| textOnAccentColor | ✓ | ✓ | 24处引用 | 完整 |
| hoverColor | ✓ | ✓ | 27处引用 | 完整 |
| itemHoverColor | ✓ | ✓ | 3处引用 | 完整 |
| alternateRowColor | ✓ | ✓ | 1处引用 | 完整 |
| switchTrackColor | ✓ | ✓ | 1处引用 | 完整 |
| accentPressedColor | ✓ | ✓ | 1处引用 | 完整 |
| closeHoverColor | ✓ | ✓ | 1处引用 | 完整 |
| warningBgColor | ✓ | ✓ | 1处引用 | 完整 |
| successBgColor | ✓ | ✓ | 1处引用 | 完整 |
| switchThumbColor | ✓ **[新增]** | ✓ **[新增]** | 1处引用 | 完整 |
| qrCodeBgColor | ✓ **[新增]** | ✓ **[新增]** | 3处引用 | 完整 |
| textOnAccentSecondaryColor | ✓ **[新增]** | ✓ **[新增]** | 2处引用 | 完整 |
| overlayColor | ✓ **[新增]** | ✓ **[新增]** | 2处引用 | 完整 |

**总计：23个颜色属性，覆盖全部68处硬编码颜色值。**

---

## 6. 扩展性设计

### 6.1 添加新主题（需编译）

1. 创建 `src/gui/themes/ocean.json`
2. 在 `src/gui/CMakeLists.txt` 的 `RESOURCES` 中添加 `qml/themes/ocean.json`
3. 编译

### 6.2 添加新主题（运行时，无需编译）

1. 将 JSON 文件放到 `<exe_dir>/themes/` 目录
2. ThemeProvider 自动扫描并加载
3. 设置页自动显示新主题选项

### 6.3 ThemeProvider 主题加载优先级

```
1. QRC 内置主题（:/themes/dark.json, :/themes/light.json）
2. 外部主题文件（<exe_dir>/themes/*.json）
3. 内置默认暗色主题（代码中的硬编码回退值）
```

### 6.4 主题文件校验

加载外部主题文件时进行校验：
- JSON 格式正确
- 包含 `id`、`name`、`colors` 字段
- `colors` 包含所有23个必需的颜色键
- 颜色值为合法的 `#RRGGBB` 或 `#AARRGGBB` 格式

校验失败的主题跳过并输出 WARN 日志，不影响其他主题。

### 6.5 已知限制

| 限制 | 原因 | 解决方案 |
|------|------|----------|
| QMenu（托盘菜单）无法适配应用主题 | QMenu 是 Qt Widgets 组件，不受 QML 主题控制 | 跟随系统主题，或改用 QML Menu |
| FileDialog/FolderDialog 无法适配 | 系统原生对话框 | 跟随系统主题 |
| 字体大小未纳入主题配置 | 180处硬编码 font.pixelSize，变化不大 | 后续可扩展，当前不影响配色 |
| 圆角大小未纳入主题配置 | 93处硬编码 radius，变化不大 | 后续可扩展，当前不影响配色 |
