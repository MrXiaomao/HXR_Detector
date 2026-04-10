#include "dataprocessor.h"
#include <QTimer>

DataProcessor::DataProcessor(quint8 index, QTcpSocket* socket, QObject *parent)
    : CommandAdapter(index, parent)
{
    m_parseData = nullptr;
    mAccumulateSpec.resize(8192);
    mDataProcessThread = new QLiteThread(this);
    mDataProcessThread->setWorkThreadProc([=](){
        while (!mTerminatedDataThread)
        {
            QByteArray localRaw;
            {
                QMutexLocker locker(&mDataLocker);
                while (!mTerminatedDataThread && mRawData.isEmpty() && !mParserResetRequested) {
                    mDataCondition.wait(&mDataLocker);
                }
                if (mTerminatedDataThread)
                    break;

                if (mParserResetRequested) {
                    // 清空动作固定在数据线程执行，避免与解析并发访问 mCachePool
                    mRawData.clear();
                    mCachePool.clear();
                    mParserResetRequested = false;
                    mParserResetDone = true;
                    mParserResetCondition.wakeAll();
                    continue;
                }

                localRaw.swap(mRawData);
            }

            mCachePool.append(localRaw);
            if (!mTerminatedDataThread)
                analyzeCommands(mCachePool);
        }
    });
    mDataProcessThread->start();
    connect(this, &DataProcessor::destroyed, [=]() {
        mDataProcessThread->exit(0);
        mDataProcessThread->wait(500);
    });

    // 直接连接，确保能谱数据处理的线程安全性，能谱解析使其处于解析线程，避免与主线程抢资源。
    connect(this, &CommandAdapter::reportSpectrumData, this, &DataProcessor::mergeSubSpec_Thread, Qt::DirectConnection);
    connect(this, &CommandAdapter::reportWaveformData, this, &DataProcessor::extractWaveformData, Qt::DirectConnection);
    connect(this, &CommandAdapter::reportParticleData, this, &DataProcessor::extractParticleData, Qt::DirectConnection);
    connect(this, &CommandAdapter::reportTimestampData, this, &DataProcessor::extractTimestampData, Qt::DirectConnection);

#if ENABLE_LIBHV
#else
    if (mTcpSocket)
        connect(mTcpSocket, SIGNAL(readyRead()), this, SLOT(readyRead()));
#endif //ENABLE_LIBHV

    mTempTimeoutTimer.setSingleShot(true);
    connect(&mTempTimeoutTimer, &QTimer::timeout, this, [this](){
        qWarning().noquote().nospace() << tr("谱仪[#%1]超过%2秒未收到温度数据，触发报警！").arg(mIndex).arg(mTempTimeoutTimer.interval() / 1000);

        // 给CommHelper模块上报温度超时报警
        emit reportTemperatureTimeout(mIndex);
    });

    // 温度刷新，重启心跳定时器
    connect(this, &DataProcessor::reportTemperatureData, this, [this](){
        restartTempTimeout();
    });

}


