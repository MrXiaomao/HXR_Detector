#include "spectrumh5rebuildwindow.h"
#include "ui_spectrumh5rebuildwindow.h"

#include "globalsettings.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QSet>
#include <QTableWidgetItem>
#include <QThread>
#include <QTimer>

namespace {

constexpr int kColType = 0;
constexpr int kColFileName = 1;
constexpr int kColDetector = 2;

} // namespace

SpectrumH5RebuildWindow::SpectrumH5RebuildWindow(bool isDarkTheme, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::SpectrumH5RebuildWindow)
    , mIsDarkTheme(isDarkTheme)
    , mainWindow(static_cast<QGoodWindowHelper *>(parent))
{
    ui->setupUi(this);
    initUi();
    restoreSettings();
    applyColorTheme();

    connect(this, &SpectrumH5RebuildWindow::reporWriteLog,
            this, &SpectrumH5RebuildWindow::replyWriteLog);
    connect(ui->comboBox_batch, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SpectrumH5RebuildWindow::on_comboBox_batch_currentIndexChanged);

    qRegisterMetaType<QtMsgType>("QtMsgType");

    QTimer::singleShot(0, this, [this]() {
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
        QGoodWindow::setAppCustomTheme(mIsDarkTheme, mThemeColor);
    });

    QTimer::singleShot(0, this, [this]() {
        if (mainWindow)
            mainWindow->fixMenuBarWidth();
    });
}

SpectrumH5RebuildWindow::~SpectrumH5RebuildWindow()
{
    if (mWorkerThread) {
        if (mRebuilder)
            mRebuilder->requestCancel();
        mWorkerThread->quit();
        mWorkerThread->wait(3000);
    }
    delete ui;
}

void SpectrumH5RebuildWindow::initUi()
{
    ui->tableWidget_files->horizontalHeader()->setStretchLastSection(true);
    ui->tableWidget_files->horizontalHeader()->setSectionResizeMode(kColFileName, QHeaderView::Stretch);
    ui->tableWidget_files->verticalHeader()->setVisible(false);
    ui->comboBox_batch->setEnabled(false);
    setBusy(false);
}

void SpectrumH5RebuildWindow::applyColorTheme()
{
    Q_UNUSED(mThemeColorEnable);
}

void SpectrumH5RebuildWindow::restoreSettings()
{
    GlobalSettings settings;
    const QString lastPath = settings.value("SpectrumH5Rebuild/LastShotDir", QDir::homePath()).toString();
    if (!lastPath.isEmpty())
        ui->lineEdit_shotDir->setText(lastPath);
}

void SpectrumH5RebuildWindow::replyWriteLog(const QString &msg, QtMsgType msgType)
{
    appendLog(msg, msgType);
}

void SpectrumH5RebuildWindow::appendLog(const QString &msg, QtMsgType type)
{
    const QString prefix = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"));
    QString line = QStringLiteral("[%1] %2").arg(prefix, msg);
    if (type == QtWarningMsg)
        line.prepend(QStringLiteral("[警告] "));
    else if (type == QtCriticalMsg || type == QtFatalMsg)
        line.prepend(QStringLiteral("[错误] "));

    ui->plainTextEdit_log->appendPlainText(line);
}

QString SpectrumH5RebuildWindow::batchDisplayText(const RebuildBatchSummary &batch)
{
    const QDateTime dt = QDateTime::fromString(batch.measureStartTime, QStringLiteral("yyyy-MM-dd_HHmmss"));
    const QString timeText = dt.isValid()
                                 ? dt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))
                                 : batch.measureStartTime;
    return SpectrumH5RebuildWindow::tr("%1（%2 个能谱 dat）").arg(timeText).arg(batch.datFileCount);
}

void SpectrumH5RebuildWindow::setBusy(bool busy)
{
    ui->action_selectDir->setEnabled(!busy);
    ui->action_startRebuild->setEnabled(!busy);
    ui->bt_browseDir->setEnabled(!busy);
    ui->bt_startRebuild->setEnabled(!busy);
    ui->action_stopRebuild->setEnabled(busy);
    ui->bt_stopRebuild->setEnabled(busy);
    ui->comboBox_batch->setEnabled(!busy && !mBatches.isEmpty());
    ui->bt_addDat->setEnabled(!busy && !mScanResult.measureStartTime.isEmpty());
    ui->bt_addParam->setEnabled(!busy && !mScanResult.measureStartTime.isEmpty());
    ui->bt_removeFile->setEnabled(!busy && ui->tableWidget_files->rowCount() > 0);
}

