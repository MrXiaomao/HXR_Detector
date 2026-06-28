/*
 * @Author: MrPan
 * @Date: 2025-12-23 11:10:46
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2025-12-26 10:44:36
 * @Description: 请填写简介
 */
#ifndef QHUAWEISWITCHERHELPER_H
#define QHUAWEISWITCHERHELPER_H

#include <QObject>
#include <QEventLoop>
#include <QTimer>
#include <QMap>
#include <QElapsedTimer>
#include <QMutex>
#include <QFile>
#include <QTextStream>

#define USE_ACTION_WAIT 1
#ifndef USE_QTELNET
class QTcpSocket;
#else
class QTelnet;
#endif

enum _action{
    acNone = 0x00,
    acQueryState = 0x01,        //对应指令display poe power-state interface GigabitEthernet0/0/%1\r\n
    acSwitchInterface = 0x02,   //对应指令interface GigabitEthernet 0/0/%1\r\n
    acOpenPoe = 0x03,           //对应指令poe enable\r\n
    acClosePoe = 0x04,          //对应指令undo poe enable\r\n
    acKeepLive = 0x05
};

typedef struct _action_wait{
    quint8 poeIndex;
    _action action;//0-查询 1-开 2-关
    _action_wait(quint8 v1, _action v2){
        this->poeIndex = v1;
        this->action = v2;
    }
}ACTION_WAIT;

class QHuaWeiSwitcherHelper : public QObject
{
    Q_OBJECT
public:
    enum class State {
        Disconnected,
        LoginUsername,
        LoginPassword,
        UserView,
        SystemView,
        CommandExecuting
    };

    explicit QHuaWeiSwitcherHelper(QString ip, QObject *parent = nullptr);
    ~QHuaWeiSwitcherHelper();

    QString ip() const{
        return mIp;
    }

    //判断谱仪编号是否存在
    bool contains(quint8 index);

    //交换机登录保持检测
    void checkLoginAlive();

    //根据POE端口号找谱仪编号
    qint8 indexOfPort(quint8 port);

    //根据谱仪编号找POE端口号
    qint8 portOfIndex(quint8 index);

    void setAssociatedDetector(quint8 index);
    void setAssociatedDetector(QString);

    /** 全部开/关时保留该 POE 端口的队列（时间同步触发模块），0 表示无保护端口 */
    void setProtectedPoePort(quint8 port);

    /*
     连接并查询电源状态
    */
    void queryPowerStatus();

    /*
     强制打开电源，一般用于自动化测量
    */
    void forceOpenPower();

    /*
     执行下一个动作
    */
    void executeNextAction(bool keepFirst = false);

    /*
    * 主动退出登录/断开 Telnet 连接。
    * 逻辑：system-view 下先 quit 回到 <HUAWEI>，再 quit 退出会话，最后关闭 socket。
    */
    Q_SLOT void logout();

    /*********************************************************
     交换机指令
    ***********************************************************/
    /*
     打开交换机POE口输出电源
    */
    Q_SLOT bool openSwitcherPOEPower(quint8 port = 0);

    /*
     关闭交换机POE口输出电源
    */
    Q_SLOT bool closeSwitcherPOEPower(quint8 port = 0, bool disconnect = false);

    Q_SIGNAL void switcherLogged(QString);//交换机登陆
    Q_SIGNAL void switcherConnected(QString);//交换机连接
    Q_SIGNAL void switcherDisconnected(QString);//交换机断开
    Q_SIGNAL void reportPoePowerStatus(quint8, bool); //POE电源开关
    Q_SIGNAL void poeActionQueueIdle(); // POE 指令队列已全部执行完毕

    bool hasPendingActions();

signals:


private:
    enum class PoeIntent {
        None,
        Open,
        Close
    };

    struct PoeVerifyContext {
        PoeIntent intent = PoeIntent::None;
        int retryCount = 0;
        bool waitingRetry = false;
    };

