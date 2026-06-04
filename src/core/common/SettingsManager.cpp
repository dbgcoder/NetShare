#include "SettingsManager.h"
#include <QDir>
#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <QSettings>
#endif

SettingsManager::SettingsManager(QObject* parent)
    : QObject(parent)
    , m_settings(new QSettings(QSettings::IniFormat, QSettings::UserScope, "NetShare", "config", this))
{
}

SettingsManager::~SettingsManager() = default;

QVariant SettingsManager::value(const QString& key, const QVariant& defaultValue) const
{
    return m_settings->value(key, defaultValue);
}

void SettingsManager::setValue(const QString& key, const QVariant& value)
{
    m_settings->setValue(key, value);
}

bool SettingsManager::load(const QString& filePath)
{
    if (filePath.isEmpty()) {
        return false;
    }

    QSettings* newSettings = new QSettings(filePath, QSettings::IniFormat);
    if (newSettings->status() != QSettings::NoError) {
        delete newSettings;
        return false;
    }

    QSettings* oldSettings = m_settings;
    m_settings = newSettings;
    delete oldSettings;

    return true;
}

bool SettingsManager::save(const QString& filePath)
{
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        return false;
    }

    if (!filePath.isEmpty()) {
        return load(filePath);
    }
    return true;
}

void SettingsManager::sync()
{
    m_settings->sync();
}

QStringList SettingsManager::keys() const
{
    return m_settings->allKeys();
}

void SettingsManager::clear()
{
    m_settings->clear();
}

QString SettingsManager::getString(const QString& key, const QString& defaultValue) const
{
    return m_settings->value(key, defaultValue).toString();
}

int SettingsManager::getInt(const QString& key, int defaultValue) const
{
    return m_settings->value(key, defaultValue).toInt();
}

bool SettingsManager::getBool(const QString& key, bool defaultValue) const
{
    return m_settings->value(key, defaultValue).toBool();
}

QString SettingsManager::getDefaultUploadPath() const
{
    return QDir::homePath() + "/NetShare/Uploads";
}

QString SettingsManager::getUploadPath() const
{
    QString custom = m_settings->value("Paths/UploadDir").toString();
    return custom.isEmpty() ? getDefaultUploadPath() : custom;
}

void SettingsManager::setUploadPath(const QString& path)
{
    m_settings->setValue("Paths/UploadDir", path);
}

bool SettingsManager::isAutoStartEnabled() const
{
#ifdef Q_OS_WIN
    QSettings reg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                  QSettings::NativeFormat);
    return reg.value("NetShare").toString() ==
           QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
#else
    return false;
#endif
}

void SettingsManager::setAutoStartEnabled(bool enabled)
{
#ifdef Q_OS_WIN
    QSettings reg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                  QSettings::NativeFormat);
    if (enabled) {
        reg.setValue("NetShare", QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
    } else {
        reg.remove("NetShare");
    }
#else
    Q_UNUSED(enabled)
#endif
}
