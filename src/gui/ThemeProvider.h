#pragma once

#include <QObject>
#include <QColor>
#include <QMap>
#include <QSettings>
#include <QtQml/qqml.h>

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
    static ThemeProvider* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
    {
        Q_UNUSED(qmlEngine)
        Q_UNUSED(jsEngine)
        static ThemeProvider instance;
        return &instance;
    }

    QString currentTheme() const { return m_currentTheme; }

    void setCurrentTheme(const QString& themeId)
    {
        if (m_currentTheme == themeId) return;
        applyTheme(themeId);
    }

    QStringList availableThemes() const
    {
        return m_themes.keys();
    }

    Q_INVOKABLE void switchTheme(const QString& themeId)
    {
        applyTheme(themeId);
        QSettings settings;
        settings.setValue("General/Theme", themeId);
    }

    QColor backgroundColor() const { return m_activeData.colors.value("backgroundColor", QColor("#202020")); }
    QColor surfaceColor() const { return m_activeData.colors.value("surfaceColor", QColor("#2d2d30")); }
    QColor sidebarColor() const { return m_activeData.colors.value("sidebarColor", QColor("#2b2b2d")); }
    QColor accentColor() const { return m_activeData.colors.value("accentColor", QColor("#007acc")); }
    QColor textColor() const { return m_activeData.colors.value("textColor", QColor("#ffffff")); }
    QColor textSecondary() const { return m_activeData.colors.value("textSecondary", QColor("#cccccc")); }
    QColor borderColor() const { return m_activeData.colors.value("borderColor", QColor("#3e3e42")); }
    QColor successColor() const { return m_activeData.colors.value("successColor", QColor("#4caf50")); }
    QColor warningColor() const { return m_activeData.colors.value("warningColor", QColor("#ff9800")); }
    QColor errorColor() const { return m_activeData.colors.value("errorColor", QColor("#f44336")); }
    QColor textOnAccentColor() const { return m_activeData.colors.value("textOnAccentColor", QColor("#ffffff")); }
    QColor hoverColor() const { return m_activeData.colors.value("hoverColor", QColor("#3e3e42")); }
    QColor itemHoverColor() const { return m_activeData.colors.value("itemHoverColor", QColor("#333336")); }
    QColor alternateRowColor() const { return m_activeData.colors.value("alternateRowColor", QColor("#252528")); }
    QColor switchTrackColor() const { return m_activeData.colors.value("switchTrackColor", QColor("#555555")); }
    QColor accentPressedColor() const { return m_activeData.colors.value("accentPressedColor", QColor("#005a9e")); }
    QColor closeHoverColor() const { return m_activeData.colors.value("closeHoverColor", QColor("#e81123")); }
    QColor warningBgColor() const { return m_activeData.colors.value("warningBgColor", QColor("#3e2a1a")); }
    QColor successBgColor() const { return m_activeData.colors.value("successBgColor", QColor("#1a3e2a")); }
    QColor switchThumbColor() const { return m_activeData.colors.value("switchThumbColor", QColor("#ffffff")); }
    QColor qrCodeBgColor() const { return m_activeData.colors.value("qrCodeBgColor", QColor("#ffffff")); }
    QColor textOnAccentSecondaryColor() const { return m_activeData.colors.value("textOnAccentSecondaryColor", QColor("#cccccc")); }
    QColor overlayColor() const { return m_activeData.colors.value("overlayColor", QColor(128, 0, 0, 0)); }

    int fontSizeLarge() const { return m_activeData.fontSizes.value("large", 24); }
    int fontSizeMedium() const { return m_activeData.fontSizes.value("medium", 16); }
    int fontSizeSmall() const { return m_activeData.fontSizes.value("small", 12); }
    int spacing() const { return m_activeData.metrics.value("spacing", 8); }
    int padding() const { return m_activeData.metrics.value("padding", 16); }
    int radius() const { return m_activeData.metrics.value("radius", 4); }

    Q_INVOKABLE QString themeName(const QString& themeId) const
    {
        if (m_themes.contains(themeId)) return m_themes[themeId].name;
        return themeId;
    }

signals:
    void themeChanged();

private:
    ThemeProvider()
    {
        loadBuiltinThemes();
        QSettings settings;
        QString saved = settings.value("General/Theme", "dark").toString();
        applyTheme(saved);
    }

    ~ThemeProvider() = default;
    ThemeProvider(const ThemeProvider&) = delete;
    ThemeProvider& operator=(const ThemeProvider&) = delete;

    struct ThemeData {
        QString id;
        QString name;
        QMap<QString, QColor> colors;
        QMap<QString, int> fontSizes;
        QMap<QString, int> metrics;
    };

    void loadBuiltinThemes();
    bool loadThemeFromJson(const QString& jsonData);
    void applyTheme(const QString& themeId);
    QColor parseColor(const QString& colorStr) const;

    QMap<QString, ThemeData> m_themes;
    QString m_currentTheme;
    ThemeData m_activeData;
};
