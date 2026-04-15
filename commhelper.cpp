#include "commhelper.h"
#include <QDataStream>
#include <QNetworkSession>
#include <QNetworkConfigurationManager>

// 全局类名与 commhelper.h 中 friend 声明一致
class SpectrumH5FlushThread : public QThread {
public:
    explicit SpectrumH5FlushThread(CommHelper* owner)
        : QThread(owner), mOwner(owner) {}

protected:
    void run() override
    {
        while (!isInterruptionRequested()) {
            for (int ms = 0; ms < 1000 && !isInterruptionRequested(); ms += 100)
                QThread::msleep(100);
            if (!isInterruptionRequested())
                mOwner->flushSpectrumH5BuffersOnWorkerThread();
        }
    }

private:
    CommHelper* mOwner = nullptr;
};

class SpectrumRawFlushThread : public QThread {
public:
    explicit SpectrumRawFlushThread(CommHelper* owner)
        : QThread(owner), mOwner(owner) {}

protected:
    void run() override
    {
        while (!isInterruptionRequested()) {
            for (int ms = 0; ms < 1000 && !isInterruptionRequested(); ms += 100)
                QThread::msleep(100);
            if (!isInterruptionRequested())
                mOwner->flushNetRawBuffersOnWorkerThread(false);
        }
    }

private:
    CommHelper* mOwner = nullptr;
};

CommHelper::CommHelper(QObject *parent)
    : QObject{parent}
{
    //屏蔽libhv日志输出
    hlog_disable();

    mPendingH5ByDetector.resize(DET_NUM + 1);
    mPendingNetRawByDetector.resize(DET_NUM + 1);

    /*初始化网络*/
    initSocket();
    initDataProcessor();

    mSpectrumH5FlushThread = new SpectrumH5FlushThread(this);
    mSpectrumH5FlushThread->start();
    mSpectrumRawFlushThread = new SpectrumRawFlushThread(this);
    mSpectrumRawFlushThread->start();

    connect(this, &CommHelper::settingfinished, this, [=](){        
        //更改了设置，这里需要重新对数据处理器进行关联
        HDF5Settings *settings = HDF5Settings::instance();
        QMap<quint8, DetParameter>& detParameters = settings->detParameters();

        auto it = this->mConnectionPeers.begin();
        while (it != this->mConnectionPeers.end()) {
#if ENABLE_LIBHV
            hv::SocketChannelPtr connection = *it;
            // ip修改可能导致关联的探测器索引也发生改变，这里改为重新分配
            qint8 index =  (qint8)(intptr_t)(connection->context());
            if (index >= 1 && index <= DET_NUM){
                DetParameter& detParameter = detParameters[index];

                QString detectorAddress = QString::fromStdString(detParameter.det_Ip_port);
                QString peerAddress = QString::fromStdString(connection->peerip());
                if (peerAddress != detectorAddress){
                    DetParameter& detParameter = detParameters[index];
                    QMetaObject::invokeMethod(this, "detectorOffline",
                                              Qt::QueuedConnection,
                                              Q_ARG(quint8, index));

                    // 3. 取消数据处理器关联
                    freeDataProcessor(connection);

                    connection->setContext((void*)(intptr_t)index);
                    allocDataProcessor(connection);
                    index =  (qint8)(intptr_t)(connection->context());
                    QMetaObject::invokeMethod(this, "detectorOnline", Qt::QueuedConnection, Q_ARG(quint8, index));
                }
                else{
                    // ip没做更改，只需要更新参数即可
                    mDetectorDataProcessor[index]->updateSetting(&detParameter);
                }
            }
#else
            QTcpSocket* connection = *it;

            // ip修改可能导致关联的探测器索引也发生改变，这里改为重新分配
            if (connection->property("detectorIndex").isValid())
            {
                qint8 index = connection->property("detectorIndex").toInt();
                DetParameter& detParameter = detParameters[index];

                QString detectorAddress = QString::fromStdString(detParameter.det_Ip_port);
                QString peerAddress = connection->peerAddress().toString();
                if (peerAddress != detectorAddress){
                    DetParameter& detParameter = detParameters[index];
                    QMetaObject::invokeMethod(this, "detectorOffline",
                                              Qt::QueuedConnection,
                                              Q_ARG(quint8, index));

                    // 3. 取消数据处理器关联
                    freeDataProcessor(connection);
                    connection->setProperty("detectorIndex", QVariant());
                    allocDataProcessor(connection);
                    index = connection->property("detectorIndex").toInt();
                    QMetaObject::invokeMethod(this, "detectorOnline", Qt::QueuedConnection, Q_ARG(quint8, index));
                }
                else{
                    // ip没做更改，只需要更新参数即可
                    mDetectorDataProcessor[index]->updateSetting(detParameter);
                }
            }
#endif //ENABLE_LIBHV

            ++it;
        }
    });
}

CommHelper::~CommHelper()
{
    if (mSpectrumRawFlushThread) {
        mSpectrumRawFlushThread->requestInterruption();
        mSpectrumRawFlushThread->wait(30000);
    }
    flushNetRawBuffersOnWorkerThread(true);

    if (mSpectrumH5FlushThread) {
        mSpectrumH5FlushThread->requestInterruption();
        mSpectrumH5FlushThread->wait(30000);
    }
    // flushSpectrumH5BuffersOnWorkerThread();

    this->stopServer();

    for (int index = 1; index <= mDetectorDataProcessor.size() + 1; ++index){
        DataProcessor* detectorDataProcessor = mDetectorDataProcessor[index];
        if (detectorDataProcessor)
            detectorDataProcessor->deleteLater();
    }
    mDetectorDataProcessor.clear();
}

