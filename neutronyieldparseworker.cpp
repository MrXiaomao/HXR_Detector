#include "neutronyieldparseworker.h"

NeutronYieldParseWorker::NeutronYieldParseWorker(QObject *parent)
    : QObject(parent)
{
}

NeutronYieldParseWorker::~NeutronYieldParseWorker()
{
    qDeleteAll(mParseResults);
    mParseResults.clear();
}

void NeutronYieldParseWorker::requestCancel()
{
    mCancelled.store(true, std::memory_order_relaxed);
}

ParseData *NeutronYieldParseWorker::takeParseData(int detId)
{
    return mParseResults.take(detId);
}

void NeutronYieldParseWorker::run(const NeutronYieldParseJob &job)
{
    mCancelled.store(false, std::memory_order_relaxed);
    qDeleteAll(mParseResults);
    mParseResults.clear();

    QString detectorList;
    const int channelTotal = job.detIds.size();

    for (int channelIndex = 0; channelIndex < job.detIds.size(); ++channelIndex) {
        if (mCancelled.load(std::memory_order_relaxed))
            break;

        const int detID = job.detIds.at(channelIndex);
        if (detID < 1 || detID > DET_NUM) {
            emit logMessage(QObject::tr("无效通道号 %1，已跳过").arg(detID), QtWarningMsg);
            continue;
        }

        if (!detectorList.isEmpty())
            detectorList += QLatin1Char(',');
        detectorList += QString::number(detID);

        auto *parseData = new ParseData();
        parseData->setStartTime(job.beginTimeByDet.value(detID, 0));
        parseData->setSpectrumFitR2Min(job.spectrumFitR2Min);
        parseData->setParseCancelFlag(&mCancelled);

        const int channelNo = channelIndex + 1;
        parseData->setParseProgressCallback(
            [this, detID, channelNo, channelTotal, perChannelMode = job.perChannelMode](
                ParseProgressPhase phase, int current, int total) {
                QString message;
                if (phase == ParseProgressPhase::Merge) {
                    if (perChannelMode) {
                        message = QObject::tr("谱仪 %1：合并第 %2 段").arg(detID).arg(current);
                    } else {
                        message = QObject::tr("谱仪 %1/%2：合并第 %3 段")
                                      .arg(detID)
                                      .arg(channelTotal)
                                      .arg(current);
                    }
                } else {
                    if (perChannelMode) {
                        message = QObject::tr("谱仪 %1：拟合第 %2/%3 段").arg(detID).arg(current).arg(total);
                    } else {
                        message = QObject::tr("谱仪 %1/%2：拟合第 %3/%4 段")
                                      .arg(detID)
                                      .arg(channelTotal)
                                      .arg(current)
                                      .arg(total);
                    }
                }

                emit progress(current, qMax(1, total), message);
            });

        const bool mergeOk = parseData->getResult_offlineMultiFileUtc(
            job.h5FilePaths,
            static_cast<quint32>(detID),
            job.fusionTime,
            job.windowLeft,
            job.windowRight,
            job.timeStepSec);

        parseData->clearParseProgressCallback();

        const QStringList fitErrors = parseData->fitFailureMessages();
        const bool fitOk = parseData->successfulFitSegmentCount() > 0;
        const bool channelOk = mergeOk && fitOk;

        if (channelOk) {
            if (ParseData *old = mParseResults.value(detID, nullptr))
                delete old;
            mParseResults.insert(detID, parseData);
        } else {
            delete parseData;
        }

        emit channelFinished(detID, channelOk, fitErrors);

        if (mCancelled.load(std::memory_order_relaxed))
            break;
    }

    QString summaryMessage;
    if (mCancelled.load(std::memory_order_relaxed)) {
        summaryMessage = QObject::tr("解析已中断。");
    } else if (job.perChannelMode) {
        const int detID = job.detIds.isEmpty() ? 0 : job.detIds.first();
        const bool ok = mParseResults.contains(detID);
        summaryMessage = ok ? QObject::tr("谱仪%1解析成功。").arg(detID)
                            : QObject::tr("谱仪%1解析失败。").arg(detID);
    } else {
        if (!detectorList.isEmpty())
            summaryMessage = QObject::tr("文件全部解析完成，包含谱仪：%1").arg(detectorList);
        else
            summaryMessage = QObject::tr("没有可解析的谱仪通道。");
    }

    emit finished(mCancelled.load(std::memory_order_relaxed), summaryMessage);
}
