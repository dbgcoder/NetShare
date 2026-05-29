#include "ChatService.h"
#include "mDNSService.h"
#include "CivetWebServer.h"
#include "SettingsManager.h"
#include "Logger.h"
#include "ShareManager.h"

#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QHostAddress>
#include <QSysInfo>

ChatService::ChatService(mDNSService* mdnsService,
                         CivetWebServer* civetServer,
                         SettingsManager* settingsManager,
                         QObject* parent)
    : QObject(parent)
    , m_mdnsService(mdnsService)
    , m_civetServer(civetServer)
    , m_settingsManager(settingsManager)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_cleanupTimer(new QTimer(this))
{
    loadLocalDeviceInfo();

    if (m_mdnsService) {
        connect(m_mdnsService, &mDNSService::serviceDiscovered,
                this, &ChatService::onServiceDiscovered);
        connect(m_mdnsService, &mDNSService::serviceLost,
                this, &ChatService::onServiceLost);

        auto existing = m_mdnsService->discoveredServices();
        for (auto it = existing.constBegin(); it != existing.constEnd(); ++it) {
            ChatUser user;
            user.name = it.key();
            user.address = it.value().first.toString();
            user.port = it.value().second;
            user.isOnline = true;
            user.deviceType = QStringLiteral("desktop");
            m_discoveredUsers[user.address] = user;
        }
    }

    m_cleanupTimer->setInterval(60000);
    connect(m_cleanupTimer, &QTimer::timeout, this, &ChatService::cleanupStaleAnonymousUsers);
    m_cleanupTimer->start();

    LOG_INFO("ChatService initialized, local device: %s (%s)",
             qPrintable(m_localDeviceName), qPrintable(m_localIp));
}

ChatService::~ChatService()
{
    m_cleanupTimer->stop();
}

void ChatService::loadLocalDeviceInfo()
{
    if (m_settingsManager) {
        m_localDeviceName = m_settingsManager->getString("General/DeviceName");
        if (m_localDeviceName.isEmpty()) {
            m_localDeviceName = QSysInfo::machineHostName();
        }
    } else {
        m_localDeviceName = QSysInfo::machineHostName();
    }

    auto& shareMgr = ShareManager::instance();
    m_localIp = shareMgr.localIp();
}

int ChatService::totalUnreadCount() const
{
    int total = 0;
    for (auto it = m_discoveredUsers.constBegin(); it != m_discoveredUsers.constEnd(); ++it) {
        total += it.value().unreadCount;
    }
    for (auto it = m_anonymousUsers.constBegin(); it != m_anonymousUsers.constEnd(); ++it) {
        total += it.value().unreadCount;
    }
    return total;
}

QVariantList ChatService::userList() const
{
    return getUserList();
}

QVariantList ChatService::getUserList() const
{
    QVariantList result;

    for (auto it = m_discoveredUsers.constBegin(); it != m_discoveredUsers.constEnd(); ++it) {
        const ChatUser& user = it.value();
        QVariantMap entry;
        entry[QStringLiteral("name")] = user.name;
        entry[QStringLiteral("address")] = user.address;
        entry[QStringLiteral("port")] = user.port;
        entry[QStringLiteral("isOnline")] = user.isOnline;
        entry[QStringLiteral("deviceType")] = user.deviceType;
        entry[QStringLiteral("unreadCount")] = user.unreadCount;
        entry[QStringLiteral("isAnonymous")] = false;
        QString lastMsg;
        auto histIt = m_chatHistory.find(user.address);
        if (histIt != m_chatHistory.constEnd() && !histIt.value().isEmpty()) {
            lastMsg = histIt.value().last().content;
            if (lastMsg.length() > 20) lastMsg = lastMsg.left(20) + QStringLiteral("...");
        }
        entry[QStringLiteral("lastMessage")] = lastMsg;
        result.append(entry);
    }

    for (auto it = m_anonymousUsers.constBegin(); it != m_anonymousUsers.constEnd(); ++it) {
        const ChatUser& user = it.value();
        QVariantMap entry;
        entry[QStringLiteral("name")] = user.name;
        entry[QStringLiteral("address")] = user.address;
        entry[QStringLiteral("port")] = user.port;
        entry[QStringLiteral("isOnline")] = user.isOnline;
        entry[QStringLiteral("deviceType")] = user.deviceType;
        entry[QStringLiteral("unreadCount")] = user.unreadCount;
        entry[QStringLiteral("isAnonymous")] = true;
        QString lastMsg;
        auto histIt = m_chatHistory.find(user.address);
        if (histIt != m_chatHistory.constEnd() && !histIt.value().isEmpty()) {
            lastMsg = histIt.value().last().content;
            if (lastMsg.length() > 20) lastMsg = lastMsg.left(20) + QStringLiteral("...");
        }
        entry[QStringLiteral("lastMessage")] = lastMsg;
        result.append(entry);
    }

    return result;
}

