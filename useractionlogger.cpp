#include "useractionlogger.h"

#include <QDebug>

namespace {

const char kLogPrefix[] = "[UserAction]";

} // namespace

void UserActionLogger::logMessage(const QString &message)
{
    qInfo().noquote() << kLogPrefix << message;
}

void UserActionLogger::logParamChanged(const QString &name, const QString &value)
{
    logMessage(QStringLiteral("参数变更 %1=%2").arg(name, value));
}

bool UserActionLogger::shouldExcludeFromUiLog(const QString &formattedMessage)
{
    return formattedMessage.startsWith(QLatin1String(kLogPrefix));
}
