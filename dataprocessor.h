/*
 * @Author: MrPan
 * @Date: 2025-11-13 11:36:00
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2026-04-10 19:13:07
 * @Description: 处理界面与探测器之间的数据交互，包括网络数据、能谱数据、温度数据等，控制探测器的工作模式、参数设置、数据采集等
 */
#ifndef DATAPROCESSOR_H
#define DATAPROCESSOR_H

#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <QtEndian>
#include <QFile>
#include <QDateTime>
#include <QTimer>

#include "qlitethread.h"
#include "commandadapter.h"
#include "parsedata.h"

#if ENABLE_LIBHV
#include <hv/TcpServer.h>
#include <hv/htime.h>
#else
#include <QTcpSocket>
#endif

class DataProcessor : public CommandAdapter
{
    Q_OBJECT
public:
    explicit DataProcessor(quint8 index, QTcpSocket* socket = nullptr, QObject *parent = nullptr);
    ~DataProcessor();

    virtual void sendCmdToSocket(CommandItem cmdItem) const override;

    void reallocIndex(quint8 index);//重新分配通道索引
#if ENABLE_LIBHV
    //void reallocSocket(const hv::SocketChannelPtr& tcpSocket);
    void reallocSocket(const hv::SocketChannelPtr& tcpSocket, DetParameter* detParameter = nullptr);
#else
    void reallocSocket(QTcpSocket *tcpSocket);
    void reallocSocket(QTcpSocket *tcpSocket, DetParameter& detParameter);
#endif //ENABLE_LIBHV
    bool isFreeSocket();//是否关联Socket

    // 追加原始网络数据
    void appendRawTcpData(const QByteArray& data);

    /*
     * 开始测量
     */
    void startMeasure(CommandAdapter::WorkMode workMode);
    void startAutoMeasure(QDateTime tmStart, CommandAdapter::WorkMode workMode);

    /*
     * 停止测量
     */
    void stopMeasure();

    //获取探测器编号1-32
    quint8 getIndex(){
        return this->mIndex;
    };

    void updateSetting(DetParameter* detParameter, bool isRead = false);

    bool extractSpectrumData(const QByteArray& packetData, SubSpectrumPacket& subSpec);

    void startOTAUpgrade(){mIsUpgrading = true;}
    void endOTAUpgrade(){mIsUpgrading = false;};
    bool sendOTAUpgradeData(const QByteArray& data);
    bool isUpgrading() const{return mIsUpgrading;}

public slots:
    void readyRead();
    // 收到温度时调用
    void restartTempTimeout();   
    // 解析能谱数据包，并拼接成完整能谱
    void mergeSubSpec_Thread(quint8 no, const QByteArray& data);
    // 解析波形数据包
    void extractWaveformData(quint8 no, const QByteArray& packetData);
    // 解析粒子数据包
    void extractParticleData(quint8 no, const QByteArray& packetData);
    // 解析时间戳数据包
    void extractTimestampData(quint8 no, const QByteArray& packetData);

signals:   
    void relayConnected();// 继电器
    void relayDisconnected();
    void relayPowerOn();
    void relayPowerOff();

    void detectorConnected(quint8 index);  // 探测器
    void detectorDisconnected(quint8 index);

    // 上报分包数据
    void reportRawNetData(quint8 index, const QByteArray& data); // 上报原始网络数据
    void reportOTAUpgradeData(quint8 index, const QByteArray& data); // 上报OTA升级数据

    // 上传解析后的数据
    void reportFullSpectrum(quint8 index, const FullSpectrum& fullSpectrum); // 发送完整的能谱数据
    void reportWaveCurveInfo(quint8, QVector<quint16>& wave); // 发送解析后的波形曲线
    void reportParticleInfo(quint8 index, QVector<ParticleData>& particles); // 发送解析后的粒子信息
    void reportTriggerTimeInfo(quint8 index, QDateTime trTime); // 发送解析后的触发时间戳信息
    void reportTemperatureTimeout(quint8 index); // 温度心跳超时信号

private:
    void requestParserResetOnDataThread();

#if ENABLE_LIBHV
    hv::SocketChannelPtr mTcpSocket;
#else
    QTcpSocket *mTcpSocket = nullptr;
#endif  //ENABLE_LIBHV

    QByteArray mRawData; // 存储网络原始数据
    quint64 mRawDataPeak = 0; // mRawData历史峰值（字节）
    QByteArray mCachePool; // 缓存数据，数据处理之前，先转移到二级缓存池
    bool mTerminatedDataThread = false;
    QMutex mDataLocker;
    QWaitCondition mDataCondition;
    
    QWaitCondition mParserResetCondition;
    bool mParserResetRequested = false;
    bool mParserResetDone = false;

    QLiteThread* mDataProcessThread = nullptr;// 处理线程
    bool mWaveMeasuring = false;     //波形测量中
    quint8 mTransferMode = 0x00;//传输模式
    quint32 mWaveLength = 512;// 波形长度
    quint8 mChWaveDataValidTag = 0x00;//通道数据是否完整

    // 新增成员变量
    QHash<quint32, FullSpectrum> mFullSpectrums; // 按能谱序号存储拼接中的能谱
    quint32 mLastSpectrumSeq = 0;
    FullSpectrum mLastFullSpectrum; // 保存上一个全能谱，做缺包补偿用
    QVector<quint32> mAccumulateSpec;
    ParseData* m_parseData;
    
    QTimer mTempTimeoutTimer; //心跳检测定时器

    QFile *mNetDataFile = nullptr;//网络数据保存文件
    bool mIsUpgrading = false; // 是否正在进行OTA升级

    QMutex mSocketLocker;
};

#endif // DATAPROCESSOR_H
