#ifndef USERACTIONLOGGER_H
#define USERACTIONLOGGER_H

#include <QString>

/**
 * @brief 关键用户操作日志（qInfo → Log4Qt 文件；不写入主界面日志框）
 */
class UserActionLogger
{
public:
    static void logMessage(const QString &message);
    static void logParamChanged(const QString &name, const QString &value);
    /** 格式化后的 Qt 日志是否为用户操作日志（主界面日志框应排除） */
    static bool shouldExcludeFromUiLog(const QString &formattedMessage);
};

#endif // USERACTIONLOGGER_H
