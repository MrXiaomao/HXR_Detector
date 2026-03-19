#include "commhelper.h"
#include "globalsettings.h"

#include <QTimer>
#include <QDataStream>
#include <QNetworkSession>
#include <QNetworkConfigurationManager>

CommHelper::CommHelper(QObject *parent)
    : QObject{parent}
{
    /*初始化网络*/
    initSocket();
    initDataProcessor();

    connect(this, &CommHelper::settingfinished, this, [=](){        
        //更改了设置，这里需要重新对数据处理器进行关联
        HDF5Settings *settings = HDF5Settings::instance();
        QMap<quint8, DetParameter>& detParameters = settings->detParameters();

        auto it = this->mConnectionPeers.begin();
        while (it != this->mConnectionPeers.end()) {
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

                    //qInfo().nospace() << "谱仪[#" << index << "]下线，已执行断开连接处理";
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

            ++it;
        }
    });
}

CommHelper::~CommHelper()
{
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
}

void CommHelper::initDataProcessor()
{
    for (int index = 0; index <= DET_NUM; ++index){
        DataProcessor* detectorDataProcessor = new DataProcessor(index, nullptr, this);
        mDetectorDataProcessor[index] = detectorDataProcessor;

        // 更新温度数据
        connect(detectorDataProcessor, &DataProcessor::reportTemperatureData,
        this, [=](double temperature){
            DataProcessor* processor = qobject_cast<DataProcessor*>(sender());
            emit reportDetectorTemperature(processor->index(), temperature);
        });
        
        //对温度超时报警进行处理
        connect(detectorDataProcessor, &DataProcessor::reportTemperatureTimeout, this, [=](){
            DataProcessor* processor = qobject_cast<DataProcessor*>(sender());
            quint8 detID = processor->index();
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
        });

        // 能谱数据
        connect(detectorDataProcessor, &DataProcessor::reportSpectrumData, this, [=](QByteArray& data){
            DataProcessor* processor = qobject_cast<DataProcessor*>(sender());
            {
                if (!mDetectorFileProcessor.contains(processor->index())){
                    QString filePath = QString("%1/%2/%3_%4_能谱.dat").arg(mShotDir).arg(mShotNum).arg(mMeasureStartTime).arg(processor->index());
                    mDetectorFileProcessor[processor->index()] = new QFile(filePath);
                    mDetectorFileProcessor[processor->index()]->open(QIODevice::WriteOnly);
                    mDataFileList.push_back(filePath);

                    // 初始化flush信息
                    {
                        QMutexLocker flushLocker(&mMutexFileFlush);
                        mFileFlushInfo[processor->index()] = FileFlushInfo();
                    }

                    QFileInfo info(filePath);
                    QString absolutePath = info.absoluteFilePath();
                    qInfo().nospace() << "谱仪[#"<< processor->index() << "]创建存储文件：" << absolutePath;

                    filePath = QString("%1/%2/%3.H5").arg(mShotDir).arg(mShotNum).arg(mMeasureStartTime);
                    HDF5Settings::instance()->createH5Spectrum(filePath);
                }

                if (mDetectorFileProcessor[processor->index()]->isOpen()){
                    qint64 bytesWritten = mDetectorFileProcessor[processor->index()]->write((const char *)data.constData(), data.size());
                    // 检查是否需要flush（每10秒或每1MB）
                    checkAndFlushFile(processor->index(), bytesWritten);
                }
            }
            // 数据解包
            detectorDataProcessor->inputSpectrumData(processor->index(), data);
        });

        connect(detectorDataProcessor, &DataProcessor::reportFullSpectrum,
                this, [=](quint8 index, const FullSpectrum& fullSpectrum){
                    emit reportFullSpectrum(index, fullSpectrum);
                });


        // 原始网络数据
        connect(detectorDataProcessor, &DataProcessor::reportOTAUpgradeData, this, [=](const QByteArray& data){
            DataProcessor* processor = qobject_cast<DataProcessor*>(sender());
            quint8 detId = processor->index();
            emit reportOTAUpgradeData(detId, data);
        });

        connect(detectorDataProcessor, &DataProcessor::reportRawNetData, this, [=](const QByteArray& data){
            DataProcessor* processor = qobject_cast<DataProcessor*>(sender());
            /*
                保存数据
            */
            quint8 detId = processor->index();
            {
                if (mNetRawFileProcessor.contains(processor->index()) && mNetRawFileProcessor[processor->index()]->isOpen()){
                    qint64 bytesWritten = mNetRawFileProcessor[processor->index()]->write((const char *)data.constData(), data.size());
                    // 实时flush，确保数据都存下来了
                    mNetRawFileProcessor[processor->index()]->flush();
                }
            }
        });

        //波形数据
        connect(detectorDataProcessor, &DataProcessor::reportWaveformData, this, [=](QByteArray& data){
            DataProcessor* processor = qobject_cast<DataProcessor*>(sender());
            /*
                保存数据
            */
            quint8 detId = processor->index();
            {
                // QMutexLocker locker(&mMutexTriggerTime);
                // if (mTriggerTime.isEmpty()){
                //     mTriggerTime = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss");
                // }

                if (!mDetectorFileProcessor.contains(processor->index())){
                    QString filePath = QString("%1/%2/%3_%4_波形.dat").arg(mShotDir).arg(mShotNum).arg(mMeasureStartTime).arg(processor->index());
                    mDetectorFileProcessor[processor->index()] = new QFile(filePath);
                    mDetectorFileProcessor[processor->index()]->open(QIODevice::WriteOnly | QIODevice::Append); //覆盖式写入
                    mDataFileList.push_back(filePath);

                    // 初始化flush信息
                    {
                        QMutexLocker flushLocker(&mMutexFileFlush);
                        mFileFlushInfo[processor->index()] = FileFlushInfo();
                    }

                    QFileInfo info(filePath);
                    QString absolutePath = info.absoluteFilePath();
                    qInfo().nospace() << "谱仪[#"<< processor->index() << "]创建存储文件：" << absolutePath;
                }

                if (mDetectorFileProcessor[processor->index()]->isOpen()){
                    qint64 bytesWritten = mDetectorFileProcessor[processor->index()]->write((const char *)data.constData(), data.size());
                    // 检查是否需要flush（每10秒或每1MB）
                    checkAndFlushFile(processor->index(), bytesWritten);
                }
            }

            data.remove(0, 6);//移除包头
            data.chop(8);//移除包尾

            //从HDF5加载配置信息
            HDF5Settings *settings = HDF5Settings::instance();
            QMap<quint8, DetParameter>& detParameters = settings->detParameters();
            DetParameter& detParameter = detParameters[detId];
            quint32 waveLen = detParameter.waveformLength;

            //暂时只取第一个波形出来
            {
                // 数据解包
                QVector<quint16> waveform;
                waveform.reserve(waveLen-2);//减去包头包尾两个数据
                for (int i=2; i<(waveLen-1)*2; i+=2){//跨过包头两个字节
                    bool ok;
                    quint16 amplitude = data.mid(i, 2).toHex().toUShort(&ok, 16);
                    waveform.append(amplitude);
                }

                emit reportWaveformCurveData(processor->index(), waveform);
            }
        });

        //粒子数据
        connect(detectorDataProcessor, &DataProcessor::reportParticleData, this, [=](QByteArray& data){
            DataProcessor* processor = qobject_cast<DataProcessor*>(sender());

            /*
                保存粒子数据
            */
            quint8 detId = processor->index();
            {
                // QMutexLocker locker(&mMutexTriggerTime);
                // if (mTriggerTime.isEmpty()){
                //     mTriggerTime = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss");
                // }

                if (!mDetectorFileProcessor.contains(processor->index())){
                    QString filePath = QString("%1/%2/%3_%4_粒子.txt").arg(mShotDir).arg(mShotNum).arg(mMeasureStartTime).arg(processor->index());
                    mDetectorFileProcessor[processor->index()] = new QFile(filePath);
                    mDetectorFileProcessor[processor->index()]->open(QIODevice::WriteOnly | QIODevice::Append); //覆盖式写入
                    mDataFileList.push_back(filePath);
                    // 写入表头
                    QString line = QString("测量时长(s),时间,ADC采样幅度,粒子类型\n");
                    qint64 bytesWritten = mDetectorFileProcessor[processor->index()]->write((const char *)line.toUtf8());
                    // 检查是否需要flush（每10秒或每1MB）

                    // 初始化flush信息
                    {
                        QMutexLocker flushLocker(&mMutexFileFlush);
                        mFileFlushInfo[processor->index()] = FileFlushInfo();
                    }

                    QFileInfo info(filePath);
                    QString absolutePath = info.absoluteFilePath();
                    qInfo().nospace() << "谱仪[#"<< processor->index() << "]创建存储文件：" << absolutePath;
                }
            }

            data.remove(0, 6);//移除包头
            data.chop(8);//移除包尾

            // 解析数据
            bool ok;
            quint32 sequence = data.left(4).toHex().toUInt(&ok, 16); // 包序号
            data.remove(0, 4);//移除序号

            for (int i=0; i<130; ++i)
            {
                qDebug() << data.left(8).toHex(' ');
                bool ok;
                quint16 utc = data.mid(i*8, 2).toHex().toUInt(&ok, 16); // utc时间戳
                quint16 msecond = data.mid(i*8+2, 2).toHex().toUInt(&ok, 16)*1000/65536; // 毫秒
                quint16 deathT = data.mid(i*8+4, 2).toHex().toUShort(&ok, 16); // 死时间
                quint16 _type = data.mid(i*8+6, 2).toHex().toUShort(&ok, 16); // 粒子类型
                quint16 typeFlag = (_type & 0x8000) >> 15;// 类型 （0：γ；1：α）
                quint16 amplitude = _type & 0x7FFF; // 幅度

                // 保存时间、能量、粒子类型
                // 读取系统当前UTC时间戳（从1900年1月1日00：00：00开始计算，表示至今的整数秒数）
                QDateTime epoch(QDate(1900, 1, 1), QTime(0, 0, 0), Qt::UTC);
                quint64 tmUtc = epoch.secsTo(QDateTime::currentDateTimeUtc()); // UTC时间戳（1900）
                // 获取UTC时间高16bit
                tmUtc = tmUtc & 0xffffffffffff0000;
                // 将新的高16位加上去
                tmUtc = tmUtc | utc; // 合成新的utc时间戳
                // UTC1900转UTC1970-Unix时间戳，也就是从1970年1月1日00：00：00开始计算的整数秒数
                quint64 tmUnix = tmUtc - 2208988800; // UTC时间戳（1970）-unix时间戳
                QDateTime tm = QDateTime::fromMSecsSinceEpoch(tmUnix*1000 + msecond);

                if (mDetectorFileProcessor[processor->index()]->isOpen()){
                    QString line = QString("%1,%2,%3,%4\n")
                                    .arg(sequence)
                                    .arg(tm.toString("yyyy-MM-dd HH:mm:ss.zzz"))
                                    .arg(amplitude)
                                    .arg(typeFlag);
                    QByteArray lineData = line.toUtf8();
                    qint64 bytesWritten = mDetectorFileProcessor[processor->index()]->write((const char *)lineData.constData(), lineData.size());
                }
            }

            mDetectorFileProcessor[processor->index()]->flush();

            // 上报计数
            emit reportParticleCurveData(index, sequence);
        });

        connect(detectorDataProcessor, &DataProcessor::reportTimestampData, this, [=](QByteArray& data){
            DataProcessor* processor = qobject_cast<DataProcessor*>(sender());
            bool ok;
            quint32 utc = data.mid(6, 4).toHex().toUInt(&ok, 16); // utc时间戳
            quint32 msecond = data.mid(10, 4).toHex().toUInt(&ok, 16)*1000/65536; // 毫秒

            // UTC转Unix时间戳，也就是从1970年1月1日00：00：00开始计算的整数秒数
            quint64 tmUnix = utc - 2208988800; // UTC时间戳（1970）-unix时间戳
            QDateTime tm = QDateTime::fromMSecsSinceEpoch(tmUnix*1000 + msecond);

            // 上报时间戳信息
            emit reportTimestampe(index, tm);
        });

        connect(detectorDataProcessor, &DataProcessor::reportParamterData, this, [=](QByteArray& data){
            DataProcessor* processor = qobject_cast<DataProcessor*>(sender());

            // 上报配置参数信息
            emit reportParamterData(index, data);
        });
    }
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

    return this->mTcpServer->listen(QHostAddress(settings.value("Local/ServerIp", "0.0.0.0").toString()), settings.value("Local/ServerPort", 6000).toUInt());
}
/*
 断开网络
*/
void CommHelper::stopServer()
{
    this->mTcpServer->close();
    for (auto it = mConnectionPeers.rbegin(); it != mConnectionPeers.rend(); ++it) {
        QTcpSocket* connection = *it;
        emit connection->disconnected();
    }


    // for (auto connection : mConnectionPeers){
    //     emit connection->disconnected();

    //     // quint8 index = connection->property("detectorIndex").toInt();
    //     // handleDetectorDisconnection(index);
    //     //connection->close();
    //     //connection->deleteLater();
    //     //connection = nullptr;
    // }
    //mConnectionPeers.clear();

    //QThread::msleep(1000);//睡眠等待进入析构睡眠
    //this->mTcpServer->close();
}

