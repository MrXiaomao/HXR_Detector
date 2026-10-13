#include "waveformwindow.h"
#include "ui_waveformwindow.h"
#include "commhelper.h"
#include "useractionlogger.h"
#include <QToolButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QAction>

WaveformWindow::WaveformWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::WaveformWindow)
    , measuring(false)
{
    ui->setupUi(this);

    commHelper = CommHelper::instance();
    connect(commHelper, &CommHelper::reportWaveCurve, this, [=](){
        pkgCount.store(pkgCount.load()+1);
        if (!measuring){
            ui->pushButton_stop->setEnabled(true);
            ui->pushButton_start->setEnabled(false);
            ui->pushButton_save->setEnabled(false);
        }
    }, Qt::DirectConnection);

    //波形路径
    {
        QAction *action = ui->lineEdit_path->addAction(QIcon(":/open.png"), QLineEdit::TrailingPosition);
        QToolButton* button = qobject_cast<QToolButton*>(action->associatedWidgets().last());
        button->setCursor(QCursor(Qt::PointingHandCursor));
        connect(button, &QToolButton::pressed, this, [=](){
            QString dir = QFileDialog::getExistingDirectory(this);
            ui->lineEdit_path->setText(dir);
        });
    }

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [=](){
        QDateTime now = QDateTime::currentDateTime();
        qint64 tt = timerStart.secsTo(now);
        QTime time = QTime(0,0,0).addSecs(tt);
        ui->label_5->setText(time.toString("HH:mm:ss"));
        ui->lineEdit_pkgCount->setText(QString::number(pkgCount.load()));
    });

    ui->pushButton_save->setEnabled(false);
    // ui->pushButton_start->setEnabled(commHelper->isOpen());
}

WaveformWindow::~WaveformWindow()
{
    disconnect(commHelper, nullptr, this, nullptr);
    if (measuring){
        commHelper->stopMeasure();
    }

    delete ui;
}

void WaveformWindow::on_pushButton_start_clicked()
{
    if (!commHelper->isOpen()){
        QMessageBox::information(this, tr("提示"), tr("服务未开启！"), QMessageBox::Ok, QMessageBox::Ok);
        return;
    }

    pkgCount.store(0);
    ui->lineEdit_pkgCount->setText(QString::number(0));
    commHelper->startMeasure(QDateTime::currentDateTime(), CommandAdapter::MeasureMode::mmManual, CommandAdapter::WorkMode::wmWaveform);

    ui->pushButton_save->setEnabled(false);
    ui->pushButton_start->setEnabled(false);
    ui->pushButton_stop->setEnabled(true);

    measuring = true;
    timerStart = QDateTime::currentDateTime();
    timer->start(1000);
    ui->label_2->setText(timerStart.toString("yyyy-MM-dd HH:mm:ss"));
}


void WaveformWindow::on_pushButton_save_clicked()
{
    //存储路径
    //存储文件名
    QString exportPath = ui->lineEdit_path->text();
    if (!exportPath.endsWith("/"))
        exportPath += "/";
    QDir dir(exportPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    if (!dir.exists()) {
        QMessageBox::information(this, tr("提示"), tr("路径不存在，请检查后重新输入。"));
        return;
    }

    // 保存参数
    if (commHelper->exportFile(exportPath)){
        UserActionLogger::logMessage(QStringLiteral("波形模式导出数据 path=%1").arg(exportPath));
        QMessageBox::information(this, tr("提示"), tr("数据导出成功！"), QMessageBox::Ok, QMessageBox::Ok);
    } else {
        QMessageBox::information(this, tr("提示"), tr("数据导出失败！"), QMessageBox::Ok, QMessageBox::Ok);
    }
}


void WaveformWindow::on_pushButton_stop_clicked()
{
    timer->stop();
    commHelper->stopMeasure();
    ui->pushButton_save->setEnabled(true);
    ui->pushButton_start->setEnabled(true);
    ui->pushButton_stop->setEnabled(false);
}

#include <QCloseEvent>
void WaveformWindow::closeEvent(QCloseEvent *event)
{
    if (!ui->pushButton_start->isEnabled()){
        event->ignore();
        QMessageBox::warning(this, tr("提示"), tr("测量过程中，禁止窗口关闭！"), QMessageBox::Ok, QMessageBox::Ok);
        return;
    }
}
