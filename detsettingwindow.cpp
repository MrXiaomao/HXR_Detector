#include "detsettingwindow.h"
#include "ui_detsettingwindow.h"
#include "commhelper.h"
#include "qcomboboxdelegate.h"
#include "globalsettings.h"
#include "detparamlogger.h"
#include "useractionlogger.h"
#include <QStandardItemModel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QTimer>

namespace {
constexpr int kSpectrumRefreshTimeSpinMax = 10 * 1000; // 10 s，单位 ms（协议字段为 4 字节 quint32）

int spectrumRefreshTimeToSpinValue(quint32 ms)
{
    if (ms < 1)
        ms = 1;
    return static_cast<int>(qMin<quint32>(ms, static_cast<quint32>(kSpectrumRefreshTimeSpinMax)));
}
} // namespace

DetSettingWindow::DetSettingWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DetSettingWindow)
{
    ui->setupUi(this);

    ui->spinBox_spectrumRefreshTime->setMinimum(1);
    ui->spinBox_spectrumRefreshTime->setMaximum(kSpectrumRefreshTimeSpinMax);

    // 设置每个列宽都可拉伸
    QHeaderView* header = ui->tableWidget->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);

    // 设置初始行高/列宽
    ui->tableWidget->setRowHeight(0, 30); //行高
    ui->tableWidget->setColumnWidth(0, 80);// 名称
    ui->tableWidget->setColumnWidth(1, 120);// ip
    ui->tableWidget->setColumnWidth(2, 100);// 增益
    ui->tableWidget->setColumnWidth(3, 100);// 阈值
    ui->tableWidget->setColumnWidth(4, 100);// 死时间
    ui->tableWidget->setColumnWidth(5, 100);// 高压

    QList<QSpinBox*> spinBoxs = this->findChildren<QSpinBox*>();
    for (auto spinBox: spinBoxs){
        spinBox->installEventFilter(this);
    }

    auto clampAlphaGammaSpinBox = [](QSpinBox *spinBox) {
        bool ok = false;
        int v = spinBox->text().trimmed().toInt(&ok);
        if (!ok)
            v = spinBox->minimum();
        spinBox->setValue(qBound(spinBox->minimum(), v, spinBox->maximum()));
    };
    connect(ui->spinBox_alphaGammaUpperThold, &QSpinBox::editingFinished, this, [=]() {
        clampAlphaGammaSpinBox(ui->spinBox_alphaGammaUpperThold);
    });
    connect(ui->spinBox_alphaGammaLowerThold, &QSpinBox::editingFinished, this, [=]() {
        clampAlphaGammaSpinBox(ui->spinBox_alphaGammaLowerThold);
    });

    QList<QComboBox*> comboBoxs = this->findChildren<QComboBox*>();
    for (auto comboBox: comboBoxs){
        comboBox->installEventFilter(this);
    }

    // 创建模型和视图
    // QStandardItemModel model(24, 2);
    // for (int i = 0; i < model.rowCount(); ++i) {
    //     model.setData(model.index(i, 0), QString("谱仪#%1").arg(i));
    //     model.setData(model.index(i, 1), "0.0.0.0");
    // }
    // ui->tableWidget->setModel(&model);

    QComboBoxDelegate *delegate = new QComboBoxDelegate(this);
    delegate->insertItem(QString("0.0.0.0"));
    ui->tableWidget->setItemDelegateForColumn(1, delegate); // 将委托应用到特定列
    connect(this, &DetSettingWindow::connectPeerConnection, this, [=](QString peerAddress, quint16 peerPort){
        delegate->insertItem(QString("%1").arg(peerAddress));
    });

    connect(this, &DetSettingWindow::disconnectPeerConnection, this, [=](QString peerAddress, quint16 peerPort){
        delegate->removeItem(QString("%1:%2").arg(peerAddress).arg(peerPort));
    });

    ui->tableWidget->setItemDelegate(
        new DetParamDelegate(ui->tableWidget));
    /*connect(ui->tableWidget, &QTableWidget::currentItemChanged, this, [=](QTableWidgetItem *current, QTableWidgetItem *previous){
        // if (previous)
        //     saveAt(previous->row() + 1);
        if (current)
            loadAt(current->row() + 1);
    });*/
    
    connect(ui->checkBox_selectAll, &QCheckBox::stateChanged, this, [=](int state){
        if (mUpdatingSelectAll) return;

        bool checked = (state == Qt::Checked);

        // 避免逐个触发导致反复更新
        mUpdatingSelectAll = true;
        for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
            if (auto *cb = qobject_cast<QCheckBox*>(ui->tableWidget->cellWidget(row, 0))) {
                cb->setChecked(checked);
            }
        }
        mUpdatingSelectAll = false;

        updateSelectAllState();
    });

    for (int i = 1; i <= DET_NUM; ++i){
        int row = ui->tableWidget->rowCount();
        ui->tableWidget->insertRow(row);
        ui->tableWidget->setItem(row, 0, new QTableWidgetItem(QString("谱仪#%1").arg(i)));
        ui->tableWidget->item(row, 0)->setTextAlignment(Qt::AlignCenter);
        ui->tableWidget->item(row, 0)->setFlags(ui->tableWidget->item(row, 0)->flags() & (~Qt::ItemIsEditable));//禁止编辑

        for (int j=1; j<=5; ++j){
            ui->tableWidget->setItem(row, j, new QTableWidgetItem(""));
            ui->tableWidget->item(row, j)->setTextAlignment(Qt::AlignCenter);
        }
    }

    mBlockParameterLogging = true;
    // 初始化表格时：给每行第0列放checkbox
    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        auto *cb = new QCheckBox(ui->tableWidget);
        cb->setProperty("row", row); // 方便反查
        ui->tableWidget->setCellWidget(row, 0, cb);
        updateSelectAllState();

        connect(cb, &QCheckBox::stateChanged, this, [=](int){
            if (mUpdatingSelectAll)
                return;
            const quint8 detId = static_cast<quint8>(row + 1);
            if (cb->isChecked())
                loadAt(detId, true);
            updateSelectAllState();
        });
        cb->blockSignals(true);
        cb->setChecked(true);
        cb->blockSignals(false);
        loadAt(static_cast<quint8>(row + 1), false);
    }

    //从HDF5加载配置信息
    HDF5Settings *settings = HDF5Settings::instance();
    const QMap<quint8, DetParameter*> detParameters = settings->getDetParameters();
    for (int i=1; i<=DET_NUM; ++i){
        DetParameter *detParameter = detParameters[i];
        ui->tableWidget->item(i - 1, 1)->setText(detParameter->det_Ip_port);
        ui->tableWidget->item(i - 1, 2)->setText(QString::number(detParameter->gain));
        ui->tableWidget->item(i - 1, 3)->setText(QString::number(detParameter->triggerThold));
        ui->tableWidget->item(i - 1, 4)->setText(QString::number(detParameter->deathTime));
        ui->tableWidget->item(i - 1, 5)->setText(QString::number(detParameter->highVoltageOutLevel));
        ui->tableWidget->item(i - 1, 5)->setCheckState(detParameter->highVoltageEnable ? Qt::Checked : Qt::Unchecked);
    }

    ui->tableWidget->setCurrentItem(ui->tableWidget->item(0, 0));

    mCommhelper = CommHelper::instance();
    QStringList gainList;
    gainList << QStringLiteral("0.08");
    gainList << QStringLiteral("0.16");
    gainList << QStringLiteral("0.32");
    gainList << QStringLiteral("0.63");
    gainList << QStringLiteral("1.26");
    gainList << QStringLiteral("2.52");
    gainList << QStringLiteral("5.01");
    gainList << QStringLiteral("10.0");
    QVector<quint32> waveformlengthList;
    waveformlengthList << 64;
    waveformlengthList << 128;
    waveformlengthList << 256;
    waveformlengthList << 512;

    connect(mCommhelper, &CommHelper::reportParamterData, this, [=](int, const QByteArray& cachePool){
        qInfo().noquote() << "收到参数配置数据包：" << cachePool.toHex(' ');

        QList<QTableWidgetItem*> items = ui->tableWidget->selectedItems();
        if (items.size() <= 0)
            return;

        quint8 row = items.first()->row();
        QAbstractItemModel *model = ui->tableWidget->model();
        const QMap<quint8, DetParameter*> detParameters = settings->getDetParameters();
        const DetParameter *detParameter = detParameters.value(row + 1);

        bool ok = false;
        //增益指令
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FA 10"))){
                if (gainList.size()>=(quint8)cachePool.at(9)){
                    qInfo().noquote() << "增益：" << gainList[(quint8)cachePool.at(9) - 1];
                    //ui->comboBox_gain->setCurrentIndex((quint8)cachePool.at(9) - 1);
                    model->setData(model->index(row, 2), gainList[(quint8)cachePool.at(9) - 1]);

                    double gain = gainList[(quint8)cachePool.at(9) - 1].toDouble();
                    if (queryMsg.isEmpty() && !qFuzzyCompare(gain, detParameter->gain)){
                        queryMsg = "增益设置失败。";
                    }
                }
                else{
                    queryMsg = "增益值与本地设置不一致。";
                    qInfo().noquote() << "增益索引：" << (quint8)cachePool.at(9);
                }
            }
        }

        //死时间配置(ns)
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FA 11"))){
                quint8 deathTime = cachePool.mid(8, 2).toHex().toUShort(&ok, 16);
                qInfo().noquote() << "死时间/*10ns：" << deathTime;
                //ui->spinBox_deathTime->setValue(deathTime);
                model->setData(model->index(row, 4), QString::number(deathTime));

                if (queryMsg.isEmpty() && (deathTime != detParameter->deathTime)){
                    queryMsg = "死时间与本地设置不一致。";
                }
            }
        }

        //触发阈值
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FA 12"))){
                quint16 triggerThold = cachePool.mid(8, 2).toHex().toUShort(&ok, 16);
                qInfo().noquote() << "触发阈值：" << triggerThold;
                //ui->spinBox_triggerThold->setValue(triggerThold);
                model->setData(model->index(row, 3), QString::number(triggerThold));

                if (queryMsg.isEmpty() && (triggerThold != detParameter->triggerThold)){
                    queryMsg = "触发阈值与本地设置不一致。";
                }
            }
        }

        /*********************************************************
         波形基本配置
        ***********************************************************/
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FC 10"))){
                quint8 triggerMode = (quint8)cachePool.mid(7, 1).toHex().toUShort(&ok, 16);
                qInfo().noquote() << "触发模式：" << ((triggerMode == CommandAdapter::tmTimer) ? "定时触发" : "正常触发模式");
                CommandAdapter::WaveformLength waveformLength = (CommandAdapter::WaveformLength)((quint8)cachePool.mid(9, 1).toHex().toUShort(&ok, 16));
                if (waveformLength == CommandAdapter::wl64)
                    qInfo().noquote() << "波形长度：64";
                else if (waveformLength == CommandAdapter::wl128)
                    qInfo().noquote() << "波形长度：128";
                else if (waveformLength == CommandAdapter::wl256)
                    qInfo().noquote() << "波形长度：256";
                else if (waveformLength == CommandAdapter::wl512)
                    qInfo().noquote() << "波形长度：512";
                else
                    qInfo().noquote() << "波形长度：未知值";

                ui->comboBox_waveformLength->setCurrentIndex(waveformLength);

                if (queryMsg.isEmpty() && (waveformlengthList[waveformLength] != detParameter->waveformLength)){
                    queryMsg = "波形长度与本地设置不一致。";
                }
            }
        }

        /*********************************************************
         能谱基本配置
        ***********************************************************/
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FD 10"))){
                quint32 spectrumRefreshTime = cachePool.mid(6, 4).toHex().toUInt(&ok, 16);
                qInfo().noquote() << "能谱刷新时间/ms：" << spectrumRefreshTime;
                ui->spinBox_spectrumRefreshTime->setValue(spectrumRefreshTimeToSpinValue(spectrumRefreshTime));

                if (queryMsg.isEmpty() && (spectrumRefreshTime != detParameter->spectrumRefreshTime)){
                    queryMsg = "能谱刷新时间与本地设置不一致。";
                }
            }
        }

        /*********************************************************
         梯形成型基本配置
        ***********************************************************/
        //梯形成型时间常数配置
        /*{
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FE 10"))){
                quint16 d1 = cachePool.mid(6, 2).toHex().toUShort(&ok, 16);
                quint16 d2 = cachePool.mid(8, 2).toHex().toUShort(&ok, 16);
                qInfo().noquote() << "梯形成型时间常数，d1=" << d1 << "，d2="<<d2;
                ui->sb_trapShapeTimeConstD1->setValue(d1);
                ui->sb_trapShapeTimeConstD2->setValue(d2);
                if (queryMsg.isEmpty() && (qAbs(d1-detParameter->trapShapeTimeConstD1)>=5 || qAbs(d2-detParameter->trapShapeTimeConstD2)>=5)){
                    queryMsg = "梯形成型时间常数与本地设置不一致。";
                }
            }
        }*/

        //上升沿、平顶、下降沿长度配置
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FE 11"))){
                quint8 rise = (quint8)cachePool.at(7);
                quint8 peak = (quint8)cachePool.at(8);
                quint8 fall = (quint8)cachePool.at(9);
                qInfo().noquote() << "上升沿=" << rise << "，平顶="<<peak<< "，下降沿="<<fall;
                // ui->spinBox_trapShapeRisePoint->setValue(rise);
                // ui->spinBox_trapShapePeakPoint->setValue(peak);
                // ui->spinBox_trapShapeFallPoint->setValue(fall);

                if (queryMsg.isEmpty() && (rise != detParameter->trapShapeRisePoint || peak != detParameter->trapShapePeakPoint || fall != detParameter->trapShapeFallPoint)){
                    queryMsg = "上升沿、平顶、下降沿长度与本地设置不一致。";
                }
            }
        }

        //梯形成型使能配置
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FE 12"))){
                bool trapShapeEnable = (quint8)cachePool.at(9) == 0x01;
                qInfo().noquote() << "梯形成型使能状态：" << (trapShapeEnable ? "打开" : "关闭");
                ui->trapShapeEnable->setChecked(trapShapeEnable);

                if (queryMsg.isEmpty() && (trapShapeEnable != detParameter->trapShapeEnable)){
                    queryMsg = "梯形成型使能状态设与本地设置不一致。";
                }
            }
        }

        /*********************************************************
         α/γ甄别阈值配置
        ***********************************************************/
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A F7 10"))){
                quint16 upperThold = cachePool.mid(6, 2).toHex().toUShort(&ok, 16);
                quint16 lowerThold = cachePool.mid(8, 2).toHex().toUShort(&ok, 16);
                qInfo().noquote() << "α/γ甄别上阈值：" << upperThold << "，下阈值：" << lowerThold;
                ui->spinBox_alphaGammaUpperThold->setValue(upperThold);
                ui->spinBox_alphaGammaLowerThold->setValue(lowerThold);

                if (queryMsg.isEmpty() && (upperThold != detParameter->alphaGammaUpperThold || lowerThold != detParameter->alphaGammaLowerThold)){
                    queryMsg = "α/γ甄别阈值与本地设置不一致。";
                }
            }
        }

        /*********************************************************
         工作模式配置
        ***********************************************************/
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FF 10"))){
                CommandAdapter::WorkMode workMode = (CommandAdapter::WorkMode)cachePool.at(9);
                if (workMode == CommandAdapter::wmWaveform)
                    qInfo().noquote() << "工作模式：波形模式";
                else if (workMode == CommandAdapter::wmSpectrum)
                    qInfo().noquote() << "工作模式：能谱模式";
                else if (workMode == CommandAdapter::wmParticle)
                    qInfo().noquote() << "工作模式：粒子模式";
                else
                    qInfo().noquote() << "工作模式：未知";
            }
        }

        /*********************************************************
         高压电源配置
        ***********************************************************/
        //高压使能配置
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A F9 11"))){
                CommandAdapter::HighVolgateOutLevelEnable highVoltageEnable = (CommandAdapter::HighVolgateOutLevelEnable)(quint8)cachePool.at(9);
                qInfo().noquote() << "高压使能状态：" << (highVoltageEnable==CommandAdapter::HighVolgateOutLevelEnable::hvClose ? "关闭" : "打开");
                //ui->highVoltageEnable->setChecked(highVoltageEnable==CommandAdapter::HighVolgateOutLevelEnable::hvOpen);
                if ((quint8)cachePool.at(9) != 0x00)
                    model->setData(model->index(row, 5), Qt::Checked, Qt::CheckStateRole);
                else
                    model->setData(model->index(row, 5), Qt::Unchecked, Qt::CheckStateRole);

                if (queryMsg.isEmpty() && (highVoltageEnable != static_cast<CommandAdapter::HighVolgateOutLevelEnable>(detParameter->highVoltageEnable))){
                    queryMsg = "高压使能状态与本地设置不一致。";
                }

                // 高压使能配置是查询最后一条指令
                queryWaitEvent.quit();
            }
        }

        //DAC输出电平配置
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A F9 10"))){
                quint16 level = cachePool.mid(8, 2).toHex().toUShort(&ok, 16);
                quint16 highVolgateOutLevel = static_cast<quint16>(std::round(level*2.5*441.0/65536.0));
                qInfo().noquote() << "DAC输出电平值：" << highVolgateOutLevel << "V";
                //ui->spinBox_highVoltageOutLevel->setValue(highVolgateOutLevel);
                model->setData(model->index(row, 5), QString::number(highVolgateOutLevel));

                if (queryMsg.isEmpty() && (highVolgateOutLevel != detParameter->highVoltageOutLevel)){
                    queryMsg = "DAC输出电平值与本地设置不一致。";
                }
            }
        }

        /*********************************************************
         应答类指令
        ***********************************************************/
        //程序版本号查询
        if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A DA 10"))){
            //硬件版本号
            QString hardVersion = QString("%1.%2.%3").arg(cachePool.at(6)).arg(cachePool.at(7)).arg(cachePool.at(8));
            //测试版本标志位
            bool isTest = cachePool.at(9) == 0x01;

            qInfo().noquote() << "硬件版本号：" << hardVersion;
            qInfo().noquote() << "是否测试版本：" << (isTest ? "是" : "否");
        }
    });

    mBlockParameterLogging = false;
}

