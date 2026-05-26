#include "NotificationManager.h"
#include "Logger.h"

NotificationManager::NotificationManager(QSystemTrayIcon* trayIcon, QObject* parent)
    : QObject(parent)
    , m_trayIcon(trayIcon)
    , m_enabled(true)
    , m_silentMode(false)
{
    if (m_trayIcon) {
        connect(m_trayIcon, &QSystemTrayIcon::messageClicked,
                this, &NotificationManager::onMessageClicked);
    }
}

NotificationManager::~NotificationManager() = default;

void NotificationManager::notify(const QString& title, const QString& message,
                                   int priority, int timeoutMs)
{
    if (!m_enabled || m_silentMode) return;

    QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information;
    switch (priority) {
    case High:
    case Critical:
        icon = QSystemTrayIcon::Critical;
        break;
    case Normal:
        icon = QSystemTrayIcon::Information;
        break;
    case Low:
        icon = QSystemTrayIcon::NoIcon;
        break;
    }

    if (m_trayIcon && m_trayIcon->isVisible()) {
        m_trayIcon->showMessage(title, message, icon, timeoutMs);
    }

    LOG_INFO("Notification: [%s] %s - %s",
             priority == Critical ? "CRITICAL" : priority == High ? "HIGH" : "INFO",
             qPrintable(title), qPrintable(message));

    emit notificationShown(title, message);
}

void NotificationManager::notifyDownloadComplete(const QString& fileName, const QString& savePath)
{
    m_lastAction = "openFolder";
    m_lastData = savePath;
    notify("下载完成", QString("文件 %1 已下载完成").arg(fileName), Normal);
}

void NotificationManager::notifyUploadComplete(const QString& fileName)
{
    m_lastAction = "uploadComplete";
    m_lastData = fileName;
    notify("上传完成", QString("文件 %1 已上传完成").arg(fileName), Normal);
}

void NotificationManager::notifyShareCreated(const QString& token, const QString& filePath)
{
    m_lastAction = "shareCreated";
    m_lastData = token;
    QFileInfo fi(filePath);
    notify("分享已创建", QString("已分享: %1").arg(fi.fileName()), Normal);
}

void NotificationManager::notifyShareAccessed(const QString& token, const QString& address)
{
    m_lastAction = "shareAccessed";
    m_lastData = token;
    notify("分享被访问", QString("来自 %1 的访问").arg(address), Low);
}

void NotificationManager::notifyError(const QString& title, const QString& error)
{
    m_lastAction = "error";
    m_lastData = error;
    notify(title, error, Critical);
}

void NotificationManager::notifyTransferProgress(const QString& fileName, int progress, int speed)
{
    Q_UNUSED(fileName)
    Q_UNUSED(progress)
    Q_UNUSED(speed)
}

void NotificationManager::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

bool NotificationManager::isEnabled() const
{
    return m_enabled;
}

void NotificationManager::setSilentMode(bool silent)
{
    m_silentMode = silent;
}

bool NotificationManager::isSilentMode() const
{
    return m_silentMode;
}

void NotificationManager::onMessageClicked()
{
    if (!m_lastAction.isEmpty()) {
        emit notificationClicked(m_lastAction, m_lastData);
    }
}
