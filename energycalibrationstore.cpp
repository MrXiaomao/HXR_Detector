#include "energycalibrationstore.h"
#include "globalsettings.h"
#include "parsedata.h"

#include <QObject>
#include <cmath>

namespace EnergyCalibrationStore {
namespace {

bool validLinearKb(double k, double b)
{
    return std::isfinite(k) && std::isfinite(b) && !qFuzzyIsNull(k);
}

bool validSigmaTriplet(const double sigma[3])
{
    return sigma
        && std::isfinite(sigma[0]) && std::isfinite(sigma[1]) && std::isfinite(sigma[2])
        && sigma[0] > 0.0 && sigma[1] > 0.0 && sigma[2] > 0.0;
}

bool channelsFromPointsOrKb(const DetectorRecord &rec, double *ch511, double *ch909)
{
    if (!ch511 || !ch909)
        return false;

    if (rec.pointsX.size() >= 2 && rec.pointsY.size() >= 2
        && rec.pointsX.size() == rec.pointsY.size()) {
        int idx511 = -1;
        int idx909 = -1;
        for (int i = 0; i < rec.pointsY.size(); ++i) {
            if (qFuzzyCompare(rec.pointsY.at(i), 511.0))
                idx511 = i;
            else if (qFuzzyCompare(rec.pointsY.at(i), 909.0))
                idx909 = i;
        }
        if (idx511 >= 0 && idx909 >= 0) {
            *ch511 = rec.pointsX.at(idx511);
            *ch909 = rec.pointsX.at(idx909);
            return std::isfinite(*ch511) && std::isfinite(*ch909)
                && !qFuzzyCompare(*ch511, *ch909);
        }
        // 无前两点兜底（常见顺序为 511 再 909）
        *ch511 = rec.pointsX.at(0);
        *ch909 = rec.pointsX.at(1);
        return std::isfinite(*ch511) && std::isfinite(*ch909)
            && !qFuzzyCompare(*ch511, *ch909);
    }

    if (rec.type == 1 && validLinearKb(rec.c0, rec.c1)) {
        *ch511 = (511.0 - rec.c1) / rec.c0;
        *ch909 = (909.0 - rec.c1) / rec.c0;
        return std::isfinite(*ch511) && std::isfinite(*ch909)
            && !qFuzzyCompare(*ch511, *ch909);
    }
    return false;
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

    const double sigma511 = settings.value(QStringLiteral("sigma511"), 0.0).toDouble();
    const double sigma846 = settings.value(QStringLiteral("sigma846"), 0.0).toDouble();
    const double sigma909 = settings.value(QStringLiteral("sigma909"), 0.0).toDouble();
    const double sigma[3] = {sigma511, sigma846, sigma909};
    if (validSigmaTriplet(sigma)) {
        rec.hasSigma = true;
        rec.sigma511 = sigma511;
        rec.sigma846 = sigma846;
        rec.sigma909 = sigma909;
    }
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

bool saveLinearDetector(int detectorId,
                        const QVector<double> &pointsX,
                        const QVector<double> &pointsY,
                        double k,
                        double b,
                        const double sigma[3],
                        QString *errorText)
{
    if (detectorId < 1 || detectorId > DET_NUM
        || pointsX.isEmpty() || pointsX.size() != pointsY.size()
        || !validLinearKb(k, b) || !validSigmaTriplet(sigma)) {
        if (errorText)
            *errorText = QObject::tr("能量刻度参数无效");
        return false;
    }

    GlobalSettings settings(DEVICE_CONFIG_FILE);
    settings.beginGroup(QStringLiteral("EnCalibration"));
    settings.beginGroup(QStringLiteral("Detector%1").arg(detectorId));
    settings.remove(QString());
    settings.setDoubleVector(QStringLiteral("pointsX"), pointsX);
    settings.setDoubleVector(QStringLiteral("pointsY"), pointsY);
    settings.setValue(QStringLiteral("type"), 1);
    settings.setValue(QStringLiteral("c0"), k);
    settings.setValue(QStringLiteral("c1"), b);
    settings.setValue(QStringLiteral("sigma511"), sigma[0]);
    settings.setValue(QStringLiteral("sigma846"), sigma[1]);
    settings.setValue(QStringLiteral("sigma909"), sigma[2]);
    settings.endGroup();
    settings.endGroup();
    settings.sync();
    return true;
}

bool importInitialFromDeviceConfig(const QSet<int> &selectedDetectors,
                                   int *importedCountOut,
                                   int *skippedCountOut,
                                   QStringList *messagesOut)
{
    if (importedCountOut)
        *importedCountOut = 0;
    if (skippedCountOut)
        *skippedCountOut = 0;

    if (selectedDetectors.isEmpty())
        return false;

    ParseData::resetInitialCalibrationChannels();
    ParseData::resetInitialSigmas();

    int importedCount = 0;
    int skippedCount = 0;
    QSet<int> importedDetectors;

    for (int detectorId : selectedDetectors) {
        if (detectorId < 1 || detectorId > DET_NUM) {
            ++skippedCount;
            continue;
        }

        DetectorRecord rec;
        if (!loadDetector(detectorId, &rec)) {
            ++skippedCount;
            if (messagesOut) {
                messagesOut->append(QObject::tr("谱仪%1未在 device_config.ini 中读取到有效能量刻度，继续使用默认初始道址")
                                        .arg(detectorId));
            }
            continue;
        }

        double ch511 = 0.0;
        double ch909 = 0.0;
        if (!channelsFromPointsOrKb(rec, &ch511, &ch909)
            || !ParseData::setInitialCalibrationChannels(detectorId, ch511, ch909)) {
            ++skippedCount;
            if (messagesOut) {
                messagesOut->append(QObject::tr("谱仪%1能量刻度换算失败，继续使用默认初始道址").arg(detectorId));
            }
            continue;
        }

        double sigma[3] = {5.0, 6.0, 7.0};
        if (rec.hasSigma) {
            sigma[0] = rec.sigma511;
            sigma[1] = rec.sigma846;
            sigma[2] = rec.sigma909;
        }
        if (!ParseData::setInitialSigmas(detectorId, sigma[0], sigma[1], sigma[2])) {
            ++skippedCount;
            if (messagesOut) {
                messagesOut->append(QObject::tr("谱仪%1 sigma 参数无效，继续使用默认 sigma").arg(detectorId));
            }
            continue;
        }

        ++importedCount;
        importedDetectors.insert(detectorId);
        if (messagesOut) {
            messagesOut->append(QObject::tr("谱仪%1导入初始能量刻度：511keV=%2道，909keV=%3道")
                                    .arg(detectorId)
                                    .arg(ch511, 0, 'f', 3)
                                    .arg(ch909, 0, 'f', 3));
        }
    }

    if (importedCountOut)
        *importedCountOut = importedCount;
    if (skippedCountOut)
        *skippedCountOut = skippedCount;
    return importedCount > 0;
}

} // namespace EnergyCalibrationStore
