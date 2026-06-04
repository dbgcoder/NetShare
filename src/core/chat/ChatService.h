#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QMap>
#include <QList>
#include <QVariantList>
#include <QVariantMap>
#include <QNetworkAccessManager>
#include <QTimer>

class mDNSService;
class CivetWebServer;
class SettingsManager;

struct ChatMessage
{
    QString msgId;
    QString fromUser;
    QString toUser;
    QString content;
    QDateTime timestamp;
    bool isSent = false;
    bool sendFailed = false;
};

struct ChatUser
{
    QString name;
    QString address;
    int port = 0;
    bool isOnline = false;
    QString deviceType;
    int unreadCount = 0;
    QDateTime lastMessageTime;
};

class ChatService : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int totalUnreadCount READ totalUnreadCount NOTIFY unreadCountChanged)
    Q_PROPERTY(QVariantList userList READ userList NOTIFY userListChanged)

public:
    explicit ChatService(mDNSService* mdnsService,
                         CivetWebServer* civetServer,
                         SettingsManager* settingsManager,
                         QObject* parent = nullptr);
    ~ChatService() override;

    int totalUnreadCount() const;
    QVariantList userList() const;

    Q_INVOKABLE QVariantList getChatHistory(const QString& userAddress) const;
    Q_INVOKABLE void sendMessage(const QString& toAddress, int toPort, const QString& content);
    Q_INVOKABLE void clearUnread(const QString& userAddress);
    Q_INVOKABLE QVariantList getUserList() const;
    Q_INVOKABLE void renameDevice(const QString& address, const QString& name);
    Q_INVOKABLE QString getDeviceName(const QString& address) const;

    void onMessageReceived(const QString& fromUser, const QString& fromIp,
                           const QString& content, const QString& timestamp,
                           const QString& remoteAddress);

signals:
    void messageReceived(const QString& fromAddress);
    void messageSent(const QString& toAddress);
    void messageSendFailed(const QString& toAddress, const QString& errorMsg);
    void unreadCountChanged();
    void userListChanged();

private slots:
    void onServiceDiscovered(const QString& name, const QHostAddress& address, quint16 port);
    void onServiceLost(const QString& name);
    void onSendReplyFinished(QNetworkReply* reply, const QString& toAddress, const ChatMessage& msg);

private:
    void loadLocalDeviceInfo();
    void loadDeviceNameMap();
    void appendMessage(const QString& address, const ChatMessage& msg);
    void updateAnonymousUser(const QString& remoteAddress, const QString& fromName, const QString& deviceType);
    void cleanupStaleAnonymousUsers();
    QString generateMsgId() const;
    QString extractPureIp(const QString& remoteAddress) const;

    mDNSService* m_mdnsService;
    CivetWebServer* m_civetServer;
    SettingsManager* m_settingsManager;
    QNetworkAccessManager* m_networkManager;

    QString m_localDeviceName;
    QString m_localIp;

    QMap<QString, ChatUser> m_discoveredUsers;
    QMap<QString, ChatUser> m_anonymousUsers;
    QMap<QString, QList<ChatMessage>> m_chatHistory;
    QMap<QString, QString> m_deviceNameMap;

    QTimer* m_cleanupTimer;
};

#endif
