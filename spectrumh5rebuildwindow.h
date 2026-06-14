#ifndef SPECTRUMH5REBUILDWINDOW_H
#define SPECTRUMH5REBUILDWINDOW_H

#include <QMainWindow>

#include "QGoodWindowHelper"
#include "spectrumh5rebuilder.h"

namespace Ui {
class SpectrumH5RebuildWindow;
}

class QThread;

class SpectrumH5RebuildWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit SpectrumH5RebuildWindow(bool isDarkTheme = true, QWidget *parent = nullptr);
    ~SpectrumH5RebuildWindow();

    void initUi();
    void applyColorTheme();
    void restoreSettings();

    Q_SIGNAL void reporWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);
    Q_SIGNAL void sigWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);
    Q_SLOT void replyWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);

private slots:
    void on_action_selectDir_triggered();
    void on_action_startRebuild_triggered();
    void on_action_stopRebuild_triggered();
    void on_action_exit_triggered();
    void on_bt_browseDir_clicked();
    void on_bt_startRebuild_clicked();
    void on_bt_stopRebuild_clicked();
    void on_bt_addDat_clicked();
    void on_bt_addParam_clicked();
    void on_bt_removeFile_clicked();
    void on_comboBox_batch_currentIndexChanged(int index);

    void onRebuildProgress(int percent, const QString &message);
    void onRebuildLogMessage(const QString &msg, QtMsgType type);
    void onRebuildFinished(bool ok, const QString &outputPath);
    void onWorkerThreadFinished();

private:
    enum FileRowKind {
        RowKindParam = 0,
        RowKindDat = 1
    };

    void setBusy(bool busy);
    void scanDirectoryAndSelectBatch();
    void populateBatchCombo();
    void applyBatch(const QString &measureStartTime);
    void populateFileTable();
    bool syncScanResultFromTable(QString *errorMessage = nullptr);
    void appendLog(const QString &msg, QtMsgType type = QtInfoMsg);
    static QString batchDisplayText(const RebuildBatchSummary &batch);

    Ui::SpectrumH5RebuildWindow *ui;
    bool mIsDarkTheme = true;
    bool mThemeColorEnable = true;
    QColor mThemeColor = QColor(255, 255, 255);
    QGoodWindowHelper *mainWindow = nullptr;

    QString mShotDirectory;
    QString mShotDirPath;
    QString mShotNum;
    QVector<RebuildBatchSummary> mBatches;
    RebuildScanResult mScanResult;
    bool mUpdatingBatchUi = false;

    SpectrumH5Rebuilder *mRebuilder = nullptr;
    QThread *mWorkerThread = nullptr;
};

#endif // SPECTRUMH5REBUILDWINDOW_H
