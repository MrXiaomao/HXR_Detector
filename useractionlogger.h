#ifndef USERACTIONLOGGER_H
#define USERACTIONLOGGER_H

#include <QString>

/**
 * @brief 关键用户操作日志（qInfo → 界面日志 / Log4Qt）
 */
class UserActionLogger
{
public:
    static void logMessage(const QString &message);
    static void logParamChanged(const QString &name, const QString &value);
};

#endif // USERACTIONLOGGER_H
