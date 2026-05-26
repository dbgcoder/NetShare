#include <QCoreApplication>
#include <QApplication>
#include <QStyle>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QIcon>
#include <QWindow>
#include <QQuickWindow>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QSplashScreen>
#include <QTimer>
#include <QDebug>
#include <QFile>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslConfiguration>

#include "core/common/Logger.h"
#include "core/common/SettingsManager.h"
#include "core/common/ServiceLocator.h"
#include "NetShareVersion.h"
#include "core/share/ShareManager.h"
#include "core/transfer/FileTransferEngine.h"
#include "core/share/FileBrowser.h"
#include "core/share/FolderPacker.h"
#include "core/transfer/ChunkManager.h"
#include "core/transfer/ResumeManager.h"
#include "core/transfer/BandwidthManager.h"
#include "core/transfer/TransferLogService.h"
#include "core/notification/NotificationManager.h"
#include "database/DatabaseManager.h"
#include "network/HttpServer.h"
#include "network/mDNSService.h"
#include "network/RequestHandler.h"
#include "network/WebSocketHandler.h"
#ifdef Q_OS_WIN
#include <dwmapi.h>
#endif

#include "gui/QRCodeHelper.h"

static void setupHighDpiSupport()
{
    // Use Basic style for full control customization on dark theme
    QQuickStyle::setStyle("Basic");
}

#ifdef Q_OS_WIN
static void configureWindowsFirewall(quint16 port)
{
    QString ruleName = "NetShare HTTP Server";

    // Use PowerShell to check if the firewall rule already exists
    int checkRet = QProcess::execute("powershell", {
        "-NoProfile", "-Command",
        QString("if (Get-NetFirewallRule -DisplayName '%1' -ErrorAction SilentlyContinue) { exit 0 } else { exit 1 }").arg(ruleName)
    });

    if (checkRet == 0) {
        LOG_INFO("Windows Firewall rule already exists for '%s'", qPrintable(ruleName));
        return;
    }

    // Rule does not exist, add new rule
    int ret = QProcess::execute("netsh", {
        "advfirewall", "firewall", "add", "rule",
        QString("name=%1").arg(ruleName),
        "dir=in", "action=allow", "protocol=TCP",
        QString("localport=%1").arg(port)
    });

    if (ret == 0) {
        LOG_INFO("Windows Firewall rule added for port %d", port);
    } else {
        LOG_WARN("Failed to add Windows Firewall rule for port %d (need admin). "
                 "Please manually allow NetShare through Windows Firewall.", port);
    }
}
#endif

