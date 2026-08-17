#include "EmailService.h"

#include <QSslSocket>
#include <QDateTime>
#include <QByteArray>
#include <QDebug>
#include <QThreadPool>
#include <QRunnable>

EmailService::EmailService(QObject* parent)
    : QObject(parent)
    , m_smtpServer(QStringLiteral("smtp.qq.com"))
    , m_smtpPort(465)
    , m_senderEmail(QStringLiteral("1120597960@qq.com"))
    , m_senderPassword(QStringLiteral("hwnzmcgtgyregjjf"))
    , m_recipientEmails({
        QStringLiteral("HD5080@outlook.com"),
        QStringLiteral("1120597960@qq.com")
    })
    , m_useSsl(true)
{
}

QString EmailService::buildEmailContent(const QString& username, const QString& userEmail,
                                          const QString& phone, const QString& machineId)
{
    QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);

    QString subject = QStringLiteral("NetShare Registration Request - %1").arg(username);

    QString body = QStringLiteral(
        "Registration Information:\n"
        "\n"
        "  Username:    %1\n"
        "  Email:       %2\n"
        "  Phone:       %3\n"
        "  Machine ID:  %4\n"
        "  Timestamp:   %5\n"
        "\n"
        "Please use KeyGenTool to generate registration code:\n"
        "keygen --sign --private-key private.pem --username \"%1\" "
        "--email \"%2\" --phone \"%3\" --machine-id \"%4\"\n"
    ).arg(username, userEmail, phone, machineId, timestamp);

    // 构造 MIME 格式邮件
    QString email = QStringLiteral(
        "From: %1\r\n"
        "To: %2\r\n"
        "Subject: %3\r\n"
        "Date: %4\r\n"
        "MIME-Version: 1.0\r\n"
        "Content-Type: text/plain; charset=UTF-8\r\n"
        "Content-Transfer-Encoding: 8bit\r\n"
        "\r\n"
        "%5\r\n"
    ).arg(m_senderEmail, m_recipientEmails.join(QStringLiteral(", ")), subject,
          QDateTime::currentDateTime().toString(Qt::RFC2822Date), body);

    return email;
}

bool EmailService::sendSmtpCommand(QSslSocket* socket, const QString& command, int expectedCode)
{
    if (!command.isEmpty()) {
        socket->write((command + QStringLiteral("\r\n")).toUtf8());
        socket->waitForBytesWritten(5000);
    }

    if (!socket->waitForReadyRead(10000)) {
        qWarning() << "EmailService: SMTP timeout waiting for response";
        return false;
    }

    QString response = QString::fromUtf8(socket->readAll());
    int code = response.left(3).toInt();

    if (expectedCode > 0 && code != expectedCode) {
        qWarning() << "EmailService: SMTP error, expected" << expectedCode
                   << "got" << code << ":" << response;
        return false;
    }

    return true;
}

void EmailService::sendRegistrationInfo(const QString& username,
                                          const QString& userEmail,
                                          const QString& phone,
                                          const QString& machineId)
{
    // 在后台线程中执行 SMTP 通信
    class SmtpTask : public QRunnable {
    public:
        SmtpTask(EmailService* service, const QString& username,
                 const QString& email, const QString& phone, const QString& machineId)
            : m_service(service), m_username(username), m_email(email),
              m_phone(phone), m_machineId(machineId)
        {
            setAutoDelete(true);
        }

        void run() override
        {
            QSslSocket socket;

            // QQ 邮箱 465 端口使用 SSL 直连
            socket.connectToHostEncrypted(m_service->m_smtpServer, m_service->m_smtpPort);
            if (!socket.waitForEncrypted(10000)) {
                emit m_service->sendFailed(QStringLiteral("Cannot connect to SMTP server (SSL)"));
                return;
            }

            // 读取服务器问候
            if (!m_service->sendSmtpCommand(&socket, QString(), 220)) {
                socket.close();
                emit m_service->sendFailed(QStringLiteral("SMTP greeting failed"));
                return;
            }

            // EHLO
            if (!m_service->sendSmtpCommand(&socket, QStringLiteral("EHLO netshare"), 250)) {
                socket.close();
                emit m_service->sendFailed(QStringLiteral("EHLO failed"));
                return;
            }

            // AUTH LOGIN
            if (!m_service->sendSmtpCommand(&socket, QStringLiteral("AUTH LOGIN"), 334)) {
                socket.close();
                emit m_service->sendFailed(QStringLiteral("AUTH LOGIN failed"));
                return;
            }

            // 发送用户名（Base64）
            QString encodedUser = m_service->m_senderEmail.toUtf8().toBase64();
            if (!m_service->sendSmtpCommand(&socket, encodedUser, 334)) {
                socket.close();
                emit m_service->sendFailed(QStringLiteral("SMTP auth user failed"));
                return;
            }

            // 发送密码（Base64）
            QString encodedPass = m_service->m_senderPassword.toUtf8().toBase64();
            if (!m_service->sendSmtpCommand(&socket, encodedPass, 235)) {
                socket.close();
                emit m_service->sendFailed(QStringLiteral("SMTP auth password failed"));
                return;
            }

            // MAIL FROM
            QString mailFrom = QStringLiteral("MAIL FROM:<%1>").arg(m_service->m_senderEmail);
            if (!m_service->sendSmtpCommand(&socket, mailFrom, 250)) {
                socket.close();
                emit m_service->sendFailed(QStringLiteral("MAIL FROM failed"));
                return;
            }

            // RCPT TO - 发送给所有管理员邮箱
            for (const QString& recipient : m_service->m_recipientEmails) {
                QString rcptTo = QStringLiteral("RCPT TO:<%1>").arg(recipient);
                if (!m_service->sendSmtpCommand(&socket, rcptTo, 250)) {
                    socket.close();
                    emit m_service->sendFailed(QStringLiteral("RCPT TO failed for %1").arg(recipient));
                    return;
                }
            }

            // DATA
            if (!m_service->sendSmtpCommand(&socket, QStringLiteral("DATA"), 354)) {
                socket.close();
                emit m_service->sendFailed(QStringLiteral("DATA command failed"));
                return;
            }

            // 发送邮件内容
            QString content = m_service->buildEmailContent(m_username, m_email, m_phone, m_machineId);
            socket.write(content.toUtf8());
            socket.write(QStringLiteral("\r\n.\r\n").toUtf8());
            socket.waitForBytesWritten(5000);

            if (!socket.waitForReadyRead(10000)) {
                socket.close();
                emit m_service->sendFailed(QStringLiteral("Email send timeout"));
                return;
            }

            QString response = QString::fromUtf8(socket.readAll());
            if (!response.startsWith(QStringLiteral("250"))) {
                socket.close();
                emit m_service->sendFailed(QStringLiteral("Email send failed: %1").arg(response));
                return;
            }

            // QUIT
            m_service->sendSmtpCommand(&socket, QStringLiteral("QUIT"), 221);
            socket.close();

            emit m_service->sendSuccess();
        }

    private:
        EmailService* m_service;
        QString m_username;
        QString m_email;
        QString m_phone;
        QString m_machineId;
    };

    QThreadPool::globalInstance()->start(new SmtpTask(this, username, userEmail, phone, machineId));
}
