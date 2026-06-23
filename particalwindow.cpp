#include "particalwindow.h"
#include "ui_particalwindow.h"
#include "useractionlogger.h"
#include <QToolButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QAction>

ParticalWindow::ParticalWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ParticalWindow)
    , measuring(false)
{
    ui->setupUi(this);

    commHelper = CommHelper::instance();
    connect(commHelper, &CommHelper::reportParticle, this, [=]{
        pkgCount.store(pkgCount.load()+1);
        if (!measuring){
            ui->pushButton_stop->setEnabled(true);
            ui->pushButton_start->setEnabled(false);
            ui->pushButton_save->setEnabled(false);
        }
    }, Qt::QueuedConnection);

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

ParticalWindow::~ParticalWindow()
{
    disconnect(commHelper, nullptr, this, nullptr);
    if (measuring){
        commHelper->stopMeasure();
    }

    delete ui;
}

void ParticalWindow::on_pushButton_start_clicked()
{
    if (!commHelper->isOpen()){
        QMessageBox::information(this, tr("提示"), tr("服务未开启！"), QMessageBox::Ok, QMessageBox::Ok);
        return;
    }

    ui->lineEdit_pkgCount->setText(QString::number(0));
    commHelper->startMeasure(QDateTime::currentDateTime(), CommandAdapter::MeasureMode::mmManual,
                             CommandAdapter::WorkMode::wmParticle, 0, QString(), true);

    ui->pushButton_save->setEnabled(false);
    ui->pushButton_start->setEnabled(false);
    ui->pushButton_stop->setEnabled(true);

    measuring = true;
    timerStart = QDateTime::currentDateTime();
    timer->start(1000);
    ui->label_2->setText(timerStart.toString("yyyy-MM-dd HH:mm:ss"));
    /*QTimer::singleShot(3000, this, [=](){
        //指定时间未收到开始测量指令，则按钮恢复初始状态
        if (!measuring){
            ui->pushButton_start->setEnabled(true);
        }
    });*/
}


void ParticalWindow::on_pushButton_save_clicked()
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
        UserActionLogger::logMessage(QStringLiteral("粒子模式导出数据 path=%1").arg(exportPath));
        QMessageBox::information(this, tr("提示"), tr("数据导出成功！"), QMessageBox::Ok, QMessageBox::Ok);
    } else {
        QMessageBox::information(this, tr("提示"), tr("数据导出失败！"), QMessageBox::Ok, QMessageBox::Ok);
    }
}


void ParticalWindow::on_pushButton_stop_clicked()
{
    commHelper->stopMeasure();
    timer->stop();
    measuring = false;
    ui->pushButton_save->setEnabled(true);
    ui->pushButton_start->setEnabled(true);
    ui->pushButton_stop->setEnabled(false);
}


#include <QCloseEvent>
void ParticalWindow::closeEvent(QCloseEvent *event)
{
    if (!ui->pushButton_start->isEnabled()){
        event->ignore();
        QMessageBox::warning(this, tr("提示"), tr("测量过程中，禁止窗口关闭！"), QMessageBox::Ok, QMessageBox::Ok);
        return;
    }
}