    static constexpr int kMaxPoeRetries = 3;
    static constexpr int kRetryDelaysMs[kMaxPoeRetries] = { 2000, 10000, 20000 };

    void enqueuePoeOpenActions(quint8 port);
    void enqueuePoeCloseActions(quint8 port);
    bool verifyPoeState(const QMap<QString, QString> &mapValues, PoeIntent intent) const;
    void applyVerifiedPoeState(quint8 port, qint8 detIndex, const QMap<QString, QString> &mapValues, PoeIntent intent);
    void handlePoeQueryResult(quint8 port, qint8 detIndex, const QMap<QString, QString> &mapValues);
    void schedulePoeRetry(quint8 port, PoeIntent intent, int delayMs);
    bool hasWaitingPoeRetry() const;
    bool hasPendingPoeActionsForPort(quint8 port) const;
    void removePendingPoeActionsForPort(quint8 port);
    void removePendingQueryActionsForPort(quint8 port);
    void removeAllPendingQueryActions();
    void clearAllPendingActionsExceptProtected();
    void finishCurrentActionAndAdvance();
    bool isPoeSequenceInProgress() const;
    void flushCommandLog();

private:
#ifndef USE_QTELNET
    QTcpSocket *mTelnet = nullptr;//华为交换机控制POE电源
#else
    QTelnet *mTelnet = nullptr;//华为交换机控制POE电源
#endif

    QTimer *mHeartbeatTimer = nullptr;//心跳检测定时器

    State mState;//交换机状态
    // QTimer *mStatusRefreshTimer = nullptr;//状态定时刷新时钟

    QString mIp;
    QEventLoop mSwitcherEventLoop;    
    std::atomic<bool> mSwitcherIsBusy = false;
    bool mSwitcherIsLoginOk = false;// 交换机是否登录成功
    bool mSwitcherInSystemView = false;// 交换机是否处于视图模式
    bool mDisconnect = false;//关闭网络

    QByteArray mRespondString;
    
    // 心跳检测相关
    QElapsedTimer mLastHeartbeatTime;  // 记录最后心跳响应时间
    bool mWaitingHeartbeatResponse = false;  // 是否正在等待心跳响应
    bool mIsReconnecting = false;  // 是否正在重连中
    
    // 心跳检测和重连
    void startHeartbeatCheck();
    void stopHeartbeatCheck();
    void performHeartbeatCheck();
    void reconnectSwitcher();

    // 重启心跳监测，避免指令冲突
    void restartHeartbeatCheck();

    // 退出登录相关
    bool mIsLoggingOut = false;
    quint8 mLogoutStep = 0; // 0=未退出, 1=已发第一条 quit, 2=已发第二条 quit

    QMap<quint8, bool> mMapPoeState;
    QMap<quint8, PoeVerifyContext> mVerifyByPort;
    QMutex mActionLocker;
    QVector<ACTION_WAIT> mActionsWait;// 等待执行的动作
    quint8 mCurrentPoeIndex = 0;// 当前操作的POE索引
    _action mCurrentAction = acNone;// 当前需要执行的动作
    QElapsedTimer mBusyStartTimer;//指令开始直接时间
    QString mCurrentCommand;
    quint64 mActionGeneration = 0; // 动作生成序号
    quint8 mProtectedPoePort = 0;  // 全部开/关时不清理、不操作的 POE 端口
    bool mQueueReset = false;      // 队列被重置后，忽略当前在途指令的 removeFirst

    QVector<quint8/*index-谱仪编号*/> mMapAssociatedDetector;// 当前交换机所关联的POE端口号
    // uint8_t mQueryPosition = 0;
    // bool mBatchOn = false;//批量开
    // bool mSingleOn = false;//单开
    // bool mBatchOff = false;//批量关
    // bool mSingleOff = false;//批量关
    // quint8 mCurrentQueryPort = 0;

    QFile* mFileCommand;//记录指令
    QTextStream* mTextStream; // 指令输出流
};

#endif // QHUAWEISWITCHERHELPER_H