QVariantList ChatService::getChatHistory(const QString& userAddress) const
{
    QVariantList result;
    auto it = m_chatHistory.find(userAddress);
    if (it == m_chatHistory.constEnd()) return result;

    const QList<ChatMessage>& messages = it.value();
    for (const ChatMessage& msg : messages) {
        QVariantMap entry;
        entry[QStringLiteral("msgId")] = msg.msgId;
        entry[QStringLiteral("fromUser")] = msg.fromUser;
        entry[QStringLiteral("toUser")] = msg.toUser;
        entry[QStringLiteral("content")] = msg.content;
        entry[QStringLiteral("timestamp")] = msg.timestamp.toString(Qt::ISODate);
        entry[QStringLiteral("isSent")] = msg.isSent;
        entry[QStringLiteral("sendFailed")] = msg.sendFailed;
        result.append(entry);
    }
    return result;
}

void ChatService::sendMessage(const QString& toAddress, int toPort, const QString& content)
{
    if (content.trimmed().isEmpty()) return;

    ChatMessage msg;
    msg.msgId = generateMsgId();
    msg.fromUser = m_localDeviceName;
    msg.toUser = toAddress;
    msg.content = content;
    msg.timestamp = QDateTime::currentDateTime();
    msg.isSent = true;
    msg.sendFailed = false;

    appendMessage(toAddress, msg);

    bool isAnonymous = m_anonymousUsers.contains(toAddress);

    if (isAnonymous || toPort <= 0) {
        if (m_civetServer) {
            QJsonObject wsData;
            wsData[QStringLiteral("fromUser")] = m_localDeviceName;
            wsData[QStringLiteral("fromAddress")] = m_localIp;
            wsData[QStringLiteral("content")] = content;
            wsData[QStringLiteral("timestamp")] = msg.timestamp.toString(Qt::ISODate);
            wsData[QStringLiteral("msgId")] = msg.msgId;
            bool sent = m_civetServer->sendToIp(
                toAddress, QStringLiteral("chat_message"), wsData);
            LOG_INFO("Chat message sent to anonymous user %s via WebSocket, result=%s",
                     qPrintable(toAddress), sent ? "OK" : "FAILED");
        } else {
            LOG_INFO("Chat message to anonymous user %s: no WebSocket server available",
                     qPrintable(toAddress));
        }
    } else {
        QJsonObject json;
        json[QStringLiteral("from")] = m_localDeviceName;
        json[QStringLiteral("fromIp")] = m_localIp;
        json[QStringLiteral("content")] = content;
        json[QStringLiteral("timestamp")] = msg.timestamp.toString(Qt::ISODate);
        json[QStringLiteral("msgId")] = msg.msgId;

        QJsonDocument doc(json);
        QByteArray body = doc.toJson(QJsonDocument::Compact);

        QString scheme = QStringLiteral("http");
        QUrl url(QStringLiteral("%1://%2:%3/api/chat/message").arg(scheme, toAddress).arg(toPort));
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setTransferTimeout(5000);

        QNetworkReply* reply = m_networkManager->post(request, body);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, toAddress, msg]() { onSendReplyFinished(reply, toAddress, msg); });
    }

    emit messageSent(toAddress);
}