DetSettingWindow::~DetSettingWindow()
{
    disconnect(mCommhelper, nullptr, this, nullptr);
    delete ui;
}

void DetSettingWindow::updateSelectAllState()
{
    int total = ui->tableWidget->rowCount();
    int checkedCount = 0;

    for (int row = 0; row < total; ++row) {
        if (auto *cb = qobject_cast<QCheckBox*>(ui->tableWidget->cellWidget(row, 0))) {
            if (cb->isChecked()) checkedCount++;
        }
    }

    // ✅ 没有勾选任何框就隐藏，有勾选就显示
    // ui->scrollArea->setVisible(checkedCount > 0);
    // ui->stackedWidget->setCurrentIndex(checkedCount > 0 ? 1 : 0);

    mUpdatingSelectAll = true;
    if (checkedCount == 0) {
        ui->checkBox_selectAll->setTristate(false);
        ui->checkBox_selectAll->setCheckState(Qt::Unchecked);
    } else if (checkedCount == total) {
        ui->checkBox_selectAll->setTristate(false);
        ui->checkBox_selectAll->setCheckState(Qt::Checked);
    } else {
        ui->checkBox_selectAll->setTristate(true);
        ui->checkBox_selectAll->setCheckState(Qt::PartiallyChecked);
    }
    mUpdatingSelectAll = false;
}

