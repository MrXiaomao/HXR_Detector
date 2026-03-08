#include "dataprocessor.h"
#include <QDebug>
#include <QTimer>

DataProcessor::DataProcessor(quint8 index, QTcpSocket* socket, QObject *parent)
    : CommandAdapter(parent)
    , mIndex(index)
{
    m_parseData = nullptr;
    mAccumulateSpec.resize(8192);
    mDataProcessThread = new QLiteThread(this);
    mDataProcessThread->setWorkThreadProc([=](){
        while (!mTerminatedDataThread)
        {
            {
                QMutexLocker locker(&mDataLocker);
                if (mRawData.size() == 0){
                    while (!mDataReady){
                        mDataCondition.wait(&mDataLocker);
                    }

                    mCachePool.append(mRawData);
                    mRawData.clear();
                    mDataReady = false;
                }
                else{
                    mCachePool.append(mRawData);
                    mRawData.clear();
                }
            }

            if (!mTerminatedDataThread)
                analyzeCommands(mCachePool);
        }
    });
    mDataProcessThread->start();
    connect(this, &DataProcessor::destroyed, [=]() {
        mDataProcessThread->exit(0);
        mDataProcessThread->wait(500);
    });

    if (mTcpSocket)
        connect(mTcpSocket, SIGNAL(readyRead()), this, SLOT(readyRead()));

    mTempTimeoutTimer.setSingleShot(true);
    connect(&mTempTimeoutTimer, &QTimer::timeout, this, [this](){
        qWarning().noquote() << tr("超过%1秒未收到温度数据，触发报警！").arg(mTempTimeoutTimer.interval() / 1000);
        reportTemperatureTimeout();
    });

    // 温度刷新，重启心跳定时器
    connect(this, &DataProcessor::reportTemperatureTimeout, this, [this](){
        restartTempTimeout();
    });
}


DataProcessor::~DataProcessor()
{
    if (mDataProcessThread){
        // 终止线程
        mTerminatedDataThread = true;
        mDataReady = true;
        mDataCondition.wakeAll();
        mDataProcessThread->wait();
    }

    // 析构时确保释放资源
    if (m_parseData) {
        delete m_parseData;
        m_parseData = nullptr;
    }
}

void DataProcessor::reallocIndex(quint8 index)
{
    mIndex = index;
}

void DataProcessor::reallocSocket(QTcpSocket *tcpSocket)
{
    if (mTcpSocket){
        disconnect(mTcpSocket, SIGNAL(readyRead()), this, nullptr);
    }

    mTcpSocket = tcpSocket;
    if (mTcpSocket){
        connect(mTcpSocket, SIGNAL(readyRead()), this, SLOT(readyRead()));
    }
}

void DataProcessor::reallocSocket(QTcpSocket *tcpSocket, DetParameter& detParameter)
{
    if (mTcpSocket){
        disconnect(mTcpSocket, SIGNAL(readyRead()), this, nullptr);
    }

    mTcpSocket = tcpSocket;
    if (mTcpSocket){
        connect(mTcpSocket, SIGNAL(readyRead()), this, SLOT(readyRead()));

        updateSetting(detParameter);
    }
}

void DataProcessor::updateSetting(DetParameter& detParameter, bool isRead/* = false*/)
{
    // 设置基本参数
    {
        //清空指令
        this->clear();

        //#01 增益=1|0|1|1234000FFA1000000000ABCD
        this->sendGain(isRead, detParameter.gain);
        //#02 死时间=1|0|3|1234000FFA1100000000ABCD
        this->sendDeathTime(isRead, detParameter.deathTime);
        //#03 触发阈值=1|0|5|1234000FFA1200000000ABCD
        this->sendTriggerThold(isRead, detParameter.triggerThold);
        //#04 触发模式=1|0|7|1234000FFC1000000003ABCD
        this->sendWaveformMode(isRead, (TriggerMode)detParameter.waveformTriggerMode, detParameter.waveformLength);
        //#05 能谱刷新时间=1|0|9|1234000FFD1000000000ABCD
        this->sendSprectnumRefreshTimelength(isRead, detParameter.spectrumRefreshTime);

        if (detParameter.trapShapeEnable || isRead){
            //#07 梯形成型常数=1|0|13|1234000FFE1000000000ABCD
            this->sendTrapTimeConst(isRead, detParameter.trapShapeTimeConstD1, detParameter.trapShapeTimeConstD2);
            //#08 上升沿参数=1|0|15|1234000FFE1100000000ABCD
            this->sendRisePeakFallPoints(isRead, detParameter.trapShapeRisePoint, detParameter.trapShapePeakPoint, detParameter.trapShapeFallPoint);
        }

        //#09 梯形成型使能=1|0|17|1234000FFE1200000000ABCD
        this->sendTrapShapeEnable(isRead, (TrapShapeEnable)detParameter.trapShapeEnable);
        
        //#10 高压输出电平=1|0|21|1234000FF91100000000ABCD,暂时不再使用，后续直接FPGA程序内部写死，不再界面可调。
        // if (detParameter.highVoltageEnable || isRead)
        //     this->sendHighVolgateOutLevel(isRead, detParameter.highVoltageOutLevel);

        //#11 高压电平使能=1|0|19|1234000FF91000000000ABCD
        this->sendHighVolgateOutLevelEnable(isRead, (HighVolgateOutLevelEnable)detParameter.highVoltageEnable);

        //通知发送指令
        this->notifySendNextCmd();
    }
}

