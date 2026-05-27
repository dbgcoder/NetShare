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
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/common/Logger.h"
#include "core/common/SettingsManager.h"
#include "core/common/DiContainer.h"
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
#include "network/CivetWebServer.h"
#include "network/mDNSService.h"
#include "network/RequestHandler.h"
#include "core/common/TlsCertificateGenerator.h"
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

        m_notificationManager = new NotificationManager(m_trayIcon, this);
        LOG_INFO("NotificationManager initialized");

        buildInjector();

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

        if (m_mdnsService) { m_mdnsService->unregisterService(); }
        if (m_civetServer) { m_civetServer->stop(); }
        if (m_transferEngine) { m_transferEngine->stopAllTasks(); }
        if (m_bandwidthManager) { m_bandwidthManager->stopMonitoring(); }
        if (m_settings) { m_settings->sync(); }
        if (m_database) { m_database->close(); }

        ShareManager::shutdown();

        m_injector.reset();

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
        m_database = new DatabaseManager(this);
        if (!m_database->open(dbPath)) {
            LOG_ERROR("Failed to open database: %s", qPrintable(dbPath));
            return false;
        }
        if (!m_database->initialize()) {
            LOG_ERROR("Failed to initialize database schema");
            return false;
        }
        LOG_INFO("Database initialized successfully");
        return true;
    }

    bool initializeSettings()
    {
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

        m_settings = new SettingsManager(this);
        if (!m_settings->load(configPath)) {
            LOG_WARN("Failed to load settings, using defaults");
        }
        LOG_INFO("Settings loaded successfully");
        return true;
    }

    bool initializeCoreServices()
    {
        m_shareManager = &ShareManager::instance();
        m_shareManager->setDatabase(m_database);
        LOG_INFO("ShareManager initialized");

        m_transferEngine = new FileTransferEngine(this);

        m_transferLog = new TransferLogService(this);
        m_transferLog->setDatabase(m_database);
        LOG_INFO("TransferLogService initialized");

        m_transferEngine->setTransferLogService(m_transferLog);

        if (!m_transferEngine->initialize()) {
            LOG_ERROR("Failed to initialize FileTransferEngine");
            return false;
        }
        LOG_INFO("FileTransferEngine initialized");

        m_fileBrowser = new FileBrowser(this);
        LOG_INFO("FileBrowser initialized");

        m_folderPacker = new FolderPacker(this);
        LOG_INFO("FolderPacker initialized");

        m_chunkManager = new ChunkManager(this);
        LOG_INFO("ChunkManager initialized");

        m_resumeManager = new ResumeManager(this);
        LOG_INFO("ResumeManager initialized");

        m_bandwidthManager = new BandwidthManager(this);
        m_bandwidthManager->startMonitoring();
        LOG_INFO("BandwidthManager initialized");

        m_transferEngine->setManagers(m_shareManager, m_chunkManager, m_resumeManager, m_bandwidthManager);

        return true;
    }

    bool initializeNetworkServer()
    {
        m_civetServer = new CivetWebServer(this);

        auto* requestHandler = new RequestHandler(m_shareManager, m_fileBrowser, m_folderPacker, this);
        QString uploadDir = m_settings->getUploadPath();
        QDir().mkpath(uploadDir);
        requestHandler->setUploadDir(uploadDir);
        requestHandler->setSettingsManager(m_settings);

        if (m_transferEngine) {
            requestHandler->setTransferEngine(m_transferEngine);
            m_transferEngine->setUploadPauseCallback([requestHandler](const QString& taskId) {
                requestHandler->pauseUploadForTask(taskId);
            });
            m_transferEngine->setUploadResumeCallback([requestHandler](const QString& taskId) {
                requestHandler->resumeUploadForTask(taskId);
            });
        }
        if (m_transferLog) {
            requestHandler->setTransferLogService(m_transferLog);
        }

        requestHandler->registerRoutes(m_civetServer);

        quint16 port = m_settings->value("Network/Port", 8080).toUInt();
        QString bindAddress = m_settings->value("Network/BindAddress", "0.0.0.0").toString();

        if (m_settings->getBool("server/tlsEnabled", false)) {
            LOG_INFO("TLS is enabled, configuring certificates...");
            QString certPath = m_settings->getString("server/tlsCertPath");
            QString keyPath = m_settings->getString("server/tlsKeyPath");

            if (certPath.isEmpty() || keyPath.isEmpty()) {
                QString certDir = TlsCertificateGenerator::defaultCertDir();
                if (!TlsCertificateGenerator::certificatesExist(certDir)) {
                    LOG_INFO("No TLS certificates found, auto-generating self-signed certificate...");
                    auto result = TlsCertificateGenerator::generateSelfSignedCert(certDir);
                    if (!result.success) {
                        LOG_WARN("Failed to auto-generate TLS certificate: %s, falling back to non-secure",
                                 qPrintable(result.errorMessage));
                    } else {
                        certPath = result.certPath;
                        keyPath = result.keyPath;
                        m_settings->setValue("server/tlsCertPath", certPath);
                        m_settings->setValue("server/tlsKeyPath", keyPath);
                    }
                } else {
                    certPath = TlsCertificateGenerator::defaultCertPath();
                    keyPath = TlsCertificateGenerator::defaultKeyPath();
                }
            }

            if (!certPath.isEmpty() && !keyPath.isEmpty()) {
                if (!QFileInfo::exists(certPath) || !QFileInfo::exists(keyPath)) {
                    LOG_ERROR("TLS certificate or key file not found: %s, %s",
                              qPrintable(certPath), qPrintable(keyPath));
                } else {
                    m_civetServer->setSslCertificate(certPath, keyPath);
                    m_civetServer->setTlsEnabled(true);
                    LOG_INFO("TLS configured successfully with cert: %s", qPrintable(certPath));
                }
            }
        }

        m_civetServer->enableWebSocket(
            "/ws",
            [](const mg_connection* conn) -> int {
                Q_UNUSED(conn)
                return 0;
            },
            [](mg_connection* conn) {
                Q_UNUSED(conn)
            },
            [this](mg_connection* conn, int op, char* data, size_t len) -> int {
                if (op == MG_WEBSOCKET_OPCODE_TEXT) {
                    QByteArray msg(data, static_cast<int>(len));
                    QJsonDocument doc = QJsonDocument::fromJson(msg);
                    if (doc.isObject()) {
                        QJsonObject obj = doc.object();
                        QString type = obj["type"].toString();
                        if (type == "subscribe" && obj.contains("token")) {
                            m_civetServer->subscribeClient(conn, obj["token"].toString());
                        } else if (type == "unsubscribe" && obj.contains("token")) {
                            m_civetServer->unsubscribeClient(conn, obj["token"].toString());
                        }
                    }
                }
                return 0;
            },
            [this](const mg_connection* conn) {
                m_civetServer->unsubscribeClientFromAll(const_cast<mg_connection*>(conn));
            }
        );

        if (!m_civetServer->start(port, bindAddress)) {
            LOG_ERROR("Failed to start HTTP server on port %d", port);
            return false;
        }
        LOG_INFO("HTTP/CivetWeb server started on %s:%d (TLS: %s)",
                 qPrintable(bindAddress), port,
                 m_settings->getBool("server/tlsEnabled", false) ? "enabled" : "disabled");

#ifdef Q_OS_WIN
        configureWindowsFirewall(port);
#endif

        m_mdnsService = new mDNSService(this);
        QString serviceName = m_settings->getString("advanced/mDNSServiceName", "NetShare");
        m_mdnsService->registerService(serviceName, port);
        LOG_INFO("mDNS service registered as '%s'", qPrintable(serviceName));

        if (m_transferEngine) {
            connect(m_transferEngine, &FileTransferEngine::taskProgress,
                    this, [this, requestHandler](const QString& taskId, int progress, int speed) {
                Q_UNUSED(speed)

                QJsonObject data;
                data["progress"] = progress;
                data["speed"] = speed;
                data["taskId"] = taskId;

                QString token = requestHandler->tokenForTask(taskId);
                if (!token.isEmpty()) {
                    m_civetServer->broadcastToSubscribers(token, "transfer_update", data);
                }

                QString shareToken = requestHandler->shareTokenForTask(taskId);
                if (!shareToken.isEmpty() && shareToken != token) {
                    m_civetServer->broadcastToSubscribers(shareToken, "transfer_update", data);
                }
            });

            auto broadcastWsEvent = [this, requestHandler](const QString& taskId, const QString& eventType) {
                QJsonObject data;
                data["taskId"] = taskId;

                QString token = requestHandler->tokenForTask(taskId);
                if (!token.isEmpty()) {
                    m_civetServer->broadcastToSubscribers(token, eventType, data);
                }

                QString shareToken = requestHandler->shareTokenForTask(taskId);
                if (!shareToken.isEmpty() && shareToken != token) {
                    m_civetServer->broadcastToSubscribers(shareToken, eventType, data);
                }
            };

            connect(m_transferEngine, &FileTransferEngine::taskPaused, this,
                    [broadcastWsEvent](const QString& taskId) { broadcastWsEvent(taskId, "pause_upload"); });
            connect(m_transferEngine, &FileTransferEngine::taskResumed, this,
                    [broadcastWsEvent](const QString& taskId) { broadcastWsEvent(taskId, "resume_upload"); });
        }

        return true;
    }

    bool initializeQml()
    {
        m_engine = new QQmlApplicationEngine(this);

        qRegisterMetaType<ShareInfo>("ShareInfo");
        qRegisterMetaType<TransferTask>("TransferTask");
        qRegisterMetaType<FileEntry>("FileEntry");

        m_engine->rootContext()->setContextProperty("shareManager", m_shareManager);
        m_engine->rootContext()->setContextProperty("transferEngine", m_transferEngine);
        m_engine->rootContext()->setContextProperty("settingsManager", m_settings);
        m_engine->rootContext()->setContextProperty("fileBrowser", m_fileBrowser);
        m_engine->rootContext()->setContextProperty("folderPacker", m_folderPacker);
        m_engine->rootContext()->setContextProperty("chunkManager", m_chunkManager);
        m_engine->rootContext()->setContextProperty("resumeManager", m_resumeManager);
        m_engine->rootContext()->setContextProperty("bandwidthManager", m_bandwidthManager);
        m_engine->rootContext()->setContextProperty("transferLogService", m_transferLog);
        m_engine->rootContext()->setContextProperty("notificationManager", m_notificationManager);
        m_engine->rootContext()->setContextProperty("mdnsService", m_mdnsService);
        m_engine->rootContext()->setContextProperty("webSocketHandler", m_civetServer);

        auto* qrCodeHelper = new QRCodeHelper(this);
        m_engine->rootContext()->setContextProperty("qrCodeHelper", qrCodeHelper);

        // Load QML: environment variable override > QML module > qrc
        bool loaded = false;
        QString qmlPath = qEnvironmentVariable("NETSHARE_QML_PATH");

        if (!qmlPath.isEmpty()) {
            LOG_INFO("NETSHARE_QML_PATH set, loading from: %s", qPrintable(qmlPath));
            if (QFileInfo::exists(qmlPath)) {
                QFileInfo qmlFileInfo(qmlPath);
                m_engine->addImportPath(qmlFileInfo.absolutePath());
                m_engine->load(QUrl::fromLocalFile(qmlPath));
                loaded = !m_engine->rootObjects().isEmpty();
            } else {
                LOG_WARN("NETSHARE_QML_PATH points to non-existent file: %s", qPrintable(qmlPath));
            }
        }

        if (!loaded) {
            m_engine->loadFromModule("NetShare", "Main");
            loaded = !m_engine->rootObjects().isEmpty();
            if (loaded) LOG_INFO("QML loaded from NetShare module");
        }

        if (!loaded && QFile::exists(":/qt/qml/NetShare/qml/Main.qml")) {
            m_engine->load(QUrl("qrc:/qt/qml/NetShare/qml/Main.qml"));
            loaded = !m_engine->rootObjects().isEmpty();
            if (loaded) LOG_INFO("QML loaded from qrc resources");
        }

        if (!loaded) {
            LOG_ERROR("Cannot find or load Main.qml (tried: NETSHARE_QML_PATH env, QML module, qrc)");
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
        auto* settingsSvc = m_settings;
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
    std::unique_ptr<NetShareInjector> m_injector;

    DatabaseManager*      m_database         = nullptr;
    SettingsManager*      m_settings         = nullptr;
    ShareManager*         m_shareManager     = nullptr;
    FileTransferEngine*   m_transferEngine   = nullptr;
    TransferLogService*   m_transferLog      = nullptr;
    FileBrowser*          m_fileBrowser      = nullptr;
    FolderPacker*         m_folderPacker     = nullptr;
    ChunkManager*         m_chunkManager     = nullptr;
    ResumeManager*        m_resumeManager    = nullptr;
    BandwidthManager*     m_bandwidthManager = nullptr;
    CivetWebServer*       m_civetServer      = nullptr;
    mDNSService*          m_mdnsService      = nullptr;
    NotificationManager*  m_notificationManager = nullptr;
    QSystemTrayIcon*      m_trayIcon         = nullptr;
    QQmlApplicationEngine* m_engine          = nullptr;
    QWindow*              m_mainWindow       = nullptr;

    void buildInjector()
    {
        m_injector = std::make_unique<NetShareInjector>(
            di::make_injector(
                CoreModule(*m_shareManager, *m_fileBrowser, *m_folderPacker),
                TransferModule(*m_transferEngine, *m_chunkManager, *m_resumeManager, *m_bandwidthManager),
                NetworkModule(*m_civetServer, *m_mdnsService, *m_notificationManager),
                InfraModule(*m_database, *m_settings, *m_transferLog)
            )
        );
        LOG_INFO("Boost.DI injector built successfully");
    }

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
