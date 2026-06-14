#include "spectrumh5rebuilder.h"

#include "offlinespectrumparser.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>

namespace {

// CommHelper::mMeasureStartTime 格式 + 探测器编号，与
// QString("%1_%2_能谱.dat").arg(mMeasureStartTime).arg(index) 一致
const QRegularExpression kSpectrumDatNamePattern(
    QStringLiteral(R"(^(\d{4}-\d{2}-\d{2}_\d{6})_(\d+)_能谱\.dat$)"),
    QRegularExpression::CaseInsensitiveOption);

// MainWindow: QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + "_measureParam.h5"
const QRegularExpression kMeasureParamNamePattern(
    QStringLiteral(R"(^(\d{8}_\d{6})_measureParam\.h5$)"),
    QRegularExpression::CaseInsensitiveOption);

} // namespace

SpectrumH5Rebuilder::SpectrumH5Rebuilder(QObject *parent)
    : QObject(parent)
{
}

QString SpectrumH5Rebuilder::spectrumDatFileName(const QString &measureStartTime, quint8 detectorId)
{
    return QStringLiteral("%1_%2_能谱.dat").arg(measureStartTime).arg(detectorId);
}

QString SpectrumH5Rebuilder::spectrumDatFilePath(const QString &shotDir, const QString &shotNum,
                                                 const QString &measureStartTime, quint8 detectorId)
{
    return QStringLiteral("%1/%2/%3").arg(shotDir, shotNum,
                                          spectrumDatFileName(measureStartTime, detectorId));
}

QString SpectrumH5Rebuilder::spectrumH5FileName(const QString &measureStartTime)
{
    return QStringLiteral("%1.H5").arg(measureStartTime);
}

QString SpectrumH5Rebuilder::spectrumH5FilePath(const QString &shotDir, const QString &shotNum,
                                              const QString &measureStartTime)
{
    return QStringLiteral("%1/%2/%3").arg(shotDir, shotNum, spectrumH5FileName(measureStartTime));
}

bool SpectrumH5Rebuilder::parseSpectrumDatFileName(const QString &fileName, QString *measureStartTime,
                                                   quint8 *detectorId)
{
    const QRegularExpressionMatch match = kSpectrumDatNamePattern.match(fileName);
    if (!match.hasMatch())
        return false;

    bool ok = false;
    const int id = match.captured(2).toInt(&ok);
    if (!ok || id < 1 || id > DET_NUM)
        return false;

    if (measureStartTime)
        *measureStartTime = match.captured(1);
    if (detectorId)
        *detectorId = static_cast<quint8>(id);
    return true;
}

QString SpectrumH5Rebuilder::measureParamFileName(const QString &paramTimestamp)
{
    return QStringLiteral("%1_measureParam.h5").arg(paramTimestamp);
}

QString SpectrumH5Rebuilder::measureParamFilePath(const QString &shotDir, const QString &shotNum,
                                                  const QString &paramTimestamp)
{
    return QStringLiteral("%1/%2/%3").arg(shotDir, shotNum, measureParamFileName(paramTimestamp));
}

bool SpectrumH5Rebuilder::parseMeasureParamFileName(const QString &fileName, QString *paramTimestamp)
{
    const QRegularExpressionMatch match = kMeasureParamNamePattern.match(fileName);
    if (!match.hasMatch())
        return false;

    const QString timestamp = match.captured(1);
    const QDateTime dt = QDateTime::fromString(timestamp, QStringLiteral("yyyyMMdd_HHmmss"));
    if (!dt.isValid())
        return false;

    if (paramTimestamp)
        *paramTimestamp = timestamp;
    return true;
}

QString SpectrumH5Rebuilder::measureStartTimeToParamTimestamp(const QString &measureStartTime)
{
    QString timestamp = measureStartTime;
    timestamp.remove(QLatin1Char('-'));
    const QDateTime dt = QDateTime::fromString(timestamp, QStringLiteral("yyyyMMdd_HHmmss"));
    return dt.isValid() ? timestamp : QString();
}

