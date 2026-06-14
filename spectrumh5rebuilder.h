#ifndef SPECTRUMH5REBUILDER_H
#define SPECTRUMH5REBUILDER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <atomic>

#include "globalsettings.h"

struct SpectrumDatEntry {
    QString filePath;
    quint8 detectorId = 0;
    QString measureStartTime; // yyyy-MM-dd_HHmmss，与 CommHelper::mMeasureStartTime 一致
};

struct RebuildScanResult {
    QString shotDirectory;   // {mShotDir}/{mShotNum}
    QString shotDir;           // mShotDir
    QString shotNum;           // mShotNum
    QString measureParamPath;
    QString measureParamTimestamp; // yyyyMMdd_HHmmss，与 MainWindow 保存规则一致
    QString measureStartTime;  // yyyy-MM-dd_HHmmss（来自能谱 dat / H5）
    QVector<SpectrumDatEntry> datFiles;
    QString defaultOutputH5Path; // {shotDir}/{shotNum}/{measureStartTime}.H5，与 CommHelper 一致
};

/** @brief 目录内按开测时刻划分的一个测量批次 */
struct RebuildBatchSummary {
    QString measureStartTime; // yyyy-MM-dd_HHmmss
    int datFileCount = 0;
    QString measureParamPath;
    QString measureParamTimestamp;
    QString defaultOutputH5Path;
};

struct RebuildJob {
    QString shotDirectory;
    QString measureParamPath;
    QString outputH5Path;
    QString startTime;
    QString shotTime;
    QVector<SpectrumDatEntry> datFiles;
};

class SpectrumH5Rebuilder : public QObject
{
    Q_OBJECT

public:
    explicit SpectrumH5Rebuilder(QObject *parent = nullptr);

    static bool scanShotDirectory(const QString &shotDir, RebuildScanResult &result, QString *errorMessage = nullptr);

    /** @brief 扫描目录内全部测量批次（按能谱 dat 开测时刻分组） */
    static bool scanShotDirectoryBatches(const QString &shotDir,
                                         QString &shotDirectory,
                                         QString &shotDirPath,
                                         QString &shotNum,
                                         QVector<RebuildBatchSummary> &batches,
                                         QString *errorMessage = nullptr);

    /** @brief 构建指定批次的扫描结果（dat 列表为目录内该时刻的全部有效文件） */
    static bool buildBatchScanResult(const QString &shotDirectory,
                                     const QString &measureStartTime,
                                     RebuildScanResult &result,
                                     QString *errorMessage = nullptr);

    /** @brief 校验并解析单个能谱 dat 完整路径 */
    static bool tryMakeDatEntry(const QString &filePath,
                                const QString &shotDirPath,
                                const QString &shotNum,
                                SpectrumDatEntry &entry,
                                QString *errorMessage = nullptr);

    /** @brief 校验并解析测量参数 h5 完整路径 */
    static bool tryMakeMeasureParamPath(const QString &filePath,
                                          const QString &shotDirPath,
                                          const QString &shotNum,
                                          QString *outPath,
                                          QString *outTimestamp,
                                          QString *errorMessage = nullptr);

    /** @brief 与 CommHelper 生成规则一致：{measureStartTime}_{index}_能谱.dat */
    static QString spectrumDatFileName(const QString &measureStartTime, quint8 detectorId);

    /** @brief 与 CommHelper 生成规则一致：{shotDir}/{shotNum}/{measureStartTime}_{index}_能谱.dat */
    static QString spectrumDatFilePath(const QString &shotDir, const QString &shotNum,
                                     const QString &measureStartTime, quint8 detectorId);

    /** @brief 与 CommHelper 生成规则一致：{measureStartTime}.H5 */
    static QString spectrumH5FileName(const QString &measureStartTime);

    /** @brief 与 CommHelper 生成规则一致：{shotDir}/{shotNum}/{measureStartTime}.H5 */
    static QString spectrumH5FilePath(const QString &shotDir, const QString &shotNum,
                                      const QString &measureStartTime);

    /** @brief 解析能谱 dat 文件名，格式：{yyyy-MM-dd_HHmmss}_{index}_能谱.dat */
    static bool parseSpectrumDatFileName(const QString &fileName, QString *measureStartTime,
                                         quint8 *detectorId = nullptr);

    /** @brief 与 MainWindow 保存规则一致：{yyyyMMdd_HHmmss}_measureParam.h5 */
    static QString measureParamFileName(const QString &paramTimestamp);

    /** @brief 与 MainWindow 保存规则一致：{shotDir}/{shotNum}/{yyyyMMdd_HHmmss}_measureParam.h5 */
    static QString measureParamFilePath(const QString &shotDir, const QString &shotNum,
                                        const QString &paramTimestamp);

    /** @brief 解析测量参数文件名，格式：{yyyyMMdd_HHmmss}_measureParam.h5 */
    static bool parseMeasureParamFileName(const QString &fileName, QString *paramTimestamp = nullptr);

    /** @brief 能谱开测时刻 yyyy-MM-dd_HHmmss 转为参数文件时间戳 yyyyMMdd_HHmmss */
    static QString measureStartTimeToParamTimestamp(const QString &measureStartTime);

    bool rebuild(const RebuildJob &job);
    void requestCancel();
    bool isCancelled() const { return mCancelled; }

signals:
    void progress(int percent, const QString &message);
    void logMessage(const QString &msg, QtMsgType type = QtInfoMsg);
    void finished(bool ok, const QString &outputPath);

private:
    bool applyMeasureParameters(const QString &measureParamPath, QString *errorMessage);

    std::atomic<bool> mCancelled{false};
    QVector<DetParameter> mLoadedParameters;
};

#endif // SPECTRUMH5REBUILDER_H
