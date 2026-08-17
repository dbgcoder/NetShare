#ifndef AUTHSERVICE_H
#define AUTHSERVICE_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QTimer>

class DatabaseManager;
class MachineFingerprint;
class EmailService;

class AuthService : public QObject
{
    Q_OBJECT
public:
    explicit AuthService(DatabaseManager* db, MachineFingerprint* fingerprint,
                         EmailService* emailService, QObject* parent = nullptr);

    // 注册：验证输入 + 验证注册码 + 存入数据库
    Q_INVOKABLE bool registerUser(const QString& username,
                                   const QString& email, const QString& phone,
                                   const QString& registrationCode);
    // 发送注册信息到指定邮箱（用户点击"注册"时调用）
    Q_INVOKABLE bool sendRegistrationEmail(const QString& username,
                                            const QString& email, const QString& phone);
    // 试用模式：标记为试用用户，直接进入软件
    Q_INVOKABLE bool enterTrialMode();
    // 输入合法性校验
    Q_INVOKABLE bool validateEmail(const QString& email) const;
    Q_INVOKABLE bool validatePhone(const QString& phone) const;
    Q_INVOKABLE bool validateUsername(const QString& username) const;
    // 检查是否已注册（非试用模式）
    Q_INVOKABLE bool isRegistered() const;
    // 检查是否为试用模式
    Q_INVOKABLE bool isTrialMode() const;
    // 获取当前机器码（供 QML 展示）
    Q_INVOKABLE QString currentMachineId() const;
    // 获取注册用户信息（供"关于"页展示，仅用户名、注册日期、机器码）
    Q_INVOKABLE QString registeredUsername() const;
    Q_INVOKABLE QString registeredDate() const;
    Q_INVOKABLE QString registeredCode() const;
    Q_INVOKABLE bool clearRegistration();
    // 校验当前机器码与注册时是否匹配（启动时调用）
    Q_INVOKABLE bool verifyMachineMatch() const;
    // 检查 securityToken 是否有效（核心功能启动前调用）
    Q_INVOKABLE bool hasValidSecurityToken() const;
    // 生成 securityToken（启动时对已有用户调用）
    void generateSecurityToken();

signals:
    void registrationSuccess();
    void registrationFailed(const QString& reason);
    void emailSent();
    void emailSendFailed(const QString& reason);
    void registrationCompleted();  // 注册弹窗关闭时发射，通知 C++ 层启动网络服务
    void registrationCleared();    // 重新注册时发射，通知 QML 刷新 UI

private:
    DatabaseManager* m_db;
    MachineFingerprint* m_fingerprint;
    EmailService* m_emailService;

    // securityToken：随机生成的 QByteArray，防止简单修改 QML 属性绕过注册
    QByteArray m_securityToken;
    bool m_tokenValid = false;

    // 定时反调试检测
    QTimer m_antiDebugTimer;

    void startAntiDebugTimer();
    void onAntiDebugCheck();
};

#endif // AUTHSERVICE_H
