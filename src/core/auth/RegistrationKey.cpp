#include "RegistrationKey.h"
#include "AntiDebug.h"

#include <QCryptographicHash>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace RegistrationKey {

// 公钥 PEM 常量
// 构建步骤：
//   1. 运行 KeyGenTool --generate --private-key private.pem --public-key public.pem
//   2. 将 public.pem 的内容替换下面的占位符
//   3. private.pem 由管理员离线保管，绝不出现在代码或安装包中
//
// 注意：占位符公钥会导致 verifyRegistrationCode() 始终返回 false，
//       发布前必须替换为真实公钥
const char* PUBLIC_KEY_PEM =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA46gMfEIYKUH/yrYFDk8z\n"
    "4PgkDQGL2lRXjiGlhtzZPuXRoZwMQgLZ3L33Zrs0gckYK8ZUflFEyADkS33ZOtMb\n"
    "zB0Lb3XoiZrtNcQ2WlF5rf8QQW77AY4HpCadg2aX0RRVE2mqDpjM8QnivgOUbLoF\n"
    "A4fncCkeATiWP/MRtBW8rzyfV4En6FGuA+EfEdyBEc4X/8rb56oZLy1R8Bjc4N3H\n"
    "JCj+a+nRcerqHjTiUeK48SKDPb81NpztrmKBrLNJOnQG+LN3BW2E5WkBQxgHmeoW\n"
    "IQ53OA59Xq2uoNQq6hOe5hPFn4iUAiFtgri2g/1jO5vMeXyNz6eNbjwoWZeWm6jf\n"
    "xQIDAQAB\n"
    "-----END PUBLIC KEY-----\n";

QString generatePayload(const QString& username, const QString& machineId)
{
    QString raw = QStringLiteral("%1|%2").arg(username, machineId);
    QByteArray hash = QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex());
}

