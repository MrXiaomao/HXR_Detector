#include "commandadapter.h"
#include <QDebug>
#include <QtEndian>
#include <QTimer>
#include <QDateTime>
#include "globalsettings.h"

const QByteArray CommandAdapter::kCmdTempHead = QByteArray::fromHex("12 34 00 0A DA 11");
const QByteArray CommandAdapter::kCmdGainHead = QByteArray::fromHex("12 34 00 0A FA 10");
const QByteArray CommandAdapter::kCmdDeadHead = QByteArray::fromHex("12 34 00 0A FA 11");
const QByteArray CommandAdapter::kCmdThresHead = QByteArray::fromHex("12 34 00 0A FA 12");
const QByteArray CommandAdapter::kCmdWaveformLengthHead = QByteArray::fromHex("12 34 00 0A FC 10");
const QByteArray CommandAdapter::kCmdSpectrumRefreshTimeHead = QByteArray::fromHex("12 34 00 0A FD 10");
const QByteArray CommandAdapter::kCmdTrapezoidTimeConstantHead = QByteArray::fromHex("12 34 00 0A FE 10");
const QByteArray CommandAdapter::kCmdTrapezoidLengthHead = QByteArray::fromHex("12 34 00 0A FE 11");
const QByteArray CommandAdapter::kCmdTrapShapeEnableHead = QByteArray::fromHex("12 34 00 0A FE 12");
const QByteArray CommandAdapter::kCmdHVOutputHead = QByteArray::fromHex("12 34 00 0A F9 10");
const QByteArray CommandAdapter::kCmdHVEnableHead = QByteArray::fromHex("12 34 00 0A F9 11");
const QByteArray CommandAdapter::kCmdWorkModeHead = QByteArray::fromHex("12 34 00 0A FF 10");
const QByteArray CommandAdapter::kCmdFPGAVersionHead = QByteArray::fromHex("12 34 00 0A DA 10");
const QByteArray CommandAdapter::kCmdReloadFPGAHead = QByteArray::fromHex("12 34 00 0A FF 13");
const QByteArray CommandAdapter::kspecHead = QByteArray::fromHex("FF FF AA B1");
const QByteArray CommandAdapter::kspecTail = QByteArray::fromHex("FF FF CC D1");


CommandAdapter::CommandAdapter(quint8 index, QObject *parent)
    : QObject{parent}
    , mIndex(index)
{
    // mParserStatTimer.start();
    mCmdProcessThread = new QLiteThread(this) ;
    //mCmdProcessThread->setObjectName("mCmdProcessThread");
    mCmdProcessThread->setWorkThreadProc([=](){
        qRegisterMetaType<QByteArray>("QByteArray&");
        //需要参数类型已注册为元类型
        qRegisterMetaType<QVector<quint32>>("QVector<quint32>");//用于传输能谱
        qRegisterMetaType<quint32>("quint32");      //用于传输探测器通道号

        while (!mTerminatedThread)
        {
            {
                QMutexLocker locker(&mCmdMutex);
                while (!mCmdReady){
                    mCmdWaitCondition.wait(&mCmdMutex);
                }

                if (cmdPool.size() > 0){
                    CommandItem cmdItem = cmdPool.takeFirst();
                    sendCmdToSocket(cmdItem);
                }
                mCmdReady = false;
            }

            QThread::msleep(1);
        }
    });
    mCmdProcessThread->start();
    connect(this, &CommandAdapter::destroyed, [=]() {
        mCmdProcessThread->exit(0);
        mCmdProcessThread->wait(500);
    });
}

CommandAdapter::~CommandAdapter()
{
    mTerminatedThread = true;
    mCmdReady = true;
    mCmdWaitCondition.wakeAll();
    mCmdProcessThread->wait();
}

/*
 * 发送下一条指令
*/
void CommandAdapter::pushCmd(CommandItem cmdItem)
{
    QMutexLocker locket(&mCmdMutex);
    //cmdPool.enqueue(cmdItem);//因为指令没有返回码，所以没必要加入堆栈
    sendCmdToSocket(cmdItem);
}

