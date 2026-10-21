#include "detparamlogger.h"

#include <QDebug>

namespace DetParamLogger {

QString formatDetParameter(const DetParameter *det)
{
    if (!det)
        return QStringLiteral("(null DetParameter)");

    return QStringLiteral(
        "id=%1 ip=%2 gain=%3 deathTime=%4 triggerThold=%5 "
        "spectrumRefreshTime=%6ms spectrumLength=%7 "
        "waveformTriggerMode=%8 waveformLength=%9 "
        "trapShapeEnable=%10 trapD1=%11 trapD2=%12 trapRise=%13 trapPeak=%14 trapFall=%15 "
        "highVoltageEnable=%16 highVoltageOutLevel=%17V "
        "alphaGammaUpperThold=%18 alphaGammaLowerThold=%19")
        .arg(det->id)
        .arg(QString::fromLatin1(det->det_Ip_port))
        .arg(det->gain, 0, 'g', 6)
        .arg(det->deathTime)
        .arg(det->triggerThold)
        .arg(det->spectrumRefreshTime)
        .arg(det->spectrumLength)
        .arg(det->waveformTriggerMode)
        .arg(det->waveformLength)
        .arg(det->trapShapeEnable ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(det->trapShapeTimeConstD1)
        .arg(det->trapShapeTimeConstD2)
        .arg(det->trapShapeRisePoint)
        .arg(det->trapShapePeakPoint)
        .arg(det->trapShapeFallPoint)
        .arg(det->highVoltageEnable ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(det->highVoltageOutLevel)
        .arg(det->alphaGammaUpperThold)
        .arg(det->alphaGammaLowerThold);
}

void logDetParameters(const QString &reason, const DetParameter *det)
{
    qDebug().noquote() << QStringLiteral("[DetParam]") << reason << formatDetParameter(det);
}

} // namespace DetParamLogger