quint8 DetSettingWindow::currentSelectedDetId() const
{
    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        if (auto *cb = qobject_cast<QCheckBox *>(ui->tableWidget->cellWidget(row, 0))) {
            if (cb->isChecked())
                return static_cast<quint8>(row + 1);
        }
    }
    return 1;
}

void DetSettingWindow::logHardwareParametersFromUi(const QString &reason, quint8 detId)
{
    if (mBlockParameterLogging)
        return;
    if (detId < 1 || detId > DET_NUM)
        return;

    DetParameter snapshot;
    snapshot.id = detId;

    const int row = detId - 1;
    QAbstractItemModel *model = ui->tableWidget->model();

    const QString detIp = model->data(model->index(row, 1)).toString();
    memset(snapshot.det_Ip_port, 0, IP_LENGTH);
    qstrncpy(snapshot.det_Ip_port, detIp.toLatin1().constData(), IP_LENGTH - 1);

    snapshot.gain = model->data(model->index(row, 2)).toDouble();
    snapshot.triggerThold = static_cast<quint16>(model->data(model->index(row, 3)).toUInt());
    snapshot.deathTime = static_cast<quint8>(model->data(model->index(row, 4)).toUInt());
    snapshot.highVoltageOutLevel = static_cast<quint16>(model->data(model->index(row, 5)).toUInt());
    snapshot.highVoltageEnable =
        static_cast<Qt::CheckState>(model->data(model->index(row, 5), Qt::CheckStateRole).toInt()) == Qt::Checked;

    snapshot.spectrumRefreshTime = static_cast<quint32>(ui->spinBox_spectrumRefreshTime->value());
    snapshot.spectrumLength = kFullSpectrumChannels;
    snapshot.waveformTriggerMode = static_cast<quint8>(ui->comboBox_waveformTriggerMode->currentIndex());
    snapshot.waveformLength = static_cast<quint32>(ui->comboBox_waveformLength->currentText().toUInt());
    snapshot.trapShapeEnable = ui->trapShapeEnable->isChecked();
    // snapshot.trapShapeTimeConstD1 = static_cast<quint16>(ui->sb_trapShapeTimeConstD1->value());
    // snapshot.trapShapeTimeConstD2 = static_cast<quint16>(ui->sb_trapShapeTimeConstD2->value());
    snapshot.trapShapeRisePoint = static_cast<quint8>(GlobalSettings(DEVICE_CONFIG_FILE).value("FPGA/RisePoint", 10).toUInt());//static_cast<quint8>(ui->spinBox_trapShapeRisePoint->value());
    snapshot.trapShapePeakPoint = static_cast<quint8>(GlobalSettings(DEVICE_CONFIG_FILE).value("FPGA/PeakPoint", 15).toUInt());//static_cast<quint8>(ui->spinBox_trapShapePeakPoint->value());
    snapshot.trapShapeFallPoint = static_cast<quint8>(GlobalSettings(DEVICE_CONFIG_FILE).value("FPGA/FallPoint", 10).toUInt());//static_cast<quint8>(ui->spinBox_trapShapeFallPoint->value());
    snapshot.alphaGammaUpperThold = static_cast<quint16>(ui->spinBox_alphaGammaUpperThold->value());
    snapshot.alphaGammaLowerThold = static_cast<quint16>(ui->spinBox_alphaGammaLowerThold->value());

    DetParamLogger::logDetParameters(reason + QStringLiteral(" (界面快照 det#%1)").arg(detId), &snapshot);
}