#ifdef Q_OS_WIN
bool verifyRegistrationCode(const QString& username, const QString& machineId,
                             const QString& code)
{
    // 分散式反调试检测：在关键验证函数中嵌入独立检测点
    // 即使攻击者绕过 AuthService 中的集中检测，此处仍可拦截
    if (AntiDebug::isDebuggerPresent() || AntiDebug::isTimingAnomaly()) {
        return false;
    }

    // 1. 生成载荷哈希
    QString payload = generatePayload(username, machineId);
    QByteArray payloadBytes = payload.toUtf8();

    // 2. 解码 Base64 注册码为签名数据
    QByteArray signature = QByteArray::fromBase64(code.toLatin1());
    if (signature.isEmpty()) {
        qWarning() << "RegistrationKey: Invalid base64 registration code";
        return false;
    }

    // 3. 从 SubjectPublicKeyInfo DER 中提取 modulus 和 exponent
    // 正确解析 PEM DER 结构，构建 BCRYPT_RSAPUBLIC_BLOB
    QByteArray derKey;
    {
        QString pemStr = QString::fromUtf8(PUBLIC_KEY_PEM);
        pemStr.remove(QStringLiteral("-----BEGIN PUBLIC KEY-----"));
        pemStr.remove(QStringLiteral("-----END PUBLIC KEY-----"));
        pemStr.remove('\n').remove('\r').remove(' ');
        derKey = QByteArray::fromBase64(pemStr.toLatin1());
    }

    if (derKey.isEmpty()) {
        qWarning() << "RegistrationKey: Failed to parse public key PEM";
        return false;
    }

    // 解析 SubjectPublicKeyInfo DER
    int pos = 0;
    // 外层 SEQUENCE
    if (static_cast<unsigned char>(derKey[pos]) != 0x30) return false;
    pos++;
    if (static_cast<unsigned char>(derKey[pos]) & 0x80) {
        pos += 1 + (static_cast<unsigned char>(derKey[pos]) & 0x7F);
    } else {
        pos++;
    }

    // AlgorithmIdentifier SEQUENCE (skip)
    if (static_cast<unsigned char>(derKey[pos]) != 0x30) return false;
    pos++;
    int algoLen = 0;
    if (static_cast<unsigned char>(derKey[pos]) & 0x80) {
        int lenBytes = (static_cast<unsigned char>(derKey[pos]) & 0x7F);
        pos++;
        for (int i = 0; i < lenBytes; i++)
            algoLen = (algoLen << 8) | static_cast<unsigned char>(derKey[pos++]);
    } else {
        algoLen = static_cast<unsigned char>(derKey[pos++]);
    }
    pos += algoLen;

    // BIT STRING
    if (static_cast<unsigned char>(derKey[pos]) != 0x03) return false;
    pos++;
    if (static_cast<unsigned char>(derKey[pos]) & 0x80) {
        pos += 1 + (static_cast<unsigned char>(derKey[pos]) & 0x7F);
    } else {
        pos++;
    }
    pos++; // skip unused bits byte

    // RSA 公钥 SEQUENCE { INTEGER modulus, INTEGER exponent }
    pos++; // skip SEQUENCE tag (0x30)
    if (static_cast<unsigned char>(derKey[pos]) & 0x80) {
        pos += 1 + (static_cast<unsigned char>(derKey[pos]) & 0x7F);
    } else {
        pos++;
    }

    // INTEGER modulus
    if (static_cast<unsigned char>(derKey[pos]) != 0x02) return false;
    pos++;
    int modLen = 0;
    if (static_cast<unsigned char>(derKey[pos]) & 0x80) {
        int lenBytes = (static_cast<unsigned char>(derKey[pos]) & 0x7F);
        pos++;
        for (int i = 0; i < lenBytes; i++)
            modLen = (modLen << 8) | static_cast<unsigned char>(derKey[pos++]);
    } else {
        modLen = static_cast<unsigned char>(derKey[pos++]);
    }
    QByteArray modulus = derKey.mid(pos, modLen);
    pos += modLen;
    if (modulus.size() > 0 && modulus[0] == '\0') modulus = modulus.mid(1);

    // INTEGER exponent
    if (static_cast<unsigned char>(derKey[pos]) != 0x02) return false;
    pos++;
    int expLen = 0;
    if (static_cast<unsigned char>(derKey[pos]) & 0x80) {
        int lenBytes = (static_cast<unsigned char>(derKey[pos]) & 0x7F);
        pos++;
        for (int i = 0; i < lenBytes; i++)
            expLen = (expLen << 8) | static_cast<unsigned char>(derKey[pos++]);
    } else {
        expLen = static_cast<unsigned char>(derKey[pos++]);
    }
    QByteArray exponent = derKey.mid(pos, expLen);
    if (exponent.size() > 0 && exponent[0] == '\0') exponent = exponent.mid(1);

    // 3b. 构建 BCRYPT_RSAPUBLIC_BLOB
    QByteArray bcryptBlob;
    BCRYPT_RSAKEY_BLOB header;
    header.Magic = BCRYPT_RSAPUBLIC_MAGIC;
    header.BitLength = modulus.size() * 8;
    header.cbPublicExp = exponent.size();
    header.cbModulus = modulus.size();
    header.cbPrime1 = 0;
    header.cbPrime2 = 0;

    bcryptBlob.append(reinterpret_cast<const char*>(&header), sizeof(header));
    bcryptBlob.append(exponent);
    bcryptBlob.append(modulus);

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_RSA_ALGORITHM, nullptr, 0);
    if (status != 0) {
        qWarning() << "RegistrationKey: BCryptOpenAlgorithmProvider failed";
        return false;
    }

    BCRYPT_KEY_HANDLE hKey = nullptr;
    status = BCryptImportKeyPair(hAlg, nullptr, BCRYPT_RSAPUBLIC_BLOB,
                                 &hKey,
                                 reinterpret_cast<PUCHAR>(bcryptBlob.data()),
                                 bcryptBlob.size(), 0);
    if (status != 0) {
        qWarning() << "RegistrationKey: BCryptImportKeyPair failed, status=" << status;
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    // 4. 计算载荷的 SHA-256 哈希
    QByteArray hashValue = QCryptographicHash::hash(payloadBytes, QCryptographicHash::Sha256);

    // 5. 使用 BCrypt 验证 PKCS1 签名
    BCRYPT_PKCS1_PADDING_INFO paddingInfo = {};
    paddingInfo.pszAlgId = BCRYPT_SHA256_ALGORITHM;

    status = BCryptVerifySignature(
        hKey,
        &paddingInfo,
        reinterpret_cast<PUCHAR>(const_cast<char*>(hashValue.constData())),
        hashValue.size(),
        reinterpret_cast<PUCHAR>(const_cast<char*>(signature.constData())),
        signature.size(),
        BCRYPT_PAD_PKCS1);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (status == 0) {
        return true;
    } else {
        qWarning() << "RegistrationKey: Signature verification failed, status=" << status;
        return false;
    }
}
#else // Non-Windows: 使用 OpenSSL 命令行验证
bool verifyRegistrationCode(const QString& username, const QString& machineId,
                             const QString& code)
{
    Q_UNUSED(username)
    Q_UNUSED(machineId)
    Q_UNUSED(code)
    qWarning() << "RegistrationKey: Non-Windows verification not implemented";
    return false;
}
#endif

} // namespace RegistrationKey