void SpectrumH5RebuildWindow::populateBatchCombo()
{
    mUpdatingBatchUi = true;
    ui->comboBox_batch->clear();

    for (const RebuildBatchSummary &batch : mBatches)
        ui->comboBox_batch->addItem(batchDisplayText(batch), batch.measureStartTime);

    ui->comboBox_batch->setEnabled(!mBatches.isEmpty());
    mUpdatingBatchUi = false;
}

void SpectrumH5RebuildWindow::populateFileTable()
{
    ui->tableWidget_files->setRowCount(0);

    if (!mScanResult.measureParamPath.isEmpty()) {
        const int row = ui->tableWidget_files->rowCount();
        ui->tableWidget_files->insertRow(row);

        auto *typeItem = new QTableWidgetItem(tr("参数"));
        typeItem->setData(Qt::UserRole, RowKindParam);
        ui->tableWidget_files->setItem(row, kColType, typeItem);

        auto *nameItem = new QTableWidgetItem(QFileInfo(mScanResult.measureParamPath).fileName());
        nameItem->setData(Qt::UserRole, mScanResult.measureParamPath);
        nameItem->setToolTip(mScanResult.measureParamPath);
        ui->tableWidget_files->setItem(row, kColFileName, nameItem);

        ui->tableWidget_files->setItem(row, kColDetector, new QTableWidgetItem(QStringLiteral("-")));
    }

    for (const SpectrumDatEntry &entry : mScanResult.datFiles) {
        const int row = ui->tableWidget_files->rowCount();
        ui->tableWidget_files->insertRow(row);

        auto *typeItem = new QTableWidgetItem(tr("能谱"));
        typeItem->setData(Qt::UserRole, RowKindDat);
        ui->tableWidget_files->setItem(row, kColType, typeItem);

        auto *nameItem = new QTableWidgetItem(QFileInfo(entry.filePath).fileName());
        nameItem->setData(Qt::UserRole, entry.filePath);
        nameItem->setToolTip(entry.filePath);
        ui->tableWidget_files->setItem(row, kColFileName, nameItem);

        ui->tableWidget_files->setItem(row, kColDetector,
                                       new QTableWidgetItem(QString::number(entry.detectorId)));
    }

    ui->lineEdit_outputH5->setText(mScanResult.defaultOutputH5Path);
    setBusy(mWorkerThread != nullptr);
}

void SpectrumH5RebuildWindow::applyBatch(const QString &measureStartTime)
{
    mScanResult = RebuildScanResult{};
    ui->tableWidget_files->setRowCount(0);
    ui->lineEdit_outputH5->clear();

    if (measureStartTime.isEmpty() || mShotDirectory.isEmpty())
        return;

    QString errorMessage;
    if (!SpectrumH5Rebuilder::buildBatchScanResult(mShotDirectory, measureStartTime,
                                                   mScanResult, &errorMessage)) {
        appendLog(errorMessage, QtWarningMsg);
        return;
    }

    populateFileTable();
    appendLog(tr("已加载批次 %1：%2 个能谱 dat，参数文件 %3")
                  .arg(measureStartTime)
                  .arg(mScanResult.datFiles.size())
                  .arg(QFileInfo(mScanResult.measureParamPath).fileName()));
}