DataProcessor::~DataProcessor()
{
    if (mDataProcessThread){
        // 终止线程
        mTerminatedDataThread = true;
        mParserResetCondition.wakeAll();
        mDataCondition.wakeOne();
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

#if ENABLE_LIBHV
void DataProcessor::reallocSocket(const hv::SocketChannelPtr& tcpSocket, DetParameter* detParameter)
{
    mTcpSocket = tcpSocket;
    if (mTcpSocket){
        tcpSocket->onread = [=](hv::Buffer* buf){
            if(buf->size()==0) return;
            QByteArray rawData = QByteArray::fromRawData((const char*)buf->data(), buf->size());
            // 上报原始网络数据
            if (mIsUpgrading)
                emit reportOTAUpgradeData(mIndex, rawData);
            else
                emit reportRawNetData(mIndex, rawData);

            this->appendRawTcpData(rawData);
        };

        tcpSocket->onwrite = [=](hv::Buffer* buf) {
            if (tcpSocket->isWriteComplete()) {
                // 所有数据已发送完成，执行后续逻辑（如关闭连接、发送下一批数据）
            } else {
                // 部分数据未发送，等待下一次回调
            }
        };

        // 关联端口，自动修改配置地址信息，方便打开探测器设计页面，就知道关联的是哪个ip地址
        if (detParameter){
            QString peerAddress = QString::fromStdString(tcpSocket->peerip());
            memset(detParameter->det_Ip_port, 0, IP_LENGTH);
            strcpy(detParameter->det_Ip_port, peerAddress.toStdString().c_str());
            HDF5Settings::instance()->sync();
            updateSetting(detParameter);
        }

        // 启动温度超时检测
        restartTempTimeout();
    }
}
#else
#include <QHostAddress>
void DataProcessor::reallocSocket(QTcpSocket *tcpSocket, DetParameter* detParameter)
{
    QMutexLocker locker(&mSocketLocker);
    if (mTcpSocket){
        disconnect(mTcpSocket, SIGNAL(readyRead()), this, SLOT(readyRead()));
    }

    mTcpSocket = tcpSocket;
    if (mTcpSocket){
        connect(mTcpSocket, SIGNAL(readyRead()), this, SLOT(readyRead()));

        if (detParameter){
            // 关联端口，自动修改配置地址信息，方便打开探测器设计页面，就知道关联的是哪个ip地址
            QString peerAddress = tcpSocket->peerAddress().toString();
            memset(detParameter->det_Ip_port, 0, IP_LENGTH);
            strcpy(detParameter->det_Ip_port, peerAddress.toStdString().c_str());
            HDF5Settings::instance()->sync();
            updateSetting(detParameter);
        }
    }
}
#endif //ENABLE_LIBHV

void DataProcessor::updateSetting(DetParameter* detParameter, bool isRead/* = false*/)
{
    // 设置基本参数
    {
        //清空指令
        this->clear();

        //#01 增益=1|0|1|1234000FFA1000000000ABCD
        this->sendGain(isRead, detParameter->gain);
        //#02 死时间=1|0|3|1234000FFA1100000000ABCD
        this->sendDeathTime(isRead, detParameter->deathTime);
        //#03 触发阈值=1|0|5|1234000FFA1200000000ABCD
        this->sendTriggerThold(isRead, detParameter->triggerThold);
        //#04 触发模式=1|0|7|1234000FFC1000000003ABCD
        this->sendWaveformMode(isRead, (TriggerMode)detParameter->waveformTriggerMode, detParameter->waveformLength);
        //#05 能谱刷新时间=1|0|9|1234000FFD1000000000ABCD
        this->sendSprectnumRefreshTimelength(isRead, detParameter->spectrumRefreshTime);

        if (detParameter->trapShapeEnable || isRead){
            //#07 梯形成型常数=1|0|13|1234000FFE1000000000ABCD
            this->sendTrapTimeConst(isRead, detParameter->trapShapeTimeConstD1, detParameter->trapShapeTimeConstD2);
            //#08 上升沿参数=1|0|15|1234000FFE1100000000ABCD
            this->sendRisePeakFallPoints(isRead, detParameter->trapShapeRisePoint, detParameter->trapShapePeakPoint, detParameter->trapShapeFallPoint);
        }

        //#09 梯形成型使能=1|0|17|1234000FFE1200000000ABCD
        this->sendTrapShapeEnable(isRead, (TrapShapeEnable)detParameter->trapShapeEnable);
        
        //#10 高压输出电平=1|0|21|1234000FF91100000000ABCD,暂时不再使用，后续直接FPGA程序内部写死，不再界面可调。
        if (detParameter->highVoltageEnable || isRead)
            this->sendHighVolgateOutLevel(isRead, detParameter->highVoltageOutLevel);

        //#11 高压电平使能=1|0|19|1234000FF91000000000ABCD
        this->sendHighVolgateOutLevelEnable(isRead, (HighVolgateOutLevelEnable)detParameter->highVoltageEnable);

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
    /*开始测量之前清空解析缓存，防止野数据存在*/
    requestParserResetOnDataThread();
    mAccumulateSpec.clear();
    mAccumulateSpec.resize(8192);
    mFullSpectrums.clear();
    mLastSpectrumSeq = 0;

    if (m_parseData) {
        delete m_parseData;
        m_parseData = nullptr;
    }
    // 创建新对象，初始化测量状态
    m_parseData = new ParseData();

    QString mMeasureStartTime = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss");

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
    /*开始测量之前清空解析缓存，防止野数据存在*/
    requestParserResetOnDataThread();
    mAccumulateSpec.clear();
    mAccumulateSpec.resize(8192);
    mFullSpectrums.clear();
    mLastSpectrumSeq = 0;

    if (m_parseData) {
        delete m_parseData;
        m_parseData = nullptr;
    }
    // 创建新对象，初始化测量状态
    m_parseData = new ParseData();

    QString mMeasureStartTime = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss");

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

    mTempTimeoutTimer.stop();
}

void DataProcessor::sendCmdToSocket(CommandItem cmdItem) const
{
#if ENABLE_LIBHV
    if (mTcpSocket->isOpened()){
        mTcpSocket->write(cmdItem.data.constData(), cmdItem.data.size());
        ::QThread::msleep(5);// 每条指令发送完成暂停5毫秒

        qDebug().noquote()<< QString("[%1]Send HEX: %2 [%3]")
                                  .arg(mIndex)
                                  .arg(QString(cmdItem.data.toHex(' ')))
                                  .arg(cmdItem.name);
    }
#else
    if (mTcpSocket && mTcpSocket->isOpen()){
        mTcpSocket->write(cmdItem.data);
        mTcpSocket->waitForBytesWritten();
        ::QThread::msleep(5);// 每条指令发送完成暂停5毫秒

        qDebug().noquote()<< QString("[%1]Send HEX: %2 [%3]")
                                  .arg(mIndex)
                                  .arg(QString(cmdItem.data.toHex(' ')))
                                  .arg(cmdItem.name);
    }
#endif //ENABLE_LIBHV
}

void DataProcessor::readyRead()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (socket->bytesAvailable() <= 0)
        return;

    QByteArray rawData = socket->readAll();

    // 上报原始网络数据
    if (mIsUpgrading)
        emit reportOTAUpgradeData(mIndex,rawData);
    else
        emit reportRawNetData(mIndex, rawData);

    this->appendRawTcpData(rawData);

    mDataCondition.wakeOne();
}

void DataProcessor::appendRawTcpData(const QByteArray& data)
{
    QMutexLocker locker(&mDataLocker);
    mRawData.append(data);
    const quint64 currentSize = static_cast<quint64>(mRawData.size());
    if (currentSize > mRawDataPeak) {
        mRawDataPeak = currentSize;
        // qDebug().noquote().nospace()
        //     << "谱仪[#"<< mIndex << "] mRawData峰值更新(appendRawTcpData): "
        //     << mRawDataPeak << " bytes, ts="
        //     << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    }

    mDataCondition.wakeOne();
}

void DataProcessor::requestParserResetOnDataThread()
{
    QMutexLocker locker(&mDataLocker);
    mParserResetRequested = true;
    mParserResetDone = false;
    mDataCondition.wakeOne();
    while (!mTerminatedDataThread && !mParserResetDone) {
        mParserResetCondition.wait(&mDataLocker);
    }
}


void DataProcessor::restartTempTimeout() {
    // 如果是模拟器发数据，这里直接return即可！！！
#if USED_SIMULATOR
    return;
#endif //
    return;

    //从HDF5加载配置信息
    HDF5Settings *settings = HDF5Settings::instance();
    mTempTimeoutTimer.start(settings->pluseTime()*1000);
}

/**
 * @brief 解析能谱数据包，并拼接成完整能谱，一个能谱由32个子包组成，每个子包包含256道能谱数据
 * @param no 探测器通道编号
 * @param data 完整的能谱数据包，传入进来时已经验证了包的完整性
 */
void DataProcessor::mergeSubSpec_Thread(quint8 no, const QByteArray& data){
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

    // 跳过异常分包号，避免位运算和数组偏移越界
    const quint8 part = subSpecPackket.spectrumSubNo; // 1..32
    if (part < 1 || part > 32) {
        qWarning() << "Invalid sub-spectrum part, seq:" << spectrumSeq
                   << ", part:" << static_cast<int>(part);
        return;
    }

    FullSpectrum fullSpectrumCopy;
    bool hasFullSpectrum = false;
    {
        QMutexLocker locker(&mDataLocker);

        // 查找/创建对应序号的完整能谱（单次加锁完成）
        auto it = mFullSpectrums.find(spectrumSeq);
        if (it == mFullSpectrums.end()) {
            FullSpectrum newSpectrum = {};
            newSpectrum.sequence = spectrumSeq;
            newSpectrum.timeUTCs = subSpecPackket.timeUTCs;
            newSpectrum.timeUTCms = subSpecPackket.timeUTCms;
            newSpectrum.measureTime = subSpecPackket.measureTime;
            newSpectrum.deathTime = subSpecPackket.deathTime;
            it = mFullSpectrums.insert(spectrumSeq, newSpectrum);
        }
        FullSpectrum& fullSpectrum = it.value();

        // 跳过已接收的重复子包（避免覆盖和重复计数）
        const quint32 bit = 1u << (part - 1);
        if (fullSpectrum.receivedMask & bit)
        {
            qCritical() << "Duplicate sub-spectrum packet, seq:" << spectrumSeq
                        << ", part:" << static_cast<int>(part) << "(ignored)";
            return;
        }

        // 拷贝子能谱数据到完整能谱
        memcpy(fullSpectrum.spectrum + (part - 1) * 256, subSpecPackket.spectrum, 256 * 4);
        fullSpectrum.receivedMask |= bit;

        if (fullSpectrum.receivedMask == 0xFFFFFFFFu) {
            fullSpectrum.isComplete = true;

            // 使用指针循环加速累加（更利于编译器向量化）
            quint32* acc = mAccumulateSpec.data();
            const quint32* src = fullSpectrum.spectrum;
            for (int i = 0; i < 8192; ++i) {
                acc[i] += src[i];
            }

            fullSpectrumCopy = fullSpectrum; // 拷贝数据，避免锁外访问共享对象
            hasFullSpectrum = true;
            mFullSpectrums.remove(spectrumSeq);
            mLastSpectrumSeq = spectrumSeq;
        }
    }

    if (hasFullSpectrum) {
        // 锁外发送，降低锁持有时长
        emit reportFullSpectrum(mIndex, fullSpectrumCopy);
    }
}

// 从波形数据包中提取波形数据
void DataProcessor::extractWaveformData(quint8 no, const QByteArray& data)
{
    // 包结构：包头6字节 + 波形区 + 包尾8字节
    if (data.size() < 14)
        return;

    // 从 HDF5 加载配置信息
    HDF5Settings *settings = HDF5Settings::instance();
    QMap<quint8, DetParameter>& detParameters = settings->detParameters();
    DetParameter& detParameter = detParameters[no];
    quint32 waveLen = detParameter.waveformLength;

    if (waveLen < 2)
        return;

    const char* raw = data.constData();

    // 跳过整个网络包头 6 字节，再跳过波形内部前 2 字节
    const int payloadOffset = 6;
    const int waveformDataOffset = payloadOffset + 2;

    // 需要读取 (waveLen - 2) 个 quint16
    const int pointCount = static_cast<int>(waveLen) - 2;
    const int bytesNeeded = waveformDataOffset + pointCount * 2;

    // 末尾还有 8 字节包尾，所以只要总长度足够即可
    if (data.size() < bytesNeeded + 8)
        return;

    QVector<quint16> waveform;
    waveform.reserve(pointCount);

    for (int i = 0; i < pointCount; ++i) {
        const uchar* p = reinterpret_cast<const uchar*>(raw + waveformDataOffset + i * 2);//获取指针位置
        quint16 amplitude = qFromBigEndian<quint16>(p); //从指针位置连续读两个字节转化为大端序的quint16
        waveform.append(amplitude);
    }

    emit reportWaveCurveInfo(no, waveform);
}

// 从粒子数据包中提取粒子数据
void DataProcessor::extractParticleData(quint8 no, const QByteArray& data)
{
    // 至少要保证长度够
    if (data.size() < 6 + 130 * 8 + 8)  return;
    
    const uchar* p = reinterpret_cast<const uchar*>(data.constData());

    int offset = 0;

    offset += 6; // 跳过包头

    // ===== 读取包序号 =====
    quint32 sequence = qFromBigEndian<quint32>(p + offset);
    offset += 4;

    // ===== 提前计算 UTC 基准（避免循环里重复计算）=====
    static const QDateTime epoch(QDate(1900, 1, 1), QTime(0, 0, 0), Qt::UTC);
    quint64 tmUtcBase = epoch.secsTo(QDateTime::currentDateTimeUtc());
    tmUtcBase &= 0xffffffffffff0000;

    // ===== 循环解析 130 个粒子 =====
    QVector<ParticleData> particles;
    for (int i = 0; i < 130; ++i)
    {
        const uchar* item = p + offset + i * 8;

        quint16 utc       = qFromBigEndian<quint16>(item + 0);
        quint16 msecond   = qFromBigEndian<quint16>(item + 2) * 1000 / 65536;
        quint16 deathT    = qFromBigEndian<quint16>(item + 4);
        quint16 _type     = qFromBigEndian<quint16>(item + 6);

        quint16 typeFlag  = (_type & 0x8000) >> 15;
        quint16 amplitude = (_type & 0x7FFF);

        // ===== 时间计算 =====
        quint64 tmUtc = tmUtcBase | utc;
        quint64 tmUnix = tmUtc - 2208988800ULL;

        QDateTime tm = QDateTime::fromMSecsSinceEpoch(
            tmUnix * 1000 + msecond
        );
        ParticleData particle{tm, amplitude, typeFlag};
        particles.push_back(particle);
    }
    
    emit reportParticleInfo(mIndex, particles);
}

// 解析时间戳数据包
void DataProcessor::extractTimestampData(quint8 no, const QByteArray& packetData)
{
    bool ok;
    quint32 utc = packetData.mid(6, 4).toHex().toUInt(&ok, 16); // utc时间戳
    quint32 msecond = packetData.mid(10, 4).toHex().toUInt(&ok, 16)*1000/65536; // 毫秒

    // UTC转Unix时间戳，也就是从1970年1月1日00：00：00开始计算的整数秒数
    quint64 tmUnix = utc - 2208988800; // UTC时间戳（1970）-unix时间戳
    QDateTime tm = QDateTime::fromMSecsSinceEpoch(tmUnix*1000 + msecond);

    // 上报时间戳信息
    emit reportTriggerTimeInfo(no, tm);
}

/**
 * @brief 将网口数据直接转化为结构体SubSpectrumPacket
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
    return true;
}

bool DataProcessor::sendOTAUpgradeData(const QByteArray& data)
{
    if (mTcpSocket){
#if ENABLE_LIBHV
        mTcpSocket->write(data.constData(), data.size());
#else
        mTcpSocket->write(data);
        mTcpSocket->waitForBytesWritten();
#endif //ENABLE_LIBHV
        return true;
    }
    else
        return false;
}
