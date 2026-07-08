#include "parsedata.h"
#include "offlinespectrumparser.h"
#include "globalsettings.h"
#include <QFile>
#include <QFileInfo>
#include <QDataStream>
#include <QElapsedTimer>
#include <QTextCodec>
#include <cmath>
#include <cstring> // 需要包含memcpy
#include <algorithm>
#include "sysutils.h"

#include "curveFit.h"
#include <QDebug>

namespace {

qint64 resolveFileStartMs(const QString &filePath, const std::string &filePathStd)
{
    const QDateTime startDt = HDF5Settings::resolveMeasureStartTime(filePath, filePathStd);
    if (!startDt.isValid())
        return 0;
    return startDt.toMSecsSinceEpoch();
}

qint64 syntheticSpectrumStartMs(qint64 fileStartMs, quint32 sequence0, quint32 measureTimeMs, quint32 sequence)
{
    return fileStartMs + static_cast<qint64>(sequence - sequence0)
                       * static_cast<qint64>(measureTimeMs);
}

/** 按 sequence 连续段切分（相邻序号差 > 1 视为断点） */
QVector<QVector<H5Spectrum>> splitBySequenceRuns(const QVector<H5Spectrum> &fileSpec)
{
    QVector<QVector<H5Spectrum>> runs;
    QVector<H5Spectrum> currentRun;
    quint32 prevSequence = 0;
    bool hasPrev = false;

    for (const H5Spectrum &spec : fileSpec) {
        if (spec.measureTime == 0)
            continue;
        if (hasPrev && spec.sequence > prevSequence + 1) {
            if (!currentRun.isEmpty())
                runs.append(currentRun);
            currentRun.clear();
        }
        currentRun.append(spec);
        prevSequence = spec.sequence;
        hasPrev = true;
    }
    if (!currentRun.isEmpty())
        runs.append(currentRun);
    return runs;
}

struct DetectorCalChannel {
    double ch511;
    double ch909;
};

/** 与 MATLAB Data_Analysis_Main.m V1.1 各探测器 CH 表一致（1~29） */
const DetectorCalChannel kDetectorChTable[] = {
    {373, 659}, {325, 573}, {296, 525}, {374, 662}, {301, 533},
    {343, 614}, {383, 677}, {298, 533}, {434, 762}, {328, 584},
    {407, 724}, {389, 698}, {428, 763}, {344, 613}, {327, 584},
    {415, 738}, {307, 545}, {326, 578}, {315, 558}, {323, 573},
    {278, 490}, {327, 580}, {329, 585}, {392, 698}, {324, 595},
    {377, 666}, {312, 551}, {327, 582}, {312, 564},
};

constexpr int kDetectorChTableSize = sizeof(kDetectorChTable) / sizeof(kDetectorChTable[0]);
constexpr int kRefreshTimeSec = 60;
constexpr double kPeakHeightMin = 100.0;

bool initialEnergyScaleForDetector(int detId, double outScale[2])
{
    if (detId < 1 || detId > kDetectorChTableSize)
        return false;

    const DetectorCalChannel &entry = kDetectorChTable[detId - 1];
    double r2 = 0.0;
    QVector<QPointF> pts;
    pts << QPointF(entry.ch511, 511.0) << QPointF(entry.ch909, 909.0);
    bool flag = CurveFit::fit_linear(pts, outScale, &r2);
    qDebug()<<"initialEnergyScaleForDetector: k="<<outScale[0]<<", b="<<outScale[1];
    return flag;
}

double deadTimeRateFromSegment(const ParseData::mergeSpecData &spec)
{
    if (spec.rowCount == 0 || spec.sumMeasureTimeMs == 0)
        return 0.0;
    const double avgDeadSec = static_cast<double>(spec.deathTime) / static_cast<double>(spec.rowCount) / 1e9;
    const double sumMeasureSec = static_cast<double>(spec.sumMeasureTimeMs) / 1000.0;
    if (sumMeasureSec <= 0.0)
        return 0.0;
    return avgDeadSec / sumMeasureSec;
}
}

const double diffstep = 1.4901e-08;
ParseData::ParseData() {
    mDetId = 0;
}

ParseData::~ParseData() {}

void ParseData::setParseProgressCallback(ParseProgressCallback callback)
{
    m_parseProgressCallback = std::move(callback);
}

void ParseData::clearParseProgressCallback()
{
    m_parseProgressCallback = nullptr;
}

void ParseData::setParseCancelFlag(std::atomic<bool> *flag)
{
    m_parseCancelFlag = flag;
}

bool ParseData::isParseCancelled() const
{
    if (m_parseCancelFlag && m_parseCancelFlag->load(std::memory_order_relaxed))
        return true;
    return m_localCancelFlag.load(std::memory_order_relaxed);
}

void ParseData::reportParseProgress(ParseProgressPhase phase, int current, int total) const
{
    if (!m_parseProgressCallback)
        return;
    const int safeTotal = qMax(1, total);
    const int safeCurrent = qBound(0, current, safeTotal);
    m_parseProgressCallback(phase, safeCurrent, safeTotal);
}

void ParseData::mergeSpecTime_online(const H5Spectrum& specPack)
{
    // -------- 0. 先做道址压缩：8192 道 -> 2048 道，每 4 道求和 --------
    quint32 compressedSpec[mCHANNEL2048];
    for (int ch = 0; ch < mCHANNEL2048; ++ch) {
        const int base = ch * 4;
        // 简单相加，和你后面 m_mergeSpec 的累加一样，本身就可能溢出，所以这里不额外做饱和判断
        compressedSpec[ch] =
            specPack.spectrum[base]
            + specPack.spectrum[base + 1]
            + specPack.spectrum[base + 2]
            + specPack.spectrum[base + 3];
    }

    // -------- 1. 统计计数率：用压缩后的谱 --------
    int sumCount = 0;
    for (int i = 0; i < mCHANNEL2048; ++i) {
        sumCount += compressedSpec[i];
    }

    allSpecTime.push_back(specPack.sequence * specPack.measureTime);
    allSpecCount.push_back(sumCount);

    // 记录打靶时刻
    if (shotTime < 0 && sumCount > 1000) {
        shotTime = specPack.sequence * specPack.measureTime;
        spectDeltaT = specPack.measureTime; // 单个能谱测量时长
    }

    // 当前时刻（ms）
    qint64 currentTime = specPack.sequence * specPack.measureTime;
    int timeAfterShot = (currentTime - shotTime) / 1000; // 单位 s

    if (timeAfterShot > startTime_online) {
        quint64 lossTime = 0; // ns

        int timeMerge = timeAfterShot - startTime_online; // s
        int mergeID = int(ceil(timeMerge * 1.0 / mTimeBin_online));

        // 计算丢包带来的死时间
        qint64 lossTimeTemp =
            (specPack.sequence - lastSpecID_online - 1) * spectDeltaT; // ms
        lossTime = lossTimeTemp * 1000000 + specPack.deathTime * 10;   // ns

        if (m_mergeSpec.size() < mergeID) {
            // 新的分时能谱
            if (m_mergeSpec.size() > 0) {
                QVector<mergeSpecData> mergeSpec_temp = m_mergeSpec;
                // 处理前面已完整的分时能谱，进行拟合
                getResult(mergeSpec_temp);
            }

            mergeSpecData tempMerge;
            tempMerge.currentTime = currentTime;
            tempMerge.deathTime   = lossTime;

            // ★ 这里用压缩后的 2048 道
            for (int i = 0; i < mCHANNEL2048; ++i) {
                tempMerge.spectrum[i] += compressedSpec[i];
            }
            m_mergeSpec.push_back(tempMerge);

            qDebug() << "合并一个分时能谱，mergeID = " << mergeID;
        } else {
            // 往已有分时能谱里继续累加
            m_mergeSpec[mergeID - 1].currentTime = currentTime;
            m_mergeSpec[mergeID - 1].deathTime  += lossTime;

            for (int i = 0; i < mCHANNEL2048; ++i) {
                m_mergeSpec[mergeID - 1].spectrum[i] += compressedSpec[i];
            }
        }
    }

    lastSpecID_online = specPack.sequence;
}


