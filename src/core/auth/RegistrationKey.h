#ifndef REGISTRATIONKEY_H
#define REGISTRATIONKEY_H

#include <QString>

namespace RegistrationKey {

// 公钥 PEM 常量（由 KeyGenTool --generate 产出后填入）
// 占位符：首次生成密钥对后替换为实际公钥 PEM
extern const char* PUBLIC_KEY_PEM;

// 使用公钥验证注册码签名
// username, machineId: 注册信息
// code: Base64 编码的 RSA 签名（注册码）
// 返回 true 表示签名验证通过
bool verifyRegistrationCode(const QString& username, const QString& machineId,
                             const QString& code);

// 生成待签名的载荷字符串
// 格式：SHA-256(username|machineId) 的十六进制表示
QString generatePayload(const QString& username, const QString& machineId);

} // namespace RegistrationKey

#endif // REGISTRATIONKEY_H