void ChatService::clearUnread(const QString& userAddress)
{
    bool changed = false;
    auto dit = m_discoveredUsers.find(userAddress);
    if (dit != m_discoveredUsers.end() && dit.value().unreadCount > 0) {
        dit.value().unreadCount = 0;
        changed = true;
    }
    auto ait = m_anonymousUsers.find(userAddress);
    if (ait != m_anonymousUsers.end() && ait.value().unreadCount > 0) {
        ait.value().unreadCount = 0;
        changed = true;
    }
    if (changed) {
        emit unreadCountChanged();
        emit userListChanged();
    }
}

void ChatService::onMessageReceived(const QString& fromUser, const QString& fromIp,
                                     const QString& content, const QString& timestamp,
                                     const QString& remoteAddress)
{
    QString pureIp = extractPureIp(remoteAddress);
    if (pureIp.isEmpty()) pureIp = extractPureIp(fromIp);
    if (pureIp.isEmpty()) pureIp = fromIp;

    bool isFromDiscovered = m_discoveredUsers.contains(pureIp);

    ChatMessage msg;
    msg.msgId = generateMsgId();
    msg.fromUser = fromUser;
    if (msg.fromUser.isEmpty()) {
        QStringList parts = pureIp.split(QStringLiteral("."));
        QString suffix = parts.isEmpty() ? pureIp : parts.last();
        msg.fromUser = QStringLiteral("移动端-%1").arg(suffix);
    }
    msg.toUser = m_localDeviceName;
    msg.content = content;
    msg.timestamp = QDateTime::fromString(timestamp, Qt::ISODate);
    if (!msg.timestamp.isValid()) msg.timestamp = QDateTime::currentDateTime();
    msg.isSent = false;
    msg.sendFailed = false;

    if (!isFromDiscovered) {
        QString deviceType = QStringLiteral("mobile");
        updateAnonymousUser(pureIp, fromUser, deviceType);
    }

    appendMessage(pureIp, msg);

    bool unreadChanged = false;
    auto dit = m_discoveredUsers.find(pureIp);
    if (dit != m_discoveredUsers.end()) {
        dit.value().unreadCount++;
        dit.value().lastMessageTime = msg.timestamp;
        unreadChanged = true;
    }
    auto ait = m_anonymousUsers.find(pureIp);
    if (ait != m_anonymousUsers.end()) {
        ait.value().unreadCount++;
        ait.value().lastMessageTime = msg.timestamp;
        unreadChanged = true;
    }

    if (unreadChanged) {
        emit unreadCountChanged();
    }
    emit userListChanged();
    emit messageReceived(pureIp);

    if (m_civetServer) {
        QJsonObject wsData;
        wsData[QStringLiteral("fromUser")] = fromUser;
        wsData[QStringLiteral("fromAddress")] = pureIp;
        wsData[QStringLiteral("content")] = content;
        wsData[QStringLiteral("timestamp")] = msg.timestamp.toString(Qt::ISODate);
        wsData[QStringLiteral("msgId")] = msg.msgId;
        m_civetServer->broadcastToSubscribers(
            QStringLiteral("chat"), QStringLiteral("chat_message"), wsData);
    }

    LOG_INFO("Chat message received from %s (%s): %s",
             qPrintable(fromUser), qPrintable(pureIp), qPrintable(content.left(50)));
}