bool SpectrumH5RebuildWindow::syncScanResultFromTable(QString *errorMessage)
{
    if (mScanResult.measureStartTime.isEmpty()) {
        if (errorMessage)
            *errorMessage = tr("请先选择测量批次。");
        return false;
    }

    RebuildScanResult synced;
    synced.shotDirectory = mScanResult.shotDirectory;
    synced.shotDir = mScanResult.shotDir;
    synced.shotNum = mScanResult.shotNum;
    synced.measureStartTime = mScanResult.measureStartTime;

    QSet<quint8> usedDetectors;
    for (int row = 0; row < ui->tableWidget_files->rowCount(); ++row) {
        QTableWidgetItem *typeItem = ui->tableWidget_files->item(row, kColType);
        QTableWidgetItem *nameItem = ui->tableWidget_files->item(row, kColFileName);
        if (!typeItem || !nameItem)
            continue;

        const QString filePath = nameItem->data(Qt::UserRole).toString();
        const int kind = typeItem->data(Qt::UserRole).toInt();

        if (kind == RowKindParam) {
            QString timestamp;
            if (!SpectrumH5Rebuilder::tryMakeMeasureParamPath(filePath, mShotDirPath, mShotNum,
                                                              &synced.measureParamPath,
                                                              &timestamp, errorMessage)) {
                return false;
            }
            synced.measureParamTimestamp = timestamp;
            continue;
        }

        if (kind == RowKindDat) {
            SpectrumDatEntry entry;
            if (!SpectrumH5Rebuilder::tryMakeDatEntry(filePath, mShotDirPath, mShotNum,
                                                      entry, errorMessage)) {
                return false;
            }
            if (entry.measureStartTime != synced.measureStartTime) {
                if (errorMessage) {
                    *errorMessage = tr("能谱 dat 与当前批次开测时刻不一致：%1")
                                        .arg(QFileInfo(filePath).fileName());
                }
                return false;
            }
            if (usedDetectors.contains(entry.detectorId)) {
                if (errorMessage) {
                    *errorMessage = tr("探测器 %1 存在重复的能谱 dat。").arg(entry.detectorId);
                }
                return false;
            }
            usedDetectors.insert(entry.detectorId);
            synced.datFiles.append(entry);
        }
    }

    if (synced.measureParamPath.isEmpty()) {
        if (errorMessage)
            *errorMessage = tr("请至少保留一个测量参数 h5 文件。");
        return false;
    }

    if (synced.datFiles.isEmpty()) {
        if (errorMessage)
            *errorMessage = tr("请至少保留一个能谱 dat 文件。");
        return false;
    }

    std::sort(synced.datFiles.begin(), synced.datFiles.end(),
              [](const SpectrumDatEntry &a, const SpectrumDatEntry &b) {
                  return a.detectorId < b.detectorId;
              });

    synced.defaultOutputH5Path = SpectrumH5Rebuilder::spectrumH5FilePath(
        synced.shotDir, synced.shotNum, synced.measureStartTime);
    mScanResult = synced;
    return true;
}

void SpectrumH5RebuildWindow::scanDirectoryAndSelectBatch()
{
    ui->tableWidget_files->setRowCount(0);
    ui->lineEdit_outputH5->clear();
    ui->comboBox_batch->clear();
    mBatches.clear();
    mScanResult = RebuildScanResult{};

    const QString shotDir = ui->lineEdit_shotDir->text().trimmed();
    if (shotDir.isEmpty())
        return;

    QString errorMessage;
    if (!SpectrumH5Rebuilder::scanShotDirectoryBatches(shotDir, mShotDirectory, mShotDirPath,
                                                       mShotNum, mBatches, &errorMessage)) {
        appendLog(errorMessage, QtWarningMsg);
        return;
    }

    populateBatchCombo();

    int selectedIndex = mBatches.size() - 1;
    if (mBatches.size() > 1) {
        QStringList batchLabels;
        for (const RebuildBatchSummary &batch : mBatches)
            batchLabels.append(batchDisplayText(batch));

        bool ok = false;
        const QString selectedLabel = QInputDialog::getItem(
            this,
            tr("选择测量批次"),
            tr("目录中共检测到 %1 个测量批次，请选择要重建的批次：").arg(mBatches.size()),
            batchLabels,
            selectedIndex,
            false,
            &ok);
        if (ok) {
            selectedIndex = batchLabels.indexOf(selectedLabel);
            if (selectedIndex < 0)
                selectedIndex = mBatches.size() - 1;
        }
    }

    mUpdatingBatchUi = true;
    ui->comboBox_batch->setCurrentIndex(selectedIndex);
    mUpdatingBatchUi = false;

    applyBatch(mBatches.at(selectedIndex).measureStartTime);
    appendLog(tr("扫描完成：发次 %1，共 %2 个批次").arg(mShotNum).arg(mBatches.size()));
}