#include <winsock2.h>   // WSAIoctl函数定义
#include <mstcpip.h>    // 包含 TCP_KEEPALIVE 结构体定义
void CommHelper::initSocket()
{
#if ENABLE_LIBHV
    mTcpServer = new hv::TcpServer();
    mTcpServer->setThreadNum(32);
    mTcpServer->onConnection = [&](const hv::SocketChannelPtr& channel)
    {
        if (channel->isConnected()){// isOpened()){// ->status == hv::SocketChannel::CONNECTED){
            channel->setKeepaliveTimeout(5000);// 5s没有数据交互就断开

            QString peerAddress = QString::fromStdString(channel->peerip());
            quint16 peerPort = channel->peerport();
            qDebug().nospace() << "新客户端连接，地址：" << peerAddress << ", 端口：" << peerPort;
            QMetaObject::invokeMethod(this, "connectPeerConnection",
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, peerAddress),
                                      Q_ARG(quint16, peerPort));

            {
                QMutexLocker locker(&mPeersMutex);
                this->mConnectionPeers.push_back(channel);
            }

            //给新上线客户端分配数据处理器
            qint8 index = allocDataProcessor(channel);
            if (index < 0)
            {
                //connection->close();
                //delete connection;
                return;
            }

            channel->setContext((void*)(intptr_t)index);
            QMetaObject::invokeMethod(this, "detectorOnline", Qt::QueuedConnection, Q_ARG(quint8, index));
        }
        else if (channel->isClosed()){//status == hv::SocketChannel::CLOSED){
            QString peerAddress = QString::fromStdString(channel->peerip());
            quint16 peerPort = channel->peerport();
            qDebug().nospace() << "客户端断开连接，地址：" << peerAddress << ", 端口：" << peerPort;

            //根据配置解析是哪一路探测器下线了
            qint8 index = indexOfAddress(channel->id());// peerAddress, peerPort);
            if (index >= 0)
            {
                handleDetectorDisconnection(index);
            }
            //移除连接列表
            {
                QMutexLocker locker(&mPeersMutex);
                mConnectionPeers.removeOne(channel);
            }

            // 1. 触发 disconnectPeerConnection 信号
            QMetaObject::invokeMethod(this, "disconnectPeerConnection",
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, peerAddress),
                                      Q_ARG(quint16, peerPort));
        }
        else {
            return;
        }
    };
#else
    this->mTcpServer = new TcpAgentServer();
    connect(this->mTcpServer, &TcpAgentServer::newConnection, this, [=](qintptr socketDescriptor){
        QTcpSocket* connection = new QTcpSocket(this);
        connection->setSocketDescriptor(socketDescriptor);
        connection->setSocketOption(QAbstractSocket::KeepAliveOption, QVariant(true)); // 启用保活

        QString peerAddress = connection->peerAddress().toString();
        quint16 peerPort = connection->peerPort();
        qDebug().nospace() << "新客户端连接，地址：" << peerAddress << ", 端口：" << peerPort;
        connection->setProperty("peerAddress", peerAddress);
        connection->setProperty("peerPort", peerPort);
        connection->setProperty("socketDescriptor", socketDescriptor);
        QMetaObject::invokeMethod(this, "connectPeerConnection",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, peerAddress),
                                  Q_ARG(quint16, peerPort));

        {
            QMutexLocker locker(&mPeersMutex);
            this->mConnectionPeers.push_back(connection);
        }

        //给新上线客户端分配数据处理器
        qint8 index = allocDataProcessor(connection);
        if (index < 0)
        {
            //connection->close();
            //delete connection;
            return;
        }

        // 构造保活参数结构体
        tcp_keepalive keepAlive = {0};
        keepAlive.onoff = TRUE;               // 启用保活
        keepAlive.keepalivetime = 500;      // 空闲1秒后开始探测（单位：ms）
        keepAlive.keepaliveinterval = 100;   // 探测间隔1秒（单位：ms）

        // 调用 WSAIoctl 设置参数
        DWORD bytesReturned = 0;
        int result = WSAIoctl(
            connection->socketDescriptor(),  // QT socket 底层句柄
            SIO_KEEPALIVE_VALS,                // 控制码：设置保活参数
            &keepAlive,                        // 输入：保活参数结构体
            sizeof(keepAlive),                 // 输入大小
            NULL,                              // 输出缓冲区（无需）
            0,                                 // 输出大小
            &bytesReturned,                    // 实际输出字节数
            NULL,                              // 重叠结构（同步调用时为 NULL）
            NULL                               // 完成例程（同步调用时为 NULL）
            );

        if (result == SOCKET_ERROR) {
            qDebug() << "设置保活参数失败，错误码：" << WSAGetLastError();
        }

        connect(connection, &QAbstractSocket::disconnected, this, [=](){
            QString peerAddress = connection->property("peerAddress").toString();
            quint16 peerPort = connection->property("peerPort").toUInt();
            qDebug().nospace() << "客户端断开连接，地址：" << peerAddress << ", 端口：" << peerPort;

            //根据配置解析是哪一路探测器下线了
            qint8 index = indexOfAddress(connection->socketDescriptor());// peerAddress, peerPort);
            if (index >= 0)
            {
                handleDetectorDisconnection(index);
            }
            //移除连接列表
            {
                QMutexLocker locker(&mPeersMutex);
                mConnectionPeers.removeOne(connection);
            }
            connection->deleteLater();

            // 1. 触发 disconnectPeerConnection 信号
            QMetaObject::invokeMethod(this, "disconnectPeerConnection",
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, peerAddress),
                                      Q_ARG(quint16, peerPort));
        });

        connection->setProperty("detectorIndex", index);
        QMetaObject::invokeMethod(this, "detectorOnline", Qt::QueuedConnection, Q_ARG(quint8, index));
    });
#endif //ENABLE_LIBHV
}

void CommHelper::initDataProcessor()
{
    // 这里必须是从0开始，0对应的是时间同步模块，1-32对应探测器索引，这样可以通过索引直接访问对应的处理器
    for (int index = 0; index <= DET_NUM; ++index){
        DataProcessor* processor = new DataProcessor(index, nullptr, this);
        mDetectorDataProcessor[index] = processor;

        // 更新温度数据
        connect(processor, &DataProcessor::reportTemperatureData, this, [=](double temperature){
            emit reportDetectorTemperature(processor->getIndex(), temperature);
        }, Qt::DirectConnection);
        
        //对温度超时报警进行处理
        connect(processor, &DataProcessor::reportTemperatureTimeout, this, 
            &CommHelper::onTemperatureTimeout, Qt::DirectConnection);

        // 能谱数据（解析后的完整能谱数据）
        connect(processor, &DataProcessor::reportFullSpectrum, this, [=](quint8 detID, const FullSpectrum& fullSpectrum){
            emit reportFullSpectrum(detID, fullSpectrum); // 通知界面更新能谱数据

            if (detID < 1 || detID > DET_NUM)
                return;
            H5Spectrum h5Spectrum = FullSpectoH5Spec(fullSpectrum);
            QMutexLocker lock(&mSpectrumH5MutexByDetector[detID]);
            mPendingH5ByDetector[detID].append(h5Spectrum);
        }, Qt::DirectConnection);

        // 波形数据（解析后的数据）
        connect(processor, &DataProcessor::reportWaveCurveInfo, this, [=](quint8 detID, QVector<quint16>& wave){
            emit reportWaveCurve(detID, wave); //通知界面更新波形数据
        }, Qt::QueuedConnection);

        // 粒子数据（解析后的粒子信息）
        connect(processor, &DataProcessor::reportParticleInfo, this, 
            &CommHelper::onParticleInfo, Qt::DirectConnection);

        // 触发信号时间戳(解析后的数据)
        connect(processor, &DataProcessor::reportTriggerTimeInfo, this, [=](quint8 detID, QDateTime triggerTime){
            emit reportTimestampe(triggerTime); //通知界面更新触发时间戳
        }, Qt::DirectConnection);

        // 原始OTA升级数据
        connect(processor, &DataProcessor::reportOTAUpgradeData, this, [=](quint8 detID, const QByteArray& data){
            emit reportOTAUpgradeData(detID, data);
        }, Qt::QueuedConnection);

        // 原始网络数据
        connect(processor, &DataProcessor::reportRawNetData, this, 
            &CommHelper::onRawNetData, Qt::DirectConnection);

        // 能谱数据
        /*connect(processor, &DataProcessor::reportSpectrumData, this,
            &CommHelper::onSpectrumData, Qt::DirectConnection);

        // 波形数据包
        connect(processor, &DataProcessor::reportWaveformData, this, 
            &CommHelper::onWaveformData, Qt::DirectConnection);*/
        
        // 配置参数数据包
        connect(processor, &DataProcessor::reportParamterData, this, [=](quint8 detID, const QByteArray& data){
            // 上报配置参数信息
            emit reportParamterData(detID, data);
        }, Qt::DirectConnection);
    }
}

