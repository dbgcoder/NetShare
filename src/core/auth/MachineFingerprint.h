#ifndef MACHINEFINGERPRINT_H
#define MACHINEFINGERPRINT_H

#include <QObject>
#include <QString>
#include <QJsonObject>

class MachineFingerprint : public QObject
{
    Q_OBJECT
public:
    explicit MachineFingerprint(QObject* parent = nullptr);

    // 获取当前机器码（SHA-256 哈希前16字节 Base64）
    Q_INVOKABLE QString machineId() const;

    // 获取各硬件项原始值（用于调试/展示）
    Q_INVOKABLE QString baseboardSerial() const;
    Q_INVOKABLE QString biosSerial() const;
    Q_INVOKABLE QString cpuId() const;
    Q_INVOKABLE QString primaryMacAddress() const;

    // 硬件变更容错：计算当前硬件与注册时的匹配度（0-100分）
    // registeredComponents: 数据库中存储的 hardware_components JSON
    Q_INVOKABLE int matchScore(const QString& registeredComponents) const;

    // 获取当前各硬件项的哈希 JSON（用于存储到数据库）
    Q_INVOKABLE QString currentComponentsJson() const;

private:
    // 采集各硬件信息
    QString queryWmi(const QString& wmiClass, const QString& property) const;
    QString getCpuId() const;
    QString getPrimaryMacAddress() const;

    // 分项哈希（用于权重容错匹配）
    QString hashComponent(const QString& componentValue) const;

    // 缓存
    mutable QString m_cachedMachineId;
    mutable QString m_cachedBaseboard;
    mutable QString m_cachedBios;
    mutable QString m_cachedCpuId;
    mutable QString m_cachedMac;
    mutable bool m_initialized = false;

    void ensureInitialized() const;
};

#endif // MACHINEFINGERPRINT_H
