#include "AuthService.h"
#include "MachineFingerprint.h"
#include "RegistrationKey.h"
#include "EmailService.h"
#include "AntiDebug.h"
#include "DatabaseManager.h"

#include <QRegularExpression>
#include <QDateTime>
#include <QRandomGenerator>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#include <bcrypt.h>
#endif

AuthService::AuthService(DatabaseManager* db, MachineFingerprint* fingerprint,
                         EmailService* emailService, QObject* parent)
    : QObject(parent)
    , m_db(db)
    , m_fingerprint(fingerprint)
    , m_emailService(emailService)
{
    // 连接 EmailService 信号
    connect(m_emailService, &EmailService::sendSuccess, this, &AuthService::emailSent);
    connect(m_emailService, &EmailService::sendFailed, this, &AuthService::emailSendFailed);

    // 启动定时反调试检测（仅 Release 构建）
    startAntiDebugTimer();
}

bool AuthService::validateEmail(const QString& email) const
{
    QRegularExpression re(QStringLiteral("^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}$"));
    return re.match(email).hasMatch();
}

bool AuthService::validatePhone(const QString& phone) const
{
    QRegularExpression re(QStringLiteral("^1[3-9]\\d{9}$"));
    return re.match(phone).hasMatch();
}

bool AuthService::validateUsername(const QString& username) const
{
    // 支持：中文（至少2个字符）、字母、数字、下划线，总长度2-20
    // 使用 PCRE2 的 \x{} 语法表示 Unicode 范围
    static const QRegularExpression re(
        QString::fromLatin1("^[\\x{4e00}-\\x{9fff}A-Za-z0-9_]{2,20}$"));
    return re.match(username).hasMatch();
}

bool AuthService::registerUser(const QString& username, const QString& email,
                                const QString& phone, const QString& registrationCode)
{
    // 反调试检测：检测到调试环境则静默返回 false
    if (AntiDebug::isDebugEnvironment()) {
        // 不提示攻击者，返回伪造的"通过"但内部标记无效
        emit registrationFailed(QStringLiteral("Internal error"));
        return false;
    }

    // 校验输入合法性
    if (!validateUsername(username)) {
        emit registrationFailed(QStringLiteral("Invalid username: 2-20 characters (Chinese, letters, numbers, underscore)"));
        return false;
    }
    if (!validateEmail(email)) {
        emit registrationFailed(QStringLiteral("Invalid email format"));
        return false;
    }
    if (!validatePhone(phone)) {
        emit registrationFailed(QStringLiteral("Invalid phone number"));
        return false;
    }
    if (registrationCode.isEmpty()) {
        emit registrationFailed(QStringLiteral("Registration code is required"));
        return false;
    }

    // 获取当前机器码
    QString machineId = m_fingerprint->machineId();

    // 验证注册码签名
    if (!RegistrationKey::verifyRegistrationCode(username, machineId, registrationCode)) {
        emit registrationFailed(QStringLiteral("Invalid registration code"));
        return false;
    }

    // 检查用户名是否已存在
    QVariant existing = m_db->queryValue(
        QStringLiteral("SELECT id FROM users WHERE username = ?"), {username});
    if (existing.isValid()) {
        emit registrationFailed(QStringLiteral("Username already exists"));
        return false;
    }

    QString currentDate = QDateTime::currentDateTime().toString(Qt::ISODate);
    QString componentsJson = m_fingerprint->currentComponentsJson();

    // 若当前为试用用户则 UPDATE 现有记录
    QVariant trialUser = m_db->queryValue(
        QStringLiteral("SELECT id FROM users WHERE is_trial = 1 AND machine_id = ?"),
        {machineId});

    bool success = false;
    if (trialUser.isValid()) {
        // 更新试用用户为正式注册用户
        success = m_db->execute(
            QStringLiteral("UPDATE users SET username = ?, email = ?, phone = ?, "
                           "registration_code = ?, is_trial = 0, registered_at = ?, "
                           "hardware_components = ?, updated_at = ? "
                           "WHERE id = ?"),
            {username, email, phone, registrationCode, currentDate,
             componentsJson, currentDate, trialUser});
    } else {
        // 新增正式注册用户
        success = m_db->execute(
            QStringLiteral("INSERT INTO users (username, email, phone, machine_id, "
                           "hardware_components, registration_code, is_trial, registered_at) "
                           "VALUES (?, ?, ?, ?, ?, ?, 0, ?)"),
            {username, email, phone, machineId, componentsJson, registrationCode, currentDate});
    }

    if (success) {
        generateSecurityToken();
        emit registrationSuccess();
        return true;
    } else {
        emit registrationFailed(QStringLiteral("Database error"));
        return false;
    }
}

bool AuthService::sendRegistrationEmail(const QString& username,
                                          const QString& email, const QString& phone)
{
    // 校验输入
    if (!validateUsername(username)) {
        emit emailSendFailed(QStringLiteral("Invalid username"));
        return false;
    }
    if (!validateEmail(email)) {
        emit emailSendFailed(QStringLiteral("Invalid email"));
        return false;
    }
    if (!validatePhone(phone)) {
        emit emailSendFailed(QStringLiteral("Invalid phone number"));
        return false;
    }

    QString machineId = m_fingerprint->machineId();
    m_emailService->sendRegistrationInfo(username, email, phone, machineId);
    return true;
}

