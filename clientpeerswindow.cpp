#include "clientpeerswindow.h"
#include "ui_clientpeerswindow.h"
#include <QDateTime>

ClientPeersWindow::ClientPeersWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ClientPeersWindow)
{
    ui->setupUi(this);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    ui->tableWidget->setColumnWidth(1, 200);
    ui->tableWidget->setRowHeight(0, 30);
    ui->tableWidget->horizontalHeader()->setSortIndicatorShown(true); // 显示排序箭头
    ui->tableWidget->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder); // 0是第一列，升序

    connect(this, &ClientPeersWindow::connectPeerConnection, this, [=](QString peerAddress, quint16 peerPort){
        int row = ui->tableWidget->rowCount();
        ui->tableWidget->insertRow(row);
        ui->tableWidget->setItem(row, 0, new QTableWidgetItem(QString("%1:%2").arg(peerAddress).arg(peerPort)));
        ui->tableWidget->setItem(row, 1, new QTableWidgetItem(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")));
        ui->tableWidget->item(row, 0)->setTextAlignment(Qt::AlignCenter);
        ui->tableWidget->item(row, 1)->setTextAlignment(Qt::AlignCenter);
    });

    connect(this, &ClientPeersWindow::disconnectPeerConnection, this, [=](QString peerAddress, quint16 peerPort){
        int row = ui->tableWidget->rowCount();
        QString peerInfo = QString("%1:%2").arg(peerAddress).arg(peerPort);
        for (int row =0; row<ui->tableWidget->rowCount(); ++row){
            if (ui->tableWidget->item(row, 0)->text() == peerInfo){
                ui->tableWidget->removeRow(row);
                break;
            }
        }
    });
}

ClientPeersWindow::~ClientPeersWindow()
{
    delete ui;
}

void ClientPeersWindow::on_pushButton_clicked()
{
    this->close();
}

