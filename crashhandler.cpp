#include "crashhandler.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <QtGlobal>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>

#if defined(_MSC_VER)
#include <stdlib.h>
#endif
#if defined(__MINGW32__)
#include <stdlib.h>
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>

namespace {

QMutex g_infoMutex;
QString g_logDir = QStringLiteral("logs");
QString g_appVersion;
QString g_gitBranch;
QString g_gitHash;
QString g_gitDate;

char g_breadcrumb[512] = {};
std::atomic<qint64> g_lastHeartbeatMs{0};
std::atomic<bool> g_hangReported{false};
std::atomic<bool> g_inCrashHandler{false};
/** 主线程事件循环至少处理过一次心跳定时器后才检测卡死，避免启动阶段误报 */
std::atomic<bool> g_guiEventLoopHeartbeat{false};
QThread *g_watchThread = nullptr;
int g_watchPollMs = 500;

void copyQStringToBuffer(const QString &src, char *dst, size_t dstSize)
{
    if (dstSize == 0)
        return;
    const QByteArray utf8 = src.toUtf8();
    std::snprintf(dst, dstSize, "%s", utf8.constData());
}

void crashWrite(HANDLE file, const char *text)
{
    if (!file || file == INVALID_HANDLE_VALUE || !text)
        return;
    const DWORD len = static_cast<DWORD>(std::strlen(text));
    if (len == 0)
        return;
    DWORD written = 0;
    WriteFile(file, text, len, &written, nullptr);
}

void crashWriteFmt(HANDLE file, const char *fmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    crashWrite(file, buffer);
}

const char *exceptionCodeName(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DENORMAL_OPERAND: return "EXCEPTION_FLT_DENORMAL_OPERAND";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INEXACT_RESULT: return "EXCEPTION_FLT_INEXACT_RESULT";
    case EXCEPTION_FLT_INVALID_OPERATION: return "EXCEPTION_FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW: return "EXCEPTION_FLT_OVERFLOW";
    case EXCEPTION_FLT_STACK_CHECK: return "EXCEPTION_FLT_STACK_CHECK";
    case EXCEPTION_FLT_UNDERFLOW: return "EXCEPTION_FLT_UNDERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW: return "EXCEPTION_INT_OVERFLOW";
    case EXCEPTION_INVALID_DISPOSITION: return "EXCEPTION_INVALID_DISPOSITION";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_SINGLE_STEP: return "EXCEPTION_SINGLE_STEP";
    case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
    default: return "UNKNOWN_EXCEPTION";
    }
}

void writeStackTrace(HANDLE file, CONTEXT *context)
{
    crashWrite(file, "\r\n--- stack trace ---\r\n");

    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);

    wchar_t moduleDir[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, moduleDir, MAX_PATH) > 0) {
        wchar_t *lastSlash = wcsrchr(moduleDir, L'\\');
        if (lastSlash)
            *lastSlash = L'\0';
        SymSetSearchPathW(process, moduleDir);
    }

    STACKFRAME64 frame = {};
#if defined(_M_X64) || defined(__x86_64__)
    const DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context->Rip;
    frame.AddrFrame.Offset = context->Rbp;
    frame.AddrStack.Offset = context->Rsp;
#else
    const DWORD machineType = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context->Eip;
    frame.AddrFrame.Offset = context->Ebp;
    frame.AddrStack.Offset = context->Esp;
