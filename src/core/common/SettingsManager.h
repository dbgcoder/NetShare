#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QObject>
#include <QVariant>
#include <QSettings>
#include <QString>
#include <QtQml/qqml.h>

class SettingsManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit SettingsManager(QObject* parent = nullptr);
    ~SettingsManager() override;

    Q_INVOKABLE QVariant value(const QString& key, const QVariant& defaultValue = QVariant()) const;
    Q_INVOKABLE void setValue(const QString& key, const QVariant& value);

    bool load(const QString& filePath);
    bool save(const QString& filePath);
    Q_INVOKABLE void sync();

    Q_INVOKABLE QStringList keys() const;
    Q_INVOKABLE void clear();

    Q_INVOKABLE QString getString(const QString& key, const QString& defaultValue = QString()) const;
    Q_INVOKABLE int getInt(const QString& key, int defaultValue = 0) const;
    Q_INVOKABLE bool getBool(const QString& key, bool defaultValue = false) const;

    Q_INVOKABLE QString getDefaultUploadPath() const;
    Q_INVOKABLE QString getUploadPath() const;
    Q_INVOKABLE void setUploadPath(const QString& path);

    Q_INVOKABLE bool isAutoStartEnabled() const;
    Q_INVOKABLE void setAutoStartEnabled(bool enabled);

private:
    QSettings* m_settings;
};

#endif