int ParseData::getResult(QVector<mergeSpecData> mergeSpec)
{
    clearFitResult();
    m_fitFailureMessages.clear();
    m_successfulFitSegmentCount = 0;

    if (mergeSpec.isEmpty())
        return 0;

    QVector<fit_result> peakParAll;
    
    // 511/846/909 keV寻峰成功与否的标志位，0:没找到峰；1：找到了峰；初值为2
    bool fitflag[3] = {false, false, false}; 
    {
        double *singleSpectrum = new double[mCHANNEL2048];
        for (int i = 0; i < mCHANNEL2048; ++i)
            singleSpectrum[i] = mergeSpec.at(0).spectrum[i] * 1.0;

        double energy_scale[2] = {1.272, -26.87};
        if (!initialEnergyScaleForDetector(mDetId, energy_scale))
            qWarning() << "Detector" << mDetId << "uses fallback energy scale";

        if (!initial_PeakFind(singleSpectrum, energy_scale, peakParAll, fitflag)) {
            delete[] singleSpectrum;
            m_fitFailureMessages.append(
                QStringLiteral("谱仪%1初寻峰失败（511/909 keV），解析失败").arg(mDetId));
            return 0;
        }
        delete[] singleSpectrum;
        if (!fitflag[1]) {
            qInfo().nospace() << "Detector" << mDetId
                              << "846keV peak not found on first segment, single-Gauss stripping will be used";
        }
    }

    if (peakParAll.size() < 3 || !fitflag[0] || !fitflag[2]) {
        m_fitFailureMessages.append(
            QStringLiteral("谱仪%1初寻峰失败（511/909 keV），解析失败").arg(mDetId));
        return 0;
    }

    QVector<double> initialValue = {
        peakParAll[1].c0, peakParAll[1].c1, peakParAll[1].c2,
        peakParAll[2].c0, peakParAll[2].c1, peakParAll[2].c2,
        0.0, 0.0, 0.0, 0.0, 0.0
    };

    QVector<fit_result> peakPar0;
    peakPar0 << peakParAll[0] << peakParAll[2];

    // 846/909 keV高斯峰拟合成功与否的标志位，0:没找到峰；1：找到了峰；初值为2
    bool stripFlag[2] = {fitflag[1], fitflag[2]};

    int spec_id = 0;
    const int totalSegments = mergeSpec.size();
    for (const mergeSpecData &spec : mergeSpec) {
        if (isParseCancelled())
            break;

        const int segmentIndex = spec_id + 1;
        QVector<double> fit_c = initialValue;
        fit_c[6] = 10.0;
        fit_c[7] = 1.0;
        fit_c[8] = 1.0;
        fit_c[9] = 1.0;
        fit_c[10] = 5.0;

        double *singleSpectrum = new double[mCHANNEL2048];
        for (int i = 0; i < mCHANNEL2048; ++i)
            singleSpectrum[i] = spec.spectrum[i] * 1.0;

        bool exitflag[2] = {false, false};
        QVector<fit_result> peakFindInit = peakPar0;
        if (!PeakFind(singleSpectrum, peakFindInit, exitflag)) {
            m_fitFailureMessages.append(
                QStringLiteral("谱仪%1第%2段能谱拟合失败，解析失败").arg(mDetId).arg(segmentIndex));
            delete[] singleSpectrum;
            ++spec_id;
            reportParseProgress(ParseProgressPhase::Fit, spec_id, totalSegments);
            continue;
        }

        if (!exitflag[0] || !exitflag[1]) {
            if (!exitflag[0])
                qDebug() << "511keV peak height < 100, stop processing segment" << segmentIndex;
            if (!exitflag[1])
                qDebug() << "909keV peak height < 100, stop processing segment" << segmentIndex;
            m_fitFailureMessages.append(
                QStringLiteral("谱仪%1第%2段能谱拟合失败，解析失败").arg(mDetId).arg(segmentIndex));
            delete[] singleSpectrum;
            ++spec_id;
            reportParseProgress(ParseProgressPhase::Fit, spec_id, totalSegments);
            continue;
        }

        peakPar0 = peakFindInit;

        double newEnergyScal[2] = {1.0, 1.0};
        double r2 = 0.0;
        QVector<QPointF> enFitPoints;
        enFitPoints << QPointF(peakPar0[0].c1, m_energyCalibration[0])
                    << QPointF(peakPar0[1].c1, m_energyCalibration[1]);
        CurveFit::fit_linear(enFitPoints, newEnergyScal, &r2);

        const double deathRatio = deadTimeRateFromSegment(spec);
        double peakCount = 0.0;
        double peakPos = 0.0;
        if (!SpecStripping(singleSpectrum, newEnergyScal, fit_c, stripFlag, deathRatio, &peakCount, &peakPos)) {
            m_fitFailureMessages.append(
                QStringLiteral("谱仪%1第%2段能谱拟合失败，解析失败").arg(mDetId).arg(segmentIndex));
            delete[] singleSpectrum;
            ++spec_id;
            reportParseProgress(ParseProgressPhase::Fit, spec_id, totalSegments);
            continue;
        }

        initialValue = fit_c;
        count909_time.push_back(spec.currentTime * 1.0 / 60000.0);
        count909_count.push_back(peakCount);
        count909_peakpos.push_back(peakPos);

        qDebug() << "SpecStripping End, specID:" << spec_id << "peakCount:" << peakCount;
        ++spec_id;
        ++m_successfulFitSegmentCount;
        reportParseProgress(ParseProgressPhase::Fit, spec_id, totalSegments);
        delete[] singleSpectrum;
    }

    if (isParseCancelled())
        return m_successfulFitSegmentCount;

    if (m_successfulFitSegmentCount > 1) {
        if (!fit909data()) {
            m_fitFailureMessages.append(
                QStringLiteral("谱仪%1衰减曲线拟合失败").arg(mDetId));
        }
    }

    return m_successfulFitSegmentCount;
}

/**
 * @brief 寻峰，在能量刻度的前提下，对511keV 846keV 909keV 3个峰进行寻找，修正峰飘问题，给出准确的峰位。
 * @param spectrum 能谱数组，起始道址设定为1，数组长固定G_CHANNEL-3
 * @param energy_scale 能量刻度系数y=ax+b。energy_scale = {a，b};
 * @param fit_c_2 拟合参数初值，拟合成功后，更新拟合参数
 * @param exitflag[3] 511/846/909 keV寻峰成功与否的标志位，0:没找到峰；1：找到了峰；初值为2
 * @return 511/909 keV 寻峰成功时返回 true（846 keV 可选，失败时走单高斯剥谱）
 */