void SpectrumH5RebuildWindow::on_comboBox_batch_currentIndexChanged(int index)
{
    if (mUpdatingBatchUi || index < 0 || index >= mBatches.size())
        return;

    applyBatch(mBatches.at(index).measureStartTime);
}

void SpectrumH5RebuildWindow::on_action_selectDir_triggered()
{
    on_bt_browseDir_clicked();
}

void SpectrumH5RebuildWindow::on_bt_browseDir_clicked()
{
    GlobalSettings settings;
    const QString lastPath = settings.value("SpectrumH5Rebuild/LastShotDir", QDir::homePath()).toString();
    const QString dir = QFileDialog::getExistingDirectory(this, tr("选择发次数据目录"), lastPath);
    if (dir.isEmpty())
        return;

    ui->lineEdit_shotDir->setText(dir);
    settings.setValue("SpectrumH5Rebuild/LastShotDir", dir);
    scanDirectoryAndSelectBatch();
}

void SpectrumH5RebuildWindow::on_bt_addDat_clicked()
{
    if (mScanResult.measureStartTime.isEmpty())
        return;

    const QString filter = tr("能谱 dat (*_*_能谱.dat);;所有文件 (*.*)");
    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("添加能谱 dat"), mShotDirectory, filter);
    if (filePath.isEmpty())
        return;

    SpectrumDatEntry entry;
    QString errorMessage;
    if (!SpectrumH5Rebuilder::tryMakeDatEntry(filePath, mShotDirPath, mShotNum, entry, &errorMessage)) {
        QMessageBox::warning(this, tr("提示"), errorMessage);
        return;
    }

    if (entry.measureStartTime != mScanResult.measureStartTime) {
        QMessageBox::warning(this, tr("提示"),
                             tr("该文件属于批次 %1，与当前批次 %2 不一致。")
                                 .arg(entry.measureStartTime, mScanResult.measureStartTime));
        return;
    }

    for (const SpectrumDatEntry &existing : mScanResult.datFiles) {
        if (existing.detectorId == entry.detectorId) {
            QMessageBox::warning(this, tr("提示"),
                                 tr("探测器 %1 已在列表中，请先移除旧文件。").arg(entry.detectorId));
            return;
        }
    }

    mScanResult.datFiles.append(entry);
    populateFileTable();
    appendLog(tr("已添加能谱 dat：%1").arg(QFileInfo(filePath).fileName()));
}

void SpectrumH5RebuildWindow::on_bt_addParam_clicked()
{
    if (mScanResult.measureStartTime.isEmpty())
        return;

    const QString filter = tr("测量参数 (*_measureParam.h5);;所有文件 (*.*)");
    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("添加测量参数 h5"), mShotDirectory, filter);
    if (filePath.isEmpty())
        return;

    QString paramPath;
    QString timestamp;
    QString errorMessage;
    if (!SpectrumH5Rebuilder::tryMakeMeasureParamPath(filePath, mShotDirPath, mShotNum,
                                                      &paramPath, &timestamp, &errorMessage)) {
        QMessageBox::warning(this, tr("提示"), errorMessage);
        return;
    }

    mScanResult.measureParamPath = paramPath;
    mScanResult.measureParamTimestamp = timestamp;
    populateFileTable();
    appendLog(tr("已设置测量参数：%1").arg(QFileInfo(paramPath).fileName()));
}

void SpectrumH5RebuildWindow::on_bt_removeFile_clicked()
{
    const int row = ui->tableWidget_files->currentRow();
    if (row < 0)
        return;

    ui->tableWidget_files->removeRow(row);

    QString errorMessage;
    if (!syncScanResultFromTable(&errorMessage)) {
        appendLog(errorMessage, QtWarningMsg);
        populateFileTable();
        return;
    }

    setBusy(mWorkerThread != nullptr);
}

void SpectrumH5RebuildWindow::on_action_startRebuild_triggered()
{
    on_bt_startRebuild_clicked();
}

