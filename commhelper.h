#ifndef COMMHELPER_H
#define COMMHELPER_H

#include <QObject>
#include <QTcpSocket>
#include <QMutex>
#include <QFile>
#include <QElapsedTimer>
#include <QWaitCondition>
#include <QThread>
#include <QEventLoop>
#include "qhuaweiswitcherhelper.h"
#include "dataprocessor.h"

class SpectrumH5FlushThread;
class SpectrumRawFlushThread;
class QTimer;

#if ENABLE_LIBHV
#include <hv/TcpServer.h>
#include <hv/htime.h>
#else
#include "TcpAgentServer.h"
#endif

class CommHelper : public QObject
{
    Q_OBJECT
    friend class SpectrumH5FlushThread;
    friend class SpectrumRawFlushThread;
public:
    explicit CommHelper(QObject *parent = nullptr);
    ~CommHelper();

    static CommHelper *instance() {
        static CommHelper commHelper;
        return &commHelper;
    }

    struct extendTimeSynModule{
        QString ip;
        int poeIndex;
        int switcherIndex;
        void reload()
        {
            GlobalSettings settings(DEVICE_CONFIG_FILE);
            this->ip = settings.value("TimeSynModule/ip").toString();
            this->poeIndex = settings.value("TimeSynModule/poeIndex", 24).toInt();
            this->switcherIndex = settings.value("TimeSynModule/switcherIndex", 1).toInt();
        }
    } mExtendTimeSynModule;

    /*
     打开服务
    */
    bool startServer();
    /*
     关闭服务
    */
    void stopServer();

    /*
     * 服务是否开启
     */
    bool isOpen();
    /**
     * 根据谱仪编号找到对应的交换机
     */
    QHuaWeiSwitcherHelper *indexOfHuaWeiSwitcher(int index);
    /**
     * 根据谱仪编号找到对应的交换机POE端口号
     */
    quint8 indexOfPort(int index);

    void connectSwitcher(bool query);

    //退出华为交换机登录
    void disconnectSwitcher();

    /*
     打开电源
    */
    void openPower();
    /*
     断开电源
    */
    void closePower(bool disconnect = false/*是否断开网络*/);

    /*
     开始测量
    */
    void startMeasure(QDateTime tmStart, CommandAdapter::MeasureMode measureMode, CommandAdapter::WorkMode workMode, quint8 index = 0);
    /*
     停止测量
    */
    void stopMeasure(quint8 index = 0);

    bool isMeasuring() const;

    /*
     设置发次信息
    */
    void setShotInformation(const QString shotDir, const QString shotNum);

    /*
     解析历史文件
    */
    bool openHistoryWaveFile(const QString &filePath);

    /*
     整个目录导出
    */
    bool saveAs(QString dstPath);

    /*
     当前测量数据文件导出
    */
    bool exportFile(QString dstPath);

    void startOTAUpgrade(quint8 index);
    bool sendOTAUpgradeData(quint8 index, const QByteArray& data);
    void endOTAUpgrade(quint8 index);

    Q_SIGNAL void connectPeerConnection(QString,quint16);//客户端上线
    Q_SIGNAL void disconnectPeerConnection(QString,quint16);//客户端上线

    Q_SIGNAL void switcherLogged(QString);//交换机已登录
    Q_SIGNAL void switcherConnected(QString);//交换机连接
    Q_SIGNAL void switcherDisconnected(QString);//交换机断开

    Q_SIGNAL void detectorOnline(quint8 index);  //数采板
    Q_SIGNAL void detectorOffline(quint8 index);

    Q_SIGNAL void measureStart(quint8 index); //测量开始
    Q_SIGNAL void measureStop(quint8 index); //测量停止

    Q_SIGNAL void settingfinished();//配置完成

    Q_SIGNAL void reportDetectorTemperature(quint8, float temperature); //探测器温度报告
    Q_SIGNAL void reportTemperatureTimeout(quint8 index); //探测器温度超时报警
    Q_SIGNAL void reportFullSpectrum(quint8 index, const FullSpectrum& fullSpectrum); // 发送完整的能谱数据
    Q_SIGNAL void reportWaveCurve(quint8, QVector<quint16>& data); // 发送解析后的波形曲线
    Q_SIGNAL void reportParticle(quint8 index, quint32 count); // 发送解析后的粒子个数
    Q_SIGNAL void reportOTAUpgrateData(quint8, QVector<quint16>& data);
    Q_SIGNAL void reportTimestampe(QDateTime&);
    Q_SIGNAL void reportParamterData(quint8, const QByteArray&);
    Q_SIGNAL void reportPoePowerStatus(quint8, bool); //POE电源开关
    Q_SIGNAL void reportOTAUpgradeData(quint8, const QByteArray& data); // 上报OTA升级数据

    void exportEnergyPlot(const QString fileDir, const QString triggerTime);

    /*********************************************************
     交换机指令
    ***********************************************************/
    /*
     打开交换机POE口输出电源
    */
    bool openSwitcherPOEPower(quint8 port = 0);

    /*
     关闭交换机POE口输出电源
    */
    bool closeSwitcherPOEPower(quint8 port = 0);

    /*
     打开交换机POE口输出电源(时钟同步模块)
    */
    bool openSwitcherExtendPOEPower();

    /*
     关闭交换机POE口输出电源(时钟同步模块)
    */
    bool closeSwitcherExtendPOEPower();