bool CommHelper::isOpen()
{
    return this->mTcpServer->isListening();
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
                mExtendTimeSynModule.reload();
                if (mExtendTimeSynModule.switcherIndex == (i+1))
                    switcherHelper->setAssociatedDetector(mExtendTimeSynModule.poeIndex);
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
                mExtendTimeSynModule.reload();
                if (mExtendTimeSynModule.switcherIndex == (i+1))
                    switcherHelper->setAssociatedDetector(mExtendTimeSynModule.poeIndex);
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
    mWaveAllData.clear();
    mMeasureStartTime.clear();
    mDataFileList.clear();

    mMeasureStartTime = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss");

    if (index == 0){
        for (index = 1; index <= DET_NUM; ++index){
            if (mDetectorFileProcessor.contains(index)){
                if (mDetectorFileProcessor[index]->isOpen()){
                    mDetectorFileProcessor[index]->close();
                    mDetectorFileProcessor[index]->deleteLater();
                    mDetectorFileProcessor.remove(index);
                }
            }
            // 清理flush信息
            {
                QMutexLocker flushLocker(&mMutexFileFlush);
                mFileFlushInfo.remove(index);
            }

            DataProcessor* detectorDataProcessor = mDetectorDataProcessor[index];
            if (!detectorDataProcessor->isFreeSocket()){
                //初始化网络数据文件存储
                if (!mNetRawFileProcessor.contains(index)){
                    QString filePath = QString("%1/%2/%3_%4_Net.dat").arg(mShotDir).arg(mShotNum).arg(mMeasureStartTime).arg(index);
                    mNetRawFileProcessor[index] = new QFile(filePath);
                    mNetRawFileProcessor[index]->open(QIODevice::WriteOnly | QIODevice::Append); //覆盖式写入
                    mDataFileList.push_back(filePath);

                    // 初始化flush信息
                    {
                        QMutexLocker flushLocker(&mMutexFileFlush);
                        mFileFlushInfo[index] = FileFlushInfo();
                    }

                    QFileInfo info(filePath);
                    QString absolutePath = info.absoluteFilePath();
                    qInfo().nospace() << "谱仪[#"<< index << "]创建存储文件：" << absolutePath;
                }
                

                if (measureMode == CommandAdapter::MeasureMode::mmManual)
                    detectorDataProcessor->startMeasure(workMode);
                else
                    detectorDataProcessor->startAutoMeasure(tmStart, workMode);
                emit measureStart(index);
            }
        }

        HDF5Settings::instance()->closeH5Spectrum();
    }
    else if (index >= 1 && index <= DET_NUM){
        if (mDetectorFileProcessor.contains(index)){
            if (mDetectorFileProcessor[index]->isOpen()){
                mDetectorFileProcessor[index]->close();
                mDetectorFileProcessor[index]->deleteLater();
                mDetectorFileProcessor.remove(index);
            }
        }

        // 清理flush信息
        {
            QMutexLocker flushLocker(&mMutexFileFlush);
            mFileFlushInfo.remove(index);
        }

        DataProcessor* detectorDataProcessor = mDetectorDataProcessor[index];
        if (!detectorDataProcessor->isFreeSocket()){
            if (measureMode == CommandAdapter::MeasureMode::mmManual)
                detectorDataProcessor->startMeasure(workMode);
            else
                detectorDataProcessor->startAutoMeasure(tmStart, workMode);
            emit measureStart(index);
        }
    }
}

