#ifndef ANTIDEBUG_H
#define ANTIDEBUG_H

#include <QByteArray>

namespace AntiDebug {

// 运行时反调试检测（多维度）
bool isDebuggerPresent();           // IsDebuggerPresent + CheckRemoteDebuggerPresent
bool isNtDebugged();                // NtQueryInformationProcess(ProcessDebugPort)
bool isHardwareBreakpointSet();     // 读取 Dr0-Dr3 寄存器
bool isTimingAnomaly();             // QueryPerformanceCounter 时间差检测
bool isParentSuspicious();          // 父进程非 explorer.exe

// 综合检测：调用以上所有方法，至少2项命中才返回 true（减少误报）
bool isDebugEnvironment();

// 反篡改检测
bool verifyFunctionIntegrity(const void* funcPtr, size_t funcSize,
                              const QByteArray& expectedHash);  // 内存 SHA-256

// PE 文件自身哈希校验（Release 构建后需更新哈希值）
bool verifyPeIntegrity();

} // namespace AntiDebug

#endif // ANTIDEBUG_H
