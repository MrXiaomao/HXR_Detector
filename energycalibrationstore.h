#ifndef ENERGYCALIBRATIONSTORE_H
#define ENERGYCALIBRATIONSTORE_H

#include <QVector>

/** device_config.ini [EnCalibration] 读写：能量刻度唯一持久化来源。 */
namespace EnergyCalibrationStore {

struct DetectorRecord {
    QVector<double> pointsX;
    QVector<double> pointsY;
    int type = 1;
    double c0 = 1.0;
    double c1 = 0.0;
    double c2 = 0.0;
};

bool loadDetector(int detectorId, DetectorRecord *out);
bool readLinearKb(int detectorId, double *k, double *b);

/** 能量刻度拟合所用的多道道数（EnCalibration/SpectrumLength），默认 8192。 */
int readSpectrumLength(int defaultLength = 8192);

} // namespace EnergyCalibrationStore

#endif // ENERGYCALIBRATIONSTORE_H
