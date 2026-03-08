/*
 * @Author: MrPan
 * @Date: 2025-11-13 11:36:00
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2026-01-16 10:27:56
 * @Description: 请填写简介
 */
#ifndef DATAPROCESSOR_H
#define DATAPROCESSOR_H

#include <QObject>
#include <QMutex>
#include <QWaitCondition>
#include <QtEndian>
#include <QTcpSocket>
#include <QFile>
#include <QDateTime>

#include "qlitethread.h"
#include "commandadapter.h"
#include "globalsettings.h"
#include "parsedata.h"

class DataProcessor : public CommandAdapter
{
    Q_OBJECT
public:
    explicit DataProcessor(quint8 index, QTcpSocket* socket = nullptr, QObject *parent = nullptr);
    ~DataProcessor();

    virtual void sendCmdToSocket(CommandItem cmdItem) const override;

    void reallocIndex(quint8 index);//重新分配通道索引
    void reallocSocket(QTcpSocket *tcpSocket);
    void reallocSocket(QTcpSocket *tcpSocket, DetParameter& detParameter);
    bool isFreeSocket();//是否关联Socket

    /*
     * 添加数据
     */
    void inputData(const QByteArray& data);
    void inputSpectrumData(quint8 no, QByteArray& data);

    /*
     * 开始测量
     */
    void startMeasure(CommandAdapter::WorkMode workMode);
    void startAutoMeasure(QDateTime tmStart, CommandAdapter::WorkMode workMode);

    /*
     * 停止测量
     */
    void stopMeasure();

    quint8 index(){
        return this->mIndex;
    };

    void updateSetting(DetParameter& detParameter, bool isRead = false);

    bool extractSpectrumData(const QByteArray& packetData, SubSpectrumPacket& subSpec);

    void startOTAUpgrade(){mIsUpgrading = true;}
    void endOTAUpgrade(){mIsUpgrading = false;};
    bool sendOTAUpgradeData(const QByteArray& data);

public slots:
    void readyRead();
    void restartTempTimeout();   // 收到温度时调用

signals:   
    void relayConnected();// 继电器
    void relayDisconnected();
    void relayPowerOn();
    void relayPowerOff();

    void detectorConnected(quint8 index);  // 探测器
    void detectorDisconnected(quint8 index);

    void reportWaveformCurveData(quint8, QVector<quint32>& data);
    void reportParticleCurveData(quint8, QVector<quint32>& data);
    void reportFullSpectrum(quint8 index, const FullSpectrum& fullSpectrum); // 发送完整的能谱数据

    void reportTemperatureTimeout(); // 温度心跳超时信号
    void reportRawNetData(const QByteArray& data); // 上报原始网络数据
    void reportOTAUpgradeData(const QByteArray& data); // 上报OTA升级数据

private:
    quint8 mIndex;//探测器索引
    QTcpSocket *mTcpSocket = nullptr;
    QFile* mNetFile = nullptr;

    QByteArray mRawData; // 存储网络原始数据
    QByteArray mCachePool; // 缓存数据，数据处理之前，先转移到二级缓存池
    bool mDataReady = false;// 数据长度不够，还没准备好
    bool mTerminatedDataThread = false;
    QMutex mDataLocker;
    QWaitCondition mDataCondition;
    QLiteThread* mDataProcessThread = nullptr;// 处理线程
    bool mWaveMeasuring = false;     //波形测量中
    quint8 mTransferMode = 0x00;//传输模式
    quint32 mWaveLength = 512;// 波形长度
    quint8 mChWaveDataValidTag = 0x00;//通道数据是否完整

    // 新增成员变量
    QMutex mSpectrumLocker;
    QHash<quint32, FullSpectrum> mFullSpectrums; // 按能谱序号存储拼接中的能谱
    QVector<quint32> mAccumulateSpec;
    QVector<quint32> mCurrentSpec;
    ParseData* m_parseData;
    
    QTimer mTempTimeoutTimer; //心跳检测定时器

    QFile *mNetDataFile = nullptr;//网络数据保存文件
    bool mIsUpgrading = false; // 是否正在进行OTA升级
};

#endif // DATAPROCESSOR_H