bool ParseData::initial_PeakFind(double* spectrum, double energy_scale[], QVector<fit_result>& fit_c_2, bool* exitflag)
{
    for(int e = 0; e<3; e++)
    {
        exitflag[e] = false;
    }

    //平滑两次能谱曲线
    double* spectrum_smooth1 = new double[mCHANNEL2048];
    double* spectrum_smooth2 = new double[mCHANNEL2048];
    SysUtils::smooth(spectrum, spectrum_smooth1, mCHANNEL2048, 5);
    SysUtils::smooth(spectrum_smooth1, spectrum_smooth2, mCHANNEL2048, 5);

    //搜索峰的能量，keV
    double m_energyCalibration[3] = {511.0, 846.0, 909.0};

    //峰位的sigma估值（V1.1 Sigma0 = [5 6 7]）
    double sigma[3] = {5.0, 6.0, 7.0};
    double width = 25.0;
    //------------- 寻找候选峰-------------------
    for(int e = 0; e<3; e++)
    {
        //峰位搜索宽度，根据初值，给出511keV能峰的大致范围。
        quint32 leftCH, rightCH;
        double init_peak = (m_energyCalibration[e] - energy_scale[1]) / energy_scale[0];
        leftCH = floor(init_peak) - width;
        rightCH = floor(init_peak) + width;

        //先对峰位附近划定不同的道址范围进行拟合，每次拟合的高斯项不同
        QVector<fit_result> fit_result_All; //拟合结果汇总
        int numPeak = rightCH - leftCH + 1; //待拟合的数目

        for(int peak = leftCH; peak<=rightCH; peak++)
        {
            //确定拟合道址范围
            int start_ch = peak - 3*floor(sigma[e]);
            int end_ch = peak + 3*floor(sigma[e]);

            //提取拟合数据
            QVector<QPointF> fitPoints;
            double maxY = 0.0;
            //这里需要注意，MATLAB和C++下标对齐问题。能量刻度y=ax+b时，x从1开始
            for(int i = start_ch-1; i<end_ch; i++)
            {
                double xi = (i+1)*1.0;
                double yi = spectrum_smooth2[i]*1.0;
                fitPoints.push_back(QPointF(xi,yi));
                maxY = (maxY > yi) ? maxY:yi;
            }

            //拟合参数的初值
            double p[] = {maxY, sigma[e], 10.0, 10.0};
            double chi_square = 0.0;

            //拟合并给出结果,注意：这里是对未能量刻度前的道址区间进行拟合，或者说拟合结果峰位、sigma都不带能量刻度
            CurveFit::fit_gauss_linear(fitPoints, p, peak*1.0, &chi_square);

            //提取拟合结果并汇总
            fit_result tempResult;
            tempResult.c0 = p[0];
            tempResult.c1 = peak*1.0;
            tempResult.c2 = p[1];
            tempResult.c3 = p[2];
            tempResult.c4 = p[3];
            tempResult.chi_square = chi_square;
            fit_result_All.push_back(tempResult);
        }

        //根据拟合结果的卡方值来确定最优高斯峰位
        //1、剔除不符合物理规律的拟合结果。
        QVector<fit_result> valid_result;
        QVector<double> valid_chi_square;
        for(auto tempResult:fit_result_All)
        {
            if(tempResult.c0 >= kPeakHeightMin &&
                tempResult.c2 <= 1.5*sigma[e] &&
                tempResult.c2 >= 0.5*sigma[e])
            {
                valid_result.push_back(tempResult);
                valid_chi_square.push_back(tempResult.chi_square);
            }
        }

        //全部拟合结果都不满足要求，则退出
        if(valid_result.size()<1) {
            exitflag[e] = false;
            fit_c_2.push_back(fit_result{0.0, 0.0, 0.0, 0.0, 0.0, 0.0});
            qDebug()<< QString("Failed to found peak. Found peak for the first merge Spectrum, peak psition:%1").arg(m_energyCalibration[e]);
        } else
        {
            exitflag[e] = true;
            //2、取出卡方值最小组
            int minIndex = -1;
            // 获取最小值的迭代器
            auto minIt = std::min_element(valid_chi_square.begin(), valid_chi_square.end());
            if (minIt != valid_chi_square.end()) {
                // double minValue = *minIt;                    // 最小值
                minIndex = std::distance(valid_chi_square.begin(), minIt); // 下标
                fit_result temp_result;
                temp_result = valid_result.at(minIndex);
                fit_c_2.push_back(temp_result);

                qDebug()<< QString("Sucessful to found peak for the first merge Spectrum, peak position:%1, fit results, c:").arg(m_energyCalibration[e])
                         <<temp_result.c0<<", "<<temp_result.c1<<", "<<temp_result.c2<<", "<<temp_result.c3<<", "<<temp_result.c4;
            }
        }
    }

    //-------------候选峰二次拟合--------------
    for(int e = 0; e<3; e++)
    {
        if(exitflag[e] == false){
            fit_c_2[e].c0 = 0.0;
            fit_c_2[e].c1 = 0.0;
            fit_c_2[e].c2 = 0.0;
            fit_c_2[e].c3 = 0.0;
            fit_c_2[e].c4 = 0.0;
            continue;
        }

        fit_result temp_result = fit_c_2.at(e);
        int start_ch = temp_result.c1 - 2*temp_result.c2; //峰位-2sigma
        int end_ch = temp_result.c1 + 2*temp_result.c2; //峰位+2sigma

        //提取拟合数据, 这里需要注意，MATLAB和C++下标对齐问题。能量刻度y=ax+b时，x从1开始
        QVector<QPointF> fitPoints;
        for(int i = start_ch-1; i<end_ch; i++)
        {
            double xi = (i+1)*1.0;
            double yi = spectrum_smooth2[i]*1.0;
            fitPoints.push_back(QPointF(xi,yi));
        }

        //选用上次的拟合结果作为拟合初值
        double p[5] = {temp_result.c0, temp_result.c1, temp_result.c2, temp_result.c3, temp_result.c4};

        //拟合并给出结果（寻峰不做 R² 门槛，失败时保留粗寻峰结果）
        if (CurveFit::fit_gauss_linear2(fitPoints, p, CurveFit::kNoSpectrumFitR2Check)) {
            fit_c_2[e].c0 = p[0];
            fit_c_2[e].c1 = p[1];
            fit_c_2[e].c2 = p[2];
            fit_c_2[e].c3 = p[3];
            fit_c_2[e].c4 = p[4];
        } else {
            qDebug() << "CurveFit::fit_gauss_linear2 failed, peak:" << m_energyCalibration[e]
                     << ", keep coarse peak find result";
            if (e == 1) {
                exitflag[e] = false;
                fit_c_2[e].c0 = 0.0;
                fit_c_2[e].c1 = 0.0;
                fit_c_2[e].c2 = 0.0;
                fit_c_2[e].c3 = 0.0;
                fit_c_2[e].c4 = 0.0;
            }
        }
    }

    delete[] spectrum_smooth1;
    delete[] spectrum_smooth2;
    return exitflag[0] && exitflag[2] && fit_c_2.size() >= 3;
}


