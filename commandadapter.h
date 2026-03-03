#ifndef COMMANDADAPTER_H
#define COMMANDADAPTER_H

#include <QObject>
#include <QTcpSocket>
#include <QtMath>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <qlitethread.h>
#include <QTimer>

struct CommandItem
{
    QString name;      // 指令名称（中文或英文描述）
    QByteArray data;   // 实际发送的指令内容

    CommandItem() {}
    CommandItem(const QString &n, const QByteArray &d)
        : name(n), data(d) {}
};

class CommandAdapter : public QObject
{
    Q_OBJECT
public:
    explicit CommandAdapter(QObject *parent = nullptr);
    ~CommandAdapter();

    virtual void sendCmdToSocket(CommandItem cmdItem) const{};

    Q_SIGNAL void reportSpectrumData(QByteArray&);
    Q_SIGNAL void reportWaveformData(QByteArray&);
    Q_SIGNAL void reportParticleData(QByteArray&);
    Q_SIGNAL void reportTimestampData(QByteArray&);
    Q_SIGNAL void reportParamterData(QByteArray&);
    Q_SIGNAL void reportTemperatureData(float temperature);

// signals:
// public slots:
//     void restartTempTimeout();   // 收到温度时调用

public:
    /*********************************************************
     通用基础配置
    ***********************************************************/

    //增益指令01~08
    void sendGain(bool isRead = true, double gain = 1.26);

    //死时间配置(*10ns)
    void sendDeathTime(bool isRead = true, quint8 deathTime = 30);

    //触发阈值
    void sendTriggerThold(bool isRead = true, quint16 triggerThold = 100);

    /*********************************************************
     波形基本配置
    ***********************************************************/
    enum TriggerMode{
        tmTimer = 0x00,//定时触发模式
        tmNormal = 0x01//正常触发模式
    };
    enum WaveformLength{
        wl64 = 0x00,    //波形长度64
        wl128 = 0x01,   //波形长度128
        wl256 = 0x02,   //波形长度256
        wl512 = 0x03    //波形长度512
    };
    void sendWaveformMode(bool isRead = true, TriggerMode triggerMode = tmNormal, quint16 waveformLength = 128);

    /*********************************************************
     能谱基本配置
    ***********************************************************/
    //能谱刷新时间修改(ms，默认值1000)
    void sendSprectnumRefreshTimelength(bool isRead = true, quint32 spectrumRefreshTime = 1000);

    /*********************************************************
     梯形成型基本配置
    ***********************************************************/
    //梯形成型时间常数配置
    void sendTrapTimeConst(bool isRead = true, quint16 C1 = 0x00, quint16 C2 = 0x00);

    //上升沿、平顶、下降沿长度配置
    void sendRisePeakFallPoints(bool isRead = true, quint8 rise = 0x0A, quint8 peak = 0x0f, quint8 fall = 0x0A);

    // 梯形成型使能配置
    enum TrapShapeEnable{
        teClose = 0x00,//关闭
        teOpen = 0x01 //打开
    };
    void sendTrapShapeEnable(bool isRead = true, TrapShapeEnable trapShapeEnable = teOpen);

    /*********************************************************
     高压电源配置
    ***********************************************************/
    // 高压输出电平配置，单位V
    void sendHighVolgateOutLevel(bool isRead = true, quint16 highVolgateOutLevel = 0x00);

    enum HighVolgateOutLevelEnable{
        hvClose = 0x00,//关闭
        hvOpen = 0x01 //打开
    };
    // 高压输出电平使能
    void sendHighVolgateOutLevelEnable(bool isRead = true, HighVolgateOutLevelEnable highVolgateOutLevelEnable = hvClose);

    /*********************************************************
     工作模式配置
    ***********************************************************/
    enum WorkMode{
        wmWaveform = 0x00,//波形
        wmSpectrum = 0x01,//能谱
        wmParticle = 0x02 //粒子
    };

    enum DataType{
        dtWaveform = 0x00D1,//波形
        dtSpectrum = 0x00D2,//能谱
        dtParticle = 0x00D3, //粒子
        dtTimestamp = 0x00D4 //触发时间戳
    };

    void sendWorkMode(bool isRead = true, WorkMode workMode = wmSpectrum);

    /*********************************************************
     控制类指令
    ***********************************************************/
    //开始测量
    void sendStartMeasure();

    //停止测量
    void sendStopMeasure();

    /*********************************************************
     应答类指令
    ***********************************************************/
    // 设备查询指令-程序版本号查询
    void sendSearchAppversion();
    // 心跳包响应
    void sendPluse();

    /*********************************************************
     OTA更新指令
    ***********************************************************/
    enum HostProgram{
        haMain = 0x00,//主程序
        hpBakup = 0x01//备用程序
    };
    //重加载FPGA程序
    void sendSwitchHost(HostProgram hostProgram = haMain);

   // 发送指令
    void pushCmd(CommandItem cmdItem);

   // 发送下一条指令
    void notifySendNextCmd();

   // 清空指令
    void clear();

protected:
    bool mIsMeasuring = false;//测量是否正在进行中
    void analyzeCommands(QByteArray &cachePool);

private:
    // 预定义指令头部（只构造一次）
    // 温度指令数据头部
    static const QByteArray kCmdTempHead;
    // 增益指令数据头部
    static const QByteArray kCmdGainHead;
    // 死时间指令头部
    static const QByteArray kCmdDeadHead;
    // 阈值指令头部
    static const QByteArray kCmdThresHead;
    // 波形长度指令头部
    static const QByteArray kCmdWaveformLengthHead;
    // 能谱刷新时间指令头部
    static const QByteArray kCmdSpectrumRefreshTimeHead;
    // 梯形成型时间常数指令头部
    static const QByteArray kCmdTrapezoidTimeConstantHead;
    // 梯形成型上升沿、平顶、下降沿长度指令头部
    static const QByteArray kCmdTrapezoidLengthHead;
    // 梯形成型使能指令头部
    static const QByteArray kCmdTrapShapeEnableHead;
    // 高压输出电平指令头部
    static const QByteArray kCmdHVOutputHead;
    // 高压输出电平使能指令头部
    static const QByteArray kCmdHVEnableHead;
    // 工作模式指令头部
    static const QByteArray kCmdWorkModeHead;
    // 设备查询指令-程序版本号查询头部
    static const QByteArray kCmdFPGAVersionHead;
    // 重加载FPGA程序指令头部
    static const QByteArray kCmdReloadFPGAHead;
    // 测量数据头部
    static const QByteArray kspecHead;
    // 测量数据尾部
    static const QByteArray kspecTail;

    bool mAskStopMeasure = false; //是否请求结束测量(如果已经请求了结束测量，那么就有必要解析结束测量指令)
    QVector<CommandItem> cmdPool;//发送指令等候区

    quint16 mWaveformLength = 512;//波形或能谱长度
    quint32 mValidDataPkgRef = 0;//有效数据包个数

    QMutex mCmdMutex;
    bool mTerminatedThread = false;//线程退出标识
    bool mCmdReady = false;//上一条指令是否已经收到响应
    QWaitCondition mCmdWaitCondition;
    QLiteThread* mCmdProcessThread = nullptr;// 处理指令线程
};

#endif // COMMANDADAPTER_H