void CommHelper::flushSpectrumH5BuffersOnWorkerThread(bool forceFlushAll)
{
    if (!mMeasuring && !forceFlushAll)
        return;
    HDF5Settings* settings = HDF5Settings::instance();
    for (int det = 1; det <= DET_NUM; ++det) {
        QVector<H5Spectrum> pending;
        {
            QMutexLocker lock(&mSpectrumH5MutexByDetector[det]);
            mPendingH5ByDetector[det].swap(pending);
        }
        if (pending.isEmpty())
            continue;
       settings->writeH5SpectrumBatch(static_cast<quint8>(det), pending);
    }
}

void CommHelper::flushNetRawBuffersOnWorkerThread(bool forceFlushAll)
{
    if (!mMeasuring && !forceFlushAll)
        return;

    for (int det = 1; det <= DET_NUM; ++det) {
        QByteArray pendingRaw;
        {
            QMutexLocker lock(&mSpectrumRawMutexByDetector[det]);
            mPendingNetRawByDetector[det].swap(pendingRaw);
        }
        if (pendingRaw.isEmpty())
            continue;

        if (!mDetectFileProcessor.contains(det))
            continue;
        QFile* file = mDetectFileProcessor[det];
        if (!file || !file->isOpen())
            continue;

        file->write(pendingRaw.constData(), pendingRaw.size());
        file->flush(); // 后台线程按 10s 周期落盘
    }
}

// 网口原始数据
void CommHelper::onRawNetData(quint8 detID, const QByteArray& data)
{
    return;
    // 主线程汇总测量数据，后台线程每1s批量写入文件
    if (!mMeasuring || detID < 1 || detID > DET_NUM || data.isEmpty())
        return;

    QMutexLocker lock(&mSpectrumRawMutexByDetector[detID]);
    mPendingNetRawByDetector[detID].append(data);
}

// 处理温度数据超时报警
void CommHelper::onTemperatureTimeout(quint8 detID)
{
    emit reportTemperatureTimeout(detID);//上报温度超时报警

    handleDetectorDisconnection(detID);

    //如果该探测器在手动关闭POE供电列表中，则不处理
    if (mManualClosedPOEIDs.contains(detID)){
        return;
    }

    //断电30min后重新打开供电
    stopMeasure(detID);
    //先停止测量
    int stopDelay = 30; //min
    qInfo().nospace() << "谱仪[#" << detID
            << QString("]温度心跳超时报警！停止测量并断电%1min后重新打开供电!").arg(stopDelay);
    //断电
    closeSwitcherPOEPower(detID);
    //定时30min后重新打开供电
    QTimer::singleShot(stopDelay*60*1000, this, [=](){
        if (openSwitcherPOEPower(detID))
            qInfo().nospace() << "谱仪[#" << detID << "]重启供电";
        else
            qInfo().nospace() << "谱仪[#" << detID << "]重启供电失败";
    });
}

// 处理分包后的能谱数据包
void CommHelper::onSpectrumData(quint8 detID, const QByteArray& data)
{
    // 主线程汇总能谱分包，后台线程每10s批量写入文件
    if (!mMeasuring || detID < 1 || detID > DET_NUM || data.isEmpty())
        return;

    QMutexLocker lock(&mSpectrumRawMutexByDetector[detID]);
    mPendingNetRawByDetector[detID].append(data);
}

// 处理分包后的波形数据包
void CommHelper::onWaveformData(quint8 detID, const QByteArray& data)
{
    // 保存数据
    if (mDetectFileProcessor[detID]->isOpen()){
        qint64 bytesWritten = mDetectFileProcessor[detID]->write((const char *)data.constData(), data.size());
        // 检查是否需要flush（每10秒或每1MB）
        checkAndFlushDetectFile(detID, bytesWritten);
    }
}

// 处理解析后的粒子数据信息
void CommHelper::onParticleInfo(quint8 detID, QVector<ParticleData>& particles)
{
    if (mDetectFileProcessor[detID]->isOpen()){
        for (const auto& particle : particles)
        {
            QString line = QString("%1,%2,%3\n")
                            .arg(particle.time.toString("yyyy-MM-dd HH:mm:ss.zzz"))
                            .arg(particle.energy)
                            .arg(particle.typeFlag);
            QByteArray lineData = line.toUtf8();
            qint64 bytesWritten = mDetectFileProcessor[detID]->write((const char *)lineData.constData(), lineData.size());
            checkAndFlushDetectFile(detID,bytesWritten);
        }
    }
    
    // 上报计数
    quint32 num = particles.size();
    emit reportParticle(detID, num);
}

/**
 * 根据谱仪编号找到对应的交换机
 */
QHuaWeiSwitcherHelper *CommHelper::indexOfHuaWeiSwitcher(int index)
{
    for (auto switcherHelper : mHuaWeiSwitcherHelper)
    {
        if (switcherHelper->contains(index))
            return switcherHelper;
    }

    return nullptr;
}


/**
 * 根据谱仪编号找到对应的交换机POE端口号
 */
quint8 CommHelper::indexOfPort(int index)
{
    for (auto switcherHelper : mHuaWeiSwitcherHelper)
    {
        if (switcherHelper->contains(index))
            return switcherHelper->indexOfPort(index);
    }

    return 0x00;
}

