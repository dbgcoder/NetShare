#include "AntiDebug.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#endif

// 反调试代码仅在 Release 构建中启用
// Debug 构建中完全禁用，方便开发调试
#ifdef NDEBUG

namespace AntiDebug {

bool isDebuggerPresent()
{
#ifdef Q_OS_WIN
    if (::IsDebuggerPresent())
        return true;

    BOOL debugged = FALSE;
    ::CheckRemoteDebuggerPresent(::GetCurrentProcess(), &debugged);
    if (debugged)
        return true;
#endif
    return false;
}

bool isNtDebugged()
{
#ifdef Q_OS_WIN
    // 动态加载 ntdll.dll，避免静态链接引起注意
    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (!ntdll)
        return false;

    typedef NTSTATUS(NTAPI* NtQueryInformationProcessFunc)(
        HANDLE, ULONG, PVOID, ULONG, PULONG);

    auto func = reinterpret_cast<NtQueryInformationProcessFunc>(
        ::GetProcAddress(ntdll, "NtQueryInformationProcess"));
    if (!func)
        return false;

    // ProcessDebugPort = 7
    DWORD_PTR debugPort = 0;
    NTSTATUS status = func(::GetCurrentProcess(), 7, &debugPort, sizeof(debugPort), nullptr);
    if (status == 0 && debugPort != 0)
        return true;

    // ProcessDebugObjectHandle = 30
    HANDLE debugObject = nullptr;
    status = func(::GetCurrentProcess(), 30, &debugObject, sizeof(debugObject), nullptr);
    if (status == 0 && debugObject != nullptr)
        return true;

#endif
    return false;
}

bool isHardwareBreakpointSet()
{
#ifdef Q_OS_WIN
    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;

    if (::GetThreadContext(::GetCurrentThread(), &ctx)) {
        if (ctx.Dr0 != 0 || ctx.Dr1 != 0 || ctx.Dr2 != 0 || ctx.Dr3 != 0)
            return true;
    }
#endif
    return false;
}

bool isTimingAnomaly()
{
#ifdef Q_OS_WIN
    LARGE_INTEGER freq, start, end;
    ::QueryPerformanceFrequency(&freq);
    ::QueryPerformanceCounter(&start);

    // 执行一段可预测的空循环
    volatile int dummy = 0;
    for (int i = 0; i < 1000; i++) {
        dummy += i;
    }

    ::QueryPerformanceCounter(&end);

    double elapsed = static_cast<double>(end.QuadPart - start.QuadPart) / freq.QuadPart;

    // 正常情况下这段循环耗时极短（微秒级）
    // 如果被调试器单步执行，耗时会显著增加
    // 阈值设为 0.1 秒（远超正常值）
    if (elapsed > 0.1)
        return true;
#endif
    return false;
}

bool isParentSuspicious()
{
#ifdef Q_OS_WIN
    HANDLE hSnapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return false;

    DWORD currentPid = ::GetCurrentProcessId();
    DWORD parentPid = 0;

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);

    // 找到当前进程的父进程
    if (::Process32FirstW(hSnapshot, &pe)) {
        do {
            if (pe.th32ProcessID == currentPid) {
                parentPid = pe.th32ParentProcessID;
                break;
            }
        } while (::Process32NextW(hSnapshot, &pe));
    }

    ::CloseHandle(hSnapshot);

    if (parentPid == 0)
        return false;

    // 检查父进程名是否为 explorer.exe
    hSnapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return false;

    pe.dwSize = sizeof(pe);
    bool parentIsExplorer = false;

    if (::Process32FirstW(hSnapshot, &pe)) {
        do {
            if (pe.th32ProcessID == parentPid) {
                QString parentName = QString::fromWCharArray(pe.szExeFile).toLower();
                parentIsExplorer = (parentName == QStringLiteral("explorer.exe"));
                break;
            }
        } while (::Process32NextW(hSnapshot, &pe));
    }

    ::CloseHandle(hSnapshot);

    // 父进程不是 explorer.exe 则可疑
    return !parentIsExplorer;
#else
    return false;
#endif
}

bool isDebugEnvironment()
{
    int hitCount = 0;

    if (isDebuggerPresent()) hitCount++;
    if (isNtDebugged()) hitCount++;
    if (isHardwareBreakpointSet()) hitCount++;
    if (isTimingAnomaly()) hitCount++;
    if (isParentSuspicious()) hitCount++;

    // 至少2项命中才判定为调试环境（减少误报）
    return hitCount >= 2;
}

bool verifyFunctionIntegrity(const void* funcPtr, size_t funcSize,
                              const QByteArray& expectedHash)
{
    QByteArray actualHash = QCryptographicHash::hash(
        QByteArray(reinterpret_cast<const char*>(funcPtr), funcSize),
        QCryptographicHash::Sha256);

    return actualHash == expectedHash;
}

bool verifyPeIntegrity()
{
#ifdef Q_OS_WIN
    // 获取当前 EXE 路径
    WCHAR path[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, path, MAX_PATH);

    QFile file(QString::fromWCharArray(path));
    if (!file.open(QIODevice::ReadOnly))
        return false;

    // 读取整个文件并计算哈希
    QByteArray data = file.readAll();
    QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);

    // 读取编译时保存的哈希值（由 CMake post-build 步骤生成）
    // 文件路径：<exe路径>.pe_hash.txt
    QFileInfo exeInfo(QString::fromWCharArray(path));
    QString hashPath = exeInfo.absoluteFilePath() + QStringLiteral(".pe_hash.txt");
    QFile hashFile(hashPath);
    if (!hashFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // 哈希文件不存在（开发模式），跳过校验
        return true;
    }

    QByteArray expectedHash = hashFile.readLine().trimmed();
    if (expectedHash.isEmpty()) {
        return true;
    }

    return hash.toHex() == expectedHash;
#else
    return true;
#endif
}

} // namespace AntiDebug

#else // !NDEBUG - Debug 构建中完全禁用反调试

namespace AntiDebug {

bool isDebuggerPresent() { return false; }
bool isNtDebugged() { return false; }
bool isHardwareBreakpointSet() { return false; }
bool isTimingAnomaly() { return false; }
bool isParentSuspicious() { return false; }
bool isDebugEnvironment() { return false; }
bool verifyFunctionIntegrity(const void*, size_t, const QByteArray&) { return true; }
bool verifyPeIntegrity() { return true; }

} // namespace AntiDebug

#endif // NDEBUG
