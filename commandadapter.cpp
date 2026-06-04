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
void CommandAdapter::pushCmd(CommandItem cmdItem, bool delayflag)
{
    // QMutexLocker locket(&mCmdMutex);
    //cmdPool.enqueue(cmdItem);//因为指令没有返回码，所以没必要加入堆栈
    sendCmdToSocket(cmdItem, delayflag);
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
    const int kCmdLen = 12;
    int pos = 0;

    auto hasHeadAt = [&](int at, const QByteArray& head) -> bool {
        return (cachePool.size() - at >= head.size()) &&
               (memcmp(cachePool.constData() + at, head.constData(), static_cast<size_t>(head.size())) == 0);
    };

    auto paramChunkAt = [&](int at) -> QByteArray {
        return QByteArray(cachePool.constData() + at, kCmdLen);
    };

    while (cachePool.size() - pos >= kCmdLen) {
        bool handled = false;
        bool needNotifyNextCmd = false;

        // 温度监测（心跳检测）
        if (hasHeadAt(pos, kCmdTempHead)) {
            qint32 t = qFromBigEndian<qint32>(cachePool.constData() + pos + 6);
            float temperature = t * 0.0001f;
            QMetaObject::invokeMethod(this, "reportTemperatureData", Qt::DirectConnection, Q_ARG(float, temperature));
            pos += kCmdLen;
            handled = true;
            needNotifyNextCmd = true;
        }

        // 能谱、波形、粒子、时间戳数据包
        if (!handled && hasHeadAt(pos, kspecHead)) {
            quint32 onePkgSize = 0;
            quint16 type = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(cachePool.constData() + pos + 4));
            DataType dataType = static_cast<DataType>(type);
            if (dataType == dtWaveform) // len=1038
                onePkgSize = 4 + 2 + 512*2 + 4 + 4;
            else if (dataType == dtSpectrum) // len=1060
                onePkgSize = 4 + 2 + 4 + 4 + 4 + 2 + 256*4 + 4 + 8 + 4;
            else if (dataType == dtParticle) // len=1058
                onePkgSize = 4 + 2 + 4 + 130*8 + 4 + 4;
            else if (dataType == dtTimestamp) // len=22
                onePkgSize = 4 + 2 + 8 + 4 + 4;
            else {
                ++mTypeErrorCount;
                mResyncDropBytes += 4;
                qDebug() << "数据包类型错误：" << cachePool.mid(pos + 4, 2).toHex(' ');
                pos += 4;
                handled = true;
            }

            if (!handled) {
                if (cachePool.size() - pos < static_cast<int>(onePkgSize))
                    break; // 数据不足，等待下一批

                const char* p = cachePool.constData() + pos;
                if (memcmp(p + onePkgSize - 4, kspecTail.constData(), 4) == 0) {
                    QByteArray chunk(p, onePkgSize);
                    if (mIsMeasuring || dataType == dtTimestamp) {
                        if (dataType == dtWaveform)
                            emit reportWaveformData(mIndex, chunk);
                        else if (dataType == dtSpectrum)
                            emit reportSpectrumData(mIndex, chunk);
                        else if (dataType == dtParticle)
                            emit reportParticleData(mIndex, chunk);
                        else if (dataType == dtTimestamp)
                            emit reportTimestampData(mIndex, chunk);
                    }
                    pos += static_cast<int>(onePkgSize);
                } else {
                    ++mTailErrorCount;
                    mResyncDropBytes += 4;
                    if (isFirstError) {
                        isFirstError = false;
                        qWarning().nospace().noquote() << "谱仪[#" << mIndex << "]数据包尾部错误，后续相似错误将不再提示";
                        qDebug().nospace().noquote() << "谱仪[#" << mIndex << "]数据包尾部错误[" << onePkgSize << "]";
                    }
                    pos += 4;
                }
                handled = true;
            }
        }

        // 增益
        if (!handled && hasHeadAt(pos, kCmdGainHead)) {
            quint16 gainData = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(cachePool.constData() + pos + 8));
            QString gainStr;
            switch (gainData) {
            case 0x01: gainStr = "0.08"; break;
            case 0x02: gainStr = "0.16"; break;
            case 0x03: gainStr = "0.32"; break;
            case 0x04: gainStr = "0.63"; break;
            case 0x05: gainStr = "1.26"; break;
            case 0x06: gainStr = "2.52"; break;
            case 0x07: gainStr = "5.01"; break;
            case 0x08: gainStr = "10.0"; break;
            default: gainStr = "未知值"; break;
            }
            qInfo().noquote() << "增益：" << gainStr;
            emit reportParamterData(mIndex, paramChunkAt(pos));
            pos += kCmdLen;
            handled = true;
            needNotifyNextCmd = true;
        }

        // 死时间
        if (!handled && hasHeadAt(pos, kCmdDeadHead)) {
            quint16 deadTime = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(cachePool.constData() + pos + 8));
            qInfo().noquote() << "参数查询，死时间/ns：" << deadTime * 10;
            emit reportParamterData(mIndex, paramChunkAt(pos));
            pos += kCmdLen;
            handled = true;
            needNotifyNextCmd = true;
        }

        // 触发阈值
        if (!handled && hasHeadAt(pos, kCmdThresHead)) {
            quint16 triggerThold = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(cachePool.constData() + pos + 8));
            qInfo().noquote() << "参数查询，触发阈值：" << triggerThold;
            emit reportParamterData(mIndex, paramChunkAt(pos));
            pos += kCmdLen;
            handled = true;
            needNotifyNextCmd = true;
        }

        // 波形基本配置
        if (!handled && hasHeadAt(pos, kCmdWaveformLengthHead)) {
            quint8 triggerMode = static_cast<quint8>(cachePool.at(pos + 7));
            qInfo().noquote() << "参数查询，触发模式：" << (triggerMode == tmTimer ? "定时触发" : "正常触发模式");
            quint8 lenType = static_cast<quint8>(cachePool.at(pos + 8));
            WaveformLength waveformLength = static_cast<WaveformLength>(lenType);
            if (waveformLength == wl64) qInfo().noquote() << "参数查询，波形长度：64";
            else if (waveformLength == wl128) qInfo().noquote() << "参数查询，波形长度：128";
            else if (waveformLength == wl256) qInfo().noquote() << "参数查询，波形长度：256";
            else if (waveformLength == wl512) qInfo().noquote() << "参数查询，波形长度：512";
            else qInfo().noquote() << "参数查询，波形长度：未知值";
            emit reportParamterData(mIndex, paramChunkAt(pos));
            pos += kCmdLen;
            handled = true;
            needNotifyNextCmd = true;
        }

        // 能谱刷新时间
        if (!handled && hasHeadAt(pos, kCmdSpectrumRefreshTimeHead)) {
            quint32 spectrumRefreshTime = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(cachePool.constData() + pos + 6));
            qInfo().noquote() << "参数查询，能谱刷新时间/ms：" << spectrumRefreshTime;
            emit reportParamterData(mIndex, paramChunkAt(pos));
            pos += kCmdLen;
            handled = true;
            needNotifyNextCmd = true;
        }

        // 梯形成型时间常数
        if (!handled && hasHeadAt(pos, kCmdTrapezoidTimeConstantHead)) {
            quint16 d1 = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(cachePool.constData() + pos + 6));
            quint16 d2 = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(cachePool.constData() + pos + 8));
            qInfo().noquote() << "参数查询，梯形成型时间常数，d1=" << (float)d1 / 65536 << "，d2=" << (float)d2 / 65536;
            emit reportParamterData(mIndex, paramChunkAt(pos));
            pos += kCmdLen;
            handled = true;
            needNotifyNextCmd = true;
        }

        // 上升沿/平顶/下降沿
        if (!handled && hasHeadAt(pos, kCmdTrapezoidLengthHead)) {
            quint8 rise = static_cast<quint8>(cachePool.at(pos + 7));
            quint8 peak = static_cast<quint8>(cachePool.at(pos + 8));
            quint8 fall = static_cast<quint8>(cachePool.at(pos + 9));
            qInfo().noquote() << "参数查询，上升沿=" << rise << "，平顶=" << peak << "，下降沿=" << fall;
            emit reportParamterData(mIndex, paramChunkAt(pos));
            pos += kCmdLen;
            handled = true;
            needNotifyNextCmd = true;
        }

        // 梯形成型使能
        if (!handled && hasHeadAt(pos, kCmdTrapShapeEnableHead)) {
            qInfo().noquote() << "参数查询，梯形成型使能状态：" << (static_cast<quint8>(cachePool.at(pos + 9)) == 0x00 ? "关闭" : "打开");
            emit reportParamterData(mIndex, paramChunkAt(pos));
            pos += kCmdLen;
            handled = true;
            needNotifyNextCmd = true;
        }

        // 工作模式
        if (!handled && hasHeadAt(pos, kCmdWorkModeHead)) {
            WorkMode workMode = static_cast<WorkMode>(cachePool.at(pos + 9));
            QString modeStr;
            switch(workMode) {
            case wmWaveform: modeStr = "波形模式"; break;
            case wmSpectrum: modeStr = "能谱模式"; break;
            case wmParticle: modeStr = "粒子模式"; break;
            default: modeStr = "未知模式"; break;
            }
            qInfo().noquote() << "参数查询，工作模式：" << modeStr;
            emit reportParamterData(mIndex, paramChunkAt(pos));
            pos += kCmdLen;
            handled = true;
            needNotifyNextCmd = true;
        }

        // 高压使能
        if (!handled && hasHeadAt(pos, kCmdHVEnableHead)) {
            HighVolgateOutLevelEnable highVoltageEnable = static_cast<HighVolgateOutLevelEnable>(cachePool.at(pos + 9));
            qInfo().noquote() << "参数查询，高压使能状态：" << (highVoltageEnable ? "关闭" : "打开");
            emit reportParamterData(mIndex, paramChunkAt(pos));
            pos += kCmdLen;
            handled = true;
            needNotifyNextCmd = true;
        }

        // 高压输出
        if (!handled && hasHeadAt(pos, kCmdHVOutputHead)) {
            quint16 level = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(cachePool.constData() + pos + 8));
            double HV = level * 2.5 * 441 / 65536;
            qInfo().noquote() << "参数查询，高压输出电平值（V）：" << HV;
            emit reportParamterData(mIndex, paramChunkAt(pos));
            pos += kCmdLen;
            handled = true;
            needNotifyNextCmd = true;
        }

        // FPGA版本
        if (!handled && hasHeadAt(pos, kCmdFPGAVersionHead)) {
            emit reportParamterData(mIndex, paramChunkAt(pos));
            pos += kCmdLen;
            handled = true;
            needNotifyNextCmd = true;
        }

        // 重加载FPGA应答
        if (!handled && hasHeadAt(pos, kCmdReloadFPGAHead)) {
            qInfo().noquote() << "下发指令返回：停止测量";
            pos += kCmdLen;
            handled = true;
            needNotifyNextCmd = true;
            QMetaObject::invokeMethod(this, "reportRetHostProgramSuccess", Qt::QueuedConnection);
        }

        if (needNotifyNextCmd)
            notifySendNextCmd();

        if (!handled) {
            // 没有找到有效包头，删除1字节继续找
            ++mHeaderScanDropBytes;
            ++pos;
        }
    }

    if (pos > 0)
        cachePool.remove(0, pos);
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
    mAskStopMeasure = false;
    QByteArray askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F EA 10 00 00 00 01 AB CD").toUtf8());
    if (measureMode == mmAuto)
    {
        askCurrentCmd = QByteArray::fromHex(QString("12 34 00 0F EA 10 00 00 00 02 AB CD").toUtf8());
    }

    pushCmd({QString("开始测量指令"), askCurrentCmd}, false);
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
    pushCmd({QString("停止测量指令"), askCurrentCmd}, false);

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