bool DataProcessor::isFreeSocket()
{
    return mTcpSocket == nullptr;
}

void DataProcessor::startMeasure(CommandAdapter::WorkMode workMode)
{
    /*开始测量之前清空所有数据，防止野数据存在*/
    mCachePool.clear();
    mAccumulateSpec.clear();
    mAccumulateSpec.resize(8192);
    mCurrentSpec.clear();
    mCurrentSpec.resize(8192);
    mFullSpectrums.clear();

    if (m_parseData) {
        delete m_parseData;
        m_parseData = nullptr;
    }
    // 创建新对象，初始化测量状态
    m_parseData = new ParseData();

    if (mNetFile)
    {
        mNetFile->close();
        mNetFile->deleteLater();
        mNetFile = nullptr;
    }

    QString mMeasureStartTime = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss");
    QString filePath = QString("./cache/net/%1_%2_Net.dat").arg(mIndex).arg(mMeasureStartTime);
    if (mIndex != 0)
    {
        // 时钟同步模块不需要录制网络数据
        mNetFile = new QFile(filePath);
        if (!mNetFile->open(QIODevice::WriteOnly | QIODevice::Append))
        {
            mNetFile->deleteLater();
            mNetFile = nullptr;
        }
    }

    //测量前下发配置
    //HDF5Settings *settings = HDF5Settings::instance();
    //QMap<quint8, DetParameter>& detParameters = settings->detParameters();
    //DetParameter& detParameter = detParameters[mIndex];
    //this->updateSetting(detParameter); 参数设置在上线时候已经发过了，这里就不用重复发送了

    //#12 波形工作模式=1|0|22|1234000FFF1000000000ABCD
    //#13 能谱工作模式=1|0|23|1234000FFF1000000001ABCD
    //#14 粒子工作模式=1|0|24|1234000FFF1000000002ABCD
    this->sendWorkMode(false, workMode);

    //#15 开始测量=1|0|25|1234000FEA1000000001ABCD
    this->sendStartMeasure(CommandAdapter::MeasureMode::mmManual);
}

void DataProcessor::startAutoMeasure(QDateTime tmStart, CommandAdapter::WorkMode workMode)
{
    /*开始测量之前清空所有数据，防止野数据存在*/
    mCachePool.clear();
    mAccumulateSpec.clear();
    mAccumulateSpec.resize(8192);
    mCurrentSpec.clear();
    mCurrentSpec.resize(8192);
    mFullSpectrums.clear();

    if (m_parseData) {
        delete m_parseData;
        m_parseData = nullptr;
    }
    // 创建新对象，初始化测量状态
    m_parseData = new ParseData();

    if (mNetFile)
    {
        mNetFile->close();
        mNetFile->deleteLater();
        mNetFile = nullptr;
    }

    QString mMeasureStartTime = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss");
    QString filePath = QString("./cache/net/%1_%2_Net.dat").arg(mIndex).arg(mMeasureStartTime);
    if (mIndex != 0)
    {
        // 时钟同步模块不需要录制网络数据
        mNetFile = new QFile(filePath);
        if (!mNetFile->open(QIODevice::WriteOnly | QIODevice::Append))
        {
            mNetFile->deleteLater();
            mNetFile = nullptr;
        }
    }

    //测量前下发配置
    //HDF5Settings *settings = HDF5Settings::instance();
    //QMap<quint8, DetParameter>& detParameters = settings->detParameters();
    //DetParameter& detParameter = detParameters[mIndex];
    //this->updateSetting(detParameter); 参数设置在上线时候已经发过了，这里就不用重复发送了

    //#12 波形工作模式=1|0|22|1234000FFF1000000000ABCD
    //#13 能谱工作模式=1|0|23|1234000FFF1000000001ABCD
    //#14 粒子工作模式=1|0|24|1234000FFF1000000002ABCD
    this->sendWorkMode(false, workMode);

    //#15发送测量时间
    this->sendAutoMeasureTime(tmStart);

    //#16 开始测量=1|0|25|1234000FEA1000000001ABCD
    this->sendStartMeasure(CommandAdapter::MeasureMode::mmAuto);
}