void DetSettingWindow::logHardwareParameters(const QString &reason, quint8 detId)
{
    const QMap<quint8, DetParameter *> detParameters = HDF5Settings::instance()->getDetParameters();
    const DetParameter *detParameter = detParameters.value(detId);
    if (!detParameter)
        return;
    DetParamLogger::logDetParameters(reason + QStringLiteral(" (已存配置 det#%1)").arg(detId), detParameter);
}

void DetSettingWindow::on_pushButton_save_clicked()
{
    /*quint16 d1 = ui->sb_trapShapeTimeConstD1->value();
    quint16 d2 = ui->sb_trapShapeTimeConstD2->value();
    if (ui->trapShapeEnable->isChecked() && d1 >= d2){
        QMessageBox::warning(this, tr("保存参数设置"), tr("梯形成型常量错误，d1必须小于d2"));
        return;
    }*/

    const quint16 upperThold = static_cast<quint16>(ui->spinBox_alphaGammaUpperThold->value());
    const quint16 lowerThold = static_cast<quint16>(ui->spinBox_alphaGammaLowerThold->value());
    if (upperThold <= lowerThold) {
        QMessageBox::warning(this, tr("保存参数设置"), tr("α/γ甄别阈值错误，上阈值必须大于下阈值。"));
        return;
    }

    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        if (auto *cb = qobject_cast<QCheckBox*>(ui->tableWidget->cellWidget(row, 0))) {
            if (!cb->isChecked())
                continue;
            if (QTableWidgetItem *tholdItem = ui->tableWidget->item(row, 3)) {
                bool ok = false;
                int v = tholdItem->text().trimmed().toInt(&ok);
                if (!ok)
                    v = 0;
                tholdItem->setText(QString::number(qBound(0, v, 65535)));
            }
        }
    }

    QString savedDetectors;
    bool hasOnlineSaved = false;
    //对选中的部分保存参数
    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        if (auto *cb = qobject_cast<QCheckBox*>(ui->tableWidget->cellWidget(row, 0))) {
            if (cb->isChecked()) {
                const quint8 detId = static_cast<quint8>(row + 1);
                saveAt(detId);
                savedDetectors += QString("#%1, ").arg(detId);
                if (mCommhelper->isDetectorConnected(detId))
                    hasOnlineSaved = true;
            }
        }
    }

    QProgressDialog configDialog(tr("参数配置中"), QString(), 0, 0, this);
    if (hasOnlineSaved) {
        configDialog.setWindowTitle(tr("参数配置中，请勿操作！"));
        configDialog.setWindowModality(Qt::WindowModal);
        configDialog.setMinimumDuration(0);
        configDialog.setCancelButton(nullptr);
        configDialog.setRange(0, 0);
        configDialog.show();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    // 联网探测器同步配置（settingfinished 在当前线程同步下发指令）
    emit settingfinished();

    if (hasOnlineSaved)
        configDialog.close();

    //弹出提示，提示哪些通道号的设置已保存成功
    QMessageBox::information(this, tr("保存参数设置"), tr("已保存成功！\n已保存的谱仪编号：%1").arg(savedDetectors));
}


