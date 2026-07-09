#ifndef PARSEDATA_H
#define PARSEDATA_H

//中子产额分析算法
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QVector>
#include <QtEndian> //qFromBigEndian需要
#include <cstring>      // 用于内存初始化（如memset）
#include <QVector>
#include <QTextStream> // 添加文本流支持

#include <atomic>
#include <functional>

#include "spectypes.h"
#include "curveFit.h"

enum class ParseProgressPhase {
    Merge,
    Fit
};

using ParseProgressCallback = std::function<void(ParseProgressPhase phase, int current, int total)>;

// 存放拟合参数值 fit_type = c0*exp(-0.5*pow((x-c1)/c2,2)) + c3*x + c4;
struct fit_result{
    double c0; //高斯部分的峰高
    double c1; //高斯部分的峰位
    double c2; //高斯部分的sigma
    double c3;
    double c4;
    double chi_square; //拟合结果卡方值
};

struct specStripData{
    double x; //待拟合数据点x
    double y; //待拟合数据点y
    double fit_y; //拟合曲线y
    double residual_rate; //残差率
};

enum paraseMode{
    offlineMode = 0,
    onlineMode = 1,
};

class ParseData
{
public:
    // 数据包结构定义,合并后的能谱数据，已经做了丢包处理
    #pragma pack(push, 1) // 确保1字节对齐
    struct mergeSpecData {
        qint64 currentTime; //能谱测量结束时刻，打靶时刻为零时刻，早于打靶为负数，单位ms
        quint64 specTime;   //能谱测量对应时长，单位ms
        quint64 deathTime;  //能谱测量死时间，单位ns
        quint32 rowCount = 0;         // 合并子能谱个数
        quint64 sumMeasureTimeMs = 0; // 各子能谱 measureTime 累加，单位 ms

        quint32 spectrum[2048];  // 能谱，将FPGA传输来的8192道压缩为2048道。

        mergeSpecData()
            : currentTime(0),
            specTime(0),
            deathTime(0),
            rowCount(0),
            sumMeasureTimeMs(0) {
            for (int i = 0; i < 2048; ++i) {
                spectrum[i] = 0;
            }
        }
    };
    #pragma pack(pop) // 恢复默认对齐

public:
    ParseData();
    ~ParseData();
    
    QVector<mergeSpecData> GetMergeSpec(){ return mMergeSpec;}

    /** @brief 最近一次 getResult 成功拟合的分段能谱数量 */
    int successfulFitSegmentCount() const { return mSuccessfulFitSegmentCount; }

    /** @brief 最近一次 getResult 中各分段拟合失败信息 */
    QStringList fitFailureMessages() const { return mFitFailureMessages; }

    /** @brief 读取最近一次成功剥谱分段得到的能量刻度 E = kx + b */
    bool latestEnergyCalibration(double *k, double *b) const;

    //获取剥谱图像数据,给出第i个能谱的剥谱数据，三条曲线数据
    QVector<specStripData> GetStripData(int specID);

    QVector<specStripData> GetCount909Data();

    const QVector<double> &count909PeakPos() const { return mCount909PeakPos; }

    double GetInitialCount(){return mInitialCount;}

    /**
     * @brief mergeSpecTime 提取目标时间段能谱数据，根据时间道宽合并能谱，用于离线分析
     * @param timeBin 时间宽度,单位s
     * @param start_time 起始时间,单位s
     * @param end_time 结束时间,单位s
     */
    void mergeSpecTime_offline(quint64 timeBin, quint64 start_time, quint64 end_time);

    /**
     * @brief mergeSpecTime_online
     *
     */
    void mergeSpecTime_online(const H5Spectrum& h5Spec);

    // 对第一段能谱尝试寻峰
    bool initial_PeakFind(double* spectrum, double energy_scale[], QVector<fit_result>& fit_c_2, bool* exitflag);

    /**
     * @brief PeakFind 寻峰
     * fit_type = c0*exp(-0.5*pow((x-c1)/c2,2)) + c3*x + c4
     * @param spectrum 能谱2048道
     * @param init_c 上一次寻峰结果
     * @param exitflag 寻峰标志位，false:没找到峰，true:找到了峰
     * @return 511/909 keV 寻峰成功时返回 true
     */
    bool PeakFind(double* spectrum, QVector<fit_result>& init_c, bool* exitflag);