static bool resolveMeasureParamFile(const QDir &dir, const QString &shotDir, const QString &shotNum,
                                    const QString &preferredParamTimestamp,
                                    QString *outPath, QString *outTimestamp, QString *errorMessage)
{
    struct ParamCandidate {
        QString filePath;
        QString timestamp;
    };
    QVector<ParamCandidate> candidates;

    const QStringList names = dir.entryList(
        QStringList{QStringLiteral("*_measureParam.h5")},
        QDir::Files,
        QDir::Name);

    for (const QString &name : names) {
        QString timestamp;
        if (!SpectrumH5Rebuilder::parseMeasureParamFileName(name, &timestamp))
            continue;

        const QString filePath = dir.absoluteFilePath(name);
        const QString expectedPath = SpectrumH5Rebuilder::measureParamFilePath(shotDir, shotNum, timestamp);
        if (QDir::cleanPath(filePath) != QDir::cleanPath(expectedPath))
            continue;

        candidates.append({filePath, timestamp});
    }

    if (candidates.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QObject::tr(
                "未找到有效的测量参数文件。\n"
                "期望路径：{保存路径}/{发次号}/{yyyyMMdd_HHmmss}_measureParam.h5");
        }
        return false;
    }

    if (!preferredParamTimestamp.isEmpty()) {
        for (const ParamCandidate &item : candidates) {
            if (item.timestamp == preferredParamTimestamp) {
                *outPath = item.filePath;
                *outTimestamp = item.timestamp;
                return true;
            }
        }
    }

    const ParamCandidate &selected = candidates.last();
    *outPath = selected.filePath;
    *outTimestamp = selected.timestamp;
    return true;
}

bool SpectrumH5Rebuilder::scanShotDirectory(const QString &shotDir, RebuildScanResult &result, QString *errorMessage)
{
    result = RebuildScanResult{};
    const QFileInfo dirInfo(shotDir);
    if (!dirInfo.exists() || !dirInfo.isDir()) {
        if (errorMessage)
            *errorMessage = QObject::tr("目录不存在或不可用：%1").arg(shotDir);
        return false;
    }

    result.shotDirectory = QDir(dirInfo.absoluteFilePath()).absolutePath();
    result.shotNum = dirInfo.fileName();
    result.shotDir = dirInfo.dir().absolutePath();

    QDir dir(result.shotDirectory);

    const QStringList datNames = dir.entryList(
        QStringList{QStringLiteral("*_能谱.dat")},
        QDir::Files,
        QDir::Name);
    if (datNames.isEmpty()) {
        if (errorMessage)
            *errorMessage = QObject::tr("未找到符合命名规则的能谱 dat 文件（格式：{yyyy-MM-dd_HHmmss}_{探测器编号}_能谱.dat）。");
        return false;
    }

    QString commonStartTime;
    for (const QString &name : datNames) {
        QString startTime;
        quint8 detectorId = 0;
        if (!parseSpectrumDatFileName(name, &startTime, &detectorId))
            continue;

        if (commonStartTime.isEmpty())
            commonStartTime = startTime;
        else if (commonStartTime != startTime)
            continue;

        SpectrumDatEntry entry;
        entry.filePath = dir.absoluteFilePath(name);
        entry.detectorId = detectorId;
        entry.measureStartTime = startTime;

        // 校验完整路径是否与 CommHelper 生成规则一致
        const QString expectedPath = spectrumDatFilePath(result.shotDir, result.shotNum,
                                                         startTime, detectorId);
        if (QDir::cleanPath(entry.filePath) != QDir::cleanPath(expectedPath))
            continue;

        result.datFiles.append(entry);
    }

    if (result.datFiles.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QObject::tr(
                "未识别到有效的能谱 dat 文件。\n"
                "期望路径：{保存路径}/{发次号}/{yyyy-MM-dd_HHmmss}_{探测器编号}_能谱.dat");
        }
        return false;
    }

    std::sort(result.datFiles.begin(), result.datFiles.end(),
              [](const SpectrumDatEntry &a, const SpectrumDatEntry &b) {
                  return a.detectorId < b.detectorId;
              });

    result.measureStartTime = commonStartTime;
    // 默认 H5 路径：由 dat 文件名中的 measureStartTime 推导，与 CommHelper 在线生成规则一致
    result.defaultOutputH5Path = spectrumH5FilePath(result.shotDir, result.shotNum, commonStartTime);

    const QString preferredParamTimestamp = measureStartTimeToParamTimestamp(commonStartTime);
    if (!resolveMeasureParamFile(dir, result.shotDir, result.shotNum, preferredParamTimestamp,
                                 &result.measureParamPath, &result.measureParamTimestamp, errorMessage)) {
        return false;
    }

    return true;
}