void DetSettingWindow::on_pushButton_cancel_clicked()
{
    this->close();
}

void DetSettingWindow::setSaveEnabled(bool enabled)
{
    ui->pushButton_save->setEnabled(enabled);
}

// 通过setSaveEnabled控制保存按钮的可用性，防止测量过程中修改参数
void DetSettingWindow::saveAt(quint8 detId)
{
    //从HDF5加载配置信息
    HDF5Settings *settings = HDF5Settings::instance();
    QMap<quint8, DetParameter*> detParameters = settings->getDetParameters();
    DetParameter *detParameter = detParameters.value(detId);

    // 网络设置
    //IP地址
    QAbstractItemModel *model = ui->tableWidget->model();
    memset(detParameter->det_Ip_port, 0, IP_LENGTH);
    QString det_Ip_port = model->data(model->index(detId - 1, 1)).toString();
    strcpy(detParameter->det_Ip_port, det_Ip_port.toStdString().c_str());

    detParameter->id = detId;
    detParameter->gain = model->data(model->index(detId - 1, 2)).toDouble();
    detParameter->triggerThold = model->data(model->index(detId - 1, 3)).toDouble();
    detParameter->deathTime = model->data(model->index(detId - 1, 4)).toDouble();
    detParameter->highVoltageOutLevel = model->data(model->index(detId - 1, 5)).toDouble();
    detParameter->highVoltageEnable = static_cast<Qt::CheckState>(model->data(model->index(detId - 1, 5), Qt::CheckStateRole).toInt()) == Qt::Checked;

    //能谱设置
    //能谱刷新时间（毫秒）
    detParameter->spectrumRefreshTime = static_cast<quint32>(ui->spinBox_spectrumRefreshTime->value());
    detParameter->spectrumLength = kFullSpectrumChannels;

    //波形设置
    //触发模式
    detParameter->waveformTriggerMode = static_cast<quint8>(ui->comboBox_waveformTriggerMode->currentIndex());
    //波形长度
    detParameter->waveformLength = static_cast<quint32>(ui->comboBox_waveformLength->currentText().toUInt());

    //梯形成型
    //是否启用
    detParameter->trapShapeEnable = ui->trapShapeEnable->isChecked();
    //时间常数D1
    // detParameter->trapShapeTimeConstD1 = static_cast<quint16>(ui->sb_trapShapeTimeConstD1->value());
    //时间常数D2
    // detParameter->trapShapeTimeConstD2 = static_cast<quint16>(ui->sb_trapShapeTimeConstD2->value());
    //上升沿
    detParameter->trapShapeRisePoint = static_cast<quint8>(GlobalSettings(DEVICE_CONFIG_FILE).value("FPGA/RisePoint").toUInt());//static_cast<quint8>(ui->spinBox_trapShapeRisePoint->value());
    //平顶
    detParameter->trapShapePeakPoint = static_cast<quint8>(GlobalSettings(DEVICE_CONFIG_FILE).value("FPGA/PeakPoint", 15).toUInt());//static_cast<quint8>(ui->spinBox_trapShapePeakPoint->value());
    //下降沿
    detParameter->trapShapeFallPoint = static_cast<quint8>(GlobalSettings(DEVICE_CONFIG_FILE).value("FPGA/FallPoint").toUInt());//static_cast<quint8>(ui->spinBox_trapShapeFallPoint->value());

    //α/γ甄别阈值
    detParameter->alphaGammaUpperThold = static_cast<quint16>(ui->spinBox_alphaGammaUpperThold->value());
    detParameter->alphaGammaLowerThold = static_cast<quint16>(ui->spinBox_alphaGammaLowerThold->value());

    detParameters[detId] = detParameter;
    settings->setDetParameter(detParameters);
    settings->export_LocalDetPara();

    UserActionLogger::logMessage(QStringLiteral("DetSetting 保存 det#%1 %2")
                                     .arg(detId)
                                     .arg(DetParamLogger::formatDetParameter(detParameter)));
}

