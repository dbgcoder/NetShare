#include "MachineFingerprint.h"
#include "AntiDebug.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0602  // Windows 8+ for GetAdaptersAddresses
#include <winsock2.h>
#include <windows.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <intrin.h>
#include <iphlpapi.h>
#include <tlhelp32.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "iphlpapi.lib")
#endif

MachineFingerprint::MachineFingerprint(QObject* parent)
    : QObject(parent)
{
}

void MachineFingerprint::ensureInitialized() const
{
    if (m_initialized)
        return;

    // 尝试从持久化缓存加载（C-16：避免每次启动都查询 WMI）
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(cacheDir);
    QString cachePath = cacheDir + QStringLiteral("/hwcache.conf");
    QSettings cache(cachePath, QSettings::IniFormat);

    // 缓存有效期：7天
    qint64 cacheAge = cache.value(QStringLiteral("Cache/Timestamp")).toLongLong();
    qint64 now = QDateTime::currentDateTime().toSecsSinceEpoch();
    bool cacheValid = (now - cacheAge) < 7 * 24 * 3600;

    if (cacheValid) {
        m_cachedBaseboard = cache.value(QStringLiteral("Hardware/Baseboard")).toString();
        m_cachedBios = cache.value(QStringLiteral("Hardware/Bios")).toString();
        m_cachedCpuId = cache.value(QStringLiteral("Hardware/CpuId")).toString();
        m_cachedMac = cache.value(QStringLiteral("Hardware/Mac")).toString();
    }

    // 若缓存无效或某项为空，重新采集
    if (!cacheValid || m_cachedBaseboard.isEmpty()) {
        m_cachedBaseboard = queryWmi(QStringLiteral("Win32_BaseBoard"), QStringLiteral("SerialNumber"));
    }
    if (!cacheValid || m_cachedBios.isEmpty()) {
        m_cachedBios = queryWmi(QStringLiteral("Win32_BIOS"), QStringLiteral("SerialNumber"));
    }
    if (!cacheValid || m_cachedCpuId.isEmpty()) {
        m_cachedCpuId = getCpuId();
    }
    if (!cacheValid || m_cachedMac.isEmpty()) {
        m_cachedMac = getPrimaryMacAddress();
    }

    // 更新持久化缓存
    cache.setValue(QStringLiteral("Hardware/Baseboard"), m_cachedBaseboard);
    cache.setValue(QStringLiteral("Hardware/Bios"), m_cachedBios);
    cache.setValue(QStringLiteral("Hardware/CpuId"), m_cachedCpuId);
    cache.setValue(QStringLiteral("Hardware/Mac"), m_cachedMac);
    cache.setValue(QStringLiteral("Cache/Timestamp"), now);
    cache.sync();

    // 拼接硬件信息并计算机器码
    // 降级策略（C-13）：跳过空值项，仅使用非空项计算哈希
    QStringList nonEmptyParts;
    if (!m_cachedBaseboard.isEmpty()) nonEmptyParts << m_cachedBaseboard;
    if (!m_cachedBios.isEmpty()) nonEmptyParts << m_cachedBios;
    if (!m_cachedCpuId.isEmpty()) nonEmptyParts << m_cachedCpuId;
    if (!m_cachedMac.isEmpty()) nonEmptyParts << m_cachedMac;

    QString combined;
    if (nonEmptyParts.isEmpty()) {
        combined = QStringLiteral("UNKNOWN_MACHINE");
    } else {
        combined = nonEmptyParts.join(QStringLiteral("|"));
    }

    QByteArray hash = QCryptographicHash::hash(combined.toUtf8(), QCryptographicHash::Sha256);
    // 格式化为 XXXX-XXXX-XXXX-XXXX（取前8字节=16个十六进制字符，业界标准格式）
    QString hex = QString::fromLatin1(hash.left(8).toHex()).toUpper();
    m_cachedMachineId = QStringLiteral("%1-%2-%3-%4")
        .arg(hex.mid(0, 4), hex.mid(4, 4), hex.mid(8, 4), hex.mid(12, 4));

    m_initialized = true;
}