bool AuthService::enterTrialMode()
{
    // 反调试检测
    if (AntiDebug::isDebugEnvironment()) {
        return false;
    }

    QString machineId = m_fingerprint->machineId();
    QString componentsJson = m_fingerprint->currentComponentsJson();

    // 检查是否已有该机器的记录
    QVariant existing = m_db->queryValue(
        QStringLiteral("SELECT id FROM users WHERE machine_id = ?"), {machineId});

    if (existing.isValid()) {
        // 已有记录，检查是否已注册
        QVariant isTrial = m_db->queryValue(
            QStringLiteral("SELECT is_trial FROM users WHERE machine_id = ?"), {machineId});
        if (isTrial.isValid() && isTrial.toInt() == 0) {
            // 已注册，无需进入试用模式
            generateSecurityToken();
            return true;
        }
        // 已是试用用户，直接返回
        generateSecurityToken();
        return true;
    }

    // 创建试用用户记录
    bool success = m_db->execute(
        QStringLiteral("INSERT INTO users (username, email, phone, machine_id, "
                       "hardware_components, is_trial) VALUES ('TrialUser', '', '', ?, ?, 1)"),
        {machineId, componentsJson});

    if (success) {
        generateSecurityToken();
        return true;
    }

    return false;
}

bool AuthService::isRegistered() const
{
    // 反调试检测：检测到调试则返回伪造结果
    if (AntiDebug::isDebugEnvironment()) {
        return false;
    }

    QVariant result = m_db->queryValue(
        QStringLiteral("SELECT id FROM users WHERE is_trial = 0 LIMIT 1"));
    return result.isValid();
}

bool AuthService::isTrialMode() const
{
    if (AntiDebug::isDebugEnvironment()) {
        return true;  // 调试环境下伪装为试用模式
    }

    // 有试用记录且无正式注册记录
    QVariant trial = m_db->queryValue(
        QStringLiteral("SELECT id FROM users WHERE is_trial = 1 LIMIT 1"));
    QVariant registered = m_db->queryValue(
        QStringLiteral("SELECT id FROM users WHERE is_trial = 0 LIMIT 1"));

    return trial.isValid() && !registered.isValid();
}

QString AuthService::currentMachineId() const
{
    // 反调试检测：检测到调试则返回伪造机器码
    if (AntiDebug::isDebugEnvironment()) {
        return QStringLiteral("INVALID_MACHINE_ID");
    }

    return m_fingerprint->machineId();
}

QString AuthService::registeredUsername() const
{
    QVariant result = m_db->queryValue(
        QStringLiteral("SELECT username FROM users WHERE is_trial = 0 LIMIT 1"));
    return result.isValid() ? result.toString() : QString();
}

QString AuthService::registeredDate() const
{
    QVariant result = m_db->queryValue(
        QStringLiteral("SELECT registered_at FROM users WHERE is_trial = 0 LIMIT 1"));
    return result.isValid() ? result.toString() : QString();
}

QString AuthService::registeredCode() const
{
    QVariant result = m_db->queryValue(
        QStringLiteral("SELECT registration_code FROM users WHERE is_trial = 0 LIMIT 1"));
    return result.isValid() ? result.toString() : QString();
}

bool AuthService::clearRegistration()
{
    bool success = m_db->execute(
        QStringLiteral("DELETE FROM users"));
    if (success) {
        m_securityToken.clear();
        m_tokenValid = false;
        emit registrationCleared();
    }
    return success;
}

bool AuthService::verifyMachineMatch() const
{
    if (AntiDebug::isDebugEnvironment()) {
        return false;
    }

    QVariant components = m_db->queryValue(
        QStringLiteral("SELECT hardware_components FROM users WHERE is_trial = 0 LIMIT 1"));

    if (!components.isValid())
        return false;

    int score = m_fingerprint->matchScore(components.toString());
    return score >= 60;
}

bool AuthService::hasValidSecurityToken() const
{
    return m_tokenValid && !m_securityToken.isEmpty();
}

void AuthService::generateSecurityToken()
{
    // 使用 CSPRNG 生成 securityToken
    m_securityToken.resize(32);
#ifdef Q_OS_WIN
    // Windows: 使用 BCryptGenRandom（密码学安全随机数）
    NTSTATUS status = BCryptGenRandom(
        nullptr, reinterpret_cast<PUCHAR>(m_securityToken.data()), 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status != 0) {
        // 回退到 QRandomGenerator::securelySeeded() 手动填充字节
        QRandomGenerator rng = QRandomGenerator::securelySeeded();
        for (int i = 0; i < 32; ++i) {
            m_securityToken[i] = static_cast<char>(rng.generate() & 0xFF);
        }
    }
#else
    {
        QRandomGenerator rng = QRandomGenerator::securelySeeded();
        for (int i = 0; i < 32; ++i) {
            m_securityToken[i] = static_cast<char>(rng.generate() & 0xFF);
        }
    }
#endif
    m_tokenValid = true;
}

void AuthService::startAntiDebugTimer()
{
#ifdef NDEBUG
    // 每 30 秒执行一次反调试检测
    connect(&m_antiDebugTimer, &QTimer::timeout, this, &AuthService::onAntiDebugCheck);
    m_antiDebugTimer.start(30000);
#endif
}

void AuthService::onAntiDebugCheck()
{
#ifdef NDEBUG
    if (AntiDebug::isDebugEnvironment()) {
        // 静默降级：使 securityToken 失效
        m_tokenValid = false;
        m_securityToken.clear();
    }
#endif
}
