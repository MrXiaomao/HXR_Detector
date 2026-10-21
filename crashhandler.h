#ifndef CRASHHANDLER_H
#define CRASHHANDLER_H

#include <QString>

/**
 * @brief Windows 进程崩溃/异常日志（MSVC + PDB 或 MinGW）
 *
 * 崩溃时写入 logs/crash_*.log 与 logs/crash_*.dmp（MiniDump），
 * 不依赖 Log4Qt，避免堆损坏时二次崩溃。
 * 查调用栈建议用 MSVC Release 构建（生成 HXR.pdb），
 * 再用 Visual Studio 打开同版本 exe 对应的 crash_*.dmp。
 */
class CrashHandler
{
public:
    static void install(const QString &logDirectory = QStringLiteral("logs"));

    static void setApplicationInfo(const QString &appVersion,
                                 const QString &gitBranch,
                                 const QString &gitHash,
                                 const QString &gitDate);

    /** 主线程定期调用，供界面卡死检测 */
    static void heartbeat();

    /** 记录最近业务步骤，崩溃/卡死日志会附带 */
    static void setBreadcrumb(const QString &text);

    /** 在主线程启动 GUI 看门狗，timeoutMs 无心跳则写 hang 日志 */
    static void startGuiWatchdog(int timeoutMs = 5000);

    /** 退出前停止看门狗线程（aboutToQuit 时自动调用，也可手动调用） */
    static void stopGuiWatchdog();

    /** qFatal / QtFatalMsg 时写入异常日志（进程随后 abort） */
    static void logQtFatal(const QString &message,
                           const char *file = nullptr,
                           int line = 0,
                           const char *function = nullptr);

private:
    CrashHandler() = delete;
};

#define CRASH_BREADCRUMB(msg) CrashHandler::setBreadcrumb(msg)

#endif // CRASHHANDLER_H