void ChatService::onServiceDiscovered(const QString& name, const QHostAddress& address, quint16 port)
{
    QString addr = address.toString();
    ChatUser user;
    user.name = name;
    user.address = addr;
    user.port = port;
    user.isOnline = true;
    user.deviceType = QStringLiteral("desktop");

    m_discoveredUsers[addr] = user;
    m_anonymousUsers.remove(addr);

    emit userListChanged();
    LOG_INFO("Chat user discovered: %s (%s:%d)", qPrintable(name), qPrintable(addr), port);
}

void ChatService::onServiceLost(const QString& name)
{
    Q_UNUSED(name)
    for (auto it = m_discoveredUsers.begin(); it != m_discoveredUsers.end(); ++it) {
        if (it.value().name == name && it.value().isOnline) {
            it.value().isOnline = false;
            emit userListChanged();
            LOG_INFO("Chat user lost: %s (%s)", qPrintable(name), qPrintable(it.value().address));
            break;
        }
    }
}

void ChatService::onSendReplyFinished(QNetworkReply* reply, const QString& toAddress, const ChatMessage& msg)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        auto it = m_chatHistory.find(toAddress);
        if (it != m_chatHistory.end()) {
            for (auto& m : it.value()) {
                if (m.msgId == msg.msgId) {
                    m.sendFailed = true;
                    break;
                }
            }
        }
        emit messageSendFailed(toAddress, reply->errorString());
        LOG_WARN("Chat message send failed to %s: %s", qPrintable(toAddress), qPrintable(reply->errorString()));
    } else {
        LOG_INFO("Chat message sent successfully to %s", qPrintable(toAddress));
    }
}

void ChatService::appendMessage(const QString& address, const ChatMessage& msg)
{
    m_chatHistory[address].append(msg);
    if (m_chatHistory[address].size() > 500) {
        m_chatHistory[address].removeFirst();
    }
}

void ChatService::updateAnonymousUser(const QString& remoteAddress, const QString& fromName, const QString& deviceType)
{
    if (m_discoveredUsers.contains(remoteAddress)) return;

    QString displayName = fromName;
    if (displayName.isEmpty()) {
        QStringList parts = remoteAddress.split(QStringLiteral("."));
        QString suffix = parts.isEmpty() ? remoteAddress : parts.last();
        displayName = QStringLiteral("移动端-%1").arg(suffix);
    }

    auto it = m_anonymousUsers.find(remoteAddress);
    if (it != m_anonymousUsers.end()) {
        it.value().lastMessageTime = QDateTime::currentDateTime();
    } else {
        ChatUser user;
        user.name = displayName;
        user.address = remoteAddress;
        user.port = 0;
        user.isOnline = false;
        user.deviceType = deviceType;
        user.lastMessageTime = QDateTime::currentDateTime();
        m_anonymousUsers[remoteAddress] = user;
        emit userListChanged();
    }
}

void ChatService::cleanupStaleAnonymousUsers()
{
    QDateTime threshold = QDateTime::currentDateTime().addSecs(-1800);
    QStringList toRemove;
    for (auto it = m_anonymousUsers.constBegin(); it != m_anonymousUsers.constEnd(); ++it) {
        if (it.value().lastMessageTime.isValid() && it.value().lastMessageTime < threshold) {
            toRemove.append(it.key());
        }
    }
    for (const QString& addr : toRemove) {
        m_anonymousUsers.remove(addr);
        m_chatHistory.remove(addr);
    }
    if (!toRemove.isEmpty()) {
        emit userListChanged();
        emit unreadCountChanged();
    }
}

QString ChatService::generateMsgId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString ChatService::extractPureIp(const QString& remoteAddress) const
{
    if (remoteAddress.isEmpty()) return QString();

    if (remoteAddress.contains(QStringLiteral(":"))) {
        QHostAddress addr(remoteAddress);
        if (!addr.isNull()) return addr.toString();

        QStringList parts = remoteAddress.split(QStringLiteral(":"));
        if (parts.size() >= 2) {
            QHostAddress ipAddr(parts.first());
            if (!ipAddr.isNull()) return ipAddr.toString();
        }
    }
    return remoteAddress;
}
