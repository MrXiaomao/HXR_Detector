#ifndef DETPARAMLOGGER_H
#define DETPARAMLOGGER_H

#include "spectypes.h"

namespace DetParamLogger {

QString formatDetParameter(const DetParameter *det);

void logDetParameters(const QString &reason, const DetParameter *det);

} // namespace DetParamLogger

#endif // DETPARAMLOGGER_H