#endif
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;

    char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
    auto *symbol = reinterpret_cast<SYMBOL_INFO *>(symbolBuffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;

    IMAGEHLP_LINE64 lineInfo = {};
    lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    DWORD displacement = 0;

    for (int frameIndex = 0; frameIndex < 64; ++frameIndex) {
        if (!StackWalk64(machineType, process, GetCurrentThread(), &frame, context,
                         nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            break;
        }

        DWORD64 address = frame.AddrPC.Offset;
        if (address == 0)
            break;

        const char *funcName = "???";
        if (SymFromAddr(process, address, nullptr, symbol))
            funcName = symbol->Name;

        const char *fileName = "?";
        DWORD lineNumber = 0;
        if (SymGetLineFromAddr64(process, address, &displacement, &lineInfo)) {
            fileName = lineInfo.FileName ? lineInfo.FileName : "?";
            lineNumber = lineInfo.LineNumber;
        }

        crashWriteFmt(file, "#%-2d 0x%016llX %s (%s:%lu)\r\n",
                      frameIndex,
                      static_cast<unsigned long long>(address),
                      funcName,
                      fileName,
                      static_cast<unsigned long>(lineNumber));
    }

    SymCleanup(process);
}

QString makeTimestampTag()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
}

HANDLE createCrashLogFile(const QString &prefix, QString *outPath)
{
    QDir dir(g_logDir);
    if (!dir.exists())
        dir.mkpath(QStringLiteral("."));

    const QString path = dir.filePath(prefix + makeTimestampTag() + QStringLiteral(".log"));
    if (outPath)
        *outPath = path;

    const std::wstring wpath = path.toStdWString();
    return CreateFileW(wpath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

bool writeMiniDump(EXCEPTION_POINTERS *pointers, const QString &dumpPath)
{
    const std::wstring wpath = dumpPath.toStdWString();
    HANDLE dumpFile = CreateFileW(wpath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dumpFile == INVALID_HANDLE_VALUE)
        return false;

    MINIDUMP_EXCEPTION_INFORMATION exceptionInfo = {};
    exceptionInfo.ThreadId = GetCurrentThreadId();
    exceptionInfo.ExceptionPointers = pointers;
    exceptionInfo.ClientPointers = FALSE;

    const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dumpFile,
                                      static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs
                                                                 | MiniDumpWithHandleData
                                                                 | MiniDumpWithThreadInfo),
                                      pointers ? &exceptionInfo : nullptr, nullptr, nullptr);
    CloseHandle(dumpFile);
    return ok == TRUE;
}

