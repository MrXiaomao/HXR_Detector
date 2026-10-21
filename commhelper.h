#ifndef COMMHELPER_H
#define COMMHELPER_H

#include <QObject>
#include <QTcpSocket>
#include <QMutex>
#include <QFile>
#include <QElapsedTimer>
#include <array>

#include "commandadapter.h"
#include "spectypes.h"

class QHuaWeiSwitcherHelper;
class DataProcessor;
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

    /*
     打开服务
    */
    bool startServer();
    /*
     关闭服务
    */
    void stopServer();

    /** 退出前同步停止后台线程（须在 QApplication 事件循环结束前调用） */
    void shutdown();

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
    void startMeasure(QDateTime tmStart, CommandAdapter::MeasureMode measureMode, 
        CommandAdapter::WorkMode workMode, quint8 index = 0, const QString& shotTime = QString(),
        bool sendAlphaGammaThold = false);
        
    /*
     停止测量
    */
    void stopMeasure(quint8 index = 0);

    bool isMeasuring() const;

    CommandAdapter::WorkMode currentWorkMode() const { return mCurrentWorkMode; }

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

    Q_SIGNAL void reportDetectorTemperature(quint8, float temperature, const QDateTime& trTime); //探测器温度报告
    Q_SIGNAL void reportTemperatureTimeout(quint8 index); //探测器温度超时报警
    Q_SIGNAL void reportFullSpectrum(quint8 index, const FullSpectrum& fullSpectrum); // 发送完整的能谱数据
    Q_SIGNAL void reportWaveCurve(quint8, const QVector<quint16>& data); // 发送解析后的波形曲线
    Q_SIGNAL void reportParticle(quint8 index, quint32 count); // 发送解析后的粒子个数
    Q_SIGNAL void reportOTAUpgrateData(quint8, QVector<quint16>& data);
    Q_SIGNAL void reportAutoShotTime(const QDateTime& shotTime, quint32 spectrumRefreshMs);
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

    // 主动关闭POE供电，也就是温度监测被关闭的通道
    void manualCloseSwitcherPOEPower(quint8 index)
    {
        if (!mManualClosedPOEIDs.contains(index))
            mManualClosedPOEIDs.append(index);
        if (!mIntentionalPOECloseIDs.contains(index))
            mIntentionalPOECloseIDs.append(index);
    }

    // 主动打开POE供电，也就是温度监测被打开的通道
    void manualOpenSwitcherPOEPower(quint8 port=0)
    {
        //如果port为0，则表示全部打开
        if (port == 0){
            mManualClosedPOEIDs.clear();
            mIntentionalPOECloseIDs.clear();
            return;
        }

        if (mManualClosedPOEIDs.contains(port))
        {
            mManualClosedPOEIDs.removeOne(port);
        }
        mIntentionalPOECloseIDs.removeOne(port);
    }

    /*
     * 读取网络参数
     */
    bool readParamters(quint8 detIndex);
    bool isDetectorConnected(quint8 detIndex);

    /** 是否已配置并创建了交换机 helper */
    bool hasSwitchers() const;

    qint64 lastTempUpdateTime(quint8 index) const{
        return mTempUpdateTime.value(index, 0);
    }

private slots:
    //网口原始数据（解析线程）
    void onRawNetData_Thread(quint8 index, const QByteArray& data);

    //处理温度数据超时报警（解析线程）
    void onTemperatureTimeout_Thread(quint8 detID);

    // 自动测量首帧能谱：打靶时刻 = 收到时刻 - 能谱刷新时间
    void onFirstSpectrumFrame(quint8 detID, const QDateTime& receiveTime);

    // 处理分包后的能谱数据包（解析线程）
    void onSpectrumData_Thread(quint8 index, const QByteArray& data);

    // 处理分包后的波形数据包（解析线程）
    void onWaveformData_Thread(quint8 index, const QByteArray& data);

    // 处理分包后的粒子数据（解析线程）
    void onParticleInfo_Thread(quint8 detId, const QVector<ParticleData>& particles);

private:
    // 处理探测器断开连接的统一逻辑
    void handleDetectorDisconnection(quint8 index);
    // 根据客户端地址解析谱仪编号（不绑定 socket、不下发配置）
    qint8 peekDetectorIndex(const QString& peerAddress, quint16 peerPort) const;
    // 关电流程中是否应拒绝该通道重连
    bool shouldRejectDetectorReconnect(quint8 index) const;
    void markPoeCloseInProgress();
    void tryClearPoeCloseInProgress();
    // 能谱数据落盘
    void flushSpectrumH5BuffersOnWorkerThread(bool forceFlushAll = false); 
    // 网口原始数据落盘
    void flushNetRawBuffersOnWorkerThread(bool forceFlushAll = false); 
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
    QString mMeasureStartTime;//测量开始时刻（单通道重测时会更新为该通道本次开测时刻）
    QString mSessionMeasureStartTime; // 整次开测共用 H5 的开测时刻（单通道重测不覆盖）
    QString mShotTime; // 打靶时刻字符串，重测新建通道 H5 时复用
    QMutex mAutoShotTimeMutex;
    bool mAwaitAutoShotTime = false; // 等待首帧能谱以写入打靶时刻
    bool mMeasuring = false; // 是否正在测量
    CommandAdapter::WorkMode mCurrentWorkMode = CommandAdapter::WorkMode::wmSpectrum;

    QMap<quint8, DataProcessor*> mDetectorDataProcessor;//32路探测器数据处理器
    QMap<quint8, QFile*> mDetectFileProcessor;  //32路探测器的测量数据文件句柄，不包含温度数据的数据包，可以是：能谱数据、波形数据、粒子数据。
    QMap<quint8, qint64> mTempUpdateTime; // 每个探测器最新温度更新时间

    // 每路探测器独立缓冲完整能谱转 H5 后的数据；后台线程每 10s 批量写入 HDF5
    std::array<QVector<H5Spectrum>, DET_NUM + 1> mPendingH5ByDetector;
    std::array<QMutex, DET_NUM + 1> mSpectrumH5MutexByDetector;
    SpectrumH5FlushThread* mSpectrumH5FlushThread = nullptr;
    
    // 每路探测器原始网口数据；后台线程定时批量写入 *.dat（粒子模式不写原始二进制）
    std::array<QByteArray, DET_NUM + 1> mPendingNetRawByDetector;
    std::array<QMutex, DET_NUM + 1> mSpectrumRawMutexByDetector;
    SpectrumRawFlushThread* mSpectrumRawFlushThread = nullptr;

    // 持久：用户主动关闭 POE/温度监测的通道，用于跳过 onTemperatureTimeout_Thread 的 30min 断电恢复
    QVector<quint8> mManualClosedPOEIDs;
    // 一次性：预期由主动断电引起的掉线，用于跳过 disconnect 时的自动断电重启（处理完即清除）
    QVector<quint8> mIntentionalPOECloseIDs;
    // 关电流程进行中：拒绝已标记关电通道的 TCP 重连与初始化
    bool mPoePowerClosing = false;
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

    /*
     初始化网络
    */
    void initSocket();
    void initDataProcessor();
    void initSwitchers();
    void destroySwitchers();

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