bool ParseData::PeakFind(double* spectrum, QVector<fit_result>& init_c, bool* exitflag)
{
    for(int e = 0; e<2; e++)
    {
        exitflag[e] = false;
    }

    //平滑滤波两次能谱曲线
    double* spectrum_smooth1 = new double[mCHANNEL2048];
    double* spectrum_smooth2 = new double[mCHANNEL2048];
    SysUtils::smooth(spectrum, spectrum_smooth1, mCHANNEL2048, 5);
    SysUtils::smooth(spectrum_smooth1, spectrum_smooth2, mCHANNEL2048, 5);

    //峰位的sigma估值，该值根据能量分辨率可以初步估算得到。
    double Width = 10.0;
    //------------- 寻找候选峰-------------------
    QVector<fit_result> fit_c_2; //两个候选峰峰位各自一组的最优拟合参数
    for(int e = 0; e<2; e++)
    {
        double sigma = init_c.at(e).c2;
        //峰位搜索宽度，根据初值，给出511keV能峰的大致范围。
        quint32 leftCH, rightCH;
        double ch_peak = init_c.at(e).c1;
        leftCH = floor(ch_peak - Width);
        rightCH = floor(ch_peak + Width);

        //先对峰位附近划定不同的道址范围进行拟合，每次拟合的高斯项不同
        QVector<fit_result> fit_result_All; //拟合参数c汇总
        QVector<double> chi_square_All; //拟合结果的卡方统计量汇总
        int numPeak = rightCH - leftCH + 1; //待拟合的数目

        for(int peak = leftCH; peak<=rightCH; peak++)
        {
            //确定拟合道址范围
            int start_ch = peak - 2*floor(sigma);
            int end_ch = peak + 2*floor(sigma);

            //提取拟合数据
            QVector<QPointF> fitPoints;
            double maxY = 0.0;
            //这里需要注意，MATLAB和C++下标对齐问题。能量刻度y=ax+b时，x从1开始
            for(int i = start_ch-1; i<end_ch; i++)
            {
                double xi = (i+1) * 1.0;
                double yi = spectrum_smooth2[i]*1.0;
                fitPoints.push_back(QPointF(xi, yi));
                maxY = (maxY > yi) ? maxY:yi;
            }

            //拟合参数的初值
            double p[] = {maxY, sigma, 10.0, 10.0};
            double chi_square = 0.0;

            //拟合并给出结果
            CurveFit::fit_gauss_linear(fitPoints, p, peak*1.0, &chi_square);

            //提取拟合结果并汇总
            fit_result temp_result;
            temp_result.c0 = p[0];
            temp_result.c1 = peak*1.0;
            temp_result.c2 = p[1];
            temp_result.c3 = p[2];
            temp_result.c4 = p[3];
            temp_result.chi_square = chi_square;
            fit_result_All.push_back(temp_result);
            chi_square_All.push_back(chi_square);
        }

        //根据拟合结果的卡方值来确定最优高斯峰位
        //1、剔除不符合物理规律的拟合结果。
        QVector<fit_result> valid_result;
        QVector<double> valid_chi_square;
        for(auto temp_result:fit_result_All)
        {
            if(temp_result.c0 >= kPeakHeightMin &&
                temp_result.c2 <= 1.5*sigma &&
                temp_result.c2 >= 0.5*sigma)
            {
                valid_result.push_back(temp_result);
                valid_chi_square.push_back(temp_result.chi_square);
            }
        }

        //全部拟合结果都不满足要求，则退出
        if(valid_result.size()<1) {
            exitflag[e] = false;
            fit_c_2.push_back(fit_result{0.0, 0.0, 0.0, 0.0, 0.0, 0.0});
            qDebug()<< QString("Peak position%1, Failed to found peak.").arg(m_energyCalibration[e]);
        }
        else{
            exitflag[e] = true;
            //2、取出卡方值最小组
            int minIndex = -1;
            // 获取最小值的迭代器
            auto minIt = std::min_element(valid_chi_square.begin(), valid_chi_square.end());
            if (minIt != valid_chi_square.end()) {
                // double minValue = *minIt;                    // 最小值
                minIndex = std::distance(valid_chi_square.begin(), minIt); // 下标
                fit_result temp_result;
                temp_result = valid_result.at(minIndex);
                fit_c_2.push_back(temp_result);

                qDebug()<< QString("Peak position%1, Sucessfulll found peak, fit results c:").arg(m_energyCalibration[e])
                         <<temp_result.c0<<", "<<temp_result.c1<<", "<<temp_result.c2<<", "<<temp_result.c3<<", "<<temp_result.c4;
            }
        }
    }

    //-------------候选峰二次拟合--------------
    for(int e = 0; e<2; e++)
    {
        if(exitflag[e] == false){
            fit_c_2[e].c0 = 0.0;
            fit_c_2[e].c1 = 0.0;
            fit_c_2[e].c2 = 0.0;
            fit_c_2[e].c3 = 0.0;
            fit_c_2[e].c4 = 0.0;
            continue;
        }

        fit_result temp_result = fit_c_2.at(e);
        int start_ch = temp_result.c1 - 2*temp_result.c2; //峰位-2sigma
        int end_ch = temp_result.c1 + 2*temp_result.c2; //峰位+2sigma

        //提取拟合数据
        QVector<QPointF> fitPoints;
        //这里需要注意，MATLAB和C++下标对齐问题。能量刻度y=ax+b时，x从1开始
        for(int i = start_ch-1; i<end_ch; i++)
        {
            double xi = (i+1)*1.0;
            double yi = spectrum_smooth2[i]*1.0;
            fitPoints.push_back(QPointF(xi, yi));
        }

        //选用上次的拟合结果作为拟合初值
        double p[5] = {temp_result.c0, temp_result.c1, temp_result.c2, temp_result.c3, temp_result.c4};

        //拟合并给出结果（寻峰不做 R² 门槛，失败时保留粗寻峰结果）
        if (CurveFit::fit_gauss_linear2(fitPoints, p, CurveFit::kNoSpectrumFitR2Check)) {
            fit_c_2[e].c0 = p[0];
            fit_c_2[e].c1 = p[1];
            fit_c_2[e].c2 = p[2];
            fit_c_2[e].c3 = p[3];
            fit_c_2[e].c4 = p[4];
        } else {
            qDebug() << "CurveFit::fit_gauss_linear2 failed, peak:" << m_energyCalibration[e]
                     << ", keep coarse peak find result";
        }

        if (fit_c_2[e].c0 < kPeakHeightMin)
            exitflag[e] = false;
    }

    //返回拟合结果
    init_c = fit_c_2;

    delete[] spectrum_smooth1;
    delete[] spectrum_smooth2;
    return exitflag[0] && exitflag[1];
}


/**
 * @brief 剥谱
 * @param spectrum 能谱数组，起始道址设定为1，数组长固定G_CHANNEL-3
 * @param energy_scale，能量刻度系数, E = ax+b, energy_scale =[a,b]
 * @param init_c 拟合系数初始值,11个参数，匹配拟合类型1 {p0,p1,p2,p3,p4,p5,p6,p7,p8,p9,p10,p11}，若不存在846keV则前三个参数为0
 * 拟合类型1：fit_type1 = p(1).*exp(-1/2*((x-p(2))./p(3)).^2) + p(4).*exp(-1/2*((x-p(5))./p(6)).^2) + p(7).*x.^4 + p(8).*x.^3 + p(9).*x.^2 + p(10).*x + p(11);
 * 拟合类型2：fit_type2 = p(1).*exp(-1/2*((x-p(2))./p(3)).^2) + p(4).*x.^4 + p(5).*x.^3 + p(6).*x.^2 + p(7).*x + p(8);
 * @param exitflag [846,909]两个能峰的寻峰标志位，false:没找到峰，true:找到了峰
 * @param deadTimeRate 死时间率
 * @param peakCountOut 909keV峰面积净计数
 * @param peakPositionOut 909keV峰位道址，可为空
 * @return 是否成功
 * 拟合成功后，init_c更新为拟合结果
 */
