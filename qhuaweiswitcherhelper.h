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

#if USE_QTELNET
class QTelnet;
#elif ENABLE_LIBHV
#include <hv/TcpClient.h>
#else
class QTcpSocket;
#endif
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

    /*
     连接并查询电源状态
    */
    void queryPowerStatus();

    /*
     强制打开电源，一般用于自动化测量
    */
    void forceOpenPower();

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
    Q_SLOT void openNextSwitcherPOEPower();
    Q_SLOT void queryNextSwitcherPOEPower();

    /*
     关闭交换机POE口输出电源
    */
    Q_SLOT bool closeSwitcherPOEPower(quint8 port = 0, bool disconnect = false);
    Q_SLOT void closeNextSwitcherPOEPower();

    Q_SIGNAL void switcherLogged(QString);//交换机登陆
    Q_SIGNAL void switcherConnected(QString);//交换机连接
    Q_SIGNAL void switcherDisconnected(QString);//交换机断开
    Q_SIGNAL void reportPoePowerStatus(quint8, bool); //POE电源开关

signals:


private:
#if USE_QTELNET
    QTelnet *mTelnet = nullptr;//华为交换机控制POE电源
#elif ENABLE_LIBHV
    hv::TcpClient *mTelnet = nullptr;//华为交换机控制POE电源
#else
    QTcpSocket *mTelnet = nullptr;//华为交换机控制POE电源
#endif

    State mState;//交换机状态
    // QTimer *mStatusRefreshTimer = nullptr;//状态定时刷新时钟
    QTimer *mHeartbeatTimer = nullptr;//心跳检测定时器
    QString mIp;
    QEventLoop mSwitcherEventLoop;
    quint8 mCurrentQueryPort = 0;
    bool mSwitcherIsBusy = false;
    bool mSwitcherIsLoginOk = false;// 交换机是否登录成功
    bool mSwitcherInSystemView = false;// 交换机是否处于视图模式
    bool mBatchOn = false;//批量开
    bool mSingleOn = false;//单开
    bool mBatchOff = false;//批量关
    bool mSingleOff = false;//批量关
    bool mDisconnect = false;//关闭网络
    QString mCurrentCommand;
    QByteArray mRespondString;
    QVector<quint8/*index-谱仪编号*/> mMapAssociatedDetector;
    uint8_t mQueryPosition = 0;
    
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
};

#endif // QHUAWEISWITCHERHELPER_H