/*
 * 发送下一条指令
*/
void CommandAdapter::notifySendNextCmd()
{
    mCmdReady = true;
    mCmdWaitCondition.wakeAll();
}

/*
     清空指令
    */
void CommandAdapter::clear()
{
    QMutexLocker locket(&mCmdMutex);
    cmdPool.clear();
}

void CommandAdapter::analyzeCommands(QByteArray &cachePool)
{
    // auto dumpParserStats = [&]() {
    //     if (mParserStatTimer.elapsed() >= 5000) {
    //         qDebug().noquote().nospace()
    //             << "谱仪[#"<< mIndex << "]解析统计: tailErr=" << mTailErrorCount
    //             << ", typeErr=" << mTypeErrorCount
    //             << ", removeBytes(resync4)=" << mResyncDropBytes
    //             << ", removeBytes(scan1)=" << mHeaderScanDropBytes
    //             << ", ts=" << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    //         mParserStatTimer.restart();
    //     }
    // };

    while(1){
        //判断缓存数据大小是否小于指令集长度最小单位
        if (cachePool.size() < 12)
            break;
        bool findNaul = false;

        //温度监测（心跳检测）
        if (cachePool.startsWith(kCmdTempHead)){
            qint32 t = qFromBigEndian<qint32>(cachePool.constData()+6); // 从第7个字节位置读取32位大端数据
            float temperature = t * 0.0001;// 换算系数
            // qDebug().noquote() << "temp：" << temperature;

            findNaul = true;
            cachePool.remove(0, 12);

            //上传温度数据
            QMetaObject::invokeMethod(this, "reportTemperatureData", Qt::DirectConnection, Q_ARG(float, temperature));
        }

        //增益指令
        {
            if (cachePool.startsWith(kCmdGainHead)){
                quint16 gainData = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(cachePool.constData() + 8));
                QString gainStr;
                switch (gainData)
                {
                case 0x01:
                    gainStr = "0.08";
                    break;
                case 0x02:
                    gainStr = "0.16";
                    break;
                case 0x03:
                    gainStr = "0.32";
                    break;
                case 0x04:
                    gainStr = "0.63";
                    break;
                case 0x05:
                    gainStr = "1.26";
                    break;
                case 0x06:
                    gainStr = "2.52";
                    break;
                case 0x07:
                    gainStr = "5.01";
                    break;
                case 0x08:
                    gainStr = "10.0";
                    break;
                default:
                    gainStr = "未知值";
                    break;
                }

                qInfo().noquote() << "增益：" << gainStr;

                QMetaObject::invokeMethod(this, "reportParamterData", Qt::QueuedConnection, Q_ARG(QByteArray, cachePool.left(12)));

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        //死时间配置(ns)
        {
            if (cachePool.startsWith(kCmdDeadHead)){
                // 从第9个字节位置读取16位大端数据
                quint16 deadTime = qFromBigEndian<quint16>(
                    reinterpret_cast<const uchar*>(cachePool.constData() + 8));
                qInfo().noquote() << "参数查询，死时间/ns：" << deadTime * 10;

                QMetaObject::invokeMethod(this, "reportParamterData", Qt::QueuedConnection, Q_ARG(QByteArray, cachePool.left(12)));

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        //触发阈值
        {
            if (cachePool.startsWith(kCmdThresHead)){
                // 从第9个字节位置读取16位大端数据
                quint16 triggerThold = qFromBigEndian<quint16>(
                    reinterpret_cast<const uchar*>(cachePool.constData() + 8));
                qInfo().noquote() << "参数查询，触发阈值：" << triggerThold;

                QMetaObject::invokeMethod(this, "reportParamterData", Qt::QueuedConnection, Q_ARG(QByteArray, cachePool.left(12)));

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        /*********************************************************
         波形基本配置
        ***********************************************************/
        {
            if (cachePool.startsWith(kCmdWaveformLengthHead)){
                quint8 triggerMode = cachePool.at(7);
                qInfo().noquote() << "参数查询，触发模式：" << (triggerMode == tmTimer ? "定时触发" : "正常触发模式");
                quint8 lenType = cachePool.at(8);
                WaveformLength waveformLength = (WaveformLength)lenType;
                if (waveformLength == wl64)
                    qInfo().noquote() << "参数查询，波形长度：64";
                else if (waveformLength == wl128)
                    qInfo().noquote() << "参数查询，波形长度：128";
                else if (waveformLength == wl256)
                    qInfo().noquote() << "参数查询，波形长度：256";
                else if (waveformLength == wl512)
                    qInfo().noquote() << "参数查询，波形长度：512";
                else
                    qInfo().noquote() << "参数查询，波形长度：未知值";
                QMetaObject::invokeMethod(this, "reportParamterData", Qt::QueuedConnection, Q_ARG(QByteArray, cachePool.left(12)));

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        /*********************************************************
         能谱基本配置
        ***********************************************************/
        //能谱刷新时间/ms
        {
            if (cachePool.startsWith(kCmdSpectrumRefreshTimeHead)){
                quint32 spectrumRefreshTime = qFromBigEndian<quint32>(
                    reinterpret_cast<const uchar*>(cachePool.constData() + 6));
                qInfo().noquote() << "参数查询，能谱刷新时间/ms：" << spectrumRefreshTime;

                QMetaObject::invokeMethod(this, "reportParamterData", Qt::QueuedConnection, Q_ARG(QByteArray, cachePool.left(12)));

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        /*********************************************************
         梯形成型基本配置
        ***********************************************************/
        //梯形成型时间常数配置
        {
            if (cachePool.startsWith(kCmdTrapezoidTimeConstantHead)){

                quint16 d1 = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(cachePool.constData() + 6));
                quint16 d2 = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(cachePool.constData() + 8));
                float f1 = (float)d1 / 65536;
                float f2 = (float)d2 / 65536;
                qInfo().noquote() << "参数查询，梯形成型时间常数，d1=" << f1 << "，d2="<<f2;

                QMetaObject::invokeMethod(this, "reportParamterData", Qt::QueuedConnection, Q_ARG(QByteArray, cachePool.left(12)));

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        //上升沿、平顶、下降沿长度配置
        {
            if (cachePool.startsWith(kCmdTrapezoidLengthHead)){
                quint8 rise = (quint8)cachePool.at(7);
                quint8 peak = (quint8)cachePool.at(8);
                quint8 fall = (quint8)cachePool.at(9);
                qInfo().noquote() << "参数查询，上升沿=" << rise << "，平顶="<<peak<< "，下降沿="<<fall;

                QMetaObject::invokeMethod(this, "reportParamterData", Qt::QueuedConnection, Q_ARG(QByteArray, cachePool.left(12)));

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        //梯形成型使能配置
        {
            if (cachePool.startsWith(kCmdTrapShapeEnableHead)){
                qInfo().noquote() << "参数查询，梯形成型使能状态：" << ((quint8)cachePool.at(9) == 0x00 ? "关闭" : "打开");

                QMetaObject::invokeMethod(this, "reportParamterData", Qt::QueuedConnection, Q_ARG(QByteArray, cachePool.left(12)));

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        /*********************************************************
         工作模式配置
        ***********************************************************/
        {
            if (cachePool.startsWith(kCmdWorkModeHead)){
                WorkMode workMode = (WorkMode)cachePool.at(9);
                QString modeStr;
                switch(workMode)
                {
                case wmWaveform:
                    modeStr = "波形模式";
                    break;
                case wmSpectrum:
                    modeStr = "能谱模式";
                    break;
                case wmParticle:
                    modeStr = "粒子模式";
                    break;
                default:
                    modeStr = "未知模式";
                    break;
                }
                qInfo().noquote() << "参数查询，工作模式：" << modeStr;

                QMetaObject::invokeMethod(this, "reportParamterData", Qt::QueuedConnection, Q_ARG(QByteArray, cachePool.left(12)));

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        /*********************************************************
         高压电源配置
        ***********************************************************/
        //高压使能配置
        {
            if (cachePool.startsWith(kCmdHVEnableHead)){
                HighVolgateOutLevelEnable highVoltageEnable = (HighVolgateOutLevelEnable)cachePool.at(9);
                qInfo().noquote() << "参数查询，高压使能状态：" << (highVoltageEnable ? "关闭" : "打开");

                QMetaObject::invokeMethod(this, "reportParamterData", Qt::QueuedConnection, Q_ARG(QByteArray, cachePool.left(12)));

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        //DAC模拟电平配置，用来控制高压电源输出电压值
        {
            if (cachePool.startsWith(kCmdHVOutputHead)){
                quint16 level = qFromBigEndian<quint16>(cachePool.constData() + 8);
                double HV = level * 2.5 * 441 / 65536;
                qInfo().noquote() << "参数查询，高压输出电平值（V）：" << HV;

                QMetaObject::invokeMethod(this, "reportParamterData", Qt::QueuedConnection, Q_ARG(QByteArray, cachePool.left(12)));

                findNaul = true;
                cachePool.remove(0, 12);
            }
        }

        /*********************************************************
         应答类指令
        ***********************************************************/
        //程序版本号查询
        if (cachePool.startsWith(kCmdFPGAVersionHead)){
            //硬件版本号
            QString hardVersion = QString("%1.%2.%3").arg(cachePool.at(6)).arg(cachePool.at(7)).arg(cachePool.at(8));
            //测试版本标志位
            bool isTest = cachePool.at(9) == 0x01;

            //qInfo().noquote() << "硬件版本号：" << hardVersion;
            //qInfo().noquote() << "是否测试版本：" << (isTest ? "是" : "否");
            QMetaObject::invokeMethod(this, "reportParamterData", Qt::QueuedConnection, Q_ARG(QByteArray, cachePool.left(12)));

            findNaul = true;
            cachePool.remove(0, 12);
        }

        /*********************************************************
         OTA更新指令
        ***********************************************************/
        //Flash地址设置
        //OTA更新数据传输指令

        //重加载FPGA程序
        if (cachePool.startsWith(kCmdReloadFPGAHead)){
            qInfo().noquote() << "下发指令返回：停止测量";

            findNaul = true;
            cachePool.remove(0, 12);

            //上报重加载FPGA程序状态
            QMetaObject::invokeMethod(this, "reportRetHostProgramSuccess", Qt::QueuedConnection);
        }

        if (findNaul){
            notifySendNextCmd();
            findNaul = false;
        }

        if (cachePool.startsWith(kspecHead)){
            findNaul = true;

            //有效数据包长度
            quint32 onePkgSize = 0;

            //数据类型
            bool ok;
            quint16 type = qFromBigEndian<quint16>(
                reinterpret_cast<const uchar*>(cachePool.constData() + 4));
            DataType dataType = (DataType)type;
            if (dataType == dtWaveform){
                //波形
                //包头0xFFFFAAB1 + 数据类型（0x00D1）+ 波形数据（波形长度*16bit） + 保留位（32bit）+ 包尾0xFFFFCCD1
                onePkgSize = 4 + 2 + 512*2 + 4 + 4;
            }
            else if (dataType == dtSpectrum){
                //能谱
                //包头0xFFFFAAB1 + 数据类型（0x00D2）+ 能谱序号（32bit） + 测量时间（32bit） + 死时间（32bit）+ 能谱编号（16bit）+ 能谱数据（256*32bit）+分秒-毫秒（32bit）+ 保留位（64bit） + 包尾0xFFFFCCD1
                onePkgSize = 4 + 2 + 4 + 4 + 4 + 2 + 256*4 + 4 + 8 + 4;
            }
            else if (dataType == dtParticle){
                //粒子
                //包头0xFFFFAAB1 + 数据类型（0x00D3）+ 能谱序号（32bit） + 粒子数据（130*64bit） + 保留位（32bit） + 包尾0xFFFFCCD1
                onePkgSize = 4 + 2 + 4 + 130*8 + 4 + 4;
            }
            else if (dataType == dtTimestamp){
                //触发时间戳
                //包头0xFFFFAAB1 + 数据类型（0x00D4）+ 分秒-毫秒（64bit）+ 保留位（32bit） + 包尾0xFFFFCCD1
                onePkgSize = 4 + 2 + 8 + 4 + 4;
            }
            else{
                /*异常数据，一定要注意！！！！！！！！！！！！！！！！！*/
                findNaul = false;
                ++mTypeErrorCount;
                mResyncDropBytes += 4;

                qDebug() << "数据包类型错误：" << cachePool.mid(4, 2).toHex(' ');

                //重新开始寻找包头
                cachePool.remove(0, 4);
                // dumpParserStats();
            }

            if (cachePool.size() >= onePkgSize){
                // 满足数据包长度
                // QByteArray chunk = cachePool.left(onePkgSize);
                QByteArray chunk = QByteArray(cachePool.constData(), onePkgSize);
                //继续检查包尾
                // 直接用指针判断尾（更安全）
                const char* p = cachePool.constData();

                if (memcmp(p + onePkgSize - 4, kspecTail.constData(), 4) == 0) {
                    // chunk = cachePool.left(onePkgSize); // 现在才安全
                    cachePool.remove(0, onePkgSize);
                    mValidDataPkgRef++;
                // }
                // else {
                //     cachePool.remove(0, 1);
                // }

                // if (chunk.endsWith(kspecTail)){
                //     mValidDataPkgRef++;
                //     cachePool.remove(0, onePkgSize);

                    if (mIsMeasuring || dataType == dtTimestamp)
                    {
                        if (dataType == dtWaveform)
                            QMetaObject::invokeMethod(this, "reportWaveformData", Qt::DirectConnection, Q_ARG(QByteArray&, chunk));
                        else if (dataType == dtSpectrum)
                            QMetaObject::invokeMethod(this, "reportSpectrumData", Qt::DirectConnection, Q_ARG(QByteArray&, chunk));
                        else if (dataType == dtParticle)
                            QMetaObject::invokeMethod(this, "reportParticleData", Qt::DirectConnection, Q_ARG(QByteArray&, chunk));
                        else if (dataType == dtTimestamp)
                            QMetaObject::invokeMethod(this, "reportTimestampData", Qt::DirectConnection, Q_ARG(QByteArray&, chunk));

                        //上报有效数据包个数
                        // QMetaObject::invokeMethod(this, "reportValidDataPkgRef", Qt::DirectConnection, Q_ARG(quint32, mValidDataPkgRef));
                    }
                }
                else {
                    /*异常数据，一定要注意！！！！！！！！！！！！！！！！！*/
                    findNaul = false;
                    ++mTailErrorCount;
                    mResyncDropBytes += 4;

                    if (isFirstError) {
                        qWarning().nospace().noquote() << "谱仪[#" << mIndex << "]数据包尾部错误，后续相似错误将不再提示";
                        isFirstError = false;
                        qDebug().nospace().noquote() << "谱仪[#" << mIndex << "]数据包尾部错误[" << chunk.size() << "]：" << chunk.toHex(' ');
                    }

                    // 包头/包尾数据不对称 重新开始寻找包头
                    cachePool.remove(0, 4);
                    // dumpParserStats();
                    continue;
                }
            }
            else{
                //数据不足
                break;
            }
        }
        
        if (findNaul){
            //已经处理过完整的数据包

            //继续判断剩下的数据包
            if (cachePool.size() > 12)
                continue;
            else
                break;
        }
        else if (cachePool.size() >= 12){
            //没有找到有效包头

            /*继续寻找包头,删除包头继续寻找*/
            cachePool.remove(0, 1);
            ++mHeaderScanDropBytes;
            // dumpParserStats();
            continue;
        }
        else{
            break;
        }
    }
}

//增益指令01~08
void CommandAdapter::sendGain(bool isRead, double gain){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FA 10 00 00 00 00 AB CD").toUtf8());
    if (isRead)
        askCurrentCmd[3] = 0x0A;
    else
        askCurrentCmd[3] = 0x0F;

    //根据增益值设置对应的指令值
    quint8 gainHex = 0x05; //默认值1.26
    if (qFuzzyCompare(gain, 0.08)) //浮点数相等的判断
        gainHex = 0x01;
    else if (qFuzzyCompare(gain, 0.16))
        gainHex = 0x02;
    else if (qFuzzyCompare(gain, 0.32))
        gainHex = 0x03;
    else if (qFuzzyCompare(gain, 0.63))
        gainHex = 0x04;
    else if (qFuzzyCompare(gain, 1.26))
        gainHex = 0x05;
    else if (qFuzzyCompare(gain, 2.52))
        gainHex = 0x06;
    else if (qFuzzyCompare(gain, 5.01))
        gainHex = 0x07;
    else if (qFuzzyCompare(gain, 10.0))
        gainHex = 0x08;

    askCurrentCmd[9] = (gainHex & 0xFF);
    
    if (isRead)
        pushCmd({"读：增益", askCurrentCmd});
    else
        pushCmd({QString("写：增益%1").arg(QString::number(gain)), askCurrentCmd});
}

//死时间配置(*10ns)
void CommandAdapter::sendDeathTime(bool isRead, quint8 deathTime){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FA 11 00 00 00 00 AB CD").toUtf8());
    if (isRead)
        askCurrentCmd[3] = 0x0A;
    else
        askCurrentCmd[3] = 0x0F;
    deathTime = deathTime; //死时间单位是10ns，所以这里就不需要除以10
    askCurrentCmd[9] = (deathTime & 0xFF);
    if (isRead)
        pushCmd({"读：死时间", askCurrentCmd});
    else
        pushCmd({QString("写：死时间%1ns").arg(deathTime*10), askCurrentCmd});
}

//触发阈值
void CommandAdapter::sendTriggerThold(bool isRead, quint16 triggerThold){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FA 12 00 00 00 00 AB CD").toUtf8());
    if (isRead)
        askCurrentCmd[3] = 0x0A;
    else
        askCurrentCmd[3] = 0x0F;
    askCurrentCmd[8] = ((triggerThold >> 8) & 0xFF); // 高字节
    askCurrentCmd[9] = (triggerThold & 0xFF);        // 低字节
    if (isRead)
        pushCmd({"读:触发阈值", askCurrentCmd});
    else
        pushCmd({QString("写：触发阈值%1").arg(triggerThold), askCurrentCmd});
}

/*********************************************************
 波形基本配置
***********************************************************/
void CommandAdapter::sendWaveformMode(bool isRead, TriggerMode triggerMode, quint16 waveformLength){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FC 10 00 00 00 00 AB CD").toUtf8());
    QString lengStr;

    if (isRead)
        askCurrentCmd[3] = 0x0A;
    else{
        mWaveformLength = waveformLength;
        askCurrentCmd[3] = 0x0F;
        askCurrentCmd[7] = triggerMode;
        switch (waveformLength) {
        case 64:
            askCurrentCmd[9] = wl64;
            lengStr = "64";
            break;
        case 128:
            askCurrentCmd[9] = wl128;
            lengStr = "128";
            break;
        case 256:
            askCurrentCmd[9] = wl256;
            lengStr = "256";
            break;
        case 512:
            askCurrentCmd[9] = wl512;
            lengStr = "512";
            break;
        default:
            break;
        }
    }

    QString mode = (triggerMode == tmTimer) ? "定时触发模式" : "正常触发模式";
    if (isRead)
    {
        pushCmd({"读：波形基本配置，触发模式和波形长度值", askCurrentCmd});
    }
    else{
        QString msg = QString("写：波形基本配置，触发模式：%1，波形长度：%2").arg(mode).arg(lengStr);
        pushCmd({msg, askCurrentCmd});
    }
}

/*********************************************************
 能谱基本配置
***********************************************************/
//能谱刷新时间修改(ms，默认值1000)
void CommandAdapter::sendSprectnumRefreshTimelength(bool isRead, quint32 spectrumRefreshTime){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FD 10 00 00 00 00 AB CD").toUtf8());
    if (isRead)
        askCurrentCmd[3] = 0x0A;
    else{
        askCurrentCmd[3] = 0x0F;
        askCurrentCmd[6] = (spectrumRefreshTime >> 24) & 0xFF;
        askCurrentCmd[7] = (spectrumRefreshTime >> 16) & 0xFF;
        askCurrentCmd[8] = (spectrumRefreshTime >> 8)  & 0xFF;
        askCurrentCmd[9] = (spectrumRefreshTime)       & 0xFF;
    }

    if (isRead)
        pushCmd({"读：能谱刷新时间", askCurrentCmd});
    else
        pushCmd({QString("写：能谱刷新时间%1ms").arg(spectrumRefreshTime), askCurrentCmd});
}

/*********************************************************
 梯形成型基本配置
***********************************************************/
//梯形成型时间常数配置
void CommandAdapter::sendTrapTimeConst(bool isRead, quint16 c1, quint16 c2){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FE 10 00 00 00 00 AB CD").toUtf8());
    if (isRead)
        askCurrentCmd[3] = 0x0A;
    else{
        askCurrentCmd[3] = 0x0F;
        
        if (c1 >= c2)
            return;

        askCurrentCmd[6] = ((c1 >> 8) & 0xFF);
        askCurrentCmd[7] = (c1 & 0xFF);
        askCurrentCmd[8] = ((c2 >> 8) & 0xFF);
        askCurrentCmd[9] = (c2 & 0xFF);
    }

    if (isRead)
        pushCmd({"读：梯形成型时间常数", askCurrentCmd});
    else
        pushCmd({QString("写：梯形成型时间常数:d1=%1，d2=%2").arg(c1).arg(c2), askCurrentCmd});
}

//上升沿、平顶、下降沿长度配置
void CommandAdapter::sendRisePeakFallPoints(bool isRead, quint8 rise, quint8 peak, quint8 fall){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FE 11 00 00 00 00 AB CD").toUtf8());
    if (isRead)
        askCurrentCmd[3] = 0x0A;
    else{
        askCurrentCmd[3] = 0x0F;
        askCurrentCmd[7] = rise;
        askCurrentCmd[8] = peak;
        askCurrentCmd[9] = fall;
    }

    if (isRead)
        pushCmd({QString("读：上升沿值，平顶值，下降沿值").arg(rise).arg(peak).arg(fall), askCurrentCmd});
    else
        pushCmd({QString("写：上升沿=%1，平顶=%2，下降沿=%3").arg(rise).arg(peak).arg(fall), askCurrentCmd});
}

//梯形成型使能配置
void CommandAdapter::sendTrapShapeEnable(bool isRead, TrapShapeEnable trapShapeEnable){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FE 12 00 00 00 00 AB CD").toUtf8());
    if (isRead)
        askCurrentCmd[3] = 0x0A;
    else{
        askCurrentCmd[3] = 0x0F;
        askCurrentCmd[9] = trapShapeEnable;
    }
    if (isRead)
        pushCmd({QString("读：梯形成型使能状态").arg(trapShapeEnable == teClose ? "关闭" : "打开"), askCurrentCmd});
    else
        pushCmd({QString("写：梯形成型使能状态：%1").arg(trapShapeEnable == teClose ? "关闭" : "打开"), askCurrentCmd});
}

/*********************************************************
 高压电源配置
***********************************************************/
// 高压电平配置，单位V
void CommandAdapter::sendHighVolgateOutLevel(bool isRead, quint16 highVolgateOutLevel){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F F9 10 00 00 00 00 AB CD").toUtf8());
    if (isRead)
        askCurrentCmd[3] = 0x0A;
    else{
        askCurrentCmd[3] = 0x0F;

        //高压值转DAC
        double value = highVolgateOutLevel*1.0/441.0*65536.0/2.5;

        // 四舍五入 + 范围保护
        quint16 DAC = static_cast<quint16>(
            qBound(0.0, std::round(value), 65535.0)
        );

        askCurrentCmd[8] = ((DAC >> 8) & 0xFF);
        askCurrentCmd[9] = (DAC & 0xFF);
    }

    if (isRead)
        pushCmd({QString("读：高压电平值").arg(highVolgateOutLevel), askCurrentCmd});
    else
        pushCmd({QString("写：高压电平值：%1V").arg(highVolgateOutLevel), askCurrentCmd});
}

// 高压输出使能配置
void CommandAdapter::sendHighVolgateOutLevelEnable(bool isRead, HighVolgateOutLevelEnable highVolgateOutLevelEnable){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F F9 11 00 00 00 00 AB CD").toUtf8());
    if (isRead)
        askCurrentCmd[3] = 0x0A;
    else{
        askCurrentCmd[3] = 0x0F;
        askCurrentCmd[9] = highVolgateOutLevelEnable;
    }

    if (isRead)
        pushCmd({QString("读：高压输出使能状态").arg(highVolgateOutLevelEnable == hvClose ? "关闭" : "打开"), askCurrentCmd});
    else
        pushCmd({QString("写：高压输出使能状态：%1").arg(highVolgateOutLevelEnable == hvClose ? "关闭" : "打开"), askCurrentCmd});
}

/*********************************************************
 工作模式配置
***********************************************************/
void CommandAdapter::sendWorkMode(bool isRead, WorkMode workMode){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F FF 10 00 00 00 00 AB CD").toUtf8());
    if (isRead)
        askCurrentCmd[3] = 0x0A;
    else{
        askCurrentCmd[3] = 0x0F;
        askCurrentCmd[9] = workMode;
    }

    if (isRead)
        pushCmd({QString("读：工作模式").arg(workMode == wmSpectrum ? "能谱模式" : (workMode == wmWaveform ? "波形模式" : "粒子模式")), askCurrentCmd});
    else
        pushCmd({QString("写：工作模式：%1").arg(workMode == wmSpectrum ? "能谱模式" : (workMode == wmWaveform ? "波形模式" : "粒子模式")), askCurrentCmd});
}

/*********************************************************
 控制类指令
***********************************************************/
//开始测量 波形测量/能谱测量共用一个开始
void CommandAdapter::sendStartMeasure(CommandAdapter::MeasureMode measureMode){
    isFirstError = true;
    mValidDataPkgRef = 0;
    mAskStopMeasure = false;
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F EA 10 00 00 00 01 AB CD").toUtf8());
    if (measureMode == mmAuto)
    {
        askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F EA 10 00 00 00 02 AB CD").toUtf8());
    }

    pushCmd({QString("开始测量指令"), askCurrentCmd});
    mIsMeasuring = true;

    //通知发送指令
    this->notifySendNextCmd();
}

//发送自动化测量时间
void CommandAdapter::sendAutoMeasureTime(QDateTime tm)
{
    /*强制清空之前的指令*/
    this->clear();

    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F F8 10 00 00 00 00 AB CD").toUtf8());

    quint32 tmUnix = tm.toSecsSinceEpoch();
    quint32 utc = tmUnix + 2208988800;
    askCurrentCmd[6] = ((utc >> 24) & 0xFF);
    askCurrentCmd[7] = ((utc >> 16) & 0xFF);
    askCurrentCmd[8] = ((utc >> 8) & 0xFF);
    askCurrentCmd[9] = utc & 0xFF;

    pushCmd({QString("自动化测量时间指令"), askCurrentCmd});

    //通知发送指令
    this->notifySendNextCmd();
}

//停止测量，波形测量/能谱测量共用一个停止
void CommandAdapter::sendStopMeasure(){        
    /*强制清空之前的指令*/
    this->clear();

    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F EA 10 00 00 00 00 AB CD").toUtf8());
    pushCmd({QString("停止测量指令"), askCurrentCmd});

    mAskStopMeasure = true;
    mIsMeasuring = false;

    //通知发送指令
    this->notifySendNextCmd();
}

/*********************************************************
 应答类指令
***********************************************************/
//设备查询指令-程序版本号查询
void CommandAdapter::sendSearchAppversion(){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0A DA 10 00 00 00 00 AB CD").toUtf8());
    pushCmd({QString("设备查询指令-程序版本号查询"), askCurrentCmd});
}

//心跳包响应
void CommandAdapter::sendPluse(){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0C DA 11 00 00 00 00 AB CD").toUtf8());
    pushCmd({QString("心跳包响应指令"), askCurrentCmd});
}

/*********************************************************
重加载FPGA程序
***********************************************************/
void CommandAdapter::sendSwitchHost(HostProgram hostProgram){
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F CA 12 00 00 00 00 AB CD").toUtf8());
    askCurrentCmd[9] = hostProgram;
    pushCmd({QString("重加载FPGA程序指令"), askCurrentCmd});
}
