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

} // namespace EnergyCalibrationStore

#endif // ENERGYCALIBRATIONSTORE_H