void writeCommonHeader(HANDLE file, const char *kind)
{
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    crashWriteFmt(file,
                  "========== %s ==========\r\n"
                  "time: %04u-%02u-%02u %02u:%02u:%02u.%03u\r\n",
                  kind,
                  st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    char version[256] = {};
    char branch[128] = {};
    char hash[128] = {};
    char date[128] = {};
    char breadcrumb[512] = {};
    {
        QMutexLocker lock(&g_infoMutex);
        copyQStringToBuffer(g_appVersion, version, sizeof(version));
        copyQStringToBuffer(g_gitBranch, branch, sizeof(branch));
        copyQStringToBuffer(g_gitHash, hash, sizeof(hash));
        copyQStringToBuffer(g_gitDate, date, sizeof(date));
    }
    std::strncpy(breadcrumb, g_breadcrumb, sizeof(breadcrumb) - 1);

    crashWriteFmt(file, "app_version: %s\r\n", version);
    crashWriteFmt(file, "git_branch: %s\r\n", branch);
    crashWriteFmt(file, "git_hash: %s\r\n", hash);
    crashWriteFmt(file, "git_date: %s\r\n", date);
    crashWriteFmt(file, "breadcrumb: %s\r\n", breadcrumb);

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    char exePathUtf8[MAX_PATH * 3] = {};
    WideCharToMultiByte(CP_UTF8, 0, exePath, -1, exePathUtf8, sizeof(exePathUtf8), nullptr, nullptr);
    crashWriteFmt(file, "executable: %s\r\n", exePathUtf8);
    crashWriteFmt(file, "pid: %lu\r\n", GetCurrentProcessId());
}

void handleFatalException(EXCEPTION_POINTERS *pointers, const char *source, const char *extraLine = nullptr)
{
    if (g_inCrashHandler.exchange(true))
        return;

    const QString tag = makeTimestampTag();
    QDir dir(g_logDir);
    if (!dir.exists())
        dir.mkpath(QStringLiteral("."));

    const QString logPath = dir.filePath(QStringLiteral("crash_") + tag + QStringLiteral(".log"));
    const std::wstring wLogPath = logPath.toStdWString();
    HANDLE logFile = CreateFileW(wLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (logFile == INVALID_HANDLE_VALUE)
        return;

    writeCommonHeader(logFile, source);
    if (extraLine && extraLine[0] != '\0') {
        crashWrite(logFile, extraLine);
        crashWrite(logFile, "\r\n");
    }

    if (pointers && pointers->ExceptionRecord) {
        const DWORD code = pointers->ExceptionRecord->ExceptionCode;
        crashWriteFmt(logFile, "exception_code: 0x%08lX (%s)\r\n",
                      code, exceptionCodeName(code));
        crashWriteFmt(logFile, "exception_address: 0x%p\r\n",
                      pointers->ExceptionRecord->ExceptionAddress);

        if (code == EXCEPTION_ACCESS_VIOLATION && pointers->ExceptionRecord->NumberParameters >= 2) {
            const ULONG_PTR rw = pointers->ExceptionRecord->ExceptionInformation[0];
            const ULONG_PTR address = pointers->ExceptionRecord->ExceptionInformation[1];
            crashWriteFmt(logFile, "access_type: %s\r\n", rw == 0 ? "read" : (rw == 1 ? "write" : "execute"));
            crashWriteFmt(logFile, "access_address: 0x%p\r\n", reinterpret_cast<void *>(address));
        }

        if (pointers->ContextRecord)
            writeStackTrace(logFile, pointers->ContextRecord);
    }

    const QString dumpPath = dir.filePath(QStringLiteral("crash_") + tag + QStringLiteral(".dmp"));
    if (writeMiniDump(pointers, dumpPath)) {
        crashWrite(logFile, "\r\nminidump: ");
        crashWrite(logFile, dumpPath.toUtf8().constData());
        crashWrite(logFile, "\r\n");
    } else {
        crashWrite(logFile, "\r\nminidump: (failed to write)\r\n");
    }

    crashWrite(logFile, "========== end ==========\r\n\r\n");
    FlushFileBuffers(logFile);
    CloseHandle(logFile);
}

LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS *pointers)
{
    handleFatalException(pointers, "unhandled_seh_exception");
    return EXCEPTION_EXECUTE_HANDLER;
}

void signalHandler(int sig)
{
    char source[64] = {};
    std::snprintf(source, sizeof(source), "signal_%d", sig);
    handleFatalException(nullptr, source);
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

void purecallHandler()
{
    handleFatalException(nullptr, "pure_virtual_call");
    std::abort();
}

void terminateHandler()
{
    handleFatalException(nullptr, "std_terminate");
    std::abort();
}

void invalidParameterHandler(const wchar_t *, const wchar_t *, const wchar_t *, unsigned int, uintptr_t)
{
    handleFatalException(nullptr, "invalid_parameter");
}

} // namespace

void CrashHandler::install(const QString &logDirectory)
{
    g_logDir = logDirectory;
    QDir().mkpath(g_logDir);

    SetUnhandledExceptionFilter(unhandledExceptionFilter);

    std::signal(SIGABRT, signalHandler);
    std::signal(SIGFPE, signalHandler);
    std::signal(SIGILL, signalHandler);
    std::signal(SIGSEGV, signalHandler);
    std::signal(SIGTERM, signalHandler);

    _set_purecall_handler(purecallHandler);
    std::set_terminate(terminateHandler);
#if defined(_MSC_VER) || defined(__MINGW32__)
    _set_invalid_parameter_handler(invalidParameterHandler);
#endif

    heartbeat();
}

void CrashHandler::logQtFatal(const QString &message,
                              const char *file,
                              int line,
                              const char *function)
{
    char extra[2048] = {};
    std::snprintf(extra, sizeof(extra), "qt_fatal_message: %s", message.toUtf8().constData());
    if (file && function)
        std::snprintf(extra + std::strlen(extra), sizeof(extra) - std::strlen(extra),
                      "\r\nlocation: %s:%d in %s", file, line, function);
    else if (file)
        std::snprintf(extra + std::strlen(extra), sizeof(extra) - std::strlen(extra),
                      "\r\nlocation: %s:%d", file, line);

    handleFatalException(nullptr, "qt_fatal", extra);
}

void CrashHandler::setApplicationInfo(const QString &appVersion,
                                      const QString &gitBranch,
                                      const QString &gitHash,
                                      const QString &gitDate)
{
    QMutexLocker lock(&g_infoMutex);
    g_appVersion = appVersion;
    g_gitBranch = gitBranch;
    g_gitHash = gitHash;
    g_gitDate = gitDate;
}

void CrashHandler::heartbeat()
{
    g_lastHeartbeatMs.store(QDateTime::currentMSecsSinceEpoch());
    g_hangReported.store(false);
}

void CrashHandler::setBreadcrumb(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    std::snprintf(g_breadcrumb, sizeof(g_breadcrumb), "%s", utf8.constData());
}

void CrashHandler::stopGuiWatchdog()
{
    QThread *thread = g_watchThread;
    g_watchThread = nullptr;
    if (!thread || !thread->isRunning())
        return;

    thread->requestInterruption();
    // 默认 QThread::run() 在 started 槽返回后会进入 exec()；仅 requestInterruption 无法结束该事件循环
    thread->quit();

    const int pollMs = qMax(200, g_watchPollMs);
    int waited = 0;
    const int maxWaitMs = 5000;
    while (thread->isRunning() && waited < maxWaitMs) {
        thread->wait(pollMs);
        waited += pollMs;
    }
}

void CrashHandler::startGuiWatchdog(int timeoutMs)
{
    g_guiEventLoopHeartbeat.store(false);
    heartbeat();

    QTimer *beatTimer = new QTimer(qApp);
    beatTimer->setInterval(qMax(200, timeoutMs / 4));
    QObject::connect(beatTimer, &QTimer::timeout, qApp, [] {
        heartbeat();
        g_guiEventLoopHeartbeat.store(true);
    });
    beatTimer->start();

    g_watchPollMs = qMax(200, timeoutMs / 8);

    QThread *watchThread = new QThread(qApp);
    g_watchThread = watchThread;
    QObject::connect(watchThread, &QThread::started, [timeoutMs, watchThread] {
        while (!watchThread->isInterruptionRequested()) {
            // 短间隔睡眠，确保退出时能在 aboutToQuit 内及时结束（原 timeoutMs/2 可达数秒）
            for (int waited = 0; waited < qMax(500, timeoutMs / 2) && !watchThread->isInterruptionRequested(); waited += g_watchPollMs)
                QThread::msleep(g_watchPollMs);

            if (watchThread->isInterruptionRequested())
                break;

            // 启动后 exec() 尚未驱动定时器前，勿用旧心跳时间戳误报 hang
            if (!g_guiEventLoopHeartbeat.load())
                continue;

            const qint64 last = g_lastHeartbeatMs.load();
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - last < timeoutMs)
                continue;
            if (g_hangReported.exchange(true))
                continue;

            QString hangPath;
            HANDLE hangFile = createCrashLogFile(QStringLiteral("hang_"), &hangPath);
            if (hangFile != INVALID_HANDLE_VALUE) {
                writeCommonHeader(hangFile, "gui_thread_hang");
                crashWriteFmt(hangFile, "no_heartbeat_ms: %lld\r\n", static_cast<long long>(now - last));
                crashWrite(hangFile, "========== end ==========\r\n\r\n");
                FlushFileBuffers(hangFile);
                CloseHandle(hangFile);
            }

            qWarning().noquote() << QStringLiteral("[CrashHandler] GUI thread may be blocked, see: %1").arg(hangPath);
        }
    });
    watchThread->start();

    QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [] {
        CrashHandler::stopGuiWatchdog();
    }, Qt::DirectConnection);
}