/*
 检查和执行文件flush
 每10秒或每1MB数据执行一次flush
 注意：此函数应在mMutexTriggerTimer锁的保护下调用，确保文件存在且已打开
*/
void CommHelper::checkAndFlushFile(quint8 index, qint64 bytesWritten)
{
    const qint64 FLUSH_SIZE_THRESHOLD = 5*1024 * 1024; // Bites
    const qint64 FLUSH_TIME_THRESHOLD_MS = 60*1000; // 毫秒
    
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
        bool needFlush = (flushInfo.bytesSinceLastFlush >= FLUSH_SIZE_THRESHOLD) ||
                         (flushInfo.lastFlushTimer.elapsed() >= FLUSH_TIME_THRESHOLD_MS);
        
        if (!needFlush) {
            // 不需要flush，直接返回（锁在作用域结束时自动释放）
            return;
        }
        
        // 需要flush，重置计数器（锁仍持有）
        flushInfo.bytesSinceLastFlush = 0;
        flushInfo.lastFlushTimer.restart();
    } // flushLocker在这里释放锁
    
    // 在锁外执行flush操作，避免锁持有时间过长
    // 注意：mDetectorFileProcessor的访问由调用者的mMutexTriggerTimer锁保护
    if (mDetectorFileProcessor.contains(index)) {
        QFile* file = mDetectorFileProcessor[index];
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
    //默认情况所有通道直接一次性停止测量
    if (index == 0){
        for (index = 1; index <= DET_NUM; ++index){
            DataProcessor* detectorDataProcessor = mDetectorDataProcessor[index];
            if (!detectorDataProcessor->isFreeSocket()){
                detectorDataProcessor->stopMeasure();
                emit measureStop(index);
            }

            if (mDetectorFileProcessor.contains(index)){
                
                //延迟关闭文件，确保尾包数据能够正常存储
                if (mDetectorFileProcessor.contains(index)){
                    QFile* file = mDetectorFileProcessor[index];
                    quint8 detIndex = index; // 保存当前index值，用于lambda捕获
                    if (file && file->isOpen()){
                        //先刷新缓冲区，确保已写入的数据被保存
                        file->flush();
                    }
                    //延迟500ms关闭文件，等待尾包数据写入完成
                    QTimer::singleShot(500, this, [this, detIndex](){
                        if (mDetectorFileProcessor.contains(detIndex)){
                            QFile* fileToClose = mDetectorFileProcessor[detIndex];
                            if (fileToClose && fileToClose->isOpen()){
                                fileToClose->flush(); //再次刷新，确保尾包数据写入
                                fileToClose->close();
                            }
                            fileToClose->deleteLater();
                            mDetectorFileProcessor.remove(detIndex);
                        }
                        // 清理flush信息
                        {
                            QMutexLocker flushLocker(&mMutexFileFlush);
                            mFileFlushInfo.remove(detIndex);
                        }
                    });
                }
            }

            //关闭网络存储文件
            if (mNetRawFileProcessor.contains(index)){
                QFile* file = mNetRawFileProcessor[index];
                file->flush();
                file->close();
                file->deleteLater();
                mNetRawFileProcessor.remove(index);
            }
        }

        HDF5Settings::instance()->closeH5Spectrum();
    }
    else if (index >= 1 && index <= DET_NUM){ //关闭单个通道
        DataProcessor* detectorDataProcessor = mDetectorDataProcessor[index];
        if (!detectorDataProcessor->isFreeSocket()){
            detectorDataProcessor->stopMeasure();
            emit measureStop(index);
        }

        //延迟关闭文件，确保尾包数据能够正常存储
        if (mDetectorFileProcessor.contains(index)){
            QFile* file = mDetectorFileProcessor[index];
            quint8 detIndex = index; // 保存当前index值，用于lambda捕获
            if (file && file->isOpen()){
                //先刷新缓冲区，确保已写入的数据被保存
                file->flush();
            }
            //延迟500ms关闭文件，等待尾包数据写入完成
            QTimer::singleShot(500, this, [this, detIndex](){
                if (mDetectorFileProcessor.contains(detIndex)){
                    QFile* fileToClose = mDetectorFileProcessor[detIndex];
                    if (fileToClose && fileToClose->isOpen()){
                        fileToClose->flush(); //再次刷新，确保尾包数据写入
                        fileToClose->close();
                    }
                    fileToClose->deleteLater();
                    mDetectorFileProcessor.remove(detIndex);
                }
                // 清理flush信息
                {
                    QMutexLocker flushLocker(&mMutexFileFlush);
                    mFileFlushInfo.remove(detIndex);
                }
            });

            //关闭网络存储文件
            if (mNetRawFileProcessor.contains(index)){
                QFile* file = mNetRawFileProcessor[index];
                file->flush();
                file->close();
                file->deleteLater();
                mNetRawFileProcessor.remove(index);
            }

            //关闭能谱文件
            if (mNetRawFileProcessor.isEmpty()){
                HDF5Settings::instance()->closeH5Spectrum();
            }
        }
    }
}


/*解析历史文件*/
bool CommHelper::openHistoryWaveFile(const QString &filePath)
{
    QFile file(filePath);
    if (file.open(QIODevice::ReadWrite)){
        mWaveAllData.clear();

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


void CommHelper::handleDetectorDisconnection(quint8 index)
{
    QMutexLocker locker(&mPeersMutex);
    
    // 在 mConnectionPeers 中查找对应的连接
    auto it = mConnectionPeers.begin();
    while (it != mConnectionPeers.end()) {
        QTcpSocket* connection = *it;
        quint8 detectorIndex = connection->property("detectorIndex").toUInt();
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
        detectorDataProcessor->updateSetting(detParameter, true);
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