bool ParseData::SpecStripping(double *spectrum, double energy_scale[], QVector<double> &init_c,
                              bool *exitflag, double deadTimeRate, double *peakCountOut,
                              double *peakPositionOut)
{
    // 909keV峰位
    double position909peak = 0.0; 
    
    std::vector<double> data;
    data.reserve(mCHANNEL2048);
    for (int i = 0; i < mCHANNEL2048; ++i)
        data.push_back(spectrum[i]);

    const std::vector<double> filtered = SysUtils::lowpass(data, 0.1);

    const double energyLow = exitflag[0] ? 800.0 : 850.0;
    const double energyHigh = 1100.0;
    const double a = energy_scale[0];
    const double b = energy_scale[1];

    QVector<QPointF> fitPoints;
    QVector<double> fitx;
    QVector<double> fity;
    QVector<double> fity_curve;

    for (int i = 0; i < mCHANNEL2048; ++i) {
        const double channel = static_cast<double>(i + 1);
        const double energy = channel * a + b;
        if (energy <= energyLow || energy >= energyHigh)
            continue;
        const double yi = filtered[static_cast<std::size_t>(i)];
        fitPoints.push_back(QPointF(channel, yi));
        fitx.push_back(channel);
        fity.push_back(yi);
    }

    const int ch_count = fitPoints.size();
    if (ch_count < CurveFit::gauss_arr_count_min)
        return false;

    const QVector<double> savedInitial = init_c;
    alglib::real_1d_array c;
    QVector<double> residual_rate;

    // 846keV能峰存在则采用双高斯拟合，不存在则采用单高斯拟合
    if (exitflag[0]) {
        double p[11];
        for (int i = 0; i < 11; ++i)
            p[i] = init_c[i];

        if (!CurveFit::fit_2gauss_ploy4(fitPoints, p, residual_rate, mSpectrumFitR2Min))
            return false;
        for (int i = 0; i < 11; ++i)
            init_c[i] = p[i];
        for (int i = 0; i < 6; ++i)
            init_c[i] = std::abs(init_c[i]);
        c.setcontent(11, p);

        for (int i = 0; i < ch_count; ++i) {
            alglib::real_1d_array xData;
            xData.setlength(1);
            xData[0] = fitx.at(i);
            double y = 0.0;
            CurveFit::function_2gauss_poly4(c, xData, y, nullptr);
            fity_curve.push_back(y);
        }
        // 记录909keV峰位
        position909peak = p[4];
    } else {
        double p[8];
        // 跳过前三个参数，从第四个参数开始赋值
        for (int i = 0; i < 8; ++i)
            p[i] = init_c[i + 3];

        if (!CurveFit::fit_gauss_ploy4(fitPoints, p, residual_rate, mSpectrumFitR2Min))
            return false;
        init_c[0] = savedInitial[0];
        init_c[1] = 846.0;
        init_c[2] = savedInitial[2];
        for (int i = 0; i < 8; ++i)
            init_c[i + 3] = p[i];
        for (int i = 0; i < 6; ++i)
            init_c[i] = std::abs(init_c[i]);
        c.setcontent(8, p + 0);

        for (int i = 0; i < ch_count; ++i) {
            alglib::real_1d_array xData;
            xData.setlength(1);
            xData[0] = fitx.at(i);
            double y = 0.0;
            CurveFit::function_gauss_poly4(c, xData, y, nullptr);
            fity_curve.push_back(y);
        }
        // 记录909keV峰位
        position909peak = p[1];
    }

    if (exitflag[0]) {
        const double bsl_L_846 = init_c[1] - 2 * init_c[2];
        const double bsl_H_846 = init_c[1] + 2 * init_c[2];
        QVector<double> edgeCounts;
        for (int i = 0; i < ch_count; ++i) {
            const double x = fitx.at(i);
            if (x >= bsl_L_846 && x <= bsl_H_846)
                edgeCounts.push_back(fity.at(i));
        }
        if (edgeCounts.size() >= 2) {
            const double bsl846 = (edgeCounts.first() + edgeCounts.last()) * 0.5;
            if (init_c[0] <= std::sqrt(bsl846) + 40.0)
                exitflag[0] = false;
        }
    }

    if (exitflag[1]) {
        const double bsl_L_909 = init_c[4] - 2 * init_c[5];
        const double bsl_H_909 = init_c[4] + 2 * init_c[5];
        QVector<double> edgeCounts;
        for (int i = 0; i < ch_count; ++i) {
            const double x = fitx.at(i);
            if (x >= bsl_L_909 && x <= bsl_H_909)
                edgeCounts.push_back(fity.at(i));
        }
        if (edgeCounts.size() >= 2) {
            const double bsl909 = (edgeCounts.first() + edgeCounts.last()) * 0.5;
            if (init_c[3] <= std::sqrt(bsl909) + 50.0)
                exitflag[1] = false;
        }
    }

    const double pi = 3.141592654;
    const double denom = (1.0 - deadTimeRate) > 1e-12 ? (1.0 - deadTimeRate) : 1.0;
    if (peakCountOut)
        *peakCountOut = init_c[3] * init_c[5] * std::sqrt(pi) / denom;
    if (peakPositionOut)
        *peakPositionOut = position909peak;

    specStripData_x.append(fitx);
    specStripData_y.append(fity);
    specStripData_fity.append(fity_curve);
    specStripData_residualRate.append(residual_rate);

    if (specStrip_rightCH.isEmpty())
        specStrip_rightCH.push_back(-1);
    specStrip_rightCH.push_back(specStripData_x.size() - 1);

    return true;
}

/**
 * @brief fit909data 对909全能峰计数随时间的变化拟合出来
 * @return
 */
bool ParseData::fit909data()
{
    count909_fitcount.clear();
    count909_residual.clear();
    const int ch_count = count909_count.size();
    if (ch_count < 2)
        return false;

    QVector<QPointF> fitPoints;
    QVector<double> fitx;
    QVector<double> fity;
    fitPoints.reserve(ch_count);
    fitx.reserve(ch_count);
    fity.reserve(ch_count);
    for (int i = 0; i < ch_count; ++i) {
        fitx.push_back(count909_time.at(i));
        fity.push_back(count909_count.at(i));
        fitPoints.push_back(QPointF(fitx.last(), fity.last()));
    }

    double fit_a = fity.first();
    QVector<double> residual_rate;
    if (!CurveFit::fit_exp_decay(fitPoints, &fit_a, residual_rate))
        return false;

    QVector<double> fity_curve;
    fity_curve.reserve(ch_count);
    alglib::real_1d_array c;
    c.setcontent(1, &fit_a);
    for (int i = 0; i < ch_count; ++i) {
        alglib::real_1d_array xData;
        xData.setlength(1);
        xData[0] = fitx.at(i);
        double yFit = 0.0;
        CurveFit::function_exp_decay(c, xData, yFit, nullptr);
        fity_curve.push_back(yFit);
        count909_residual.push_back((fity.at(i) - yFit) / yFit * 100.0);
    }

    count909_fitcount = fity_curve;

    const double t1 = log(2.0) / (78.4 * 60.0);
    const double timeBinMin = mTimeBin_online / 60.0;
    mInitialCount = fit_a / (std::exp(timeBinMin * t1) - 1.0);
    return true;
}

//获取剥谱图像数据,给出第i个能谱的剥谱数据，三条曲线数据
QVector<specStripData> ParseData::GetStripData(int specID)
{
    QVector<specStripData> pictureData;
    if(specID >= specStrip_rightCH.size()-1)
    {
        printf("ERROR in \"GetStripData(int specID)\": specID is over than size of \"QVector<double> specStrip_leftCH\"");
        return pictureData;
    }

    specStripData tempData;
    int left = specStrip_rightCH.at(specID) + 1;
    int right = specStrip_rightCH.at(specID+1);
    for(int i = left; i<=right; i++)
    {
        tempData.x = specStripData_x.at(i);
        tempData.y = specStripData_y.at(i);
        tempData.fit_y = specStripData_fity.at(i);
        tempData.residual_rate = specStripData_residualRate.at(i);
        pictureData.push_back(tempData);
    }
    return pictureData;
}

QVector<specStripData> ParseData::GetCount909Data()
{
    QVector<specStripData> pictureData;
    specStripData tempData;
    for(int i=0; i<count909_time.size() && i<count909_fitcount.size() && i<count909_residual.size(); i++)
    {
        tempData.x = count909_time.at(i);
        tempData.y = count909_count.at(i);
        tempData.fit_y = count909_fitcount.at(i);
        tempData.residual_rate = count909_residual.at(i);
        pictureData.push_back(tempData);
    }
    return pictureData;
}

/**
 * @brief clearFitResult 清空拟合结果
 */
void ParseData::clearFitResult()
{
    mInitialCount = 0.0;
    m_successfulFitSegmentCount = 0;
    m_fitFailureMessages.clear();
    count909_time.clear();
    count909_count.clear(); //909keV计数，采用峰面积求得
    count909_peakpos.clear(); //909keV峰位道址
    count909_fitcount.clear(); //909keV计数随时间变化的拟合曲线
    count909_residual.clear(); //残差

    specStripData_x.clear(); //拟合数据点x坐标
    specStripData_y.clear(); //拟合数据点y坐标
    specStripData_fity.clear(); //拟合曲线y
    specStripData_residualRate.clear(); //残差
    specStrip_rightCH.clear();
}

/**
 * @brief parseH5File 解析h5文件，获取能谱数据
 * @param filePath h5文件路径
 * @param detectorId 探测器ID
 * @return 成功解析的能谱数量
 */