/*
     * 停止测量
     */
void DataProcessor::stopMeasure()
{
    //#16 停止测量=1|0|26|1234000FEA1000000000ABCD
    this->sendStopMeasure();

    if (mNetFile)
    {
        mNetFile->close();
        mNetFile->deleteLater();
        mNetFile = nullptr;
    }
}

void DataProcessor::sendCmdToSocket(CommandItem cmdItem) const
{
    if (mTcpSocket && mTcpSocket->isOpen()){
        mTcpSocket->write(cmdItem.data);
        mTcpSocket->waitForBytesWritten();
        ::QThread::msleep(5);// 每条指令发送完成暂停5毫秒

        qDebug().noquote()<< QString("[%1]Send HEX: %2 [%3]")
                                  .arg(mIndex)
                                  .arg(QString(cmdItem.data.toHex(' ')))
                                  .arg(cmdItem.name);
    }
}

void DataProcessor::readyRead()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (socket->bytesAvailable() <= 0)
        return;

    QByteArray rawData = socket->readAll();
    if (mNetFile)
    {
        mNetFile->write(rawData);
        mNetFile->flush();
    }

    // 上报原始网络数据
    if (mIsUpgrading)
        emit reportOTAUpgradeData(rawData);
    else
        emit reportRawNetData(rawData);

    this->inputData(rawData);
}

void DataProcessor::inputData(const QByteArray& data)
{
    {
        QMutexLocker locker(&mDataLocker);
        if (!mIsMeasuring)  // 进入正式测量之前，每秒钟会有一个温度数据上传过来，不打印
        {
            // qDebug().noquote()<< "[" << mIndex << "] "<< "Recv HEX[" << data.size() << "]: " << data.toHex(' ');
        }

        mRawData.append(data);
        mDataReady = true;
    }

    mDataCondition.wakeAll();
}


void DataProcessor::restartTempTimeout() {
    //心跳超时时间应该是一个全局信息，不需要单独存放，暂时采用通道1的心跳时间进行存储。
    //从HDF5加载配置信息
    HDF5Settings *settings = HDF5Settings::instance();
    mTempTimeoutTimer.start(settings->pluseTime()*1000);
}

/**
 * @brief DataProcessor::inputSpectrumData
 * @param no 探测器通道编号
 * @param data 完整的能谱数据包，传入进来时已经验证了包的完整性
 */