#if ENABLE_LIBHV
qint8 CommHelper::allocDataProcessor(const hv::SocketChannelPtr& socket)
{
    mExtendTimeSynModule.reload();
    QString peerAddress = QString::fromStdString(socket->peerip());
    quint32 peerPort = socket->peerport();
    if (peerAddress == mExtendTimeSynModule.ip){
        //时钟同步模块不分配数据处理器
        mDetectorDataProcessor[0]->reallocSocket(socket);
        return 0;
    }

    if (socket->context() != nullptr)
    {
        qint8 index =  (qint8)(intptr_t)(socket->context());// channel->property("detectorIndex").isValid();
        if (index >= 1 && index <= DET_NUM){
            HDF5Settings *settings = HDF5Settings::instance();
            QMap<quint8, DetParameter>& detParameters = settings->detParameters();
            DetParameter& detParameter = detParameters[index];
            mDetectorDataProcessor[index]->reallocSocket(socket, &detParameter);
            socket->setContext((void*)(intptr_t)index);
            return index;
        }
    }

    HDF5Settings *settings = HDF5Settings::instance();
    QMap<quint8, DetParameter>& detParameters = settings->detParameters();

    // IP+PORT匹配
    {
        for (int index = 1; index <= DET_NUM; ++index){
            DetParameter& detParameter = detParameters[index];
            QString detectorAddress = QString::fromStdString(detParameter.det_Ip_port);
            if (detectorAddress == QString("%1:%2").arg(peerAddress).arg(peerPort)){
                mDetectorDataProcessor[index]->reallocSocket(socket, &detParameter);
                socket->setContext((void*)(intptr_t)index);
                return index;
            }
        }
    }

    // IP匹配
    {
        for (int index = 1; index <= DET_NUM; ++index){
            DetParameter& detParameter = detParameters[index];
            QString detectorAddress = QString::fromStdString(detParameter.det_Ip_port);
            if (detectorAddress == peerAddress){
                mDetectorDataProcessor[index]->reallocSocket(socket, &detParameter);
                socket->setContext((void*)(intptr_t)index);
                return index;
            }
        }
    }

    //重新分配空闲的数据处理器
    for (int index = 1; index <= DET_NUM; ++index){
        DetParameter& detParameter = detParameters[index];
        QString detectorAddress = QString::fromStdString(detParameter.det_Ip_port);
        if (mDetectorDataProcessor[index]->isFreeSocket() && detectorAddress==QStringLiteral("0.0.0.0")){
            qstrcpy(detParameter.det_Ip_port, peerAddress.toStdString().c_str());
            settings->sync();
            mDetectorDataProcessor[index]->reallocSocket(socket, &detParameter);
            socket->setContext((void*)(intptr_t)index);
            return index;
        }
    }

    return -1;
}

void CommHelper::freeDataProcessor(const hv::SocketChannelPtr& socket)
{
    quint8 index = indexOfAddress(socket->id());
    if (index <= 0)
        return;

    HDF5Settings *settings = HDF5Settings::instance();
    QMap<quint8, DetParameter>& detParameters = settings->detParameters();
    DetParameter& detParameter = detParameters[index];
    mDetectorDataProcessor[index]->reallocSocket(nullptr);
}

qint8 CommHelper::indexOfAddress(const uint32_t socketDescriptor/*QString peerAddress, quint16 peerPort*/)
{
    for (const auto& iter : this->mConnectionPeers)
    {
        if (iter->id() != socketDescriptor)
            continue;

        return (qint8)(intptr_t)(iter->context());
    }

    return -1;
}
#else
qint8 CommHelper::allocDataProcessor(QTcpSocket *socket)
{
    mExtendTimeSynModule.reload();
    QString peerAddress = socket->peerAddress().toString();
    quint32 peerPort = socket->peerPort();
    if (peerAddress == mExtendTimeSynModule.ip){
        //时钟同步模块不分配数据处理器
        socket->setProperty("isTimeSynModule", true);
        mDetectorDataProcessor[0]->reallocSocket(socket);
        return 0;
    }

    if (socket->property("detectorIndex").isValid())
    {
        qint8 index = socket->property("detectorIndex").toInt();
        if (index >= 1 && index <= DET_NUM){
            HDF5Settings *settings = HDF5Settings::instance();
            QMap<quint8, DetParameter>& detParameters = settings->detParameters();
            DetParameter& detParameter = detParameters[index];
            mDetectorDataProcessor[index]->reallocSocket(socket, detParameter);
            socket->setProperty("detectorIndex", index);
            return index;
        }
    }

    HDF5Settings *settings = HDF5Settings::instance();
    QMap<quint8, DetParameter>& detParameters = settings->detParameters();

    // IP+PORT匹配
    {
        for (int index = 1; index <= DET_NUM; ++index){
            DetParameter& detParameter = detParameters[index];
            QString detectorAddress = QString::fromStdString(detParameter.det_Ip_port);
            if (detectorAddress == QString("%1:%2").arg(peerAddress).arg(peerPort) &&
                mDetectorDataProcessor[index]->isFreeSocket()){
                mDetectorDataProcessor[index]->reallocSocket(socket, detParameter);
                socket->setProperty("detectorIndex", index);
                return index;
            }
        }
    }

    // IP匹配
    {
        for (int index = 1; index <= DET_NUM; ++index){
            DetParameter& detParameter = detParameters[index];
            QString detectorAddress = QString::fromStdString(detParameter.det_Ip_port);
            if (detectorAddress == peerAddress && mDetectorDataProcessor[index]->isFreeSocket()){
                mDetectorDataProcessor[index]->reallocSocket(socket, detParameter);
                socket->setProperty("detectorIndex", index);
                return index;
            }
        }
    }

    //重新分配空闲的数据处理器
    for (int index = 1; index <= DET_NUM; ++index){
        if (mDetectorDataProcessor[index]->isFreeSocket()){
            DetParameter& detParameter = detParameters[index];
            qstrcpy(detParameter.det_Ip_port, peerAddress.toStdString().c_str());
            settings->sync();
            mDetectorDataProcessor[index]->reallocSocket(socket, detParameter);
            socket->setProperty("detectorIndex", index);
            return index;
        }
    }

    return -1;
}

void CommHelper::freeDataProcessor(QTcpSocket *socket)
{
    quint8 index = indexOfAddress(socket->socketDescriptor());
    if (index <= 0)
        return;

    HDF5Settings *settings = HDF5Settings::instance();
    QMap<quint8, DetParameter>& detParameters = settings->detParameters();
    DetParameter& detParameter = detParameters[index];
    mDetectorDataProcessor[index]->reallocSocket(nullptr, detParameter);
}

