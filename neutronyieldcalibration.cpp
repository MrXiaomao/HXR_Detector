/*
 * @Author: MrPan
 * @Date: 2026-01-15 21:24:30
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2026-02-05 09:33:14
 * @Description: 请填写简介
 */
#include "neutronyieldcalibration.h"
#include "ui_neutronyieldcalibration.h"
#include <QSettings>

NeutronYieldCalibration::NeutronYieldCalibration(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::NeutronYieldCalibration)
{
    ui->setupUi(this);
    ui->pushButton_import->hide();
    ui->pushButton_export->hide();

    QSettings mSettings("./config/NeutronYield.ini", QSettings::IniFormat);
    for (int row= 0; row<ui->tableWidget->rowCount(); ++row){
        QTableWidgetItem *item = new QTableWidgetItem(mSettings.value(QString("Table/yield%1").arg(row+1), "1.0").toString());
        item->setTextAlignment(Qt::AlignCenter); // 水平+垂直居中
        ui->tableWidget->setItem(row, 0, item);

        QTableWidgetItem *item2 = new QTableWidgetItem(mSettings.value(QString("Table/initial909Count%1").arg(row+1), "1.0").toString());
        item2->setTextAlignment(Qt::AlignCenter); // 水平+垂直居中
        ui->tableWidget->setItem(row, 1, item2);
        
        // 初始化第三列（灵敏度），设为只读
        QTableWidgetItem *item3 = new QTableWidgetItem(mSettings.value(QString("Table/sensitivity%1").arg(row+1), "null").toString());
        item3->setTextAlignment(Qt::AlignCenter);
        item3->setFlags(item3->flags() & ~Qt::ItemIsEditable);
        ui->tableWidget->setItem(row, 2, item3);
    }
    ui->tableWidget->setItemDelegate(new PositiveNumberDelegate (this));
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}

NeutronYieldCalibration::~NeutronYieldCalibration()
{
    delete ui;
}

void NeutronYieldCalibration::on_pushButton_import_clicked()
{

}


void NeutronYieldCalibration::on_pushButton_export_clicked()
{

}

#include <QMessageBox>
void NeutronYieldCalibration::on_pushButton_save_clicked()
{
    QSettings mSettings("./config/NeutronYield.ini", QSettings::IniFormat);
    for (int row= 0; row<ui->tableWidget->rowCount(); ++row){
        mSettings.setValue(QString("Table/yield%1").arg(row+1), ui->tableWidget->item(row, 0)->text());
        mSettings.setValue(QString("Table/initial909Count%1").arg(row+1), ui->tableWidget->item(row, 1)->text());
        
        // 计算灵敏度（第一列 / 第二列），当第二列为0时显示"null"
        double yield = ui->tableWidget->item(row, 0)->text().toDouble();
        double initial909Count = ui->tableWidget->item(row, 1)->text().toDouble();
        QString sensitivityText;
        if (initial909Count == 0.0) {
            sensitivityText = "null";
        } else {
            double sensitivity = yield / initial909Count;
            sensitivityText = QString::number(sensitivity);
        }
        ui->tableWidget->item(row, 2)->setText(sensitivityText);
        mSettings.setValue(QString("Table/sensitivity%1").arg(row+1), ui->tableWidget->item(row, 2)->text());
    }

    // === 保存成功后，恢复所有单元格样式 ===
    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        for (int col = 0; col < ui->tableWidget->columnCount(); ++col) {
            QTableWidgetItem* item = ui->tableWidget->item(row, col);
            if (!item)
                continue;

            // 恢复字体（不加粗）
            QFont font = item->font();
            font.setBold(false);
            item->setFont(font);

            // 恢复为黑色字体
            item->setForeground(QBrush(Qt::black));
        }
    }

    QMessageBox::information(this, tr("提示"), "数据已保存。");
}


void NeutronYieldCalibration::on_pushButton_cancel_clicked()
{
    this->close();
}

QVector<QPair<double, double>> NeutronYieldCalibration::neutronYield()
{
    QVector<QPair<double, double>> result;
    QSettings mSettings("./config/NeutronYield.ini", QSettings::IniFormat);
    for (int row= 0; row<24; ++row){
        double yield = mSettings.value(QString("Table/yield%1").arg(row+1), "0.0").toDouble();
        double initial909Count = mSettings.value(QString("Table/initial909Count%1").arg(row+1), "0.0").toDouble();
        result.push_back(qMakePair(yield, initial909Count));
    }

    return result;
}