bool SpectrumH5Rebuilder::applyMeasureParameters(const QString &measureParamPath, QString *errorMessage)
{
    mLoadedParameters.clear();
    mParameterPtrMap.clear();

    if (!HDF5Settings::loadMeasureParametersFromH5(measureParamPath, mLoadedParameters)) {
        if (errorMessage)
            *errorMessage = QObject::tr("读取测量参数失败：%1").arg(measureParamPath);
        return false;
    }

    mLoadedParameters.resize(DET_NUM);
    for (int i = 0; i < DET_NUM; ++i) {
        if (mLoadedParameters[i].id == 0)
            mLoadedParameters[i].id = static_cast<quint8>(i + 1);
        mParameterPtrMap.insert(mLoadedParameters[i].id, &mLoadedParameters[i]);
    }

    HDF5Settings::instance()->setDetParameter(mParameterPtrMap);
    return true;
}

void SpectrumH5Rebuilder::requestCancel()
{
    mCancelled = true;
}

bool SpectrumH5Rebuilder::rebuild(const RebuildJob &job)
{
    mCancelled = false;

    if (job.datFiles.isEmpty()) {
        emit logMessage(tr("没有可处理的 dat 文件。"), QtWarningMsg);
        emit finished(false, {});
        return false;
    }

    if (QFileInfo::exists(job.outputH5Path)) {
        emit logMessage(tr("输出文件已存在，将跳过创建：%1").arg(job.outputH5Path), QtWarningMsg);
        emit finished(false, job.outputH5Path);
        return false;
    }

    QString errorMessage;
    if (!applyMeasureParameters(job.measureParamPath, &errorMessage)) {
        emit logMessage(errorMessage, QtWarningMsg);
        emit finished(false, {});
        return false;
    }

    emit progress(5, tr("创建 H5 文件..."));
    emit logMessage(tr("输出：%1").arg(job.outputH5Path));

    HDF5Settings::instance()->createH5Spectrum(job.outputH5Path, job.startTime, job.shotTime);

    const int total = job.datFiles.size();
    int index = 0;
    for (const SpectrumDatEntry &entry : job.datFiles) {
        if (mCancelled) {
            HDF5Settings::instance()->closeH5Spectrum();
            emit logMessage(tr("重建已取消。"), QtWarningMsg);
            emit finished(false, {});
            return false;
        }

        ++index;
        const int percent = 5 + (index * 90) / qMax(1, total);
        emit progress(percent, tr("解析探测器 %1 ...").arg(entry.detectorId));
        emit logMessage(tr("解析 %1").arg(QFileInfo(entry.filePath).fileName()));

        QVector<H5Spectrum> spectra;
        QString parseError;
        if (!OfflineSpectrumDatReader::read(entry.filePath, spectra, &parseError)) {
            emit logMessage(parseError.isEmpty()
                               ? tr("探测器 %1 未解析到有效能谱，已跳过。").arg(entry.detectorId)
                               : parseError,
                           QtWarningMsg);
            continue;
        }

        HDF5Settings::instance()->writeH5SpectrumBatch(entry.detectorId, spectra);
        emit logMessage(tr("探测器 %1 写入 %2 条能谱。").arg(entry.detectorId).arg(spectra.size()));
    }

    HDF5Settings::instance()->closeH5Spectrum();

    emit progress(100, tr("完成"));
    emit logMessage(tr("H5 重建完成：%1").arg(job.outputH5Path));
    emit finished(true, job.outputH5Path);
    return true;
}
