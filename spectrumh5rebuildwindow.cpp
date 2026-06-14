#include "spectrumh5rebuildwindow.h"
#include "ui_spectrumh5rebuildwindow.h"

#include "globalsettings.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QThread>
#include <QTimer>

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
    ui->tableWidget_files->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->tableWidget_files->verticalHeader()->setVisible(false);
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

void SpectrumH5RebuildWindow::setBusy(bool busy)
{
    ui->action_selectDir->setEnabled(!busy);
    ui->action_startRebuild->setEnabled(!busy);
    ui->bt_browseDir->setEnabled(!busy);
    ui->bt_startRebuild->setEnabled(!busy);
    ui->action_stopRebuild->setEnabled(busy);
    ui->bt_stopRebuild->setEnabled(busy);
}

void SpectrumH5RebuildWindow::refreshScanResult()
{
    ui->tableWidget_files->setRowCount(0);
    ui->lineEdit_outputH5->clear();
    mScanResult = RebuildScanResult{};

    const QString shotDir = ui->lineEdit_shotDir->text().trimmed();
    if (shotDir.isEmpty())
        return;

    QString errorMessage;
    if (!SpectrumH5Rebuilder::scanShotDirectory(shotDir, mScanResult, &errorMessage)) {
        appendLog(errorMessage, QtWarningMsg);
        return;
    }

    ui->lineEdit_outputH5->setText(mScanResult.defaultOutputH5Path);

    int row = 0;
    ui->tableWidget_files->insertRow(row);
    ui->tableWidget_files->setItem(row, 0, new QTableWidgetItem(tr("参数")));
    ui->tableWidget_files->setItem(row, 1, new QTableWidgetItem(QFileInfo(mScanResult.measureParamPath).fileName()));
    ui->tableWidget_files->setItem(row, 2, new QTableWidgetItem(QStringLiteral("-")));
    ++row;

    for (const SpectrumDatEntry &entry : mScanResult.datFiles) {
        ui->tableWidget_files->insertRow(row);
        ui->tableWidget_files->setItem(row, 0, new QTableWidgetItem(tr("能谱")));
        ui->tableWidget_files->setItem(row, 1, new QTableWidgetItem(QFileInfo(entry.filePath).fileName()));
        ui->tableWidget_files->setItem(row, 2, new QTableWidgetItem(QString::number(entry.detectorId)));
        ++row;
    }

    appendLog(tr("扫描完成：发次 %1，找到 %2 个能谱 dat，开测时刻 %3，参数文件 %4")
                  .arg(mScanResult.shotNum)
                  .arg(mScanResult.datFiles.size())
                  .arg(mScanResult.measureStartTime)
                  .arg(QFileInfo(mScanResult.measureParamPath).fileName()));
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
    refreshScanResult();
}

void SpectrumH5RebuildWindow::on_action_startRebuild_triggered()
{
    on_bt_startRebuild_clicked();
}

void SpectrumH5RebuildWindow::on_bt_startRebuild_clicked()
{
    if (mWorkerThread)
        return;

    if (mScanResult.datFiles.isEmpty())
        refreshScanResult();
    if (mScanResult.datFiles.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择有效的数据目录。"));
        return;
    }

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
            this,
            tr("确认"),
            tr("输出文件已存在，是否覆盖？\n%1").arg(job.outputH5Path));
        if (answer != QMessageBox::Yes)
            return;
        QFile::remove(job.outputH5Path);
    }

    ui->progressBar->setValue(0);
    setBusy(true);
    appendLog(tr("开始重建 H5..."));

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