    // 主动关闭POE供电，也就是温度监测被关闭的通道
    void manualCloseSwitcherPOEPower(quint8 index)
    {
        //如果该通道已经关闭，则不重复关闭
        if (mManualClosedPOEIDs.contains(index)){
            return;
        }
        mManualClosedPOEIDs.append(index);  
    }

    // 主动打开POE供电，也就是温度监测被打开的通道
    void manualOpenSwitcherPOEPower(quint8 port=0)
    {
        //如果port为0，则表示全部打开
        if (port == 0){
            mManualClosedPOEIDs.clear();
            return;
        }

        if (mManualClosedPOEIDs.contains(port))
        {
            mManualClosedPOEIDs.removeOne(port);    
        }
    }

    /*
     * 读取网络参数
     */
    bool readParamters(quint8 detIndex);

private slots:
    //网口原始数据
    void onRawNetData(quint8 index, const QByteArray& data);

    //处理温度数据超时报警
    void onTemperatureTimeout(quint8 detID);

    // 处理分包后的能谱数据包
    void onSpectrumData(quint8 index, const QByteArray& data);

    // 处理分包后的波形数据包
    void onWaveformData(quint8 index, const QByteArray& data);

    // 处理分包后的粒子数据
    void onParticleInfo(quint8 detId, QVector<ParticleData>& particles);

private:
    // 处理探测器断开连接的统一逻辑
    void handleDetectorDisconnection(quint8 index);

    void flushSpectrumH5BuffersOnWorkerThread(bool forceFlushAll = false);
    void flushSpectrumRawBuffersOnWorkerThread(bool forceFlushAll = false);
    QMutex mPeersMutex;
#if ENABLE_LIBHV
    hv::TcpServer *mTcpServer; // libhv服务器实例
    QVector<hv::SocketChannelPtr> mConnectionPeers; //客户端连接表
#else
    TcpAgentServer *mTcpServer = nullptr;//本地服务器
    QVector<QTcpSocket*> mConnectionPeers; //客户端连接表
#endif //ENABLE_LIBHV

    quint8 mHuaWeiSwitcherCount = 0;
    QList<QHuaWeiSwitcherHelper *> mHuaWeiSwitcherHelper;
    QString mShotDir;// 保存路径
    QString mShotNum;// 测量发次

    // QMutex mMutexTriggerTime;
    QString mMeasureStartTime;//测量开始时刻
    bool mMeasuring = false; // 是否正在测量

    QMap<quint8, DataProcessor*> mDetectorDataProcessor;//32路探测器数据处理器
    QMap<quint8, QFile*> mDetectFileProcessor;  //32路探测器的测量数据文件句柄，不包含温度数据的数据包，可以是：能谱数据、波形数据、粒子数据。
    QMap<quint8, QFile*> mNetRawFileProcessor;  //32路探测器原始数据文件存储句柄，包含温度数据
    // 每路探测器独立缓冲完整能谱转 H5 后的数据；后台线程每 10s 批量写入 HDF5
    QVector<QVector<H5Spectrum>> mPendingH5ByDetector;
    QMutex mSpectrumH5Mutex;
    SpectrumH5FlushThread* mSpectrumH5FlushThread = nullptr;
    // 每路探测器原始能谱分包缓存；后台线程每 10s 批量写入 *.dat
    QVector<QByteArray> mPendingSpectrumRawByDetector;
    QMutex mSpectrumRawMutex;
    SpectrumRawFlushThread* mSpectrumRawFlushThread = nullptr;

    //记录手动关闭POE供电的探测器ID
    QVector<quint8> mManualClosedPOEIDs;
    QVector<QString> mDataFileList;// 保存当前测量数据文件列表

    // 文件flush状态跟踪结构
    struct FileFlushInfo {
        QElapsedTimer lastFlushTimer;  // 上次flush的时间计时器
        qint64 bytesSinceLastFlush;    // 自上次flush以来写入的字节数
        FileFlushInfo() : bytesSinceLastFlush(0) {
            lastFlushTimer.start();
        }
    };
    QMap<quint8, FileFlushInfo> mFileFlushInfo; // 每个探测器的文件flush信息
    QMutex mMutexFileFlush; // 保护mFileFlushInfo的互斥锁

    // 对测量数据的flush进行检查和执行的辅助函数
    void checkAndFlushDetectFile(quint8 index, qint64 bytesWritten);

    // 对网口数据的flush进行检查和执行的辅助函数
    void checkAndFlushNetRawFile(quint8 index, qint64 bytesWritten);

    /*
     初始化网络
    */
    void initSocket();
    void initDataProcessor();

    /*
     分配数据处理器
    */
#if ENABLE_LIBHV
    qint8 allocDataProcessor(const hv::SocketChannelPtr& socket);
    void freeDataProcessor(const hv::SocketChannelPtr& socket);
    /*
     根据IP和端口号，解析探测器编号
    */
    qint8 indexOfAddress(const uint32_t socketDescriptor/*QString, quint16*/);
#else
    qint8 allocDataProcessor(QTcpSocket *socket);
    void freeDataProcessor(QTcpSocket *socket);
    /*
     根据IP和端口号，解析探测器编号
    */
    qint8 indexOfAddress(qintptr socketDescriptor/*QString, quint16*/);
#endif //ENABLE_LIBHV
};

#endif // COMMHELPER_H