int ParseData::parseH5File(const QString& filePath, const quint32 detectorId)
{
    mDetId = detectorId;
    if (filePath.isEmpty() || !QFileInfo::exists(filePath)){
        qDebug()<<QString("1% is not exists!").arg(filePath);
        return 0;
    }

    // 解析文件，获取能谱
    QTextCodec* gbk_codec = QTextCodec::codecForName("GBK");
    QByteArray filePathBytes = gbk_codec->fromUnicode(filePath);
    bool readFlag = HDF5Settings::readAllH5Spectrum(filePathBytes.toStdString(), detectorId, m_allSpec);
    if(readFlag)  return m_allSpec.size();
    else return 0;
}

// 解析大文件中的网络数据包（流式读取，内部委托 OfflineSpectrumDatReader）
int ParseData::parseDatFile(const QString &filePath)
{
    m_allSpec.clear();
    m_parasemode = offlineMode;

    if (!OfflineSpectrumDatReader::read(filePath, m_allSpec))
        return -1;

    return m_allSpec.size();
}

#if 0 // 旧 0x55 协议实现，已废弃
int ParseData::parseDatFile_legacy(const QString &filePath)
{
    quint64 bufferSize = 10*1024*1024;

    //重新初始化相关参数
    totalPackets = 0;
    bytesProcessed = 0;
    m_parasemode = offlineMode;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "无法打开文件:" << filePath;
        return -1;
    }

    qint64 fileSize = file.size();
    qDebug() << "开始解析文件，大小:" << fileSize << "字节";

    QElapsedTimer timer;
    timer.start();

    QByteArray readBuffer;
    readBuffer.reserve(bufferSize);

    qint64 bytesRead = 0;
    int packetsFound = 0;

    //先估算能谱的总个数，这里预分配内存
    int packSize = (1 + 2050 + 5) *4 + 13;
    m_allSpec.clear();
    m_allSpec.reserve(fileSize/packSize);

    processingBuffer.clear();
    processingBuffer.reserve(bufferSize);

    while (bytesRead < fileSize) {
        // 读取一块数据
        QByteArray chunk = file.read(bufferSize);
        if (chunk.isEmpty()) {
            break;
        }

        bytesRead += chunk.size();

        // 将新数据添加到处理缓冲区
        processingBuffer.append(chunk);

        // 处理缓冲区中的数据
        packetsFound += processBuffer(false);

        // 显示进度
        if (bytesRead % (100 * 1024 * 1024) == 0 || bytesRead == fileSize) {
            double progress = (double)bytesRead / fileSize * 100;
            qDebug() << QString("preocess: %1% (%2/%3 MB), found package: %4")
                            .arg(progress, 0, 'f', 1)
                            .arg(bytesRead / (1024 * 1024))
                            .arg(fileSize / (1024 * 1024))
                            .arg(packetsFound);
        }
    }

    // 处理缓冲区中剩余的数据
    packetsFound += processBuffer(true);

    qDebug() << "Analysis completed! elapsed time:" << timer.elapsed() / 1000.0 << "seconds";
    qDebug() << "Total package:" << packetsFound;
    qDebug() << "Total number of bytes processed:" << bytesProcessed;

    file.close();

    return packetsFound;
}
#endif

// 处理缓冲区中的数据
int ParseData::processBuffer(bool isFinal)
{
    int packetsFound = 0;
    int searchPosition = 0;

    while (searchPosition <= processingBuffer.size()) {
        // 查找包头 (0x55)
        int headerPos = findPacketHeader(searchPosition);
        if (headerPos == -1) {
            break; // 没有找到更多包头
        }

        // 查找下一个包头
        // if(packetsFound> 1) searchPosition = headerPos + specPackLen; //这种使用方式适用于网络数据不丢字节。一旦丢字节，该方式会引起更多的数据帧丢失。进过检验，运算与原方式几乎相同
        // else searchPosition = headerPos + 1;
        searchPosition = headerPos + 1;

        int nextHeaderPos = findPacketHeader(searchPosition);
        if (nextHeaderPos == -1) {
            searchPosition--; //回退一个位置，此时已经是缓存区最后一个包
            break; // 没有找到更多包头
        }

        // 提取和处理数据包
        if(processSinglePacket(headerPos, nextHeaderPos, m_parasemode)){
            packetsFound++;
        }

        // 移动到下一个包
        searchPosition = nextHeaderPos;
    }

    // 清理已处理的缓冲区数据
    cleanupBuffer(searchPosition);
    bytesProcessed += searchPosition;

    return packetsFound;
}

// 处理单个数据包
bool ParseData::processSinglePacket(int headerPos, int nextHeaderPos, paraseMode mode)
{
    int packLen = nextHeaderPos - headerPos;
    // 提取数据部分
    QByteArray packetData = processingBuffer.mid(headerPos, packLen);

    // 接收方转码
    QByteArray uncodedMsg = encode(packetData, m_receiverFrom1, m_receiverTo1, m_receiverFrom2, m_receiverTo2);

    // 检查报文完整性
    if (!checkFrame(uncodedMsg)) {
        qDebug() << Q_FUNC_INFO << u8"报文不完整";
        return 0;
    }

    //检查是否为能谱数据
    if(!isSpecData(uncodedMsg)) return 0;

    totalPackets++;
    qDebug() << "找到第" << totalPackets << "个数据包, 数据长度:" << uncodedMsg.size();
    // if (totalPackets % 1000 == 0) {
    // qDebug() << "找到第" << totalPackets << "个数据包, 数据长度:" << uncodedMsg.size();

    // 可以在这里进行更复杂的数据解析
    // }
    // 这里添加您的数据解析逻辑
    QByteArray validData = uncodedMsg.mid(19, 2048*4);
    H5Spectrum tempSpecdata;
    if(getDataFromQByte(validData, tempSpecdata))
    {
        H5Spectrum sepc_16byte; // 目标结构体

        // 复制基本字段
        sepc_16byte.sequence = tempSpecdata.sequence;
        sepc_16byte.measureTime = tempSpecdata.measureTime;
        sepc_16byte.deathTime = tempSpecdata.deathTime;

        // 转换能谱数据，舍弃高位（只保留低16位）
        for (int i = 0; i < mCHANNEL2048; ++i) {
            // 将32位数值截断为16位（舍弃高16位）
            sepc_16byte.spectrum[i+3] = static_cast<quint16>(tempSpecdata.spectrum[i]);
        }
        m_allSpec.push_back(sepc_16byte);

        //在线测量数据处理
        if(mode == onlineMode){
            mergeSpecTime_online(tempSpecdata);
        }
        return 1;
    }

    return 0;
}

// 转码
// #include <emmintrin.h> // _MM_HINT_T0
QByteArray ParseData::encode(const QByteArray &data, const QByteArray &from1, const QByteArray &to1, const QByteArray &from2, const QByteArray &to2) {

    // 数据为空或者转码规则为空 直接返回
    if (data.isEmpty() || from1.isEmpty() || from2.isEmpty()) {
        return QByteArray();
    }

    // 记录报文头
    QByteArray result;
    // 预计算转码后的数据大小，避免多次扩容
    result.reserve(specPackLen);
    result.append(data.at(0));

    // 从第二位开始进行转码
    int i = 1;
    const char* d = data.constData();
    const int from1Size = from1.size();
    const int from2Size = from2.size();
    const int dataSize = data.size();
    const char* f1 = from1.constData();
    const char* f2 = from2.constData();

    // memcmp比较可以极大地提高速度，提速33倍
    while (i < dataSize) {
        bool matched = false;
        if (i + from1Size <= dataSize && memcmp(d + i, f1, from1Size) == 0) {
            result.append(to1);
            i += from1Size;
            matched = true;
        } else if (i + from2Size <= dataSize && memcmp(d + i, f2, from2Size) == 0) {
            result.append(to2);
            i += from2Size;
            matched = true;
        }

        if (!matched) {
            result.append(d[i]);
            i++;
        }
    }

    // 返回转码后的报文
    return result;
}

