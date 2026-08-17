#ifndef EMAILSERVICE_H
#define EMAILSERVICE_H

#include <QObject>
#include <QString>
#include <QStringList>

class EmailService : public QObject
{
    Q_OBJECT
public:
    explicit EmailService(QObject* parent = nullptr);

    // 自动发送注册信息到管理员邮箱（SMTP 方式，异步执行）
    Q_INVOKABLE void sendRegistrationInfo(const QString& username,
                                           const QString& userEmail,
                                           const QString& phone,
                                           const QString& machineId);

signals:
    void sendSuccess();
    void sendFailed(const QString& reason);

private:
    QString m_smtpServer;
    int m_smtpPort;
    QString m_senderEmail;
    QString m_senderPassword;
    QStringList m_recipientEmails;  // 管理员接收邮箱列表
    bool m_useSsl;

    // SMTP 协议实现
    bool sendSmtpCommand(class QSslSocket* socket, const QString& command, int expectedCode);
    QString buildEmailContent(const QString& username, const QString& userEmail,
                               const QString& phone, const QString& machineId);
};

#endif // EMAILSERVICE_H
