#include "energycalibrationstore.h"
#include "globalsettings.h"

#include <cmath>

namespace EnergyCalibrationStore {
namespace {

bool validLinearKb(double k, double b)
{
    return std::isfinite(k) && std::isfinite(b) && !qFuzzyIsNull(k);
}

} // namespace

bool loadDetector(int detectorId, DetectorRecord *out)
{
    if (!out || detectorId < 1 || detectorId > DET_NUM)
        return false;

    GlobalSettings settings(DEVICE_CONFIG_FILE);
    settings.beginGroup(QStringLiteral("EnCalibration"));
    const QString groupName = QStringLiteral("Detector%1").arg(detectorId);
    if (!settings.contains(QStringLiteral("%1/pointsX").arg(groupName))
        && !settings.contains(QStringLiteral("%1/c0").arg(groupName))) {
        settings.endGroup();
        return false;
    }

    settings.beginGroup(groupName);
    DetectorRecord rec;
    rec.pointsX = settings.GetDoubleVector(QStringLiteral("pointsX"));
    rec.pointsY = settings.GetDoubleVector(QStringLiteral("pointsY"));
    rec.type = settings.value(QStringLiteral("type"), 1).toInt();
    rec.c0 = settings.value(QStringLiteral("c0"), 1.0).toDouble();
    rec.c1 = settings.value(QStringLiteral("c1"), 0.0).toDouble();
    rec.c2 = settings.value(QStringLiteral("c2"), 0.0).toDouble();
    settings.endGroup();
    settings.endGroup();

    const bool hasPoints = !rec.pointsX.isEmpty() && rec.pointsX.size() == rec.pointsY.size();
    const bool hasLinear = rec.type == 1 && validLinearKb(rec.c0, rec.c1);
    const bool hasQuadratic = rec.type == 2
        && std::isfinite(rec.c0) && std::isfinite(rec.c1) && std::isfinite(rec.c2);
    if (!hasPoints && !hasLinear && !hasQuadratic)
        return false;

    *out = rec;
    return true;
}

bool readLinearKb(int detectorId, double *k, double *b)
{
    DetectorRecord rec;
    if (!loadDetector(detectorId, &rec))
        return false;
    if (rec.type != 1 || !validLinearKb(rec.c0, rec.c1))
        return false;
    if (k)
        *k = rec.c0;
    if (b)
        *b = rec.c1;
    return true;
}

int readSpectrumLength(int defaultLength)
{
    GlobalSettings settings(DEVICE_CONFIG_FILE);
    settings.beginGroup(QStringLiteral("EnCalibration"));
    const int length = settings.value(QStringLiteral("SpectrumLength"), defaultLength).toInt();
    settings.endGroup();
    if (length == 1024 || length == 2048 || length == 4096 || length == 8192)
        return length;
    return defaultLength > 0 ? defaultLength : 8192;
}

} // namespace EnergyCalibrationStore
