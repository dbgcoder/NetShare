#ifndef NOTIFICATIONMANAGER_H
#define NOTIFICATIONMANAGER_H

#include <QObject>
#include <QSystemTrayIcon>

class NotificationManager : public QObject
{
    Q_OBJECT

public:
    enum Priority { Low, Normal, High, Critical };
    Q_ENUM(Priority)

    explicit NotificationManager(QSystemTrayIcon* trayIcon, QObject* parent = nullptr);
    ~NotificationManager() override;

    Q_INVOKABLE void notify(const QString& title, const QString& message,
                            int priority = Normal, int timeoutMs = 5000);
    Q_INVOKABLE void notifyDownloadComplete(const QString& fileName, const QString& savePath);
    Q_INVOKABLE void notifyUploadComplete(const QString& fileName);
    Q_INVOKABLE void notifyShareCreated(const QString& token, const QString& filePath);
    Q_INVOKABLE void notifyShareAccessed(const QString& token, const QString& address);
    Q_INVOKABLE void notifyError(const QString& title, const QString& error);
    Q_INVOKABLE void notifyTransferProgress(const QString& fileName, int progress, int speed);

    Q_INVOKABLE void setEnabled(bool enabled);
    Q_INVOKABLE bool isEnabled() const;

    Q_INVOKABLE void setSilentMode(bool silent);
    Q_INVOKABLE bool isSilentMode() const;

signals:
    void notificationClicked(const QString& action, const QString& data);
    void notificationShown(const QString& title, const QString& message);

private slots:
    void onMessageClicked();

private:
    QSystemTrayIcon* m_trayIcon;
    bool m_enabled;
    bool m_silentMode;
    QString m_lastAction;
    QString m_lastData;
};

#endif