void SpectrumH5RebuildWindow::on_bt_startRebuild_clicked()
{
    if (mWorkerThread)
        return;

    QString errorMessage;
    if (!syncScanResultFromTable(&errorMessage)) {
        if (mScanResult.measureStartTime.isEmpty() && mShotDirectory.isEmpty())
            QMessageBox::warning(this, tr("提示"), tr("请先选择有效的数据目录。"));
        else
            QMessageBox::warning(this, tr("提示"), errorMessage);
        return;
    }

    ui->lineEdit_outputH5->setText(mScanResult.defaultOutputH5Path);

    RebuildJob job;
    job.shotDirectory = mScanResult.shotDirectory;
    job.measureParamPath = mScanResult.measureParamPath;
    job.outputH5Path = ui->lineEdit_outputH5->text().trimmed();
    job.startTime = mScanResult.measureStartTime;
    job.datFiles = mScanResult.datFiles;

    if (job.outputH5Path.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请指定输出 H5 文件路径。"));
        return;
    }

    if (QFileInfo::exists(job.outputH5Path)) {
        const auto answer = QMessageBox::question(
            this, tr("确认"), tr("输出文件已存在，是否覆盖？\n%1").arg(job.outputH5Path));
        if (answer != QMessageBox::Yes)
            return;
        QFile::remove(job.outputH5Path);
    }

    ui->progressBar->setValue(0);
    setBusy(true);
    appendLog(tr("开始重建 H5（批次 %1）...").arg(job.startTime));

    mRebuilder = new SpectrumH5Rebuilder();
    mWorkerThread = new QThread(this);
    mRebuilder->moveToThread(mWorkerThread);

    connect(mWorkerThread, &QThread::started, mRebuilder, [this, job]() {
        mRebuilder->rebuild(job);
    });
    connect(mRebuilder, &SpectrumH5Rebuilder::progress,
            this, &SpectrumH5RebuildWindow::onRebuildProgress, Qt::QueuedConnection);
    connect(mRebuilder, &SpectrumH5Rebuilder::logMessage,
            this, &SpectrumH5RebuildWindow::onRebuildLogMessage, Qt::QueuedConnection);
    connect(mRebuilder, &SpectrumH5Rebuilder::finished,
            this, &SpectrumH5RebuildWindow::onRebuildFinished, Qt::QueuedConnection);
    connect(mWorkerThread, &QThread::finished,
            this, &SpectrumH5RebuildWindow::onWorkerThreadFinished);

    mWorkerThread->start();
}

void SpectrumH5RebuildWindow::on_action_stopRebuild_triggered()
{
    on_bt_stopRebuild_clicked();
}

void SpectrumH5RebuildWindow::on_bt_stopRebuild_clicked()
{
    if (mRebuilder)
        mRebuilder->requestCancel();
    appendLog(tr("正在请求停止..."), QtWarningMsg);
}

void SpectrumH5RebuildWindow::onRebuildProgress(int percent, const QString &message)
{
    ui->progressBar->setValue(percent);
    ui->statusbar->showMessage(message);
}

void SpectrumH5RebuildWindow::onRebuildLogMessage(const QString &msg, QtMsgType type)
{
    appendLog(msg, type);
}

void SpectrumH5RebuildWindow::onRebuildFinished(bool ok, const QString &outputPath)
{
    if (ok) {
        ui->progressBar->setValue(100);
        QMessageBox::information(this, tr("完成"), tr("H5 重建完成：\n%1").arg(outputPath));
    } else if (!outputPath.isEmpty() && QFileInfo::exists(outputPath)) {
        QMessageBox::warning(this, tr("未完成"), tr("重建未完成，请查看日志。"));
    }

    if (mWorkerThread)
        mWorkerThread->quit();
}

void SpectrumH5RebuildWindow::onWorkerThreadFinished()
{
    if (mRebuilder) {
        mRebuilder->deleteLater();
        mRebuilder = nullptr;
    }
    if (mWorkerThread) {
        mWorkerThread->deleteLater();
        mWorkerThread = nullptr;
    }
    setBusy(false);
}

void SpectrumH5RebuildWindow::on_action_exit_triggered()
{
    if (mWorkerThread) {
        on_bt_stopRebuild_clicked();
        return;
    }
    close();
}
