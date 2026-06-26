#include "qhuaweiswitcherhelper.h"
#include "globalsettings.h"

#ifndef USE_QTELNET
#include <QTcpSocket>
using QTelnet = QTcpSocket;
#else
#include <QTelnet.h>
#endif

QHuaWeiSwitcherHelper::QHuaWeiSwitcherHelper(QString ip, QObject *parent)
    : QObject{parent}
    , mState(State::Disconnected)
    , mIp(ip)
{
    mFileCommand = new QFile(QString("./logs/Switcher-%1.log").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd-HHmmss")));
    mFileCommand->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append);
    mTextStream = new QTextStream(mFileCommand);

    mTelnet = new QTelnet(this);
    mHeartbeatTimer = new QTimer(this);
    connect(mHeartbeatTimer, &QTimer::timeout, this, &QHuaWeiSwitcherHelper::performHeartbeatCheck);

    // 初始化心跳检测
    mHeartbeatTimer->setInterval(60000); // 每60秒检测一次
    mHeartbeatTimer->setSingleShot(false);
    mWaitingHeartbeatResponse = false;
    mIsReconnecting = false;

#ifndef USE_QTELNET
    connect(mTelnet, &QTcpSocket::readyRead,this,[=](){
        while (mTelnet->bytesAvailable() > 0)
        {
            QByteArray rx_current = mTelnet->readLine();//readAll();
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

            *mTextStream << rx_current;
            mTextStream->flush();
        }

#else
    connect(mTelnet, &QTelnet::readyRead,this,[=](const QByteArray& dest){
        mRespondString.append(dest);
        *mTextStream << dest;
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
                    mTelnet->write(cmd.toLatin1());
                    mLogoutStep = 2;
                    return;
                }
                if (inSystemView)
                    return;
            }

            // Step 2: 发完第二个 quit，通常对端会断开连接
            if (mLogoutStep == 2) {
                QTimer::singleShot(100, this, [=](){
                    mTelnet->disconnectFromHost();
                });

                stopHeartbeatCheck();
                mIsLoggingOut = false;
                mLogoutStep = 0;
                mIsReconnecting = false;
                mSwitcherIsLoginOk = false;
                mSwitcherInSystemView = false;
                mSwitcherIsBusy.store(false);
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
            mTelnet->write(data.toLatin1());
        }
        /**
         * 处理特殊情况，初始密码未修改的情况下
         * Please enter old password:
         */
        if(mRespondString.endsWith("Please enter old password: ")){
            mRespondString.clear();

            QString data = "root@12345\r\n";
            mTelnet->write(data.toLatin1());
        }

        if(mRespondString.endsWith("---- More ----")){
            mRespondString.clear();

            QString data = "\r\n";
            mTelnet->write(data.toLatin1());
        }
        else if(mRespondString.endsWith("[Y/N]:")){
            mRespondString.clear();

            QString data = "Y\r\n";
            mTelnet->write(data.toLatin1());
        }
        else if(mRespondString.endsWith("Username:")){
            mRespondString.clear();

            QString data = "root\r\n";
            mTelnet->write(data.toLatin1());
        }
        else if(mRespondString.endsWith("Password:")){
            mRespondString.clear();

            QString data = "root@12345\r\n";
            mTelnet->write(data.toLatin1());
        }
        else if(mRespondString.endsWith("<HUAWEI>") && !mSwitcherIsLoginOk){
            mSwitcherIsLoginOk = true;
            mRespondString.clear();

            QString data = "system-view\r\n";
            mTelnet->write(data.toLatin1());
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

            executeNextAction(true);
        }
        // 检测心跳响应（只要在等待心跳响应且响应包含 HUAWEI 提示符就认为是心跳响应）
        else if (mWaitingHeartbeatResponse &&
                 (mRespondString.contains("[HUAWEI]") || mRespondString.contains("<HUAWEI>") || mRespondString.contains("UTC+08:00\r\n")))
        {
            // 收到心跳响应，重置标志
            mWaitingHeartbeatResponse = false;
            mLastHeartbeatTime.restart();
            mRespondString.clear();

            executeNextAction();
            // qDebug().nospace().noquote() << "交换机[" << mIp << "]心跳检测正常";
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
             * Warning: .
            */

            //根据结果来判断
            mRespondString.replace("\r\n","\n");

            QString switchCmd = QStringLiteral("[HUAWEI-GigabitEthernet0/0/%1]").arg(mCurrentPoeIndex);//进入interface GigabitEthernet之后
            QString switchCmd3 = QStringLiteral("[HUAWEI]");//进入视图之后
            QString queryCmd = QStringLiteral("display poe power-state interface GigabitEthernet0/0/%1\r\n").arg(mCurrentPoeIndex);
            QString enterInterfaceCmd = QStringLiteral("interface GigabitEthernet 0/0/%1\r\n").arg(mCurrentPoeIndex);

            if (mCurrentAction == acQueryState){
            //if (mCurrentCommand == queryCmd){
                // 系统视图或接口视图下查询均可完成
                if (mRespondString.endsWith(switchCmd3.toLatin1()) ||
                    mRespondString.endsWith(switchCmd.toLatin1())){
                    QList<QByteArray> lines = mRespondString.split('\n');


                    QMap<QString, QString> mapValues;
                    for (auto line : lines)
                    {
                        QList<QByteArray> values = line.split(':');
                        if (values.size() > 1)
                            mapValues[values[0].trimmed()] = values[1].trimmed();
                    }

                    qDebug() << "返回交换机POE口电源查询状态：" << mCurrentPoeIndex;
                    qint8 index = this->indexOfPort(mCurrentPoeIndex);
                        handlePoeQueryResult(mCurrentPoeIndex, index, mapValues);

                    mRespondString.clear();
                    executeNextAction();
                }
            }
            else if (mCurrentAction == acSwitchInterface){//if (mCurrentCommand == enterInterfaceCmd){
                if (mRespondString.endsWith(switchCmd.toLatin1())){                    
                    mRespondString.clear();

                    // 执行下一条指令
                    executeNextAction();
                }
            }
            else if (mCurrentAction == acOpenPoe){//if (mCurrentCommand == "poe enable\r\n"){
                if (mRespondString.endsWith(switchCmd.toLatin1())){
                    qDebug() << "返回交换机POE口电源控制状态[开]：" << mCurrentPoeIndex;
                    mRespondString.clear();
                    executeNextAction();
                }
            }
            else if (mCurrentAction == acClosePoe){//if (mCurrentCommand == "undo poe enable\r\n"){
                if (mRespondString.endsWith(switchCmd.toLatin1())){
                    qDebug() << "返回交换机POE口电源控制状态[关]：" << mCurrentPoeIndex;
                    mRespondString.clear();
                    executeNextAction();
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
    });

    connect(mTelnet, &QTelnet::errorOccurred, this, [=](QAbstractSocket::SocketError socketError){
        mSwitcherIsLoginOk = false;
        mSwitcherInSystemView = false;
        QMutexLocker locker(&mActionLocker);
        mActionsWait.clear();// 交换机都下线了，就把之前的动作都清空吧
        mVerifyByPort.clear();
        stopHeartbeatCheck();  // 停止心跳检测
        qDebug().nospace().noquote() << "交换机[" << mIp << "]连接错误，错误码：" << socketError;
        emit switcherDisconnected(mIp);
    });

    // 监听连接状态变化，在断开时停止心跳检测
    connect(mTelnet, &QTelnet::stateChanged, this, [=](QAbstractSocket::SocketState socketState){
        if (socketState == QAbstractSocket::UnconnectedState) {
            stopHeartbeatCheck();
            qDebug().nospace().noquote() << "交换机[" << mIp << "]连接断开[QTelnet::stateChanged]";
            mSwitcherIsLoginOk = false;
            mSwitcherInSystemView = false;
            mIsReconnecting = false;
            mSwitcherIsBusy.store(false);
            QMutexLocker locker(&mActionLocker);
            mActionsWait.clear();// 交换机都下线了，就把之前的动作都清空吧
            mVerifyByPort.clear();
        } else if (socketState == QAbstractSocket::ConnectedState && mSwitcherInSystemView) {
            // 如果已经登录成功，重新启动心跳检测
            startHeartbeatCheck();
        }
    });

}

QHuaWeiSwitcherHelper::~QHuaWeiSwitcherHelper()
{
    delete mTextStream;
    mTextStream = nullptr;
    delete mFileCommand;
    mFileCommand = nullptr;
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
    if (mSwitcherIsBusy.load())
        return ;

    if (mMapAssociatedDetector.size() <= 0)
        return;

    {
        QMutexLocker locker(&mActionLocker);
        for (auto iter = mMapAssociatedDetector.begin(); iter!=mMapAssociatedDetector.end(); ++iter){
            mActionsWait.push_back(ACTION_WAIT(*iter, acQueryState));
        }
    }

    mTelnet->disconnectFromHost();
    mTelnet->connectToHost(mIp, 23);
    //mTelnet->waitForConnected(1000);
    if (mTelnet->state() == QAbstractSocket::ConnectedState){
        emit switcherConnected(mIp);

        if (!mSwitcherIsBusy.load() || (mBusyStartTimer.isValid() && mBusyStartTimer.elapsed() >= 30000)/*距离上次指令执行已经超过30s*/){
            executeNextAction(true);
        }
    }
    else
    {
        //emit switcherDisconnected(mIp);
    }
}

void QHuaWeiSwitcherHelper::forceOpenPower()
{
    qInfo() << "开启所有谱仪POE电源";
    openSwitcherPOEPower(0x00);
    if (mTelnet->state() == QAbstractSocket::ConnectedState)
    {
        restartHeartbeatCheck();

        emit switcherLogged(mIp);
    }
    else
    {
        mTelnet->disconnectFromHost();
        mTelnet->connectToHost(mIp, 23);
        //mTelnet->waitForConnected(1000);
        if (mTelnet->state() == QAbstractSocket::ConnectedState){
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
    if (nullptr == mTelnet || !(mTelnet->state() == QAbstractSocket::ConnectedState) || !mSwitcherInSystemView)
        return false;

    restartHeartbeatCheck();
    mDisconnect = false;

    {
        QMutexLocker locker(&mActionLocker);
        if (mBusyStartTimer.isValid() && mBusyStartTimer.elapsed() >= 30000 && mActionsWait.size() > 0)
            mActionsWait.clear();// 为了防止指令堵塞，清空闲置指令

        if (port == 0){            
            for (auto iter = mMapAssociatedDetector.begin(); iter!=mMapAssociatedDetector.end(); ++iter){
                // 按照优先级，删除之前未执行的指令，达到快速响应效果
                mActionsWait.erase(
                    std::remove_if(mActionsWait.begin(), mActionsWait.end(),
                                   [=](const ACTION_WAIT& item){
                                       return item.poeIndex == *iter;
                                   }
                                   ), mActionsWait.end()
                    );

                if (!mMapPoeState.contains(*iter) || !mMapPoeState[*iter]){
                    enqueuePoeOpenActions(*iter);
                }
            }
        }
        else{
            if (!mMapPoeState.contains(port) || !mMapPoeState[port]){
                enqueuePoeOpenActions(port);
            }
        }
    }

    if (!mSwitcherIsBusy.load() || (mBusyStartTimer.isValid() && mBusyStartTimer.elapsed() >= 30000)/*距离上次指令执行已经超过30s*/){
        executeNextAction(true);
    }

    return true;
}

/*
 关闭交换机POE口输出电源
*/
bool QHuaWeiSwitcherHelper::closeSwitcherPOEPower(quint8 port/* = 0*/, bool disconnect/* = false*/)
{
    if (nullptr == mTelnet || !(mTelnet->state() == QAbstractSocket::ConnectedState) || !mSwitcherInSystemView)
        return false;

    {
        QMutexLocker locker(&mActionLocker);
        if (mBusyStartTimer.isValid() && mBusyStartTimer.elapsed() >= 30000 && mActionsWait.size() > 0)
            mActionsWait.clear();// 为了防止指令堵塞，清空闲置指令

        if (port == 0){
            for (auto iter = mMapAssociatedDetector.begin(); iter!=mMapAssociatedDetector.end(); ++iter){
                // 按照优先级，删除之前未执行的指令，达到快速响应效果
                mActionsWait.erase(
                    std::remove_if(mActionsWait.begin(), mActionsWait.end(),
                       [=](const ACTION_WAIT& item){
                           return item.poeIndex == *iter;
                        }
                    ), mActionsWait.end()
                );
                enqueuePoeCloseActions(*iter);
            }
        }
        else{
            enqueuePoeCloseActions(port);
        }
    }

    if (!mSwitcherIsBusy.load() || (mBusyStartTimer.isValid() && mBusyStartTimer.elapsed() >= 30000)/*距离上次指令执行已经超过5s*/){
        executeNextAction(true);
    }

    return true;
}

//是否仍然处于登录状态
void QHuaWeiSwitcherHelper::checkLoginAlive()
{
    if (!(mTelnet->state() == QAbstractSocket::ConnectedState)) {
        mSwitcherIsLoginOk = false;
        return;
    }

    {
        QMutexLocker locker(&mActionLocker);
        if (mBusyStartTimer.isValid() && mBusyStartTimer.elapsed() >= 30000)
            mActionsWait.clear();// 为了防止指令堵塞，清空闲置指令

        mActionsWait.push_back(ACTION_WAIT(0x00, acKeepLive));
    }

    if (!mSwitcherIsBusy.load() || (mBusyStartTimer.isValid() && mBusyStartTimer.elapsed() >= 30000)/*距离上次指令执行已经超过5s*/){
        executeNextAction(true);
    // qDebug().nospace().noquote() << "交换机[" << mIp << "]发送心跳检测命令";
    }
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
#if (ENABLE_LIBHV_SOCKET || USE_QTELNET)
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
#if ENABLE_LIBHV_SOCKET
    if (mTelnet->isConnected())
        mTelnet->closesocket();
#else
#if USE_QTELNET
    if (mTelnet->isConnected()) {
#else
    if (mTelnet->state() == QAbstractSocket::ConnectedState) {
#endif //USE_QTELNET
        mTelnet->disconnectFromHost();
    }
#endif //ENABLE_LIBHV_SOCKET
    
    // 重置状态
    mSwitcherIsLoginOk = false;
    mSwitcherInSystemView = false;
    mRespondString.clear();
    
    // 延迟一下再重连，避免立即重连失败
#if ENABLE_LIBHV_SOCKET
    QThreadTimer::singleShot(2000, this, [=](){
        if (mTelnet->createsocket(23, mIp.toStdString().c_str())>=0){
            mTelnet->start();
#else
    QTimer::singleShot(2000, this, [=](){
#if USE_QTELNET
        mTelnet->setType(QTelnet::TCP);
        if (mTelnet->connectToHost(mIp, 23)){
#else
        mTelnet->connectToHost(mIp, 23);
        mTelnet->waitForConnected(1000);
        if (mTelnet->state() == QAbstractSocket::ConnectedState){
#endif // USE_QTELNET
#endif //ENABLE_LIBHV_SOCKET
            qInfo().nospace().noquote() << "交换机[" << mIp << "]重连请求已发送";
        } else {
            qWarning().nospace().noquote() << "交换机[" << mIp << "]重连失败，将在下次心跳检测时重试";
            mIsReconnecting = false;
            // 如果重连失败，延迟一段时间后再次尝试
#if ENABLE_LIBHV_SOCKET
            QThreadTimer::singleShot(30000, this, [=](){
#else
            QTimer::singleShot(30000, this, [=](){
#endif
                mIsReconnecting = false;
            });
        }
    });
}

void QHuaWeiSwitcherHelper::logout()
{
    // 未连接则直接置状态并发出断开信号
#if (ENABLE_LIBHV_SOCKET || USE_QTELNET)
    if (!mTelnet || !mTelnet->isConnected()) {
#else
    if (!mTelnet || !(mTelnet->state() == QAbstractSocket::ConnectedState)) {
#endif
        mSwitcherIsLoginOk = false;
        mSwitcherInSystemView = false;
        mIsLoggingOut = false;
        mLogoutStep = 0;
        mIsReconnecting = false;
        mSwitcherIsBusy.store(false);
        mRespondString.clear();
        emit switcherDisconnected(mIp);
        return;
    }

    // 若已经在退出流程中，则不重复触发
    if (mIsLoggingOut)
        return;

    stopHeartbeatCheck();
    // 停止其它业务指令，避免退出过程中还在发 poe/query 等命令
    mSwitcherIsBusy.store(true);
    mCurrentCommand.clear();
    mRespondString.clear();

    mIsLoggingOut = true;
    mLogoutStep = 1;

    // 先 quit（从 [HUAWEI] 回到 <HUAWEI>，或直接触发退出）
    QString cmd = "quit\r\n";
#if ENABLE_LIBHV_SOCKET
    mTelnet->channel->write(cmd.toStdString().c_str(), cmd.size());
#elif USE_QTELNET
    mTelnet->sendData(cmd.toStdString().c_str(), cmd.size());
#else
    mTelnet->write(cmd.toLatin1());
#endif

    // 兜底：如果对端不回显/不主动断开，3 秒后强制关闭 socket
#if ENABLE_LIBHV_SOCKET
    QThreadTimer::singleShot(3000, this, [=](){
#else
    QTimer::singleShot(3000, this, [=](){
#endif
        if (mIsLoggingOut && mTelnet) {
#if ENABLE_LIBHV_SOCKET
            mTelnet->closesocket();
#else
            mTelnet->disconnectFromHost();
#endif
            mIsLoggingOut = false;
            mLogoutStep = 0;
            mIsReconnecting = false;
            mSwitcherIsLoginOk = false;
            mSwitcherInSystemView = false;
            mSwitcherIsBusy.store(false);
            mRespondString.clear();
            emit switcherDisconnected(mIp);
        }
    });
}

/*
 执行下一个动作
*/
void QHuaWeiSwitcherHelper::enqueuePoeOpenActions(quint8 port)
{
    PoeVerifyContext ctx;
    ctx.intent = PoeIntent::Open;
    ctx.retryCount = 0;
    ctx.waitingRetry = false;
    mVerifyByPort[port] = ctx;

    mActionsWait.push_back(ACTION_WAIT(port, acSwitchInterface));
    mActionsWait.push_back(ACTION_WAIT(port, acOpenPoe));
    mActionsWait.push_back(ACTION_WAIT(port, acQueryState));
}

void QHuaWeiSwitcherHelper::enqueuePoeCloseActions(quint8 port)
{
    PoeVerifyContext ctx;
    ctx.intent = PoeIntent::Close;
    ctx.retryCount = 0;
    ctx.waitingRetry = false;
    mVerifyByPort[port] = ctx;

    mActionsWait.push_back(ACTION_WAIT(port, acSwitchInterface));
    mActionsWait.push_back(ACTION_WAIT(port, acClosePoe));
    mActionsWait.push_back(ACTION_WAIT(port, acQueryState));
}

bool QHuaWeiSwitcherHelper::verifyPoeState(const QMap<QString, QString> &mapValues, PoeIntent intent) const
{
    const QString enableState = mapValues.value(QStringLiteral("Power enable state"));
    const QString powerOnOff = mapValues.value(QStringLiteral("Power ON/OFF"));
    const QString powerStatus = mapValues.value(QStringLiteral("Power status"));

    if (enableState.isEmpty())
        return false;

    if (enableState == QStringLiteral("enable") && powerOnOff == QStringLiteral("off")) {
        if (intent == PoeIntent::Open && powerStatus == QStringLiteral("Detecting"))
            return true;
        return false;
    }
    if (enableState == QStringLiteral("disable") && powerOnOff == QStringLiteral("on"))
        return false;

    if (intent == PoeIntent::Close)
        return enableState == QStringLiteral("disable");
    if (intent == PoeIntent::Open)
        return enableState == QStringLiteral("enable")
               && (powerOnOff == QStringLiteral("on") || powerStatus == QStringLiteral("Detecting"));

    return true;
}

void QHuaWeiSwitcherHelper::applyVerifiedPoeState(quint8 port, qint8 detIndex,
                                                  const QMap<QString, QString> &mapValues,
                                                  PoeIntent intent)
{
    const bool poweredOn = (intent == PoeIntent::Open);
    mMapPoeState[port] = poweredOn;

    if (detIndex <= 0)
        return;

    qDebug().nospace() << "POE校验通过 谱仪[#" << detIndex << "] 端口" << port
                       << (poweredOn ? " 开" : " 关")
                       << " enable=" << mapValues.value(QStringLiteral("Power enable state"))
                       << " onOff=" << mapValues.value(QStringLiteral("Power ON/OFF"))
                       << " status=" << mapValues.value(QStringLiteral("Power status"));

    QMetaObject::invokeMethod(this, "reportPoePowerStatus", Qt::QueuedConnection,
                              Q_ARG(quint8, detIndex), Q_ARG(bool, poweredOn));
}

void QHuaWeiSwitcherHelper::schedulePoeRetry(quint8 port, PoeIntent intent, int delayMs)
{
    mVerifyByPort[port].waitingRetry = true;

    QTimer::singleShot(delayMs, this, [this, port, intent]() {
        if (!mVerifyByPort.contains(port))
            return;

        mVerifyByPort[port].waitingRetry = false;

        {
            QMutexLocker locker(&mActionLocker);
            mActionsWait.push_back(ACTION_WAIT(port, acSwitchInterface));
            if (intent == PoeIntent::Open)
                mActionsWait.push_back(ACTION_WAIT(port, acOpenPoe));
            else
                mActionsWait.push_back(ACTION_WAIT(port, acClosePoe));
            mActionsWait.push_back(ACTION_WAIT(port, acQueryState));
        }

        if (!mSwitcherIsBusy.load()
            || (mBusyStartTimer.isValid() && mBusyStartTimer.elapsed() >= 30000)) {
            executeNextAction(true);
        }
    });
}

void QHuaWeiSwitcherHelper::handlePoeQueryResult(quint8 port, qint8 detIndex,
                                                 const QMap<QString, QString> &mapValues)
{
    if (mapValues.contains(QStringLiteral("Error"))) {
        qDebug() << mCurrentCommand << mapValues.value(QStringLiteral("Error"));
        if (!mVerifyByPort.contains(port) || mVerifyByPort[port].intent == PoeIntent::None)
            return;
    }

    const QString enableState = mapValues.value(QStringLiteral("Power enable state"));
    const QString powerOnOff = mapValues.value(QStringLiteral("Power ON/OFF"));
    const QString powerStatus = mapValues.value(QStringLiteral("Power status"));
    const QString targetLabel = detIndex > 0
            ? QStringLiteral("谱仪[#%1]").arg(detIndex)
            : QStringLiteral("POE端口%1").arg(port);

    if (!mVerifyByPort.contains(port) || mVerifyByPort[port].intent == PoeIntent::None) {
        if (detIndex > 0) {
            if (enableState == QStringLiteral("enable")) {
                mMapPoeState[port] = true;
                qDebug() << "返回交换机POE口电源查询状态[开]：" << port;
                QMetaObject::invokeMethod(this, "reportPoePowerStatus", Qt::QueuedConnection,
                                          Q_ARG(quint8, detIndex), Q_ARG(bool, true));
            } else if (enableState == QStringLiteral("disable")) {
                mMapPoeState[port] = false;
                qDebug() << "返回交换机POE口电源查询状态[关]：" << port;
                QMetaObject::invokeMethod(this, "reportPoePowerStatus", Qt::QueuedConnection,
                                          Q_ARG(quint8, detIndex), Q_ARG(bool, false));
            } else {
                qDebug() << "返回交换机POE口电源查询状态[消息]：" << enableState << powerOnOff << powerStatus;
            }
        } else {
            qDebug() << "返回交换机POE口电源查询状态[异常] port=" << port;
        }
        return;
    }

    PoeVerifyContext &ctx = mVerifyByPort[port];
    const PoeIntent intent = ctx.intent;
    const QString intentText = (intent == PoeIntent::Open) ? QStringLiteral("开电") : QStringLiteral("关电");

    if (verifyPoeState(mapValues, intent)) {
        applyVerifiedPoeState(port, detIndex, mapValues, intent);
        mVerifyByPort.remove(port);
        return;
    }

    if (ctx.retryCount < kMaxPoeRetries) {
        const int attempt = ctx.retryCount + 1;
        const int delayMs = kRetryDelaysMs[ctx.retryCount];
        qDebug().nospace() << "POE校验失败 " << targetLabel << " 端口" << port
                           << " 意图=" << intentText
                           << " 第" << attempt << "/" << kMaxPoeRetries << "次重试"
                           << " enable=" << enableState
                           << " onOff=" << powerOnOff
                           << " status=" << powerStatus
                           << " " << delayMs << "ms后重试"
                           << (intent == PoeIntent::Open ? "poe enable" : "undo poe enable");
        ctx.retryCount = attempt;
        schedulePoeRetry(port, intent, delayMs);
        return;
    }

    qInfo().nospace() << targetLabel << intentText
                      << "失败：PoE状态未收敛(enable=" << enableState
                      << ", ON/OFF=" << powerOnOff
                      << ", status=" << powerStatus << ")，已重试" << kMaxPoeRetries << "次";

    if (detIndex > 0) {
        const bool reportOn = (intent == PoeIntent::Open);
        mMapPoeState[port] = reportOn;
        QMetaObject::invokeMethod(this, "reportPoePowerStatus", Qt::QueuedConnection,
                                  Q_ARG(quint8, detIndex), Q_ARG(bool, reportOn));
    }
    mVerifyByPort.remove(port);
}

bool QHuaWeiSwitcherHelper::hasWaitingPoeRetry() const
{
    for (auto it = mVerifyByPort.constBegin(); it != mVerifyByPort.constEnd(); ++it) {
        if (it.value().waitingRetry)
            return true;
    }
    return false;
}

void QHuaWeiSwitcherHelper::executeNextAction(bool keepFirst)
{
    mSwitcherIsBusy.store(false);
    restartHeartbeatCheck();

    _action nextAct = acNone;
    quint8 nextPoeIndex = 0xff;
    bool notifyIdle = false;
    {
        QMutexLocker locket(&mActionLocker);
        if (mActionsWait.isEmpty()) {
            if (hasWaitingPoeRetry())
                return;
            return;
        }

        if (!keepFirst)
            mActionsWait.removeFirst();

        if (mActionsWait.isEmpty()) {
            if (hasWaitingPoeRetry())
                return;
            notifyIdle = true;
        } else {
            nextPoeIndex = mActionsWait[0].poeIndex;
            nextAct = mActionsWait[0].action;
        }
    }

    if (notifyIdle) {
        QMetaObject::invokeMethod(this, [this]() {
            emit poeActionQueueIdle();
        }, Qt::QueuedConnection);
        return ;
    }

    mCurrentPoeIndex = nextPoeIndex;
    mCurrentAction = nextAct;
    if (mCurrentAction == acQueryState){
        qDebug() << "查询交换机POE口电源状态：" << mCurrentPoeIndex;
        mCurrentCommand = QString("display poe power-state interface GigabitEthernet0/0/%1\r\n").arg(mCurrentPoeIndex);
        /*
            [HUAWEI-GigabitEthernet0/0/1]display poe power-state interface GigabitEthernet0/0/1\r\n
            Legacy detect           : disable
            Power enable state      : disable
            Power fast-on state     : disable
            Single-class state      : disable
            Power-up mode           : at
            Power-on delay(s)       : 0
            PD power auto-neg state : disable
            PD power-up mode        : -
            PD support mode         : -
            Power ON/OFF            : off
            Power status            : Disabled
            PD class                : -
            Reference power(mW)     : -
            Power priority          : Low
            Max power(mW)           : 30000
            Current power(mW)       : 0
            Peak power(mW)          : 0
            Average power(mW)       : 0
        */
    }
    else if (mCurrentAction == acOpenPoe){
        qDebug() << "控制交换机POE口电源打开：" << mCurrentPoeIndex;
        mCurrentCommand = "poe enable\r\n";
    }
    else if (mCurrentAction == acClosePoe){
        qDebug() << "控制交换机POE口电源关闭：" << mCurrentPoeIndex;
        mCurrentCommand = "undo poe enable\r\n";
    }
    else if (mCurrentAction == acKeepLive){
        qDebug() << "交换机心跳检测指令";
        mCurrentCommand = "display clock\r\n";
    }
    else{
        qDebug() << "切换交换机POE口：" << mCurrentPoeIndex;
        mCurrentCommand = QString("interface GigabitEthernet 0/0/%1\r\n").arg(mCurrentPoeIndex);
    }

    mBusyStartTimer.start();
    mSwitcherIsBusy.store(true);
    QTimer::singleShot(100, this, [=](){
        if (mCurrentAction == acKeepLive){
            mWaitingHeartbeatResponse = true;
            mLastHeartbeatTime.start();
        } else {
            mBusyStartTimer.start();
        }

        *mTextStream << "\n==================================================\n";
        *mTextStream << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz :") << mCurrentCommand;
        mTelnet->write(mCurrentCommand.toLatin1());
    });
}

bool QHuaWeiSwitcherHelper::hasPendingActions()
{
    QMutexLocker locker(&mActionLocker);
    return !mActionsWait.isEmpty() || mSwitcherIsBusy.load() || hasWaitingPoeRetry();
}
