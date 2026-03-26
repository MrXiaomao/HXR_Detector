#include "qhuaweiswitcherhelper.h"
#include "globalsettings.h"
#if USE_QTELNET
#include "QTelnet.h"
#else
#include <QTcpSocket>
using QTelnet = QTcpSocket;
#endif

QHuaWeiSwitcherHelper::QHuaWeiSwitcherHelper(QString ip, QObject *parent)
    : QObject{parent}
    , mState(State::Disconnected)
    , mIp(ip)
{
#if USE_QTELNET
    mTelnet = new QTelnet(QTelnet::TCP, this);
#else
    mTelnet = new QTelnet(this);
#endif

    // 初始化心跳检测
    mHeartbeatTimer = new QTimer(this);
    mHeartbeatTimer->setInterval(30000); // 每30秒检测一次
    mHeartbeatTimer->setSingleShot(false);
    connect(mHeartbeatTimer, &QTimer::timeout, this, &QHuaWeiSwitcherHelper::performHeartbeatCheck);
    mWaitingHeartbeatResponse = false;
    mIsReconnecting = false;
    
#if USE_QTELNET
    connect(mTelnet, &QTelnet::socketReadyRead,this,[=](const char *data, int size){
        QByteArray rx_current(data, size);
        qDebug() << "STR1:" << rx_current;// << "HEX:"  << rx_current.toHex(' ');
        mRespondString.append(rx_current);
#else
    connect(mTelnet, &QTcpSocket::readyRead,this,[=](){
        QByteArray rx_current = mTelnet->readAll();
        //qDebug() << "STR1:" << rx_current;// << "HEX:"  << rx_current.toHex(' ');
        QByteArray dest;
        for (int i=0; i<rx_current.size(); ++i){
            // 保留：英文字母、数字、空格、回车、常见符号
            if (isalpha(rx_current[i]) || isdigit((uint8_t)rx_current[i]) ||
                (uint8_t)rx_current[i] == ' ' || (uint8_t)rx_current[i] == '\r' || (uint8_t)rx_current[i] == '\n' ||
                ((uint8_t)rx_current[i] >= 33 && (uint8_t)rx_current[i] <= 47) ||  // !"#$%&'()*+,-./
                ((uint8_t)rx_current[i] >= 58 && (uint8_t)rx_current[i] <= 64) ||  // :;<=>?@
                ((uint8_t)rx_current[i] >= 91 && (uint8_t)rx_current[i] <= 96) ||  // [\]^_`
                ((uint8_t)rx_current[i] >= 123 && (uint8_t)rx_current[i] <= 126)) { // {|}~
                dest.push_back(rx_current[i]);
            }
        }

        mRespondString.append(dest);
#endif
        //qDebug() << "STR2:" << mRespondString;

        // switch (mState) {
        // case State::LoginUsername:
        //     handleLoginUsername(response);
        //     break;
        // case State::LoginPassword:
        //     handleLoginPassword(response);
        //     break;
        // case State::UserView:
        //     handleUserView(response);
        //     break;
        // case State::SystemView:
        //     handleSystemView(response);
        //     break;
        // case State::CommandExecuting:
        //     handleCommandResponse(response);
        //     break;
        // default:
        //     break;
        // }

        // ------------------------------
        // 退出登录流程（优先处理，避免被其它状态机分支打断）
        // ------------------------------
        if (mIsLoggingOut) {
            const bool inUserView = mRespondString.contains("<HUAWEI>");
            const bool inSystemView = mRespondString.contains("[HUAWEI]");

            // Step 1: 在 system-view 下发了 quit，期望回到 <HUAWEI>
            if (mLogoutStep == 1) {
                if (inUserView) {
                    mRespondString.clear();
                    QString cmd = "quit\r\n"; // 退出会话
#if USE_QTELNET
                    mTelnet->sendData(cmd.toStdString().c_str(), cmd.size());
#else
                    mTelnet->write(cmd.toLatin1());
#endif
                    mLogoutStep = 2;
                    return;
                }
                if (inSystemView)
                    return;
            }

            // Step 2: 发完第二个 quit，通常对端会断开连接
            if (mLogoutStep == 2) {
                QTimer::singleShot(100, this, [=](){
                    if (mTelnet)
                        mTelnet->disconnectFromHost();
                });

                mIsLoggingOut = false;
                mLogoutStep = 0;
                mSwitcherIsLoginOk = false;
                mSwitcherInSystemView = false;
                mRespondString.clear();
                emit switcherDisconnected(mIp);
                return;
            }
        }

        /**
         * 处理特殊情况，初始密码未修改的情况下
         * Warning: The initial password poses security risks.
         * The password needs to be changed. Change now? [Y/N]:
         */
        if(mRespondString.endsWith("The password needs to be changed. Change now? [Y/N]: ")){
            mRespondString.clear();

            QString data = "N\r\n";
#if USE_QTELNET
            mTelnet->sendData(data.toStdString().c_str(), data.size());
#else
            mTelnet->write(data.toLatin1());
#endif
        }
        /**
         * 处理特殊情况，初始密码未修改的情况下
         * Please enter old password:
         */
        if(mRespondString.endsWith("Please enter old password: ")){
            mRespondString.clear();

            QString data = "root@12345\r\n";
#if USE_QTELNET
            mTelnet->sendData(data.toStdString().c_str(), data.size());
#else
            mTelnet->write(data.toLatin1());
#endif
        }

        if(mRespondString.endsWith("---- More ----")){
            mRespondString.clear();

            QString data = "\r\n";
#if USE_QTELNET
            mTelnet->sendData(data.toStdString().c_str(), data.size());
#else
            mTelnet->write(data.toLatin1());
#endif
        }
        else if(mRespondString.endsWith("[Y/N]:")){
            mRespondString.clear();

            QString data = "Y\r\n";
#if USE_QTELNET
            mTelnet->sendData(data.toStdString().c_str(), data.size());
#else
            mTelnet->write(data.toLatin1());
#endif
        }
        else if(mRespondString.endsWith("Username:")){
            mRespondString.clear();

            QString data = "root\r\n";
#if USE_QTELNET
            mTelnet->sendData(data.toStdString().c_str(), data.size());
#else
            mTelnet->write(data.toLatin1());
#endif
        }
        else if(mRespondString.endsWith("Password:")){
            mRespondString.clear();

            QString data = "root@12345\r\n";
#if USE_QTELNET
            mTelnet->sendData(data.toStdString().c_str(), data.size());
#else
            mTelnet->write(data.toLatin1());
#endif
        }
        else if(mRespondString.endsWith("<HUAWEI>") && !mSwitcherIsLoginOk){
            mSwitcherIsLoginOk = true;
            mRespondString.clear();

            QString data = "system-view\r\n";
#if USE_QTELNET
            mTelnet->sendData(data.toStdString().c_str(), data.size());
#else
            mTelnet->write(data.toLatin1());
#endif
        }
        else if(mRespondString.endsWith("[HUAWEI]") && mSwitcherIsLoginOk && !mSwitcherInSystemView)
        {
            mSwitcherInSystemView = true;
            mRespondString.clear();
            
            // 如果是重连成功，重置重连标志
            if (mIsReconnecting) {
                mIsReconnecting = false;
                qInfo().nospace().noquote() << "交换机[" << mIp << "]重连成功";
            }

            emit switcherLogged(mIp);
            
            // 登录成功后启动心跳检测
            startHeartbeatCheck();

            QTimer::singleShot(0, this, [=](){                
#if USE_QTELNET
                mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
#else
                mTelnet->write(mCurrentCommand.toLatin1());
#endif
            });
        }
        // 检测心跳响应（只要在等待心跳响应且响应包含 HUAWEI 提示符就认为是心跳响应）
        else if (mWaitingHeartbeatResponse &&
                 (mRespondString.contains("[HUAWEI]") || mRespondString.contains("<HUAWEI>") || mRespondString.contains("display clock\r")))
        {
            // 收到心跳响应，重置标志
            mWaitingHeartbeatResponse = false;
            mLastHeartbeatTime.restart();
            mRespondString.clear();
            qDebug().nospace().noquote() << "交换机[" << mIp << "]心跳检测正常";
        }
        else if (mSwitcherInSystemView){
            /*
             * Username:root
             * Password:root@12345
             * <HUAWEI>system-view
             * [HUAWEI]interface GigabitEthernet 0/0/1
             * [HUAWEI-GigabitEthernet0/0/1]poe enable
             * [HUAWEI-GigabitEthernet0/0/1]poe enable
             * Warning: This port is enabled already.
             * [HUAWEI-GigabitEthernet0/0/1]undo poe enable
             * [HUAWEI-GigabitEthernet0/0/1]undo poe enable
             * Warning: This port is disabled already.
            */

            //根据结果来判断
            mRespondString.replace("\r\n","\n");

            QString warnEnableCmd = QStringLiteral("Warning: This port is enabled already.");
            QString warnDisabledCmd = QStringLiteral("Warning: This port is disabled already.");
            QString switchCmd = QStringLiteral("[HUAWEI-GigabitEthernet0/0/%1]").arg(mCurrentQueryPort);//进入interface GigabitEthernet之后
            QString switchCmd2 = QStringLiteral("<HUAWEI>");//进入视图之前
            QString switchCmd3 = QStringLiteral("[HUAWEI]");//进入视图之后
            QString queryCmd = QStringLiteral("display poe power-state interface GigabitEthernet0/0/%1\r\n").arg(mCurrentQueryPort);
            QString enterInterfaceCmd = QStringLiteral("interface GigabitEthernet 0/0/%1\r\n").arg(mCurrentQueryPort);

            if (mCurrentCommand == queryCmd){
                if (mRespondString.endsWith(switchCmd3.toLatin1())){
                    QList<QByteArray> lines = mRespondString.split('\n');
                    mRespondString.clear();

                    QMap<QString, QString> mapValues;
                    for (auto line : lines)
                    {
                        QList<QByteArray> values = line.split(':');
                        if (values.size() > 1)
                            mapValues[values[0].trimmed()] = values[1].trimmed();
                    }

                    qint8 index = this->indexOfPort(mCurrentQueryPort);
                    if (index > 0)
                    {
                        if (mapValues["Power enable state"] == "enable")
                            QMetaObject::invokeMethod(this, "reportPoePowerStatus", Qt::QueuedConnection, Q_ARG(quint8, index), Q_ARG(bool, true));
                        else
                            QMetaObject::invokeMethod(this, "reportPoePowerStatus", Qt::QueuedConnection, Q_ARG(quint8, index), Q_ARG(bool, false));
                    }

                    // 查询下一个POE供电口状态
                    queryNextSwitcherPOEPower();
                }
            }
            else if (mCurrentCommand == enterInterfaceCmd){
                if (mRespondString.endsWith(switchCmd.toLatin1())){
                    mRespondString.clear();

                    if (mBatchOn || mSingleOn){
                        QTimer::singleShot(0, this, [=](){
                            mCurrentCommand = "poe enable\r\n";                            
#if USE_QTELNET
                            mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
#else
                            mTelnet->write(mCurrentCommand.toLatin1());
#endif
                        });
                    }
                    else if (mBatchOff || mSingleOff){
                        QTimer::singleShot(0, this, [=](){
                            mCurrentCommand = "undo poe enable\r\n";                            
#if USE_QTELNET
                            mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
#else
                            mTelnet->write(mCurrentCommand.toLatin1());
#endif
                        });
                    }
                }
            }
            else if (mCurrentCommand == "poe enable\r\n"){
                if (mRespondString.endsWith(switchCmd.toLatin1())){
                    QList<QByteArray> lines = mRespondString.split('\n');
                    mRespondString.clear();
                    if (lines.size() == 2){
                        //POE打开成功
                        qint8 index = this->indexOfPort(mCurrentQueryPort);
                        if (index > 0)
                        {
                            QMetaObject::invokeMethod(this, "reportPoePowerStatus", Qt::QueuedConnection, Q_ARG(quint8, index), Q_ARG(bool, true));
                        }
                    }
                    else if (lines.size() == 3){
                        //POE之前已经打开
                        if (lines.at(1) == QString("Warning: This port is enabled already.")){
                            qint8 index = this->indexOfPort(mCurrentQueryPort);
                            if (index > 0)
                            {
                                QMetaObject::invokeMethod(this, "reportPoePowerStatus", Qt::QueuedConnection, Q_ARG(quint8, index), Q_ARG(bool, true));
                            }
                        }
                    }

                    if (mBatchOn){
                        //打开下一个开关
                        this->openNextSwitcherPOEPower();
                    }
                }
            }
            else if (mCurrentCommand == "undo poe enable\r\n"){
                if (mRespondString.endsWith(switchCmd.toLatin1())){
                    QList<QByteArray> lines = mRespondString.split('\n');
                    mRespondString.clear();
                    if (lines.size() == 2){
                        //POE关闭成功
                        qint8 index = this->indexOfPort(mCurrentQueryPort);
                        if (index > 0)
                        {
                            QMetaObject::invokeMethod(this, "reportPoePowerStatus", Qt::QueuedConnection, Q_ARG(quint8, index), Q_ARG(bool, false));
                        }
                    }
                    else if (lines.size() == 3){
                        //POE之前已经关闭
                        if (lines.at(1) == QString("Warning: This port is disabled already.")){
                            qint8 index = this->indexOfPort(mCurrentQueryPort);
                            if (index > 0)
                            {
                                QMetaObject::invokeMethod(this, "reportPoePowerStatus", Qt::QueuedConnection, Q_ARG(quint8, index), Q_ARG(bool, false));
                            }
                        }
                    }

                    if (mBatchOff){
                        //关闭下一个开关
                        this->closeNextSwitcherPOEPower();
                    }
                }
            }

            // mSwitcherEventLoop.quit();
            // mRespondString.clear();
        }
        else if (mRespondString.endsWith("]") && mRespondString.contains("[HUAWEI-GigabitEthernet")){
            mSwitcherEventLoop.quit();
            mRespondString.clear();
        }

        if (mRespondString.contains("<HUAWEI>") || mRespondString.contains("[HUAWEI]"))
        {
            // Telnet 会话仍然存活
        }
        else if (mRespondString.contains("Username:")
            || mRespondString.contains("Password:"))
        {
            // 会话已失效，被踢回登录
            mSwitcherIsLoginOk = false;
            mSwitcherInSystemView = false;
            stopHeartbeatCheck();  // 停止心跳检测
            qInfo().nospace().noquote() << "交换机[" << mIp << "]会话失效，重新登录";
            emit switcherDisconnected(mIp);
        }

        qDebug() << "STR2:===================" << mRespondString;
    });

#if USE_QTELNET
    connect(mTelnet, &QTelnet::error, this, [=](QAbstractSocket::SocketError socketError){
#else
    connect(mTelnet, &QTelnet::errorOccurred, this, [=](QAbstractSocket::SocketError socketError){
#endif
        mSwitcherIsLoginOk = false;
        mSwitcherInSystemView = false;
        stopHeartbeatCheck();  // 停止心跳检测
        qDebug().nospace().noquote() << "交换机[" << mIp << "]连接错误，错误码：" << socketError;
        emit switcherDisconnected(mIp);
    });
    
    // 监听连接状态变化，在断开时停止心跳检测
#if USE_QTELNET
    connect(mTelnet, &QTelnet::stateChanged, this, [=](QAbstractSocket::SocketState socketState){
#else
    connect(mTelnet, &QTcpSocket::stateChanged, this, [=](QAbstractSocket::SocketState socketState){
#endif
        if (socketState == QAbstractSocket::UnconnectedState) {
            stopHeartbeatCheck();
            qDebug().nospace().noquote() << "交换机[" << mIp << "]连接断开[QTelnet::stateChanged]";
            mSwitcherIsLoginOk = false;
            mSwitcherInSystemView = false;
        } else if (socketState == QAbstractSocket::ConnectedState && mSwitcherInSystemView) {
            // 如果已经登录成功，重新启动心跳检测
            startHeartbeatCheck();
        }
    });

}

//判断谱仪编号是否存在
bool QHuaWeiSwitcherHelper::contains(quint8 index)
{
    if (index == 0x00)
        return true;
    else
        return mMapAssociatedDetector.contains(index);
}

//根据谱仪编号找POE端口号
qint8 QHuaWeiSwitcherHelper::portOfIndex(quint8 index)
{
    if (mMapAssociatedDetector.contains(index))
        return index;
    else
        return -1;
}

//根据POE端口号找谱仪编号
qint8 QHuaWeiSwitcherHelper::indexOfPort(quint8 port)
{
    if (port == 0x00)
        return 0x00;
    else
    {
        if (mMapAssociatedDetector.contains(port))
            return port;
    }

    return -1;
}

void QHuaWeiSwitcherHelper::setAssociatedDetector(quint8 detectorId)
{
    if (detectorId >= 1 && detectorId<=DET_NUM){
        if (!mMapAssociatedDetector.contains(detectorId))
            mMapAssociatedDetector.push_back(detectorId);
    }
}

void QHuaWeiSwitcherHelper::setAssociatedDetector(QString text)
{
    mMapAssociatedDetector.clear();
    QStringList lines = text.split(',');
    for (auto line : lines)
    {
        int detectorId = line.toInt();
        if (detectorId >= 1 && detectorId<=DET_NUM)
            mMapAssociatedDetector.push_back(detectorId);
    }
}
/*
 连接并查询电源状态
*/
void QHuaWeiSwitcherHelper::queryPowerStatus()
{
    // emit switcherConnected(mIp);
    // emit switcherLogged(mIp);
    // return;

    mBatchOn = true;
    mSingleOn = false;
    mBatchOff = false;
    mSingleOff = false;
    quint8 fromPort = 1;
    quint8 toPort = DET_NUM;

    //开机仅查询POE供电
    mCurrentQueryPort = 1;
    mCurrentCommand = "display poe power-state interface GigabitEthernet0/0/1\r\n";
    mTelnet->disconnectFromHost();
#if USE_QTELNET
    mTelnet->setType(QTelnet::TCP);
    if (mTelnet->connectToHost(mIp, 23)){
#else
    mTelnet->connectToHost(mIp, 23);
    mTelnet->waitForConnected(1000);
    if (mTelnet->state() == QAbstractSocket::ConnectedState){
#endif
        emit switcherConnected(mIp);
    }
    else
    {
        //emit switcherDisconnected(mIp);
    }
}

void QHuaWeiSwitcherHelper::forceOpenPower()
{
    mBatchOn = true;
    mSingleOn = false;
    mBatchOff = false;
    mSingleOff = false;
    quint8 fromPort = 1;
    quint8 toPort = DET_NUM;

    //开机仅查询POE供电
    mCurrentQueryPort = 1;
    mCurrentCommand = QString("interface GigabitEthernet 0/0/%1").arg(mCurrentQueryPort) + "\r\n";
#if USE_QTELNET
    if (mTelnet->isConnected())
#else
    if (mTelnet->state() == QAbstractSocket::ConnectedState)
#endif
    {
        restartHeartbeatCheck();
#if USE_QTELNET
        mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
#else
        mTelnet->write(mCurrentCommand.toLatin1());
#endif
    }
    else
    {
        mTelnet->disconnectFromHost();
#if USE_QTELNET
        mTelnet->setType(QTelnet::TCP);
        if (mTelnet->connectToHost(mIp, 23)){
#else
        mTelnet->connectToHost(mIp, 23);
        mTelnet->waitForConnected(1000);
        if (mTelnet->state() == QAbstractSocket::ConnectedState){
#endif
            emit switcherConnected(mIp);
        }
        else
        {
            //emit switcherDisconnected(mIp);
        }
    }
}

/*********************************************************
     交换机指令
    ***********************************************************/
/*
 打开交换机POE口输出电源
*/
bool QHuaWeiSwitcherHelper::openSwitcherPOEPower(quint8 port/* = 0*/)
{
#if USE_QTELNET
    if (nullptr == mTelnet || !mTelnet->isConnected() || !mSwitcherInSystemView)
#else
    if (nullptr == mTelnet || !(mTelnet->state() == QAbstractSocket::ConnectedState) || !mSwitcherInSystemView)
#endif
        return false;

    mBatchOn = port == 00 ? true : false;
    mSingleOn = port == 00 ? false : true;
    mBatchOff = false;
    mSingleOff = false;
    mDisconnect = false;

    restartHeartbeatCheck();
    if (mBatchOn){
        mQueryPosition = 0;
        mCurrentQueryPort = mMapAssociatedDetector[mQueryPosition];
    } else {
        mCurrentQueryPort = port==0 ? 1 : port;
    }
    mCurrentCommand = QString("interface GigabitEthernet 0/0/%1").arg(mCurrentQueryPort) + "\r\n";
#if USE_QTELNET
    mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
#else
    mTelnet->write(mCurrentCommand.toLatin1());
#endif

    return true;
}

void QHuaWeiSwitcherHelper::openNextSwitcherPOEPower()
{
    if (mCurrentQueryPort >= DET_NUM)
        return;

    restartHeartbeatCheck();
    if (++mQueryPosition>=mMapAssociatedDetector.size())
        return;
    else
        mCurrentQueryPort = mMapAssociatedDetector[mQueryPosition];
    mCurrentCommand = QString("interface GigabitEthernet 0/0/%1").arg(mCurrentQueryPort) + "\r\n";
#if USE_QTELNET
    mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
#else
    mTelnet->write(mCurrentCommand.toLatin1());
#endif
}

void QHuaWeiSwitcherHelper::queryNextSwitcherPOEPower()
{
    if (mCurrentQueryPort >= DET_NUM)
        return;

    restartHeartbeatCheck();

    if (++mQueryPosition>=mMapAssociatedDetector.size())
        return;
    else
        mCurrentQueryPort = mMapAssociatedDetector[mQueryPosition];
    //mCurrentQueryPort++;
    mCurrentCommand = QString("display poe power-state interface GigabitEthernet0/0/%1").arg(mCurrentQueryPort) + "\r\n";
#if USE_QTELNET
    mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
#else
    mTelnet->write(mCurrentCommand.toLatin1());
#endif
}

/*
 关闭交换机POE口输出电源
*/
bool QHuaWeiSwitcherHelper::closeSwitcherPOEPower(quint8 port/* = 0*/, bool disconnect/* = false*/)
{
#if USE_QTELNET
    if (nullptr == mTelnet || !mTelnet->isConnected() || !mSwitcherInSystemView)
#else
    if (nullptr == mTelnet || !(mTelnet->state() == QAbstractSocket::ConnectedState) || !mSwitcherInSystemView)
#endif
        return false;

    mBatchOn = false;
    mSingleOn = false;
    mDisconnect = disconnect;
    mBatchOff = port == 00 ? true : false;
    mSingleOff = port == 00 ? false : true;

    restartHeartbeatCheck();
    if (mBatchOff){
        mQueryPosition = 0;
        mCurrentQueryPort = mMapAssociatedDetector[mQueryPosition];
    } else {
        mCurrentQueryPort = port==0 ? 1 : port;
    }
    //mCurrentQueryPort = port==0 ? 1 : port;
    mCurrentCommand = QString("interface GigabitEthernet 0/0/%1").arg(mCurrentQueryPort) + "\r\n";
#if USE_QTELNET
    mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
#else
    mTelnet->write(mCurrentCommand.toLatin1());
#endif

    return true;
}

void QHuaWeiSwitcherHelper::closeNextSwitcherPOEPower()
{
    if (mCurrentQueryPort >= DET_NUM)
    {
        // 已经关闭了所有POE供电口
        if (mDisconnect)
        {
            mTelnet->disconnectFromHost();
            mIsLoggingOut = false;
            mLogoutStep = 0;
            mSwitcherIsLoginOk = false;
            mSwitcherInSystemView = false;
            mSwitcherIsBusy = false;
            mRespondString.clear();
            emit switcherDisconnected(mIp);
        }

        return;
    }

    restartHeartbeatCheck();
    if (++mQueryPosition>=mMapAssociatedDetector.size())
        return;
    else
        mCurrentQueryPort = mMapAssociatedDetector[mQueryPosition];
    mCurrentCommand = QString("interface GigabitEthernet 0/0/%1").arg(mCurrentQueryPort) + "\r\n";
#if USE_QTELNET
    mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
#else
    mTelnet->write(mCurrentCommand.toLatin1());
#endif
}

//是否仍然处于登录状态
void QHuaWeiSwitcherHelper::checkLoginAlive()
{
#if USE_QTELNET
    if (!mTelnet->isConnected()) {
#else
    if (!(mTelnet->state() == QAbstractSocket::ConnectedState)) {
#endif
        mSwitcherIsLoginOk = false;
        return;
    }

    // 测试过程暂时屏蔽掉
#pragma message("Temporarily disable the clock heartbeat detection: ")
    return ;

    mCurrentCommand = "display clock\r\n";
    mWaitingHeartbeatResponse = true;
    mLastHeartbeatTime.restart();
#if USE_QTELNET
    mTelnet->sendData(mCurrentCommand.toStdString().c_str(), mCurrentCommand.size());
#else
    mTelnet->write(mCurrentCommand.toLatin1());
#endif
    qDebug().nospace().noquote() << "交换机[" << mIp << "]发送心跳检测命令";
}

// 启动心跳检测
void QHuaWeiSwitcherHelper::startHeartbeatCheck()
{
    if (mHeartbeatTimer && !mHeartbeatTimer->isActive()) {
        mLastHeartbeatTime.start();
        mWaitingHeartbeatResponse = false;
        mHeartbeatTimer->start();
        qInfo().nospace().noquote() << "交换机[" << mIp << "]心跳检测已启动";
    }
}

void QHuaWeiSwitcherHelper::restartHeartbeatCheck()
{
    if (mHeartbeatTimer && mHeartbeatTimer->isActive()) {
        mLastHeartbeatTime.restart();
        mWaitingHeartbeatResponse = false;
        mHeartbeatTimer->stop();
        mHeartbeatTimer->start();
    }
}

// 停止心跳检测
void QHuaWeiSwitcherHelper::stopHeartbeatCheck()
{
    if (mHeartbeatTimer && mHeartbeatTimer->isActive()) {
        mHeartbeatTimer->stop();
        mWaitingHeartbeatResponse = false;
        qInfo().nospace().noquote() << "交换机[" << mIp << "]心跳检测已停止";
    }
}

// 执行心跳检测
void QHuaWeiSwitcherHelper::performHeartbeatCheck()
{
    // 如果正在重连中，跳过本次检测
    if (mIsReconnecting) {
        return;
    }
    
    // 如果不在系统视图或未连接，尝试重连
#if USE_QTELNET
    if (!mTelnet->isConnected() || !mSwitcherInSystemView) {
#else
    if (!(mTelnet->state() == QAbstractSocket::ConnectedState) || !mSwitcherInSystemView) {
#endif
        qInfo().nospace().noquote() << "交换机[" << mIp << "]连接状态异常，尝试重连";
        reconnectSwitcher();
        return;
    }
    
    // 如果正在等待上一次心跳响应，检查是否超时
    if (mWaitingHeartbeatResponse) {
        // 如果超过10秒没有收到响应，认为掉线
        if (mLastHeartbeatTime.elapsed() > 10000) {
            qInfo().nospace().noquote() << "交换机[" << mIp << "]心跳响应超时，可能已掉线，尝试重连";
            reconnectSwitcher();
            return;
        }
    }
    
    // 发送心跳命令
    if (!mSwitcherIsBusy) {
        checkLoginAlive();
    }
}

// 重连交换机
void QHuaWeiSwitcherHelper::reconnectSwitcher()
{
    // 防止重复重连
    if (mIsReconnecting) {
        return;
    }
    
    mIsReconnecting = true;
    stopHeartbeatCheck();
    
    qInfo().nospace().noquote() << "交换机[" << mIp << "]开始重连...";
    
    // 断开当前连接
#if USE_QTELNET
    if (mTelnet->isConnected()) {
#else
    if (mTelnet->state() == QAbstractSocket::ConnectedState) {
#endif
        mTelnet->disconnectFromHost();
    }
    
    // 重置状态
    mSwitcherIsLoginOk = false;
    mSwitcherInSystemView = false;
    mRespondString.clear();
    
    // 延迟一下再重连，避免立即重连失败
    QTimer::singleShot(2000, this, [=](){
#if USE_QTELNET
        mTelnet->setType(QTelnet::TCP);
        if (mTelnet->connectToHost(mIp, 23)){
#else
        mTelnet->connectToHost(mIp, 23);
        mTelnet->waitForConnected(1000);
        if (mTelnet->state() == QAbstractSocket::ConnectedState){
#endif
            qInfo().nospace().noquote() << "交换机[" << mIp << "]重连请求已发送";
        } else {
            qWarning().nospace().noquote() << "交换机[" << mIp << "]重连失败，将在下次心跳检测时重试";
            mIsReconnecting = false;
            // 如果重连失败，延迟一段时间后再次尝试
            QTimer::singleShot(30000, this, [=](){
                mIsReconnecting = false;
            });
        }
    });
}

void QHuaWeiSwitcherHelper::logout()
{
    // 未连接则直接置状态并发出断开信号
#if USE_QTELNET
    if (!mTelnet || !mTelnet->isConnected()) {
#else
    if (!mTelnet || !(mTelnet->state() == QAbstractSocket::ConnectedState)) {
#endif
        mSwitcherIsLoginOk = false;
        mSwitcherInSystemView = false;
        mIsLoggingOut = false;
        mLogoutStep = 0;
        mRespondString.clear();
        emit switcherDisconnected(mIp);
        return;
    }

    // 若已经在退出流程中，则不重复触发
    if (mIsLoggingOut)
        return;

    // 停止其它业务指令，避免退出过程中还在发 poe/query 等命令
    mSwitcherIsBusy = true;
    mCurrentCommand.clear();
    mRespondString.clear();

    mIsLoggingOut = true;
    mLogoutStep = 1;

    // 先 quit（从 [HUAWEI] 回到 <HUAWEI>，或直接触发退出）
    QString cmd = "quit\r\n";
#if USE_QTELNET
    mTelnet->sendData(cmd.toStdString().c_str(), cmd.size());
#else
    mTelnet->write(cmd.toLatin1());
#endif

    // 兜底：如果对端不回显/不主动断开，3 秒后强制关闭 socket
    QTimer::singleShot(3000, this, [=](){
        if (mIsLoggingOut && mTelnet) {
            mTelnet->disconnectFromHost();
            mIsLoggingOut = false;
            mLogoutStep = 0;
            mSwitcherIsLoginOk = false;
            mSwitcherInSystemView = false;
            mSwitcherIsBusy = false;
            mRespondString.clear();
            emit switcherDisconnected(mIp);
        }
    });
}
