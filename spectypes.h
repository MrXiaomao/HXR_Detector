#ifndef SPECTYPES_H
#define SPECTYPES_H

#include <QDateTime>
#include <QtEndian>
#include <QtGlobal>
#include <cstring>

#define DET_NUM 32
#define IP_LENGTH 17
#define MAC_LENGTH 18
#define DEFAULT_HDF5_FILENAME "./Config/Settings.H5"

// 子能谱数据包信息
#pragma pack(push, 1)  // 确保字节对齐
struct SubSpectrumPacket {
    quint32 header;        // FFFF AAB1
    quint16 dataType;      // 00D2
    quint32 spectrumSeq;   // 能谱序号
    quint32 measureTime;   // 测量时间
    quint32 deathTime;     // 死时间
    quint16 spectrumSubNo; // 能谱编号
    quint32 spectrum[256]; // 能谱数据 256*32bit
    quint32 timeUTCs;      // utc整数秒
    quint32 timeUTCms;     // utc小数秒
    quint32 reserved;      // 保留位 0000 0000
    quint32 tail;          // FFFF CCD1

    SubSpectrumPacket()
        : spectrumSeq(0),
        measureTime(0),
        deathTime(0),
        spectrumSubNo(0) {
        std::memset(spectrum, 0, sizeof(spectrum));
    }

    void convertNetworkToHost() {
        header = qFromBigEndian<quint32>(header);
        dataType = qFromBigEndian<quint16>(dataType);
        spectrumSeq = qFromBigEndian<quint32>(spectrumSeq);
        measureTime = qFromBigEndian<quint32>(measureTime);
        deathTime = qFromBigEndian<quint32>(deathTime);
        spectrumSubNo = qFromBigEndian<quint16>(spectrumSubNo);
        timeUTCs = qFromBigEndian<quint32>(timeUTCs);
        timeUTCms = qFromBigEndian<quint32>(timeUTCms);
        tail = qFromBigEndian<quint32>(tail);

        for (int i = 0; i < 256; ++i) {
            spectrum[i] = qFromBigEndian<quint32>(spectrum[i]);
        }
    }
};
#pragma pack(pop)

struct Sensor {
    double lonDeg;  // 经度[-180,180]
    double latDeg;  // 纬度[-90, 90]
    double value;   // 中子产额
};

struct H5Spectrum{
    quint32 sequence;      // 能谱序号
    quint32 timeUTCs;      // utc整数秒
    quint32 timeUTCms;     // utc小数秒
    quint32 measureTime;   // 能量测量时间间隔,单位ms
    quint32 deathTime;      // 死时间,单位*10ns
    quint32 spectrum[8192]; // 8192道完整数据
};

#pragma pack(push, 1)  // 确保字节对齐
struct FullSpectrum {
    quint32 sequence;      // 能谱序号
    quint32 timeUTCs;      // utc整数秒
    quint32 timeUTCms;     // utc小数秒
    quint32 measureTime;   // 能量测量时间间隔,单位ms
    quint32 deathTime;      // 死时间,单位*10ns
    quint32 spectrum[8192]; // 8192道完整数据
    quint32 receivedMask = 0;   // 32个子包位图
    QDateTime completeTime; // 完成时间,记录一个完整能谱数据接收完的北京时间
    bool isComplete;       // 是否完整

    FullSpectrum() : sequence(0), measureTime(0), deathTime(0), receivedMask(0), isComplete(false) {
        memset(spectrum, 0, sizeof(spectrum));
    }

    FullSpectrum(const FullSpectrum& other)
        : sequence(other.sequence), measureTime(other.measureTime), deathTime(other.deathTime),
          receivedMask(other.receivedMask), completeTime(other.completeTime), isComplete(other.isComplete),
          timeUTCs(other.timeUTCs), timeUTCms(other.timeUTCms) {
        memcpy(spectrum, other.spectrum, sizeof(spectrum));
    }

    FullSpectrum& operator=(const FullSpectrum& other) {
        if (this != &other) {
            sequence = other.sequence;
            timeUTCs = other.timeUTCs;
            timeUTCms = other.timeUTCms;
            measureTime = other.measureTime;
            deathTime = other.deathTime;
            receivedMask = other.receivedMask;
            completeTime = other.completeTime;
            isComplete = other.isComplete;
            memcpy(spectrum, other.spectrum, sizeof(spectrum));
        }
        return *this;
    }

    void convertNetworkToHost() {
        sequence = qFromBigEndian<quint32>(sequence);
        timeUTCs = qFromBigEndian<quint32>(timeUTCs );
        timeUTCms = qFromBigEndian<quint32>(timeUTCms);
        measureTime = qFromBigEndian<quint32>(measureTime);
        deathTime = qFromBigEndian<quint32>(deathTime);

        for (int i = 0; i < 8192; ++i) {
            spectrum[i] = qFromBigEndian<quint32>(spectrum[i]);
        }
    }
};
#pragma pack(pop)

struct ParticleData{
    QDateTime time;//粒子事件发生的时间
    quint16 energy; //粒子幅度
    quint16 typeFlag; //粒子类型标志（0：γ；1：α）
};

H5Spectrum FullSpectoH5Spec(const FullSpectrum& src);

Q_DECLARE_METATYPE(H5Spectrum)

#pragma pack(push, 1)  // 确保字节对齐
typedef struct _DetParameter{
    quint8 id;

    double longitude;
    double latitude;

    double gain;
    quint8 deathTime;
    quint16 triggerThold;

    char det_Ip_port[IP_LENGTH];

    quint32 spectrumRefreshTime;
    quint32 spectrumLength;

    double spectrumPlotEnMin;
    double spectrumPlotEnMax;

    quint8 waveformTriggerMode;
    quint32 waveformLength;

    bool trapShapeEnable;
    quint16 trapShapeTimeConstD1;
    quint16 trapShapeTimeConstD2;
    quint8 trapShapeRisePoint;
    quint8 trapShapePeakPoint;
    quint8 trapShapeFallPoint;

    bool highVoltageEnable;
    quint16 highVoltageOutLevel;

    quint16 alphaGammaUpperThold;
    quint16 alphaGammaLowerThold;

    _DetParameter(){
        id = 1;

        longitude = 0.0;
        latitude = 0.0;

        gain = 1.26;
        deathTime = 30;
        triggerThold = 100;

        memset(det_Ip_port, 0, sizeof(det_Ip_port));
        qstrcpy(det_Ip_port, "0.0.0.0");

        spectrumRefreshTime = 1000;
        spectrumLength = 8192;

        spectrumPlotEnMin = 0.0;
        spectrumPlotEnMax = 3000.0;

        waveformTriggerMode = 0;
        waveformLength = 512;

        trapShapeEnable = true;
        trapShapeTimeConstD1 = 24111;
        trapShapeTimeConstD2 = 59310;
        trapShapeRisePoint = 10;
        trapShapePeakPoint = 15;
        trapShapeFallPoint = 10;

        highVoltageEnable = true;
        highVoltageOutLevel = 800;

        alphaGammaUpperThold = 8192;
        alphaGammaLowerThold = 6500;
    }
}DetParameter;
#pragma pack(pop)

#endif // SPECTYPES_H