    // 剥谱
    bool SpecStripping(double* spectrum, double energy_scale[], QVector<double>& init_c, bool* exitflag,
                       double deadTimeRate, double *peakCountOut, double *peakPositionOut = nullptr);

    // 拟合909全能峰计数随时间的变化
    bool fit909data();

    void setStartTime(int time)
    {
        mT0BeforeShot = time;
    }

    void setParseProgressCallback(ParseProgressCallback callback);
    void clearParseProgressCallback();
    void setParseCancelFlag(std::atomic<bool> *flag);
    void setSpectrumFitR2Min(double r2Min) { mSpectrumFitR2Min = r2Min; }
    double spectrumFitR2Min() const { return mSpectrumFitR2Min; }
    /** @brief 恢复内存中的初始能量刻度道址表为程序默认值 */
    static void resetInitialCalibrationChannels();

    /** @brief 更新指定谱仪的 511/909 keV 初始寻峰道址 */
    static bool setInitialCalibrationChannels(int detectorId, double ch511, double ch909);

    /**
     * @brief 解析H5文件，读取指定探测器的所有能谱数据，目前不支持对正在测量的实验数据的分析（没做文件锁保护）。
     * @param filePath 文件名
     * @param detectorId 探测器ID：1-DET_NUM
     * @return 解析到的能谱个数
     */
    int parseH5File(const QString& filePath, const quint32 detectorId);

    // 已废弃旧 0x55 协议实现，改为与在线测量一致的拆包/拼包逻辑
    int parseDatFile(const QString &filePath);

    /** @brief 最近一次 parseDatFile / parseH5File 读到的全部能谱 */
    const QVector<H5Spectrum>& allSpec() const { return mAllSpec; }

    // 处理缓冲区中的数据
    int processBuffer(bool isFinal);

    // 查找包头
    int findPacketHeader(int startPos);

    // 清理已处理的缓冲区数据
    void cleanupBuffer(int bytesToKeep);

    // 转码
    QByteArray encode(const QByteArray &data, const QByteArray &from1, const QByteArray &to1, const QByteArray &from2, const QByteArray &to2);
    // 报文完整性检查
    bool checkFrame(const QByteArray &data);
    //检查是否是能谱数据
    bool isSpecData(const QByteArray &data);
    //直接将网口数据赋值给结构体
    static bool getDataFromQByte(const QByteArray &byteArray, H5Spectrum &DataPacket);

    /**
     * @brief processSinglePacket 处理单个数据包
     * @param headerPos 包头位置
     * @param nextHeaderPos 下一个包头位置
     * @param isOnline 是否为在线测量数据
     * @return
     */
    bool processSinglePacket(int headerPos, int nextHeaderPos, paraseMode mode = offlineMode);

    // 获取指定时间段内的能谱
    bool getResult_offline(quint64 timeBin, quint64 start_time, quint64 end_time);

    /**
     * @brief 从多个 H5 文件读取指定通道，按推算时刻分段合并（fileStart+sequence×measureTime，8192→2048），并执行寻峰/剥谱
     * @param filePaths 测量文件列表
     * @param detectorId 探测器通道号 1..DET_NUM
     * @param fusionTime 打靶时刻（零时刻）
     * @param windowLeft 解析窗口左端点（fusionTime + timeStart）
     * @param windowRight 解析窗口右端点（fusionTime + timeEnd）
     * @param stepSec      合并时间步长 Time_Bin，单位 s；spectraPerBin=stepSec×1000/measureTimeMs
     */
    bool getResult_offlineMultiFileUtc(const QStringList &filePaths,
                                       quint32 detectorId,
                                       const QDateTime &fusionTime,
                                       const QDateTime &windowLeft,
                                       const QDateTime &windowRight,
                                       int stepSec);

public:
    int mDetId; //探测器编号

private:
    /** @return 成功完成剥谱/拟合的分段能谱数量 */
    int getResult(QVector<mergeSpecData> mergeSpec);

    void clearFitResult();

    void addCompressedSpectrumToSegment(mergeSpecData &seg, const H5Spectrum &spec, quint64 lossTimeNs);

    bool isParseCancelled() const;
    void reportParseProgress(ParseProgressPhase phase, int current, int total) const;