void DetSettingWindow::loadAt(quint8 detId, bool recordLog)
{
    mBlockParameterLogging = true;
    //从HDF5加载配置信息
    HDF5Settings *settings = HDF5Settings::instance();
    const QMap<quint8, DetParameter*> detParameters = settings->getDetParameters();
    const DetParameter *detParameter = detParameters.value(detId);

    int row = detId - 1;
    QAbstractItemModel *model = ui->tableWidget->model();

    //基本设置
    //增益, 获取文本转double
    //ui->comboBox_gain->setCurrentText(QString::number(detParameter->gain));
    model->setData(model->index(row, 2), QString::number(detParameter->gain));

    //死时间
    //ui->spinBox_deathTime->setValue(detParameter->deathTime);
    model->setData(model->index(row, 4), QString::number(detParameter->deathTime));

    //触发阈值
    //ui->spinBox_triggerThold->setValue(detParameter->triggerThold);
    model->setData(model->index(row, 3), QString::number(detParameter->triggerThold));

    //网络设置
    //IP地址
    model->setData(model->index(row, 1), QString::fromLatin1(detParameter->det_Ip_port));

    //能谱设置
    //能谱刷新时间（毫秒）
    ui->spinBox_spectrumRefreshTime->setValue(spectrumRefreshTimeToSpinValue(detParameter->spectrumRefreshTime));

    //波形设置
    //触发模式
    ui->comboBox_waveformTriggerMode->setCurrentIndex(detParameter->waveformTriggerMode);
    //波形长度
    ui->comboBox_waveformLength->setCurrentText(QString::number(detParameter->waveformLength));

    //梯形成型
    //是否启用
    ui->trapShapeEnable->setChecked(detParameter->trapShapeEnable);
    //时间常数D1
    // ui->sb_trapShapeTimeConstD1->setValue(detParameter->trapShapeTimeConstD1);
    //时间常数D2
    // ui->sb_trapShapeTimeConstD2->setValue(detParameter->trapShapeTimeConstD2);
    //上升沿
    // ui->spinBox_trapShapeRisePoint->setValue(detParameter->trapShapeRisePoint);
    //平顶
    // ui->spinBox_trapShapePeakPoint->setValue(detParameter->trapShapePeakPoint);
    //下降沿
    // ui->spinBox_trapShapeFallPoint->setValue(detParameter->trapShapeFallPoint);

    //α/γ甄别阈值
    ui->spinBox_alphaGammaUpperThold->setValue(detParameter->alphaGammaUpperThold);
    ui->spinBox_alphaGammaLowerThold->setValue(detParameter->alphaGammaLowerThold);

    //高压电源
    //是否启用
    //ui->highVoltageEnable->setChecked(detParameter->highVoltageEnable);
    model->setData(model->index(row, 5), detParameter->highVoltageEnable ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);

    //DAC高压输出电平
    //ui->spinBox_highVoltageOutLevel->setValue(detParameter->highVoltageOutLevel);
    model->setData(model->index(row, 5), QString::number(detParameter->highVoltageOutLevel));

    mBlockParameterLogging = false;
    if (!recordLog)
        return;
}