// 报文完整性检查
bool ParseData::checkFrame(const QByteArray &data)
{
    // 检查报文是否完整 是否以0x55开头 是否以0x00 0x23 结尾
    if (data.isEmpty() || static_cast<quint8>(data.at(0)) != 0x55 ||
        static_cast<quint8>(data.at(data.size() - 2)) != 0x00 || static_cast<quint8>(data.at(data.size() - 1)) != 0x23) {
        qDebug() << Q_FUNC_INFO << u8"报文不完整或格式错误: 当前报文开头为" << data.at(0) << " 结尾为" << data.at(data.size() - 2) << data.at(data.size() - 1);
        return false;
    }
    // 检查帧长度是否正确
    int frameLength = static_cast<quint32>((data.at(1) << 8)) | static_cast<quint32>(data.at(2));
    int dataLength = data.size();

    if (frameLength != data.size()) {
        qDebug() << Q_FUNC_INFO << u8"帧长度错误: frameLength=" << frameLength << " data.size()=" << data.size();
        return false;
    }
    return true;
}

//检查是否是能谱数据
bool ParseData::isSpecData(const QByteArray &data)
{
    // 检查报文是否完整 是否以0x55开头 是否以0x00 0x23 结尾
    if (data.isEmpty() || static_cast<quint8>(data.at(8)) != 0xD2) {
        QByteArray codeType;
        codeType.push_back(data.at(8));
        qDebug() << Q_FUNC_INFO << u8"is not a spectrum package, comman code is :" << codeType.toHex(' ').toUpper();
        return false;
    }

    return true;
}

// 使用reinterpret_cast直接转换
bool ParseData::getDataFromQByte(const QByteArray &byteArray, H5Spectrum &DataPacket) {
    if (byteArray.size() != offsetof(H5Spectrum, sequence)) {
        qWarning() <<Q_FUNC_INFO<< "数据大小不匹配";
        return false;
    }

    memcpy(&DataPacket, byteArray.constData(), byteArray.size());

    // 字节序问题：由于x86 是小端序，网络数据QByteArray通常是大端序，这里必须转化一次才正常
    // DataPacket.convertNetworkToHost();
    return true;
}

// 清理已处理的缓冲区数据
void ParseData::cleanupBuffer(int bytesToKeep)
{
    if (bytesToKeep > 0) {
        // 保留未处理的数据
        processingBuffer = processingBuffer.mid(bytesToKeep);
    }

    // 限制缓冲区大小，防止内存占用过大
    if (processingBuffer.capacity() > 10 * 1024 * 1024) {
        processingBuffer.squeeze(); // 释放未使用的内存
    }
}

// 查找包头
int ParseData::findPacketHeader(int startPos) {
    const char* data = processingBuffer.constData();
    int size = processingBuffer.size();
    const char target = 0x55;

    // 使用指针直接操作，减少边界检查开销
    for (int i = startPos; i < size; ++i) {
        if (data[i] == target) {
            return i;
        }
    }
    return -1;
}
/**
    * mergeSpecTime_offline：提取目标时间段能谱数据，根据时间道宽合并能谱。需要处理丢包，
    * 程序使用范围：用于离线数据处理，单个能谱的测量时间必须大于1s，否则对丢包的修正处理无效。
    * quint64 timeBin, 时间宽度,单位s
    * quint64 start_time, 起始时间,单位s
    * quint64 end_time, 结束时间,单位s
**/
void ParseData::mergeSpecTime_offline(quint64 timeBin, quint64 start_time, quint64 end_time)
{
    mTimeBin_online = timeBin;
    quint32 spectrumNum = (end_time - start_time+1)/timeBin; //整除，给出合并后的能谱个数，对于最后一段时间不满timeBin宽度的能谱直接丢弃。
    if(spectrumNum == 0) return;

    //初始化合并能谱
    m_mergeSpec.resize(spectrumNum);
    for(auto it = m_mergeSpec.begin(); it!=m_mergeSpec.end(); ++it)
    {
        it->specTime = timeBin*1000;
    }

    quint64 spectDeltaT = m_allSpec.at(0).measureTime; //单个能量测量时间，单位ms. 室假设所有的能谱时间间隔都一样，如果不一样需要重新采取其他算法。
    quint64 lossTime = 0; //死时间，单位ns。
    int lastSpecID = 0; //上一个能谱的编号。
    qint64 currentTime = T0_beforeShot *1000; //当前能谱对应时刻，应是能谱结束时刻，单位ms
    qint64 accumulateTime = 0; //计算自start_time开始到当前能谱的时间。单位ms
    for(auto spec:m_allSpec)
    {
        //计算丢包带来的死时间
        qint64 lossTimeTemp = (spec.sequence - lastSpecID - 1)*spectDeltaT; //单位ms
        currentTime += spectDeltaT + lossTimeTemp; //ms
        lossTime = lossTimeTemp * 1000000 + spec.deathTime*10; //ns
        if(currentTime >= start_time*1000)
        {
            accumulateTime += lossTimeTemp + spectDeltaT;//ms

            //对于最后一段时间数据，由于时间宽度不满一个timeBin，直接舍弃。
            if(accumulateTime > spectrumNum*timeBin*1000) break;

            //给出当前所属的合并能谱序次号
            int mergeID = accumulateTime/1000/timeBin;

            //对每组合并能谱中最后一个子能谱，归并到上一能谱中。
            if( accumulateTime % (timeBin*1000) == 0)  {
                mergeID--;
                qDebug()<<QString("The last sub-energy spectrum in the %1s merged time channel ").arg(mergeID);
            }

            if(mergeID >=spectrumNum) {
                qDebug()<<"---------计算异常------------";
                break;
            }

            //更新合并能谱的数值
            m_mergeSpec[mergeID].currentTime = currentTime;
            m_mergeSpec[mergeID].deathTime += lossTime;
            for(int i=0; i<mCHANNEL2048; i++)
            {
                int ch = i*4;
                int sum = spec.spectrum[ch] + spec.spectrum[ch+1] + spec.spectrum[ch+2] + spec.spectrum[ch+3];
                m_mergeSpec[mergeID].spectrum[i] += sum;
            }
        }
        lastSpecID = spec.sequence;
    }
}

bool ParseData::getResult_offline(quint64 timeBin, quint64 start_time, quint64 end_time)
{
    mergeSpecTime_offline(timeBin, start_time, end_time);
    if (m_mergeSpec.isEmpty())
        return false;
    getResult(m_mergeSpec);
    return m_successfulFitSegmentCount > 0;
}

void ParseData::addCompressedSpectrumToSegment(mergeSpecData &seg, const H5Spectrum &spec, quint64 lossTimeNs)
{
    seg.deathTime += lossTimeNs;
    seg.sumMeasureTimeMs += spec.measureTime;
    ++seg.rowCount;
    for (int i = 0; i < mCHANNEL2048; ++i) {
        const int ch = i * 4;
        seg.spectrum[i] += spec.spectrum[ch] + spec.spectrum[ch + 1]
                         + spec.spectrum[ch + 2] + spec.spectrum[ch + 3];
    }
}