QString MachineFingerprint::machineId() const
{
    ensureInitialized();
    // 分散式反调试：机器码获取时检测调试器
    if (AntiDebug::isDebuggerPresent()) {
        return QStringLiteral("TAMPERED");
    }
    return m_cachedMachineId;
}

QString MachineFingerprint::baseboardSerial() const
{
    ensureInitialized();
    return m_cachedBaseboard;
}

QString MachineFingerprint::biosSerial() const
{
    ensureInitialized();
    return m_cachedBios;
}

QString MachineFingerprint::cpuId() const
{
    ensureInitialized();
    return m_cachedCpuId;
}

QString MachineFingerprint::primaryMacAddress() const
{
    ensureInitialized();
    return m_cachedMac;
}

int MachineFingerprint::matchScore(const QString& registeredComponents) const
{
    ensureInitialized();

    QJsonDocument doc = QJsonDocument::fromJson(registeredComponents.toUtf8());
    if (doc.isNull() || !doc.isObject())
        return 0;

    QJsonObject registered = doc.object();
    QJsonObject current;
    current[QStringLiteral("baseboard")] = hashComponent(m_cachedBaseboard);
    current[QStringLiteral("bios")] = hashComponent(m_cachedBios);
    current[QStringLiteral("cpu")] = hashComponent(m_cachedCpuId);
    current[QStringLiteral("mac")] = hashComponent(m_cachedMac);

    // 权重：主板40、BIOS 20、CPU 20、网卡20
    struct WeightItem {
        QString key;
        int weight;
    };

    QVector<WeightItem> items = {
        {QStringLiteral("baseboard"), 40},
        {QStringLiteral("bios"), 20},
        {QStringLiteral("cpu"), 20},
        {QStringLiteral("mac"), 20}
    };

    int totalScore = 0;
    for (const auto& item : items) {
        if (registered.contains(item.key) && current.contains(item.key)) {
            if (registered[item.key].toString() == current[item.key].toString()) {
                totalScore += item.weight;
            }
        }
    }

    return totalScore;
}

QString MachineFingerprint::currentComponentsJson() const
{
    ensureInitialized();

    QJsonObject obj;
    obj[QStringLiteral("baseboard")] = hashComponent(m_cachedBaseboard);
    obj[QStringLiteral("bios")] = hashComponent(m_cachedBios);
    obj[QStringLiteral("cpu")] = hashComponent(m_cachedCpuId);
    obj[QStringLiteral("mac")] = hashComponent(m_cachedMac);

    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString MachineFingerprint::hashComponent(const QString& componentValue) const
{
    if (componentValue.isEmpty())
        return QStringLiteral("empty");
    QByteArray hash = QCryptographicHash::hash(componentValue.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex());
}

#ifdef Q_OS_WIN
QString MachineFingerprint::queryWmi(const QString& wmiClass, const QString& property) const
{
    HRESULT hres;

    // 初始化 COM
    hres = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hres) && hres != RPC_E_CHANGED_MODE) {
        qWarning() << "Failed to initialize COM library";
        return QStringLiteral("");
    }

    // 设置 COM 安全级别
    hres = CoInitializeSecurity(
        nullptr, -1, nullptr, nullptr,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr, EOAC_NONE, nullptr);

    if (FAILED(hres) && hres != RPC_E_TOO_LATE) {
        qWarning() << "Failed to initialize security";
        CoUninitialize();
        return QStringLiteral("");
    }

    // 获取 WMI 定位器
    IWbemLocator* pLoc = nullptr;
    hres = CoCreateInstance(
        CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, reinterpret_cast<LPVOID*>(&pLoc));

    if (FAILED(hres)) {
        qWarning() << "Failed to create IWbemLocator";
        CoUninitialize();
        return QStringLiteral("");
    }

    // 连接 WMI
    IWbemServices* pSvc = nullptr;
    hres = pLoc->ConnectServer(
        _bstr_t(L"ROOT\\CIMV2"),
        nullptr, nullptr, nullptr, 0, nullptr, nullptr, &pSvc);

    if (FAILED(hres)) {
        qWarning() << "Failed to connect to WMI";
        pLoc->Release();
        CoUninitialize();
        return QStringLiteral("");
    }

    // 设置安全级别
    hres = CoSetProxyBlanket(
        pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr, EOAC_NONE);

    if (FAILED(hres)) {
        qWarning() << "Failed to set proxy blanket";
        pSvc->Release();
        pLoc->Release();
        CoUninitialize();
        return QStringLiteral("");
    }

    // 执行 WQL 查询
    QString wql = QStringLiteral("SELECT %1 FROM %2").arg(property, wmiClass);
    IEnumWbemClassObject* pEnumerator = nullptr;
    hres = pSvc->ExecQuery(
        bstr_t("WQL"),
        bstr_t(wql.toStdWString().c_str()),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr, &pEnumerator);

    QString result;

    if (SUCCEEDED(hres)) {
        IWbemClassObject* pclsObj = nullptr;
        ULONG uReturn = 0;

        while (pEnumerator) {
            hres = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
            if (uReturn == 0)
                break;

            VARIANT vtProp;
            hres = pclsObj->Get(property.toStdWString().c_str(), 0, &vtProp, nullptr, nullptr);

            if (SUCCEEDED(hres) && vtProp.vt == VT_BSTR) {
                result = QString::fromWCharArray(vtProp.bstrVal);
                VariantClear(&vtProp);
            }

            pclsObj->Release();

            if (!result.isEmpty())
                break;
        }

        pEnumerator->Release();
    }

    pSvc->Release();
    pLoc->Release();
    CoUninitialize();

    return result.trimmed();
}