void DetSettingWindow::on_pushButton_read_clicked()
{
    QList<QTableWidgetItem*> items = ui->tableWidget->selectedItems();
    if (items.size() <= 0)
        return;

    quint8 detId = items.first()->row() + 1;
    QTimer::singleShot(3000, this, [=](){
        queryWaitEvent.exit(-1);
    });
    queryMsg.clear();
    mCommhelper->readParamters(detId);
    if (queryWaitEvent.exec() == -1){
        QMessageBox::information(this, "查询参数", "查询参数超时！");
    } else if (queryMsg.isEmpty()){
        QMessageBox::information(this, "查询参数", "远程参数与本地配置信息一致！");
    } else {
        QMessageBox::information(this, "查询参数", queryMsg);
    }
}

bool DetSettingWindow::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::Wheel && (obj->inherits("QSpinBox")||obj->inherits("QComboBox"))) {
        return true; // 拦截滚轮事件
    }

    return QWidget::eventFilter(obj, event);
}

void DetSettingWindow::on_tableWidget_itemChanged(QTableWidgetItem *item)
{
    //防止IP关联重复
    if (item->column() == 1){
        int row = item->row();
        QString peerAddress = item->text();
        for (int i = 0; i < ui->tableWidget->rowCount(); ++i){
            QString peerAddress2 = ui->tableWidget->item(i, 1)->text();
            if (peerAddress == peerAddress2 && i != row){
                // if (i < row){
                //     ui->tableWidget->item(row, 1)->setText("0.0.0.0");
                // }
                // else {
                    ui->tableWidget->item(i, 1)->setText("0.0.0.0");
//                }
            }
        }
    }

    // 限制触发阈值输入范围（FA12：0000~FFFF），超出则钳制到边界值
    if (item->column() == 3) {
        bool ok = false;
        int v = item->text().trimmed().toInt(&ok);
        if (!ok)
            v = 0;
        const int clamped = qBound(0, v, 65535);
        if (item->text() != QString::number(clamped)) {
            ui->tableWidget->blockSignals(true);
            item->setText(QString::number(clamped));
            ui->tableWidget->blockSignals(false);
        }
    }
}