/**
 * @brief 多文件离线能谱合并与解析（对齐 MATLAB Data_Analysis_Main.m 叠加逻辑）
 *
 * 处理流程（每个 H5 文件、每个探测器通道独立进行）：
 * 1. 读取该通道全部原始能谱；能谱时刻由「文件开测时刻 + (sequence-sequence0)×measureTime」推算（不用 FPGA UTC）；
 * 2. 按 sequence 连续性切分连续段（相邻序号差 > 1 为断点），再按解析窗口筛选；
 * 3. 每连续段从首条能谱起，每 spectraPerBin 条原始能谱合并为 1 段（8192→2048 压缩求和）；
 *    spectraPerBin = stepSec×1000 / measureTimeMs；fileStart 优先 Config/StartTime，否则文件名；
 * 4. 不足一整段的尾部能谱丢弃；合并段 currentTime 取该 bin 最后一条子能谱结束时刻（推算起始+measureTime）相对打靶的右端点；
 * 5. 多文件产生的合并段按 currentTime 排序串联后，调用 getResult() 寻峰/剥谱/衰减拟合。
 *
 * @param filePaths    测量 H5 文件路径列表
 * @param detectorId   探测器编号 1..DET_NUM
 * @param fusionTime   打靶零时刻
 * @param windowLeft   解析窗口左端点 UTC（fusionTime + timeStart）
 * @param windowRight  解析窗口右端点 UTC（fusionTime + timeEnd）
 * @param stepSec      合并时间步长 Time_Bin，单位 s
 * @return 至少合并出一段能谱时返回 true（拟合失败不影响合并结果保留）
 */
bool ParseData::getResult_offlineMultiFileUtc(const QStringList &filePaths,
                                              quint32 detectorId,
                                              const QDateTime &fusionTime,
                                              const QDateTime &windowLeft,
                                              const QDateTime &windowRight,
                                              int stepSec)
{
    // -------- 0. 初始化解析状态 --------
    mDetId = detectorId;
    m_allSpec.clear();
    m_mergeSpec.clear();
    mTimeBin_online = stepSec;

    // -------- 1. 参数合法性检查 --------
    if (filePaths.isEmpty() || !fusionTime.isValid() || !windowLeft.isValid() || !windowRight.isValid())
        return false;
    if (stepSec <= 0 || windowLeft >= windowRight)
        return false;

    // 解析窗口与打靶时刻（ms）
    const qint64 leftMs = windowLeft.toMSecsSinceEpoch();
    const qint64 rightMs = windowRight.toMSecsSinceEpoch();
    const qint64 fusionMs = fusionTime.toMSecsSinceEpoch();

    QTextCodec *gbk_codec = QTextCodec::codecForName("GBK");
    quint32 totalRawRows = 0; // 参与合并的原始能谱条数（统计用）

    // -------- 2. 逐文件读取并分段合并 --------
    for (const QString &filePath : filePaths) {
        if (isParseCancelled())
            return false;

        if (filePath.isEmpty() || !QFileInfo::exists(filePath))
            continue;

        // 2.1 读取本文件指定通道的全部能谱
        QVector<H5Spectrum> fileSpec;
        const QByteArray filePathBytes = gbk_codec->fromUnicode(filePath);
        const std::string filePathStd = filePathBytes.toStdString();
        if (!HDF5Settings::readAllH5Spectrum(filePathStd, detectorId, fileSpec)
            || fileSpec.isEmpty()) {
            continue;
        }

        // 2.2 文件开测时刻与序号基准（不依赖 FPGA UTC）
        const qint64 fileStartMs = resolveFileStartMs(filePath, filePathStd);
        if (fileStartMs == 0) {
            qWarning().nospace() << "Skip file (no valid start time):" << QFileInfo(filePath).fileName();
            continue;
        }

        quint32 measureTimeMs = 0;
        quint32 sequence0 = 0;
        for (const H5Spectrum &spec : fileSpec) {
            if (spec.measureTime == 0)
                continue;
            measureTimeMs = spec.measureTime;
            sequence0 = spec.sequence;
            break;
        }
        if (measureTimeMs == 0)
            continue;

        const int spectraPerBin = static_cast<int>(static_cast<qint64>(stepSec) * 1000
                                                   / static_cast<qint64>(measureTimeMs));
        if (spectraPerBin <= 0) {
            qWarning().nospace() << "Skip file (step shorter than measureTime):" << QFileInfo(filePath).fileName()
                                 << " stepSec=" << stepSec << " measureTimeMs=" << measureTimeMs;
            continue;
        }

        const auto specStartMs = [&](const H5Spectrum &spec) -> qint64 {
            return syntheticSpectrumStartMs(fileStartMs, sequence0, measureTimeMs, spec.sequence);
        };

        int fileMergedCount = 0;
        int rawInWindow = 0;

        // 2.3 先按 sequence 断点分段，再在段内筛选窗口并合并（避免窗口截断造成伪断点）
        const QVector<QVector<H5Spectrum>> runs = splitBySequenceRuns(fileSpec);
        for (const QVector<H5Spectrum> &run : runs) {
            QVector<H5Spectrum> filtered;
            filtered.reserve(run.size());
            for (const H5Spectrum &spec : run) {
                const qint64 startMs = specStartMs(spec);
                const qint64 endMs = startMs + static_cast<qint64>(spec.measureTime);
                if (endMs <= leftMs || startMs >= rightMs)
                    continue;
                filtered.append(spec);
            }
            if (filtered.isEmpty())
                continue;

            rawInWindow += filtered.size();

            // 2.4 在连续段内按 spectraPerBin 条合并
            const int completeBins = filtered.size() / spectraPerBin;
            int lastSpecID = 0;
            for (int bin = 0; bin < completeBins; ++bin) {
                if (isParseCancelled())
                    return false;

                const int binStart = bin * spectraPerBin;
                const int binEnd = binStart + spectraPerBin;

                mergeSpecData seg;
                seg.specTime = static_cast<quint64>(stepSec) * 1000ULL;

                for (int idx = binStart; idx < binEnd; ++idx) {
                    const H5Spectrum &spec = filtered.at(idx);
                    const qint64 lossTimeTempMs = static_cast<qint64>(spec.sequence - lastSpecID - 1)
                        * static_cast<qint64>(measureTimeMs);
                    const quint64 lossTimeNs = static_cast<quint64>(qMax<qint64>(0, lossTimeTempMs)) * 1000000ULL
                        + static_cast<quint64>(spec.deathTime) * 10ULL;
                    addCompressedSpectrumToSegment(seg, spec, lossTimeNs);
                    lastSpecID = spec.sequence;
                    ++totalRawRows;
                }

                // 合并段右端点 = 本 bin 最后一条子能谱的结束时刻（起始 + measureTime），再转为相对打靶 ms
                const H5Spectrum &lastSpecInBin = filtered.at(binEnd - 1);
                const qint64 rightEndpointMs = specStartMs(lastSpecInBin)
                    + static_cast<qint64>(lastSpecInBin.measureTime);
                seg.currentTime = rightEndpointMs - fusionMs;
                m_mergeSpec.append(seg);
                ++fileMergedCount;
                reportParseProgress(ParseProgressPhase::Merge,
                                    m_mergeSpec.size(),
                                    m_mergeSpec.size());
            }
        }

        if (fileMergedCount == 0) {
            qInfo().nospace() << "Skip file (no merged segments in window):" << QFileInfo(filePath).fileName()
                              << " det" << detectorId;
            continue;
        }

        qDebug().nospace() << "Merge file:" << QFileInfo(filePath).fileName()
                          << " det" << detectorId
                          << " fileStartMs" << fileStartMs
                          << " measureTimeMs" << measureTimeMs
                          << " sequence0" << sequence0
                          << " rawInWindow" << rawInWindow
                          << " spectraPerBin" << spectraPerBin
                          << " mergedSegments" << fileMergedCount;
    }

    // -------- 3. 多文件结果校验 --------
    if (m_mergeSpec.isEmpty() || totalRawRows == 0)
        return false;

    if (isParseCancelled())
        return false;

    // -------- 4. 按合并段时刻排序后串联，执行寻峰/剥谱 --------
    std::sort(m_mergeSpec.begin(), m_mergeSpec.end(),
              [](const mergeSpecData &a, const mergeSpecData &b) {
                  return a.currentTime < b.currentTime;
              });

    const int totalSegments = m_mergeSpec.size();
    reportParseProgress(ParseProgressPhase::Fit, 0, totalSegments);
    getResult(m_mergeSpec);
    return !isParseCancelled();
}