QString MachineFingerprint::getCpuId() const
{
    int info[4] = {};

    // CPUID with EAX=1: 获取处理器特征
    __cpuid(info, 1);

    // 拼接 EAX, EBX, ECX, EDX
    QString cpuId = QStringLiteral("%1-%2-%3-%4")
        .arg(info[0], 8, 16, QLatin1Char('0'))
        .arg(info[1], 8, 16, QLatin1Char('0'))
        .arg(info[2], 8, 16, QLatin1Char('0'))
        .arg(info[3], 8, 16, QLatin1Char('0'));

    return cpuId.toUpper();
}

QString MachineFingerprint::getPrimaryMacAddress() const
{
    ULONG bufLen = 0;
    GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, nullptr, &bufLen);
    if (bufLen == 0)
        return QStringLiteral("");

    QByteArray buffer(bufLen, 0);
    PIP_ADAPTER_ADDRESSES addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());

    ULONG result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS,
        nullptr, addresses, &bufLen);
    if (result != ERROR_SUCCESS)
        return QStringLiteral("");

    // 遍历网卡，找第一个物理网卡（过滤虚拟网卡）
    QStringList virtualPrefixes = {
        QStringLiteral("VMware"),
        QStringLiteral("VirtualBox"),
        QStringLiteral("Hyper-V"),
        QStringLiteral("Tunnel"),
        QStringLiteral("Loopback"),
        QStringLiteral("Bluetooth"),
        QStringLiteral("WSL")
    };

    for (PIP_ADAPTER_ADDRESSES addr = addresses; addr; addr = addr->Next) {
        // 跳过非以太网和无线网卡
        if (addr->PhysicalAddressLength == 0)
            continue;

        QString friendlyName = QString::fromWCharArray(addr->FriendlyName);

        // 过滤虚拟网卡
        bool isVirtual = false;
        for (const auto& prefix : virtualPrefixes) {
            if (friendlyName.contains(prefix, Qt::CaseInsensitive)) {
                isVirtual = true;
                break;
            }
        }
        if (isVirtual)
            continue;

        // 格式化 MAC 地址
        QStringList macParts;
        for (ULONG i = 0; i < addr->PhysicalAddressLength; i++) {
            macParts.append(QStringLiteral("%1")
                .arg(addr->PhysicalAddress[i], 2, 16, QLatin1Char('0'))
                .toUpper());
        }
        return macParts.join(QStringLiteral(":"));
    }

    return QStringLiteral("");
}

#else // Non-Windows platforms (stub)
QString MachineFingerprint::queryWmi(const QString&, const QString&) const
{
    return QStringLiteral("");
}

QString MachineFingerprint::getCpuId() const
{
    return QStringLiteral("");
}

QString MachineFingerprint::getPrimaryMacAddress() const
{
    return QStringLiteral("");
}
#endif