qint8 CommHelper::indexOfAddress(qintptr socketDescriptor/*QString peerAddress, quint16 peerPort*/)
{
    for (const auto& iter : this->mConnectionPeers)
    {
        if (iter->socketDescriptor() != socketDescriptor)
            continue;

        return iter->property("detectorIndex").toInt();
    }

    return -1;
}
#endif //ENABLE_LIBHV

/*
 打开交换机POE口输出电源
*/
bool CommHelper::openSwitcherPOEPower(quint8 index)
{
    QHuaWeiSwitcherHelper *switcherHelper = indexOfHuaWeiSwitcher(index);
    if (nullptr == switcherHelper)
        return false;

    return switcherHelper->openSwitcherPOEPower(indexOfPort(index));
}

/*
 关闭交换机POE口输出电源
*/
bool CommHelper::closeSwitcherPOEPower(quint8 index)
{
    QHuaWeiSwitcherHelper *switcherHelper = indexOfHuaWeiSwitcher(index);
    if (nullptr == switcherHelper)
        return false;

    return switcherHelper->closeSwitcherPOEPower(indexOfPort(index));
}

/*
     打开交换机POE口输出电源(时钟同步模块)
    */
bool CommHelper::openSwitcherExtendPOEPower()
{
    mExtendTimeSynModule.reload();
    return mHuaWeiSwitcherHelper[mExtendTimeSynModule.switcherIndex-1]->openSwitcherPOEPower(mExtendTimeSynModule.poeIndex);
}

/*
     关闭交换机POE口输出电源(时钟同步模块)
    */
bool CommHelper::closeSwitcherExtendPOEPower()
{
    mExtendTimeSynModule.reload();
    return mHuaWeiSwitcherHelper[mExtendTimeSynModule.switcherIndex-1]->closeSwitcherPOEPower(mExtendTimeSynModule.poeIndex);
}

/////////////////////////////////////////////////////////////////////////////////
/*
 打开网络
*/
#include <QHostInfo>
bool CommHelper::startServer()
{    
    GlobalSettings settings(DEVICE_CONFIG_FILE);
    QString ip = settings.value("Local/ServerIp", "0.0.0.0").toString();

    // 判断网络参数是否合法
    {
        QStringList ipList;
        //获取本机IP地址列表
        //获取主机名称
        QString HostName = QHostInfo::localHostName();
        //根据主机名获取主机IP地址
        QHostInfo hostinfo = QHostInfo::fromName(HostName);
        QList<QHostAddress> AddressList = hostinfo.addresses();
        if(!AddressList.isEmpty())
        {
            for(int i = 0;i<AddressList.size();i++)
            {
                QHostAddress hostaddress = AddressList.at(i);
                if(hostaddress.protocol() == QAbstractSocket::IPv4Protocol)
                {
                    ipList.append(hostaddress.toString());
                }
            }
        }

        ipList.append("127.0.0.1");
        ipList.append("0.0.0.0");
        if (!ipList.contains(ip))
            return false;
    }

#if ENABLE_LIBHV
    initSocket();
    int ret = this->mTcpServer->createsocket(settings.value("Local/ServerPort", 6000).toUInt(), settings.value("Local/ServerIp", "0.0.0.0").toString().toStdString().c_str());
    if (ret >= 0 ){
        this->mTcpServer->start();
        return true;
    }
    else{
        stopServer();
        return false;
    }
#else
    return this->mTcpServer->listen(QHostAddress(settings.value("Local/ServerIp", "0.0.0.0").toString()), settings.value("Local/ServerPort", 6000).toUInt());
#endif //ENABLE_LIBHV
}
/*
 断开网络
*/
void CommHelper::stopServer()
{
#if ENABLE_LIBHV
    if (this->mTcpServer)
    {
        this->mTcpServer->stop();
        delete mTcpServer;
        mTcpServer = nullptr;
    }
#else
    this->mTcpServer->close();
    for (auto it = mConnectionPeers.rbegin(); it != mConnectionPeers.rend(); ++it) {
        QTcpSocket* connection = *it;
        emit connection->disconnected();
    }
#endif //ENABLE_LIBHV
}

bool CommHelper::isOpen()
{
#if ENABLE_LIBHV
    if (this->mTcpServer)
        return this->mTcpServer->listenfd >= 0;
    else
        return false;
#else
    return this->mTcpServer->isListening();
#endif //ENABLE_LIBHV
}

void CommHelper::connectSwitcher(bool query)
{
    for (auto switcherHelper : mHuaWeiSwitcherHelper)
    {
        switcherHelper->logout();
        switcherHelper->deleteLater();
        switcherHelper = nullptr;
    }
    mHuaWeiSwitcherHelper.clear();

    GlobalSettings settings(DEVICE_CONFIG_FILE);
    mHuaWeiSwitcherCount =  settings.value("Switcher/Count", 0).toUInt();
    mHuaWeiSwitcherHelper.reserve(mHuaWeiSwitcherCount);
    for (int i=0; i<mHuaWeiSwitcherCount; ++i){
        QString ip = settings.value(QString("Switcher/%1/ip").arg(i+1), "").toString();
        QString ass = settings.value(QString("Switcher/%1/detector").arg(i+1), "").toString();
        QHuaWeiSwitcherHelper* huaWeiSwitcherHelper = new QHuaWeiSwitcherHelper(ip);
        huaWeiSwitcherHelper->setAssociatedDetector(ass);
        mExtendTimeSynModule.reload();
        if (mExtendTimeSynModule.switcherIndex == (i+1))
            huaWeiSwitcherHelper->setAssociatedDetector(mExtendTimeSynModule.poeIndex);

        connect(huaWeiSwitcherHelper, &QHuaWeiSwitcherHelper::switcherLogged, this, &CommHelper::switcherLogged);
        connect(huaWeiSwitcherHelper, &QHuaWeiSwitcherHelper::switcherConnected, this, &CommHelper::switcherConnected);
        connect(huaWeiSwitcherHelper, &QHuaWeiSwitcherHelper::switcherDisconnected, this, &CommHelper::switcherDisconnected);
        //connect(huaWeiSwitcherHelper, &QHuaWeiSwitcherHelper::reportPoePowerStatus, this, &CommHelper::reportPoePowerStatus);
        connect(huaWeiSwitcherHelper, &QHuaWeiSwitcherHelper::reportPoePowerStatus, this, [=](quint8 index, bool on){
            mExtendTimeSynModule.reload();
            if (huaWeiSwitcherHelper == mHuaWeiSwitcherHelper[mExtendTimeSynModule.switcherIndex-1] && index == mExtendTimeSynModule.poeIndex)
                emit reportPoePowerStatus(0, on); // 时间同步模块特殊处理
            else
                emit reportPoePowerStatus(index, on);
        });

        mHuaWeiSwitcherHelper.push_back(huaWeiSwitcherHelper);
    }

    for (auto switcherHelper : mHuaWeiSwitcherHelper)
    {
        mExtendTimeSynModule.reload();
        if (switcherHelper == mHuaWeiSwitcherHelper[mExtendTimeSynModule.switcherIndex-1])
            switcherHelper->setAssociatedDetector(mExtendTimeSynModule.poeIndex);

        if (query)
            switcherHelper->queryPowerStatus();
        else
            switcherHelper->forceOpenPower();
    }
}

