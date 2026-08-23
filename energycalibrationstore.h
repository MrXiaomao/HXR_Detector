#ifndef ENERGYCALIBRATIONSTORE_H
#define ENERGYCALIBRATIONSTORE_H

#include <QSet>
#include <QString>
#include <QStringList>
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
    bool hasSigma = false;
    double sigma511 = 5.0;
    double sigma846 = 6.0;
    double sigma909 = 7.0;
};

bool loadDetector(int detectorId, DetectorRecord *out);
bool readLinearKb(int detectorId, double *k, double *b);

/** 能量刻度拟合所用的多道道数（EnCalibration/SpectrumLength），默认 8192。 */
int readSpectrumLength(int defaultLength = 8192);

/** 写入线性刻度点、k/b 与 sigma；不修改 SpectrumLength。 */
bool saveLinearDetector(int detectorId,
                        const QVector<double> &pointsX,
                        const QVector<double> &pointsY,
                        double k,
                        double b,
                        const double sigma[3],
                        QString *errorText = nullptr);

/** 从 device_config.ini 导入初始寻峰道址与 sigma 到 ParseData 内存表。 */
bool importInitialFromDeviceConfig(const QSet<int> &selectedDetectors,
                                   int *importedCountOut = nullptr,
                                   int *skippedCountOut = nullptr,
                                   QStringList *messagesOut = nullptr);

} // namespace EnergyCalibrationStore

#endif // ENERGYCALIBRATIONSTORE_H