    ParseProgressCallback mParseProgressCallback;
    std::atomic<bool> mLocalCancelFlag{false};
    std::atomic<bool> *mParseCancelFlag = nullptr;

    QVector<mergeSpecData> mMergeSpec; //对原始数据汇总后的各时段能谱，对丢包带来的死时间做了相应记录
    QVector<H5Spectrum> mAllSpec;// 从HDF5文件中读取到特定通道的所有能谱

    QVector<int> mAllSpecTime; //每一个计数点对应的时刻，考虑到可能丢包，所以时刻并不是连续的。
    QVector<int> mAllSpecCount; //每秒能谱总计数随时间的变化

    const double mEnergyCalibration[2] = {511.0, 909.0}; //用于能量刻度的特征峰
    const double mStripEnRange[2] = {800.0, 1100.0};//设定剥谱能量范围

    const int mChannel8192 = 8192; //能谱的道数
    static constexpr int mChannel2048 = 2048; //做能谱寻峰所用的道数（MSVC 栈数组需 constexpr）
    const int mSpecPackLen = 8237;//2050*4 + 9 +13 + 1 + 3*4+2 %完整数据包的长度（解码后长度），1是包头，9是时间信息以及空白，13是大包的帧内容
    qint32 mT0BeforeShot = 0; //能谱开测时刻相对于打靶零时刻的时间（单位s，可正数可负数)T0_beforeShot = 开测时刻 - 打靶时刻

    int mTotalPackets = 0; //读取到的有效数据包长度
    QByteArray mProcessingBuffer;
    qint64 mBytesProcessed = 0;
    // 发送方转码
    const QByteArray mSenderFrom = QByteArray::fromHex("55");                      // 发送方转码前
    const QByteArray mSenderFrom2 = QByteArray::fromHex("FF");                     // 发送方转码前
    const QByteArray mSenderTo1 = QByteArray::fromHex("FF 00");                    // 发送方转码后
    const QByteArray mSenderTo2 = QByteArray::fromHex("FF FF");                    // 发送方转码后
    // 接收方转码
    const QByteArray mReceiverFrom1 = mSenderTo1;                                 // 接收方转码前
    const QByteArray mReceiverFrom2 = mSenderTo2;                                 // 接收方转码前
    const QByteArray mReceiverTo1 = mSenderFrom;                                  // 接收方转码后
    const QByteArray mReceiverTo2 = mSenderFrom2;                                 // 接收方转码后

    // 用来存放剥谱处理结果的图像数据，结合GetStripData()
    // 所有的拟合数据依次压入specStripData数据中
    QVector<double> mCount909Time; //各分时能谱对应测量结束时间（也就是右端点），单位min
    QVector<double> mCount909Count; //909keV计数，采用峰面积求得
    QVector<double> mCount909PeakPos; //909keV峰位道址
    QVector<double> mCount909FitCount; //909keV计数随时间变化的拟合曲线
    QVector<double> mCount909Residual; //残差
    bool mHasLatestEnergyCalibration = false;
    double mLatestEnergyCalibration[2] = {1.0, 0.0};

    QVector<double> mSpecStripDataX; //拟合数据点x坐标
    QVector<double> mSpecStripDataY; //拟合数据点y坐标
    QVector<double> mSpecStripDataFity;//拟合曲线y
    QVector<double> mSpecStripDataResidualRate; //残差

    paraseMode mParaseMode;
    int mShotTime = -1; //记录打靶起始时间，单位ms (FPGA内部时钟，仪器开始测量时为时钟为零)，必须在大靶前开始测量，这样才能找到计数率暴增点（打靶瞬间）
    int mLastSpecIdOnline = 0; //上一个能谱的编号。用于在线处理
    quint64 mSpectDeltaT = 0; //单个能谱测量时长，单位ms
    int mStartTimeOnline = 240*60; //解析能谱的起始时刻，单位s
    int mTimeBinOnline = 300*60; //解析能谱的时间宽度，单位s.分时能谱的单个能谱测量时间

    double mInitialCount = 0.0; //对数拟合后，给出来的初始计数率
    QVector<int> mSpecStripRightCh; //存放所有剥谱图形的右端点下标
    int mSuccessfulFitSegmentCount = 0;
    QStringList mFitFailureMessages;
    double mSpectrumFitR2Min = CurveFit::kDefaultSpectrumFitR2Min;
};

#endif // PARSEDATA_H
