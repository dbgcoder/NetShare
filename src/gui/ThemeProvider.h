#pragma once

#include <QObject>
#include <QColor>
#include <QtQml/qqml.h>

class ThemeProvider : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Theme)
    QML_SINGLETON

    Q_PROPERTY(QColor backgroundColor READ backgroundColor CONSTANT)
    Q_PROPERTY(QColor surfaceColor READ surfaceColor CONSTANT)
    Q_PROPERTY(QColor sidebarColor READ sidebarColor CONSTANT)
    Q_PROPERTY(QColor accentColor READ accentColor CONSTANT)
    Q_PROPERTY(QColor textColor READ textColor CONSTANT)
    Q_PROPERTY(QColor textSecondary READ textSecondary CONSTANT)
    Q_PROPERTY(QColor borderColor READ borderColor CONSTANT)
    Q_PROPERTY(QColor successColor READ successColor CONSTANT)
    Q_PROPERTY(QColor warningColor READ warningColor CONSTANT)
    Q_PROPERTY(QColor errorColor READ errorColor CONSTANT)
    Q_PROPERTY(int fontSizeLarge READ fontSizeLarge CONSTANT)
    Q_PROPERTY(int fontSizeMedium READ fontSizeMedium CONSTANT)
    Q_PROPERTY(int fontSizeSmall READ fontSizeSmall CONSTANT)
    Q_PROPERTY(int spacing READ spacing CONSTANT)
    Q_PROPERTY(int padding READ padding CONSTANT)
    Q_PROPERTY(int radius READ radius CONSTANT)

public:
    static ThemeProvider* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine)
    {
        Q_UNUSED(qmlEngine)
        Q_UNUSED(jsEngine)
        static ThemeProvider instance;
        return &instance;
    }

    QColor backgroundColor() const { return QColor("#202020"); }
    QColor surfaceColor() const { return QColor("#2d2d30"); }
    QColor sidebarColor() const { return QColor("#2b2b2d"); }
    QColor accentColor() const { return QColor("#007acc"); }
    QColor textColor() const { return QColor("#ffffff"); }
    QColor textSecondary() const { return QColor("#cccccc"); }
    QColor borderColor() const { return QColor("#3e3e42"); }
    QColor successColor() const { return QColor("#4caf50"); }
    QColor warningColor() const { return QColor("#ff9800"); }
    QColor errorColor() const { return QColor("#f44336"); }

    int fontSizeLarge() const { return 24; }
    int fontSizeMedium() const { return 16; }
    int fontSizeSmall() const { return 12; }
    int spacing() const { return 8; }
    int padding() const { return 16; }
    int radius() const { return 4; }

private:
    ThemeProvider() = default;
    ~ThemeProvider() = default;
    ThemeProvider(const ThemeProvider&) = delete;
    ThemeProvider& operator=(const ThemeProvider&) = delete;
};