void DetSettingWindow::showEvent(QShowEvent */*event*/)
{
    mBlockParameterLogging = true;
    ui->pushButton_read->setEnabled(!mCommhelper->isMeasuring());
    ui->pushButton_save->setEnabled(!mCommhelper->isMeasuring());

    //从HDF5加载配置信息
    HDF5Settings *settings = HDF5Settings::instance();
    const QMap<quint8, DetParameter*> detParameters = settings->getDetParameters();
    for (int i=1; i<=DET_NUM; ++i){
        DetParameter *detParameter = detParameters[i];
        ui->tableWidget->item(i - 1, 1)->setText(detParameter->det_Ip_port);
        ui->tableWidget->item(i - 1, 2)->setText(QString::number(detParameter->gain));
        ui->tableWidget->item(i - 1, 3)->setText(QString::number(detParameter->triggerThold));
        ui->tableWidget->item(i - 1, 4)->setText(QString::number(detParameter->deathTime));
        ui->tableWidget->item(i - 1, 5)->setText(QString::number(detParameter->highVoltageOutLevel));
        ui->tableWidget->item(i - 1, 5)->setCheckState(detParameter->highVoltageEnable ? Qt::Checked : Qt::Unchecked);
    }

    loadAt(currentSelectedDetId(), false);

    mBlockParameterLogging = false;
}