class NetShareApplication : public QObject
{
    Q_OBJECT

public:
    explicit NetShareApplication(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~NetShareApplication()
    {
        shutdown();
    }

    bool initialize()
    {
        LOG_INFO("NetShare application starting...");

        if (!initializeLogger()) {
            return false;
        }

        if (!initializeDatabase()) {
            LOG_ERROR("Failed to initialize database");
            return false;
        }

        if (!initializeSettings()) {
            LOG_ERROR("Failed to initialize settings");
            return false;
        }

        if (!initializeCoreServices()) {
            LOG_ERROR("Failed to initialize core services");
            return false;
        }

        if (!initializeNetworkServer()) {
            LOG_ERROR("Failed to initialize network server");
            return false;
        }

        if (!initializeTrayIcon()) {
            LOG_ERROR("Failed to initialize system tray");
            return false;
        }

        auto* tray = m_locator.service<QSystemTrayIcon>();
        m_notificationManager = new NotificationManager(tray, this);
        m_locator.registerService(m_notificationManager);
        LOG_INFO("NotificationManager initialized");

        if (!initializeQml()) {
            LOG_WARN("Failed to initialize QML, running without GUI");
        }

        LOG_INFO("NetShare application initialized successfully");
        return true;
    }

    void run()
    {
        LOG_INFO("Starting NetShare main event loop");
    }

    void shutdown()
    {
        LOG_INFO("Shutting down NetShare application...");

        // Stop network services first
        auto* mdns = m_locator.service<mDNSService>();
        if (mdns) { mdns->unregisterService(); }

        auto* ws = m_locator.service<WebSocketHandler>();
        if (ws) { ws->stop(); }

        auto* http = m_locator.service<HttpServer>();
        if (http) { http->stop(); }

        // Stop transfer engine
        auto* engine = m_locator.service<FileTransferEngine>();
        if (engine) { engine->stopAllTasks(); }

        auto* bw = m_locator.service<BandwidthManager>();
        if (bw) { bw->stopMonitoring(); }

        // Sync settings
        auto* settings = m_locator.service<SettingsManager>();
        if (settings) { settings->sync(); }

        // Close database
        auto* db = m_locator.service<DatabaseManager>();
        if (db) { db->close(); }

        // Singletons
        ShareManager::shutdown();

        m_locator.clear();

        // Delete QML engine and tray
        delete m_notificationManager; m_notificationManager = nullptr;
        delete m_trayIcon; m_trayIcon = nullptr;
        delete m_engine; m_engine = nullptr;

        Logger::shutdown();
        LOG_INFO("NetShare application shutdown complete");
    }

private:
    bool initializeLogger()
    {
        QString logPath = getLogDirectory();
        if (!Logger::initialize(logPath, Logger::Info)) {
            qWarning() << "Failed to initialize logger, using console only";
            return false;
        }
        LOG_INFO("Logger initialized successfully");
        return true;
    }

    bool initializeDatabase()
    {
        QString dbPath = getDatabaseDirectory() + "/netshare.db";
        auto* db = new DatabaseManager(this);
        if (!db->open(dbPath)) {
            LOG_ERROR("Failed to open database: %s", qPrintable(dbPath));
            return false;
        }
        if (!db->initialize()) {
            LOG_ERROR("Failed to initialize database schema");
            return false;
        }
        m_locator.registerService(db);
        LOG_INFO("Database initialized successfully");
        return true;
    }

    bool initializeSettings()
    {
        // Migrate old config from AppLocalDataLocation to AppConfigLocation
        QString oldConfigDir = QDir::toNativeSeparators(
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
        QString oldConfigPath = oldConfigDir + "/config.json";
        QString configDir = getConfigDirectory();
        QString configPath = configDir + "/config.json";
        if (QFileInfo::exists(oldConfigPath) && !QFileInfo::exists(configPath)) {
            QDir().mkpath(configDir);
            if (QFile::copy(oldConfigPath, configPath)) {
                LOG_INFO("Migrated config from old path to new path");
            } else {
                LOG_WARN("Failed to migrate config from old path");
            }
        }

        auto* settings = new SettingsManager(this);
        if (!settings->load(configPath)) {
            LOG_WARN("Failed to load settings, using defaults");
        }
        m_locator.registerService(settings);
        LOG_INFO("Settings loaded successfully");
        return true;
    }

    bool initializeCoreServices()
    {
        auto* db = m_locator.service<DatabaseManager>();

        auto* shareManager = &ShareManager::instance();
        shareManager->setDatabase(db);
        m_locator.registerService<IShareManager>(shareManager);
        m_locator.registerService(shareManager);
        LOG_INFO("ShareManager initialized");

        auto* transferEngine = new FileTransferEngine(this);

        auto* transferLogService = new TransferLogService(this);
        transferLogService->setDatabase(db);
        m_locator.registerService(transferLogService);
        LOG_INFO("TransferLogService initialized");

        // Inject log service before initialize() so paused tasks can be restored
        transferEngine->setTransferLogService(transferLogService);

        if (!transferEngine->initialize()) {
            LOG_ERROR("Failed to initialize FileTransferEngine");
            return false;
        }
        m_locator.registerService(transferEngine);
        LOG_INFO("FileTransferEngine initialized");

        auto* fileBrowser = new FileBrowser(this);
        m_locator.registerService<IFileBrowser>(fileBrowser);
        m_locator.registerService(fileBrowser);
        LOG_INFO("FileBrowser initialized");

        auto* folderPacker = new FolderPacker(this);
        m_locator.registerService<IFolderPacker>(folderPacker);
        m_locator.registerService(folderPacker);
        LOG_INFO("FolderPacker initialized");

        auto* chunkManager = new ChunkManager(this);
        m_locator.registerService(chunkManager);
        LOG_INFO("ChunkManager initialized");

        auto* resumeManager = new ResumeManager(this);
        m_locator.registerService(resumeManager);
        LOG_INFO("ResumeManager initialized");

        auto* bandwidthManager = new BandwidthManager(this);
        bandwidthManager->startMonitoring();
        m_locator.registerService(bandwidthManager);
        LOG_INFO("BandwidthManager initialized");

        // Inject managers into the transfer engine
        transferEngine->setManagers(shareManager, chunkManager, resumeManager, bandwidthManager);

        // Connect task failure notification
        if (m_notificationManager) {
            connect(transferEngine, &FileTransferEngine::taskFailed,
                    this, [this](const QString& taskId, const QString& error) {
                Q_UNUSED(taskId)
                m_notificationManager->notifyError("传输失败", error);
            });
        }

        return true;
    }

    bool initializeNetworkServer()
    {
        auto* settings = m_locator.service<SettingsManager>();
        auto* shareManager = m_locator.service<IShareManager>();
        auto* fileBrowser = m_locator.service<IFileBrowser>();
        auto* folderPacker = m_locator.service<IFolderPacker>();

        auto* httpServer = new HttpServer(this);
        m_locator.registerService(httpServer);

        auto* requestHandler = new RequestHandler(shareManager, fileBrowser, folderPacker, this);
        QString uploadDir = settings->getUploadPath();
        QDir().mkpath(uploadDir);
        requestHandler->setUploadDir(uploadDir);
        requestHandler->setSettingsManager(settings);

        auto* transferEngine = m_locator.service<FileTransferEngine>();
        auto* transferLogService = m_locator.service<TransferLogService>();
        if (transferEngine) {
            requestHandler->setTransferEngine(transferEngine);
        }
        if (transferLogService) {
            requestHandler->setTransferLogService(transferLogService);
        }

        requestHandler->registerRoutes(httpServer);

        quint16 port = settings->value("Network/Port", 8080).toUInt();
        QString bindAddress = settings->value("Network/BindAddress", "0.0.0.0").toString();

        auto* wsHandler = new WebSocketHandler(this);

        // Configure TLS before starting servers
        if (settings->getBool("server/tlsEnabled", false)) {
            LOG_INFO("TLS is enabled, configuring certificates...");
            QString certPath = settings->getString("server/tlsCertPath");
            QString keyPath = settings->getString("server/tlsKeyPath");

            if (certPath.isEmpty() || keyPath.isEmpty()) {
                LOG_WARN("TLS certificate or key path not configured, falling back to non-secure");
            } else {
                QFile certFile(certPath);
                QFile keyFile(keyPath);
                if (!certFile.open(QIODevice::ReadOnly)) {
                    LOG_ERROR("Failed to open TLS certificate: %s", qPrintable(certPath));
                } else if (!keyFile.open(QIODevice::ReadOnly)) {
                    LOG_ERROR("Failed to open TLS key: %s", qPrintable(keyPath));
                } else {
                    QSslCertificate cert(certFile.readAll(), QSsl::Pem);
                    QSslKey key(keyFile.readAll(), QSsl::Rsa, QSsl::Pem);

                    if (cert.isNull() || key.isNull()) {
                        LOG_ERROR("Invalid TLS certificate or key");
                    } else {
                        QSslConfiguration sslConfig;
                        sslConfig.setLocalCertificate(cert);
                        sslConfig.setPrivateKey(key);
                        sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);

                        httpServer->setSslConfiguration(sslConfig);
                        httpServer->setTlsEnabled(true);
                        wsHandler->setSslConfiguration(sslConfig);
                        wsHandler->setTlsEnabled(true);

                        LOG_INFO("TLS configured successfully");
                    }
                }
            }
        }

        if (!httpServer->start(port, bindAddress)) {
            LOG_ERROR("Failed to start HTTP server on port %d", port);
            return false;
        }
        LOG_INFO("HTTP server started on %s:%d", qPrintable(bindAddress), port);

#ifdef Q_OS_WIN
        configureWindowsFirewall(port);
#endif

        auto* mdnsService = new mDNSService(this);
        QString serviceName = settings->getString("advanced/mDNSServiceName", "NetShare");
        mdnsService->registerService(serviceName, port);
        m_locator.registerService(mdnsService);
        LOG_INFO("mDNS service registered as '%s'", qPrintable(serviceName));

        quint16 wsPort = port + 1;
        if (wsHandler->start(wsPort, bindAddress)) {
            LOG_INFO("WebSocket server started on %s:%d", qPrintable(bindAddress), wsPort);
        } else {
            LOG_WARN("Failed to start WebSocket server, real-time updates will be unavailable");
        }
        m_locator.registerService(wsHandler);

        // Connect FileTransferEngine progress to WebSocket for real-time browser updates
        if (transferEngine) {
            connect(transferEngine, &FileTransferEngine::taskProgress,
                    this, [this, requestHandler, wsHandler](const QString& taskId, int progress, int speed) {
                Q_UNUSED(speed)

                QJsonObject data;
                data["progress"] = progress;
                data["speed"] = speed;
                data["taskId"] = taskId;

                // Broadcast to session subscribers (upload page)
                QString token = requestHandler->tokenForTask(taskId);
                if (!token.isEmpty()) {
                    wsHandler->broadcastToSubscribers(token, "transfer_update", data);
                }

                // Also broadcast to share token subscribers (share/download page)
                QString shareToken = requestHandler->shareTokenForTask(taskId);
                if (!shareToken.isEmpty() && shareToken != token) {
                    wsHandler->broadcastToSubscribers(shareToken, "transfer_update", data);
                }
            });
        }

        return true;
    }

    bool initializeQml()
    {
        m_engine = new QQmlApplicationEngine(this);

        // Register meta types for QML
        qRegisterMetaType<ShareInfo>("ShareInfo");
        qRegisterMetaType<TransferTask>("TransferTask");
        qRegisterMetaType<FileEntry>("FileEntry");

        // Expose backend objects to QML via context properties BEFORE loading
        auto* shareManager = m_locator.service<ShareManager>();
        auto* transferEngine = m_locator.service<FileTransferEngine>();
        auto* settings = m_locator.service<SettingsManager>();
        auto* fileBrowser = m_locator.service<FileBrowser>();
        auto* folderPacker = m_locator.service<FolderPacker>();
        auto* chunkManager = m_locator.service<ChunkManager>();
        auto* resumeManager = m_locator.service<ResumeManager>();
        auto* bandwidthManager = m_locator.service<BandwidthManager>();
        auto* transferLogService = m_locator.service<TransferLogService>();
        auto* mdnsService = m_locator.service<mDNSService>();
        auto* wsHandler = m_locator.service<WebSocketHandler>();

        m_engine->rootContext()->setContextProperty("shareManager", shareManager);
        m_engine->rootContext()->setContextProperty("transferEngine", transferEngine);
        m_engine->rootContext()->setContextProperty("settingsManager", settings);
        m_engine->rootContext()->setContextProperty("fileBrowser", fileBrowser);
        m_engine->rootContext()->setContextProperty("folderPacker", folderPacker);
        m_engine->rootContext()->setContextProperty("chunkManager", chunkManager);
        m_engine->rootContext()->setContextProperty("resumeManager", resumeManager);
        m_engine->rootContext()->setContextProperty("bandwidthManager", bandwidthManager);
        m_engine->rootContext()->setContextProperty("transferLogService", transferLogService);
        m_engine->rootContext()->setContextProperty("notificationManager", m_notificationManager);
        m_engine->rootContext()->setContextProperty("mdnsService", mdnsService);
        m_engine->rootContext()->setContextProperty("webSocketHandler", wsHandler);

        auto* qrCodeHelper = new QRCodeHelper(this);
        m_engine->rootContext()->setContextProperty("qrCodeHelper", qrCodeHelper);

        // Try loading from QML module first, fallback to qrc/filesystem
        bool loaded = false;

        // Option 1: Load via QML module import
        m_engine->loadFromModule("NetShare", "Main");
        if (!m_engine->rootObjects().isEmpty()) {
            loaded = true;
            LOG_INFO("QML loaded from NetShare module");
        }

        // Option 2: Try qrc resource (qt_add_qml_module resource path)
        if (!loaded && QFile::exists(":/qt/qml/NetShare/qml/Main.qml")) {
            m_engine->load(QUrl("qrc:/qt/qml/NetShare/qml/Main.qml"));
            if (!m_engine->rootObjects().isEmpty()) {
                loaded = true;
                LOG_INFO("QML loaded from qrc resources");
            }
        }

        // Option 3: Search filesystem (development)
        if (!loaded) {
            QDir sourceDir(QCoreApplication::applicationDirPath());
            QStringList searchPaths = {
                sourceDir.filePath("../src/gui/qml/Main.qml"),
                sourceDir.filePath("../../src/gui/qml/Main.qml"),
                QDir::currentPath() + "/src/gui/qml/Main.qml",
            };

            QDir projectRoot(sourceDir);
            for (int i = 0; i < 10; ++i) {
                projectRoot.cdUp();
                QString candidate = projectRoot.filePath("src/gui/qml/Main.qml");
                if (QFileInfo::exists(candidate)) {
                    searchPaths.prepend(candidate);
                    break;
                }
            }

            for (const QString& candidate : searchPaths) {
                if (QFileInfo::exists(candidate)) {
                    QFileInfo qmlFileInfo(candidate);
                    m_engine->addImportPath(qmlFileInfo.absolutePath());
                    m_engine->load(QUrl::fromLocalFile(candidate));
                    if (!m_engine->rootObjects().isEmpty()) {
                        loaded = true;
                        LOG_INFO("QML loaded from filesystem: %s", qPrintable(candidate));
                        break;
                    }
                }
            }
        }

        if (!loaded) {
            LOG_ERROR("Cannot find or load Main.qml");
            return false;
        }

        if (m_engine->rootObjects().isEmpty()) {
            LOG_ERROR("Failed to load QML: no root objects");
            delete m_engine;
            m_engine = nullptr;
            return false;
        }

        m_mainWindow = qobject_cast<QWindow*>(m_engine->rootObjects().first());
        if (!m_mainWindow) {
            LOG_WARN("Failed to cast root object to QWindow, trying QQuickWindow");
            QQuickWindow* quickWin = qobject_cast<QQuickWindow*>(m_engine->rootObjects().first());
            if (!quickWin) {
                LOG_ERROR("Failed to cast root object to QQuickWindow");
                delete m_engine;
                m_engine = nullptr;
                return false;
            }
            m_mainWindow = quickWin;
        }

#ifdef Q_OS_WIN
        // Enable dark title bar on Windows 10 1809+ (for taskbar & alt-tab)
        HWND hwnd = (HWND)m_mainWindow->winId();
        BOOL darkMode = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

        // Sheet of glass: extends DWM frame over entire window for drop shadow
        MARGINS margins = { -1, -1, -1, -1 };
        DwmExtendFrameIntoClientArea(hwnd, &margins);
#endif

        // Show window unless user configured start minimized
        auto* settingsSvc = m_locator.service<SettingsManager>();
        bool startMinimized = settingsSvc ? settingsSvc->value("General/MinimizeToTray", true).toBool() : false;
        if (!startMinimized) {
            m_mainWindow->show();
        }

        connect(m_mainWindow, &QWindow::visibleChanged,
                this, [this](bool visible) {
            if (!visible) {
                LOG_DEBUG("Main window hidden");
            }
        });

        LOG_INFO("QML engine initialized");
        return true;
    }

    bool initializeTrayIcon()
    {
        if (!QSystemTrayIcon::isSystemTrayAvailable()) {
            LOG_WARN("System tray is not available");
            return false;
        }

        m_trayIcon = new QSystemTrayIcon(this);

        QIcon icon = createTrayIcon();
        m_trayIcon->setIcon(icon);
        m_trayIcon->setToolTip(tr("NetShare - 局域网文件共享"));

        QMenu* menu = createTrayMenu();
        m_trayIcon->setContextMenu(menu);

        connect(m_trayIcon, &QSystemTrayIcon::activated,
                this, &NetShareApplication::onTrayIconActivated);

        m_trayIcon->show();
        LOG_INFO("System tray icon created");
        return true;
    }

    QIcon createTrayIcon() const
    {
        QIcon icon = QIcon::fromTheme("network-server");
        if (icon.isNull()) {
            icon = QApplication::style()->standardIcon(QStyle::SP_DriveNetIcon);
        }
        return icon;
    }

    QMenu* createTrayMenu()
    {
        QMenu* menu = new QMenu();

        QAction* showAction = new QAction(tr("打开主窗口"), menu);
        connect(showAction, &QAction::triggered, this, &NetShareApplication::onShowMainWindow);
        menu->addAction(showAction);

        menu->addSeparator();

        QAction* sharesAction = new QAction(tr("我的分享"), menu);
        connect(sharesAction, &QAction::triggered, this, &NetShareApplication::onShowShares);
        menu->addAction(sharesAction);

        QAction* transfersAction = new QAction(tr("传输列表"), menu);
        connect(transfersAction, &QAction::triggered, this, &NetShareApplication::onShowTransfers);
        menu->addAction(transfersAction);

        menu->addSeparator();

        QAction* settingsAction = new QAction(tr("设置"), menu);
        connect(settingsAction, &QAction::triggered, this, &NetShareApplication::onShowSettings);
        menu->addAction(settingsAction);

        menu->addSeparator();

        QAction* quitAction = new QAction(tr("退出"), menu);
        connect(quitAction, &QAction::triggered, this, &NetShareApplication::onQuit);
        menu->addAction(quitAction);

        return menu;
    }

    QString getAppDataDirectory() const
    {
        return QDir::toNativeSeparators(
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    }

    QString getConfigDirectory() const
    {
        QString dir = QDir::toNativeSeparators(
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
        QDir().mkpath(dir);
        return dir;
    }

    QString getDatabaseDirectory() const
    {
        QString dir = getConfigDirectory();
        QDir().mkpath(dir);
        return dir;
    }

    QString getLogDirectory() const
    {
        QString dir = getAppDataDirectory() + "/logs";
        QDir().mkpath(dir);
        return dir;
    }

private slots:
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason)
    {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            onShowMainWindow();
        }
    }

    void onShowMainWindow()
    {
        if (m_mainWindow) {
            m_mainWindow->show();
            m_mainWindow->raise();
            m_mainWindow->requestActivate();
            LOG_DEBUG("Main window shown");
        }
    }

    void onShowShares()
    {
        LOG_DEBUG("Show shares requested");
    }

    void onShowTransfers()
    {
        LOG_DEBUG("Show transfers requested");
    }

    void onShowSettings()
    {
        LOG_DEBUG("Show settings requested");
    }

    void onQuit()
    {
        LOG_INFO("Quit requested by user");
        QCoreApplication::quit();
    }

private:
    ServiceLocator m_locator;
    QSystemTrayIcon* m_trayIcon = nullptr;
    QQmlApplicationEngine* m_engine = nullptr;
    QWindow* m_mainWindow = nullptr;
    NotificationManager* m_notificationManager = nullptr;

};

static void setupApplicationInfo()
{
    QCoreApplication::setApplicationName("NetShare");
    QCoreApplication::setApplicationVersion(NETSHARE_VERSION);
    QCoreApplication::setOrganizationDomain(NETSHARE_ORGANIZATION_DOMAIN);

    QApplication::setApplicationDisplayName("NetShare");
    QApplication::setDesktopFileName("netshare");
}

int main(int argc, char *argv[])
{
    setupHighDpiSupport();

    QApplication app(argc, argv);
    setupApplicationInfo();

    app.setQuitOnLastWindowClosed(false);

    NetShareApplication netshare;
    if (!netshare.initialize()) {
        qCritical() << "Failed to initialize NetShare application";
        return 1;
    }

    QObject::connect(&app, &QApplication::aboutToQuit, [&netshare]() {
        netshare.shutdown();
    });

    netshare.run();

    int ret = app.exec();

    netshare.shutdown();
    return ret;
}

#include "main.moc"