void CommHelper::disconnectSwitcher()
{
    for (auto switcherHelper : mHuaWeiSwitcherHelper)
    {
        switcherHelper->logout();
    }
}
/*
 打开电源
*/
void CommHelper::openPower()
{
    GlobalSettings settings(DEVICE_CONFIG_FILE);
    mHuaWeiSwitcherCount =  settings.value("Switcher/Count", 0).toUInt();
    for (int i=0; i<mHuaWeiSwitcherCount; ++i){
        QString ip = settings.value(QString("Switcher/%1/ip").arg(i+1), "").toString();
        QString ass = settings.value(QString("Switcher/%1/detector").arg(i+1), "").toString();
        for (auto switcherHelper : mHuaWeiSwitcherHelper)
        {
            if (switcherHelper->ip() == ip){
                switcherHelper->setAssociatedDetector(ass);
                // mExtendTimeSynModule.reload();
                // if (mExtendTimeSynModule.switcherIndex == (i+1))
                //     switcherHelper->setAssociatedDetector(mExtendTimeSynModule.poeIndex);
                switcherHelper->openSwitcherPOEPower(0x00);
            }
        }
    }
}
/*
 断开电源
*/
void CommHelper::closePower(bool disconnect)
{
    GlobalSettings settings(DEVICE_CONFIG_FILE);
    mHuaWeiSwitcherCount =  settings.value("Switcher/Count", 0).toUInt();
    for (int i=0; i<mHuaWeiSwitcherCount; ++i){
        QString ip = settings.value(QString("Switcher/%1/ip").arg(i+1), "").toString();
        QString ass = settings.value(QString("Switcher/%1/detector").arg(i+1), "").toString();
        for (auto switcherHelper : mHuaWeiSwitcherHelper)
        {
            if (switcherHelper->ip() == ip){
                switcherHelper->setAssociatedDetector(ass);
                // mExtendTimeSynModule.reload();
                // if (mExtendTimeSynModule.switcherIndex == (i+1))
                //     switcherHelper->setAssociatedDetector(mExtendTimeSynModule.poeIndex);
                switcherHelper->closeSwitcherPOEPower(0x00, disconnect);
            }
        }
    }
}

/*
 设置发次信息
*/
#include <QDir>
void CommHelper::setShotInformation(const QString shotDir, const QString shotNum)
{
    this->mShotDir = shotDir;
    this->mShotNum = shotNum;
}


/**
 * @brief 开始测量
 * @param mode 波形测量/能谱测量
 * @param index index=0时则认为是全部探测器一起开始测量
 */
void CommHelper::startMeasure(QDateTime tmStart, CommandAdapter::MeasureMode measureMode, CommandAdapter::WorkMode workMode, quint8 index/* = 0*/)
{
    mMeasureStartTime = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss");
    mMeasuring = true;
    // 初始化
    {
        mPendingH5ByDetector.resize(DET_NUM + 1);
        for (int i = 1; i <= DET_NUM; ++i) {
            QMutexLocker lock(&mSpectrumH5MutexByDetector[i]);
            mPendingH5ByDetector[i].clear();
        }
    }
    mPendingNetRawByDetector.resize(DET_NUM + 1);
    for (int i = 1; i <= DET_NUM; ++i) {
        QMutexLocker lock(&mSpectrumRawMutexByDetector[i]);
        mPendingNetRawByDetector[i].clear();
    }

    if (index == 0){
        mDataFileList.clear();
        QString filePath = QString("%1/%2/%3.H5").arg(mShotDir).arg(mShotNum).arg(mMeasureStartTime);
        HDF5Settings::instance()->closeH5Spectrum();
        HDF5Settings::instance()->createH5Spectrum(filePath);

        for (index = 1; index <= DET_NUM; ++index){
            if (mDetectFileProcessor.contains(index)){
                if (mDetectFileProcessor[index]->isOpen()){
                    mDetectFileProcessor[index]->close();
                    mDetectFileProcessor[index]->deleteLater();
                    mDetectFileProcessor.remove(index);
                }
            }
            // 清理flush信息
            {
                QMutexLocker flushLocker(&mMutexFileFlush);
                mFileFlushInfo.remove(index);
            }

            DataProcessor* detectorDataProcessor = mDetectorDataProcessor[index];
            if (!detectorDataProcessor->isFreeSocket()){
                //创建有效网络数据文件存储
                if (!mDetectFileProcessor.contains(index)){
                    QString filePath = QString("%1/%2/%3_%4_能谱.dat").arg(mShotDir).arg(mShotNum).arg(mMeasureStartTime).arg(index);
                    if (CommandAdapter::WorkMode::wmWaveform == workMode)
                        filePath = QString("%1/%2/%3_%4_波形.dat").arg(mShotDir).arg(mShotNum).arg(mMeasureStartTime).arg(index);
                    else if (CommandAdapter::WorkMode::wmParticle == workMode)
                        filePath = QString("%1/%2/%3_%4_粒子.dat").arg(mShotDir).arg(mShotNum).arg(mMeasureStartTime).arg(index);

                    mDetectFileProcessor[index] = new QFile(filePath);
                    if (mDetectFileProcessor[index]->open(QIODevice::WriteOnly))
                        mDataFileList.push_back(filePath);

                    // 初始化flush信息
                    {
                        QMutexLocker flushLocker(&mMutexFileFlush);
                        mFileFlushInfo[index] = FileFlushInfo();
                    }

                    QFileInfo info(filePath);
                    QString absolutePath = info.absoluteFilePath();
                    qDebug().nospace() << "谱仪[#"<< index << "]创建存储文件：" << absolutePath;
                }

                if (measureMode == CommandAdapter::MeasureMode::mmManual)
                    detectorDataProcessor->startMeasure(workMode);
                else
                    detectorDataProcessor->startAutoMeasure(tmStart, workMode);
                emit measureStart(index);
            }
        }
    }
    else if (index >= 1 && index <= DET_NUM){
        if (mDetectFileProcessor.contains(index)){
            if (mDetectFileProcessor[index]->isOpen()){
                mDetectFileProcessor[index]->close();
                mDetectFileProcessor[index]->deleteLater();
                mDetectFileProcessor.remove(index);
            }
        }

        // 清理flush信息
        {
            QMutexLocker flushLocker(&mMutexFileFlush);
            mFileFlushInfo.remove(index);
        }

        DataProcessor* detectorDataProcessor = mDetectorDataProcessor[index];
        if (!detectorDataProcessor->isFreeSocket()){
            //创建有效网络数据文件存储
            if (!mDetectFileProcessor.contains(index)){
                QString filePath = QString("%1/%2/%3_%4_能谱.dat").arg(mShotDir).arg(mShotNum).arg(mMeasureStartTime).arg(index);
                if (CommandAdapter::WorkMode::wmWaveform == workMode)
                    filePath = QString("%1/%2/%3_%4_波形.dat").arg(mShotDir).arg(mShotNum).arg(mMeasureStartTime).arg(index);
                else if (CommandAdapter::WorkMode::wmParticle == workMode)
                    filePath = QString("%1/%2/%3_%4_粒子.dat").arg(mShotDir).arg(mShotNum).arg(mMeasureStartTime).arg(index);

                mDetectFileProcessor[index] = new QFile(filePath);
                if (mDetectFileProcessor[index]->open(QIODevice::WriteOnly))
                    mDataFileList.push_back(filePath);

                // 初始化flush信息
                {
                    QMutexLocker flushLocker(&mMutexFileFlush);
                    mFileFlushInfo[index] = FileFlushInfo();
                }

                QFileInfo info(filePath);
                QString absolutePath = info.absoluteFilePath();
                qDebug().nospace() << "谱仪[#"<< index << "]创建存储文件：" << absolutePath;
            }

            if (measureMode == CommandAdapter::MeasureMode::mmManual)
                detectorDataProcessor->startMeasure(workMode);
            else
                detectorDataProcessor->startAutoMeasure(tmStart, workMode);
            emit measureStart(index);
        }
    }
}

