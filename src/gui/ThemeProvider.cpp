#include "ThemeProvider.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QGuiApplication>
#include <QDebug>

QColor ThemeProvider::parseColor(const QString& colorStr) const
{
    QString c = colorStr.trimmed();
    if (c.startsWith("#") && c.length() == 9) {
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
    QStringList builtinThemes = {"dark", "light"};
    for (const auto& id : builtinThemes) {
        QString qrcPath = QString(":/qt/qml/NetShare/qml/themes/%1.json").arg(id);
        QFile f(qrcPath);
        if (f.open(QIODevice::ReadOnly)) {
            if (loadThemeFromJson(QString::fromUtf8(f.readAll()))) {
                qDebug() << "Theme loaded from QRC:" << id;
            } else {
                qWarning() << "Failed to parse theme from QRC:" << id;
            }
        } else {
            qWarning() << "Theme file not found in QRC:" << qrcPath;
        }
    }

    QDir themesDir(QGuiApplication::applicationDirPath() + "/themes");
    if (themesDir.exists()) {
        for (const auto& entry : themesDir.entryList({"*.json"})) {
            QFile f(themesDir.filePath(entry));
            if (f.open(QIODevice::ReadOnly)) {
                if (loadThemeFromJson(QString::fromUtf8(f.readAll()))) {
                    qDebug() << "Theme loaded from external:" << entry;
                } else {
                    qWarning() << "Failed to parse external theme:" << entry;
                }
            }
        }
    }
}

bool ThemeProvider::loadThemeFromJson(const QString& jsonData)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "Theme JSON parse error:" << err.errorString();
        return false;
    }

    QJsonObject root = doc.object();
    ThemeData data;
    data.id = root["id"].toString();
    data.name = root["name"].toString();

    if (data.id.isEmpty()) return false;

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
    if (!m_themes.contains(themeId)) {
        qWarning() << "Theme not found:" << themeId << ", falling back to dark";
        if (!m_themes.contains("dark")) return;
        m_currentTheme = "dark";
        m_activeData = m_themes["dark"];
    } else {
        m_currentTheme = themeId;
        m_activeData = m_themes[themeId];
    }
    emit themeChanged();
}