void DataProcessor::inputSpectrumData(quint8 no, QByteArray& data){
    // 能谱数据：一个能谱数据包长度为256*32bit，但完整的能谱数据为8192*32bit。
    // 1. 提取数据包数据（256道 quint32 数据）
    quint32 spectrumSeq = 0;
    SubSpectrumPacket subSpecPackket;
    if (!extractSpectrumData(data, subSpecPackket)) {
        qWarning() << "Extract sub-spectrum data failed, seq:" << spectrumSeq
                   << ", DetID:" << static_cast<int>(no);
        return;
    }
    spectrumSeq = subSpecPackket.spectrumSeq;

    // 2. 查找/创建对应序号的完整能谱（线程安全）
    FullSpectrum* fullSpectrum = nullptr;
    {
        QMutexLocker locker(&mDataLocker); // 保护 mFullSpectrums 共享资源

        // 使用 find 方法检查是否存在，避免重复插入
        // 使用 find 方法检查是否存在，避免重复插入
        auto it = mFullSpectrums.find(spectrumSeq);
        if (it == mFullSpectrums.end()) {
            // 不存在，创建新能谱并插入
            FullSpectrum newSpectrum = {}; // 初始化所有成员为默认值（0/false/空）
            newSpectrum.sequence = spectrumSeq;
            newSpectrum.isComplete = false;
            // receivedPackets 默认为空集合，无需额外初始化

            it = mFullSpectrums.insert(spectrumSeq, newSpectrum);
        }
        fullSpectrum = &(it.value());
    }

    // 3. 数据拼接与状态更新（核心逻辑，线程安全）
    {
        QMutexLocker locker(&mDataLocker);
        // 跳过已接收的重复子包（避免覆盖和重复计数）
        const quint8 part = subSpecPackket.spectrumSubNo; // 1..32
        const quint32 bit = 1u << (part - 1);
        if (fullSpectrum->receivedMask & bit)
        {
            qCritical() << "Duplicate sub-spectrum packet, seq:" << spectrumSeq
                     << ", part:" << static_cast<int>(subSpecPackket.spectrumSubNo) << "(ignored)";
            return;
        }

        // 拷贝子能谱数据到完整能谱（固定数组直接操作内存，高效安全）
        memcpy(fullSpectrum->spectrum + (part-1)*256, subSpecPackket.spectrum, 256*4);
        fullSpectrum->receivedMask |= bit;

        if (fullSpectrum->receivedMask == 0xFFFFFFFFu) {
            fullSpectrum->isComplete = true;
            fullSpectrum->completeTime = QDateTime::currentDateTime();

            // 5. 提取元数据（测量时间、死时间）- 从当前子包提取（确保数据完整性）
            const int kMetaDateMinLen = 18; // 测量时间(10-13) + 死时间(14-17)，需至少18字节
            fullSpectrum->measureTime = subSpecPackket.measureTime;
            fullSpectrum->deathTime = subSpecPackket.deathTime;

            // 6. 发送完整能谱信号（使用 QueuedConnection 避免线程阻塞）
            // 注意：QVector 拷贝完整数据，避免多线程访问冲突
            for (int i = 0; i < 8192; ++i) {
                mAccumulateSpec[i] += fullSpectrum->spectrum[i];
            }
            memcpy(mCurrentSpec.data(), fullSpectrum->spectrum, 8192*4);
            
            // 发送完整的 FullSpectrum 数据到 MainWindow
            FullSpectrum fullSpectrumCopy = *fullSpectrum; // 拷贝数据，避免在锁外访问
            emit reportFullSpectrum(mIndex, fullSpectrumCopy);

            // H5能谱文件写入
            H5Spectrum h5Spectrum = FullSpectoH5Spec(fullSpectrumCopy);
            HDF5Settings::instance()->writeH5Spectrum(mIndex, h5Spectrum);

            //if(spectrumSeq%1000 == 0){
                qDebug().nospace() << "谱仪[#" << mIndex << "]" << "Get a full spectrum, SpectrumID:" << spectrumSeq
                     << ", specMeasureTime(ms):" << fullSpectrum->measureTime
                     << ", deathTime(*10ns):" << fullSpectrum->deathTime;
            //}
            // m_parseData->mergeSpecTime_online(*fullSpectrum);

            // 7. 清理已完成的能谱数据（释放内存，可选：若需保留历史数据可注释）
            mFullSpectrums.remove(spectrumSeq);
        }
    }
}

/**
 * @brief 从数据包中提取能谱数据
 * @param packetData 完整的数据包
 * @param spectrum 输出的能谱数据数组
 * @return 成功返回true，失败返回false
 */
bool DataProcessor::extractSpectrumData(const QByteArray& packetData, SubSpectrumPacket& packet)
{
    // 检查数据包长度
    const int expectedSize = sizeof(SubSpectrumPacket);
    if (packetData.size() < expectedSize) {
        qWarning() << "数据包长度不足，期望:" << expectedSize << "实际:" << packetData.size();
        return false;
    }

    // 拷贝到结构体
    memcpy(&packet, packetData.constData(), sizeof(SubSpectrumPacket));

    // 处理字节序（Windows是小端序，网络数据通常是大端序）
    packet.convertNetworkToHost();

    // 调试信息
    // qDebug() << "Get a subSpectrum Packets, spectrumSeq:" << packet.spectrumSeq
    //          << "subSquenceID:" << packet.spectrumSubNo;

    // quint32 utc = packet.timeUTCs; // utc时间戳
    // quint32 second = packet.timeUTCms; // 小数秒
    // QDateTime tm = QDateTime::fromSecsSinceEpoch(utc, Qt::TimeSpec::UTC, second);
    // qDebug() << tm.toString("yyyy-MM-dd HH:mm:ss.zzz");

    return true;
}

bool DataProcessor::sendOTAUpgradeData(const QByteArray& data)
{
    if (mTcpSocket){
        //qDebug() << "Send OTA: " << data.toHex(' ');
        mTcpSocket->write(data);
        mTcpSocket->waitForBytesWritten();
        return true;
    }
    else
        return false;
}