/*
 检查和执行文件flush，只针对有效数据文件：能谱数据、波形数据、粒子数据
 每10秒或每1MB数据执行一次flush
 注意：此函数应在mMutexFileFlush锁的保护下调用，确保文件存在且已打开
*/
void CommHelper::checkAndFlushDetectFile(quint8 index, qint64 bytesWritten)
{
    const qint64 FLUSH_SIZE_THRESHOLD = 5*1024 * 1024; // Bytes
    const qint64 FLUSH_TIME_THRESHOLD_MS = 10000; // 10秒
    bool triggeredBySize = false;
    bool triggeredByTime = false;
    bool needFlush = false;
    
    // 快速检查：如果写入的数据很小，且还没有初始化flush信息，可能不需要检查
    // 但这需要在锁内检查，所以我们还是需要加锁
    
    // 使用独立的锁来保护flush信息，尽量缩短锁的持有时间
    {
        QMutexLocker flushLocker(&mMutexFileFlush);
        
        // 初始化或更新flush信息（文件创建时已初始化，这里只是确保存在）
        if (!mFileFlushInfo.contains(index)) {
            mFileFlushInfo[index] = FileFlushInfo();
        }
        
        FileFlushInfo& flushInfo = mFileFlushInfo[index];
        flushInfo.bytesSinceLastFlush += bytesWritten;
        
        // 检查是否达到阈值
        triggeredBySize = (flushInfo.bytesSinceLastFlush >= FLUSH_SIZE_THRESHOLD);
        triggeredByTime = (flushInfo.lastFlushTimer.elapsed() >= FLUSH_TIME_THRESHOLD_MS);
        needFlush = triggeredBySize || triggeredByTime;
        
        if (!needFlush) {
            // 不需要flush，直接返回（锁在作用域结束时自动释放）
            return;
        }
        
        // 需要flush，重置计数器（锁仍持有）
        flushInfo.bytesSinceLastFlush = 0;
        flushInfo.lastFlushTimer.restart();
    } // flushLocker在这里释放锁
    
    //打印线程号
    // qDebug() << "det[" << index << "] checkAndFlushDetectFile thread:" 
    //         << QThread::currentThreadId() << " bytesWritten:" << bytesWritten;
    // 在锁外执行flush操作，避免锁持有时间过长
    // 注意：mDetectorFileProcessor的访问由调用者的mMutexFileFlush锁保护
    if (mDetectFileProcessor.contains(index)) {
        QFile* file = mDetectFileProcessor[index];
        if (file && file->isOpen()) {
            file->flush();
        }
    }
}

/*
 停止测量
*/
void CommHelper::stopMeasure(quint8 index/* = 0*/)
{
    // 停止前先将 H5 缓存落盘，避免累计在内存中
    flushSpectrumH5BuffersOnWorkerThread(true);
    // 停止前先将网口原始数据缓存落盘，避免尾包丢失
    flushNetRawBuffersOnWorkerThread(true);

    //默认情况所有通道直接一次性停止测量
    if (index == 0){
        for (index = 1; index <= DET_NUM; ++index){
            DataProcessor* detectorDataProcessor = mDetectorDataProcessor[index];
            if (!detectorDataProcessor->isFreeSocket()){
                detectorDataProcessor->stopMeasure();
                emit measureStop(index);
            }

            if (mDetectFileProcessor.contains(index)){                
                //延迟关闭文件，确保尾包数据能够正常存储
                if (mDetectFileProcessor.contains(index)){
                    QFile* file = mDetectFileProcessor[index];
                    quint8 detIndex = index; // 保存当前index值，用于lambda捕获
                    if (file && file->isOpen()){
                        //先刷新缓冲区，确保已写入的数据被保存
                        file->flush();
                    }
                    //延迟500ms关闭文件，等待尾包数据写入完成
                    QTimer::singleShot(500, this, [this, detIndex](){
                        if (mDetectFileProcessor.contains(detIndex)){
                            QFile* fileToClose = mDetectFileProcessor[detIndex];
                            if (fileToClose && fileToClose->isOpen()){
                                fileToClose->flush(); //再次刷新，确保尾包数据写入
                                fileToClose->close();
                            }
                            fileToClose->deleteLater();
                            mDetectFileProcessor.remove(detIndex);
                        }
                        // 清理flush信息
                        {
                            QMutexLocker flushLocker(&mMutexFileFlush);
                            mFileFlushInfo.remove(detIndex);
                        }
                    });
                }
            }
        }

        mMeasuring = false;
        HDF5Settings::instance()->closeH5Spectrum();
    }
    else if (index >= 1 && index <= DET_NUM){ //关闭单个通道
        {
            QMutexLocker lock(&mSpectrumRawMutexByDetector[index]);
            if (index < mPendingNetRawByDetector.size())
                mPendingNetRawByDetector[index].clear();
        }
        DataProcessor* detectorDataProcessor = mDetectorDataProcessor[index];
        if (!detectorDataProcessor->isFreeSocket()){
            detectorDataProcessor->stopMeasure();
            emit measureStop(index);
        }

        //延迟关闭文件，确保尾包数据能够正常存储
        if (mDetectFileProcessor.contains(index)){
            QFile* file = mDetectFileProcessor[index];
            quint8 detIndex = index; // 保存当前index值，用于lambda捕获
            if (file && file->isOpen()){
                //先刷新缓冲区，确保已写入的数据被保存
                file->flush();
            }
            //延迟500ms关闭文件，等待尾包数据写入完成
            QTimer::singleShot(500, this, [this, detIndex](){
                if (mDetectFileProcessor.contains(detIndex)){
                    QFile* fileToClose = mDetectFileProcessor[detIndex];
                    if (fileToClose && fileToClose->isOpen()){
                        fileToClose->flush(); //再次刷新，确保尾包数据写入
                        fileToClose->close();
                    }
                    fileToClose->deleteLater();
                    mDetectFileProcessor.remove(detIndex);
                }
                // 清理flush信息
                {
                    QMutexLocker flushLocker(&mMutexFileFlush);
                    mFileFlushInfo.remove(detIndex);
                }
            });
            
            //关闭能谱文件
            if (mDetectFileProcessor.isEmpty()){
                mMeasuring = false;
                HDF5Settings::instance()->closeH5Spectrum();
            }
        }
    }
}


