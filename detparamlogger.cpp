#include "detparamlogger.h"

#include <QDebug>

namespace DetParamLogger {

QString formatDetParameter(const DetParameter *det)
{
    if (!det)
        return QStringLiteral("(null DetParameter)");

    return QStringLiteral(
        "id=%1 ip=%2 lon=%3 lat=%4 gain=%5 deathTime=%6 triggerThold=%7 "
        "spectrumRefreshTime=%8ms spectrumLength=%9 spectrumEn=[%10,%11]keV "
        "waveformTriggerMode=%12 waveformLength=%13 "
        "trapShapeEnable=%14 trapD1=%15 trapD2=%16 trapRise=%17 trapPeak=%18 trapFall=%19 "
        "highVoltageEnable=%20 highVoltageOutLevel=%21V")
        .arg(det->id)
        .arg(QString::fromLatin1(det->det_Ip_port))
        .arg(det->longitude, 0, 'f', 6)
        .arg(det->latitude, 0, 'f', 6)
        .arg(det->gain, 0, 'g', 6)
        .arg(det->deathTime)
        .arg(det->triggerThold)
        .arg(det->spectrumRefreshTime)
        .arg(det->spectrumLength)
        .arg(det->spectrumPlotEnMin, 0, 'f', 3)
        .arg(det->spectrumPlotEnMax, 0, 'f', 3)
        .arg(det->waveformTriggerMode)
        .arg(det->waveformLength)
        .arg(det->trapShapeEnable ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(det->trapShapeTimeConstD1)
        .arg(det->trapShapeTimeConstD2)
        .arg(det->trapShapeRisePoint)
        .arg(det->trapShapePeakPoint)
        .arg(det->trapShapeFallPoint)
        .arg(det->highVoltageEnable ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(det->highVoltageOutLevel);
}

void logDetParameters(const QString &reason, const DetParameter *det)
{
    qDebug().noquote() << QStringLiteral("[DetParam]") << reason << formatDetParameter(det);
}

} // namespace DetParamLogger