bool CommHelper::isMeasuring() const
{
    return mMeasuring;
}

/*解析历史文件*/
bool CommHelper::openHistoryWaveFile(const QString &filePath)
{
    QFile file(filePath);
    if (file.open(QIODevice::ReadWrite)){
        QVector<quint16> rawWaveData;
        QMap<quint8, QVector<quint16>> realCurve;// 4路通道实测曲线数据
        if (filePath.endsWith(".dat")){                        
            rawWaveData.resize(512);
            for (int i=1; i<=11; ++i){
                int rSize = file.read((char *)rawWaveData.data(), rawWaveData.size() * sizeof(quint16));
                if (rSize == 1024){
                    realCurve[i] = rawWaveData;
                }
            }
        }
        else{
            int chIndex = 1;
            while (!file.atEnd()){
                QByteArray lines = file.readLine();
                lines = lines.replace("\r\n", "");
                QList<QByteArray> listLine = lines.split(',');
                for( auto line : listLine){
                    rawWaveData.push_back(qRound(line.toDouble() * 0.8));
                }

            }
        }

        file.close();
        return true;
    }

    return false;
}


bool copyDir(const QString &src, const QString &dst, bool overwrite = true) {
    QDir srcDir(src);
    if (!srcDir.exists()) return false;

    QDir dstDir(dst);
    if (!dstDir.exists() && !dstDir.mkpath(".")) return false;

    foreach (QFileInfo info, srcDir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs)) {
        QString dstPath = dst + QDir::separator() + info.fileName();
        if (info.isDir()) {
            if (!copyDir(info.filePath(), dstPath, overwrite)) return false;
        } else {
            if (overwrite && QFile::exists(dstPath)) QFile::remove(dstPath);
            if (!QFile::copy(info.filePath(), dstPath)) return false;
        }
    }
    return true;
}

bool CommHelper::saveAs(QString dstPath)
{
    QString srcPath = QString("%1/%2").arg(mShotDir).arg(mShotNum);
    return copyDir(srcPath, dstPath);
}

bool CommHelper::exportFile(QString dstPath)
{
    if (mDataFileList.size() == 0)
        return false;

    for (auto srcFilePath : mDataFileList)
    {
        QFileInfo fileInfo(srcFilePath);
        QString dstFilePath = QString("%1/%2").arg(dstPath).arg(fileInfo.fileName());

        QFile::copy(srcFilePath, dstFilePath);
    }

    return true;
}


void CommHelper::handleDetectorDisconnection(quint8 index)
{
    QMutexLocker locker(&mPeersMutex);
    
    // 在 mConnectionPeers 中查找对应的连接
    auto it = mConnectionPeers.begin();
    while (it != mConnectionPeers.end()) {
#if ENABLE_LIBHV
        hv::SocketChannelPtr connection = *it;
        quint8 detectorIndex = (quint8)(intptr_t)connection->context();
#else
        QTcpSocket* connection = *it;
        quint8 detectorIndex = connection->property("detectorIndex").toUInt();
#endif //ENABLE_LIBHV
        if (index != detectorIndex) {
            ++it;
            continue;
        }

        // 2. 触发 detectorOffline 信号
        QMetaObject::invokeMethod(this, "detectorOffline",
            Qt::QueuedConnection,
            Q_ARG(quint8, index));

        // 3. 取消数据处理器关联
        freeDataProcessor(connection);

        qInfo().nospace() << "谱仪[#" << index << "]下线，已执行断开连接处理";
        return; // 找到并处理完成，退出
    }
    
    // 如果没有找到对应的连接，仍然触发 offline 信号（可能连接已经断开）
    qWarning().nospace() << "谱仪[#" << index << "]下线，但未找到对应的TCP连接";
    QMetaObject::invokeMethod(this, "detectorOffline", 
        Qt::QueuedConnection, 
        Q_ARG(quint8, index));
}

bool CommHelper::readParamters(quint8 detIndex)
{
    DataProcessor* detectorDataProcessor = mDetectorDataProcessor[detIndex];
    if (!detectorDataProcessor->isFreeSocket()){
        DetParameter detParameter;
        detectorDataProcessor->updateSetting(&detParameter, true);
        return true;
    }
    else{
        return false;
    }
}

void CommHelper::startOTAUpgrade(quint8 index)
{
    mDetectorDataProcessor[index]->startOTAUpgrade();
}

void CommHelper::endOTAUpgrade(quint8 index)
{
    mDetectorDataProcessor[index]->endOTAUpgrade();
}

bool CommHelper::sendOTAUpgradeData(quint8 index, const QByteArray& data)
{
    if (index == 0)
        return mDetectorDataProcessor[mExtendTimeSynModule.switcherIndex-1]->sendOTAUpgradeData(data);
    else
        return mDetectorDataProcessor[index]->sendOTAUpgradeData(data);
}
