#include "detsettingwindow.h"
#include "ui_detsettingwindow.h"
#include "qcomboboxdelegate.h"
#include "globalsettings.h"
#include <QStandardItemModel>
#include <QMessageBox>

DetSettingWindow::DetSettingWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DetSettingWindow)
{
    ui->setupUi(this);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    ui->tableWidget->setColumnWidth(1, 200);
    ui->tableWidget->setRowHeight(0, 30);

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

    QRegExp regExp(R"(^((25[0-5]|2[0-4]\d|1\d{2}|[1-9]\d|\d)\.){3}(25[0-5]|2[0-4]\d|1\d{2}|[1-9]\d|\d)$)");
    regExp.setPatternSyntax(QRegExp::RegExp2); // 启用Perl兼容语法（支持\d等元字符.exactMatch(str);）
    ui->lineEdit_timerSrvIp->setValidator(new QRegExpValidator(regExp, ui->lineEdit_timerSrvIp));

    connect(this, &DetSettingWindow::disconnectPeerConnection, this, [=](QString peerAddress, quint16 peerPort){
        delegate->removeItem(QString("%1:%2").arg(peerAddress).arg(peerPort));
    });

    /*connect(ui->tableWidget, &QTableWidget::currentItemChanged, this, [=](QTableWidgetItem *current, QTableWidgetItem *previous){
        // if (previous)
        //     saveAt(previous->row() + 1);
        if (current)
            loadAt(current->row() + 1);
    });*/

    connect(ui->tableWidget, &QTableWidget::cellChanged, this, [=](int row, int column){
        //防止IP关联重复
        if (column == 1){
            QString peerAddress = ui->tableWidget->item(row, 1)->text();
            for (int i = 0; i < ui->tableWidget->rowCount(); ++i){
                QString peerAddress2 = ui->tableWidget->item(i, 1)->text();
                if (peerAddress == peerAddress2 && i != row){
                    if (i < row){
                        ui->tableWidget->item(row, 1)->setText("0.0.0.0");
                    }
                    else {
                        ui->tableWidget->item(i, 1)->setText("0.0.0.0");
                    }
                }
            }
        }
    });
    
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

    for (int i = 1; i <= 24; ++i){
        int row = ui->tableWidget->rowCount();
        ui->tableWidget->insertRow(row);
        ui->tableWidget->setItem(row, 0, new QTableWidgetItem(QString("谱仪#%1").arg(i)));
        ui->tableWidget->item(row, 0)->setTextAlignment(Qt::AlignCenter);
        ui->tableWidget->item(row, 0)->setFlags(ui->tableWidget->item(row, 0)->flags() & (~Qt::ItemIsEditable));//禁止编辑

        ui->tableWidget->setItem(row, 1, new QTableWidgetItem(""));
        ui->tableWidget->item(row, 1)->setTextAlignment(Qt::AlignCenter);
    }

    // 初始化表格时：给每行第0列放checkbox
    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        auto *cb = new QCheckBox(ui->tableWidget);
        cb->setProperty("row", row); // 方便反查
        ui->tableWidget->setCellWidget(row, 0, cb);
        updateSelectAllState();

        connect(cb, &QCheckBox::stateChanged, this, [=](int){
            int currentRow = row +1;
            if(cb->isChecked()){
                //选中
                loadAt(currentRow);
            }

            updateSelectAllState();   // 见下方函数
        });
        cb->setChecked(true); // 初始化全勾选
    }

    //从HDF5加载配置信息
    HDF5Settings *settings = HDF5Settings::instance();
    QMap<quint8, DetParameter> detParameters = settings->detParameters();
    for (int i=1; i<=DET_NUM; ++i){
        DetParameter detParameter = detParameters[i];
        ui->tableWidget->item(i - 1, 1)->setText(detParameter.det_Ip_port);
    }

    ui->tableWidget->setCurrentItem(ui->tableWidget->item(0, 0));

    mCommhelper = CommHelper::instance();
    connect(mCommhelper, &CommHelper::reportParamterData, this, [=](int, const QByteArray& cachePool){
        //增益指令
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FA 10"))){
                qInfo().noquote() << "增益：" << cachePool.at(9);
            }
        }

        //死时间配置(ns)
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FA 11"))){
                qInfo().noquote() << "死时间/ns：" << cachePool.mid(8, 2).toShort();
            }
        }

        //触发阈值
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FA 12"))){
                qInfo().noquote() << "触发阈值：" << cachePool.mid(8, 2).toShort();
            }
        }

        /*********************************************************
         波形基本配置
        ***********************************************************/
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FC 10"))){
                qInfo().noquote() << "触发模式：" << (cachePool.mid(7, 1).toShort() == CommandAdapter::tmTimer ? "定时触发" : "正常触发模式");
                CommandAdapter::WaveformLength waveformLength = (CommandAdapter::WaveformLength)cachePool.mid(9, 1).toShort();
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
            }
        }

        /*********************************************************
         能谱基本配置
        ***********************************************************/
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FD 10"))){
                qInfo().noquote() << "能谱刷新时间/ms：" << cachePool.mid(6, 4).toUInt();

            }
        }

        /*********************************************************
         梯形成型基本配置
        ***********************************************************/
        //梯形成型时间常数配置
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FE 10"))){
                quint16 d1 = cachePool.mid(6, 2).toShort();
                quint16 d2 = cachePool.mid(8, 9).toShort();
                float f1 = (float)d1 / 65536;
                float f2 = (float)d2 / 65536;
                qInfo().noquote() << "梯形成型时间常数，d1=" << f1 << "，d2="<<f2;

            }
        }

        //上升沿、平顶、下降沿长度配置
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FE 11"))){
                quint8 rise = (quint8)cachePool.at(7);
                quint8 peak = (quint8)cachePool.at(8);
                quint8 fall = (quint8)cachePool.at(9);
                qInfo().noquote() << "上升沿=" << rise << "，平顶="<<peak<< "，下降沿="<<fall;

            }
        }

        //梯形成型使能配置
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A FE 12"))){
                quint8 rise = (quint8)cachePool.at(7);
                quint8 peak = (quint8)cachePool.at(8);
                quint8 fall = (quint8)cachePool.at(9);
                qInfo().noquote() << "梯形成型使能状态：" << ((quint8)cachePool.at(9) == 0x00 ? "关闭" : "打开");

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
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A F9 10"))){
                CommandAdapter::HighVolgateOutLevelEnable highVoltageEnable = (CommandAdapter::HighVolgateOutLevelEnable)cachePool.at(9);
                qInfo().noquote() << "高压使能状态：" << (highVoltageEnable ? "关闭" : "打开");

            }
        }

        //DAC输出电平配置
        {
            if (cachePool.startsWith(QByteArray::fromHex("12 34 00 0A F9 11"))){
                quint16 level = cachePool.mid(8, 2).toShort();
                qInfo().noquote() << "DAC输出电平值：" << level;

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
    ui->scrollArea->setVisible(checkedCount > 0);
    ui->stackedWidget->setCurrentIndex(checkedCount > 0 ? 1 : 0);

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

void DetSettingWindow::on_pushButton_save_clicked()
{
    QString savedDetectors;
    //对选中的部分保存参数
    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        if (auto *cb = qobject_cast<QCheckBox*>(ui->tableWidget->cellWidget(row, 0))) {
            if (cb->isChecked()) {
                saveAt(row + 1);
                savedDetectors += QString("#%1, ").arg(row + 1);
            }
        }
    }

    //弹出提示，提示哪些通道号的设置已保存成功
    QMessageBox::information(this, tr("保存参数设置"), tr("已保存成功！\n已保存的谱仪编号：%1").arg(savedDetectors));

    // 更改探测器的数据处理器与IP的绑定关系，以便新IP生效
    QMetaObject::invokeMethod(this, "settingfinished", Qt::QueuedConnection);
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
    QMap<quint8, DetParameter>& detParameters = settings->detParameters();
    DetParameter& detParameter = detParameters[detId];

    detParameter.pluseCheckTime = static_cast<quint32>(ui->spinBox_pluseCheckTime->value());

    //时间服务器
    //IP地址
    memset(detParameter.timerSrvIp, 0, IP_LENGTH);
    strcpy(detParameter.timerSrvIp, ui->lineEdit_timerSrvIp->text().toStdString().c_str());

    //基本设置
    //增益，获取文本转double
    detParameter.gain = ui->comboBox_gain->currentText().toDouble();
    //死时间
    detParameter.deathTime = static_cast<quint8>(ui->spinBox_deathTime->value());
    //触发阈值
    detParameter.triggerThold = static_cast<quint16>(ui->spinBox_triggerThold->value());

    // //网络设置
    //IP地址
    QAbstractItemModel *model = ui->tableWidget->model();
    memset(detParameter.det_Ip_port, 0, IP_LENGTH);
    QString det_Ip_port = model->data(model->index(detId - 1, 1)).toString();
    strcpy(detParameter.det_Ip_port, det_Ip_port.toStdString().c_str());

    //能谱设置
    //能谱刷新时间（毫秒）
    detParameter.spectrumRefreshTime = static_cast<quint32>(ui->spinBox_spectrumRefreshTime->value());
    //能谱长度
    detParameter.spectrumLength = static_cast<quint32>(ui->comboBox_spectrumLength->currentText().toUInt());

    //波形设置
    //触发模式
    detParameter.waveformTriggerMode = static_cast<quint8>(ui->comboBox_waveformTriggerMode->currentIndex());
    //波形长度
    detParameter.waveformLength = static_cast<quint32>(ui->comboBox_waveformLength->currentText().toUInt());

    //梯形成型
    //是否启用
    detParameter.trapShapeEnable = ui->trapShapeEnable->isChecked();
    //时间常数D1
    detParameter.trapShapeTimeConstD1 = static_cast<quint16>(ui->spinBox_trapShapeTimeConstD1->value());
    //时间常数D2
    detParameter.trapShapeTimeConstD2 = static_cast<quint16>(ui->spinBox_trapShapeTimeConstD2->value());
    //上升沿
    detParameter.trapShapeRisePoint = static_cast<quint8>(ui->spinBox_trapShapeRisePoint->value());
    //平顶
    detParameter.trapShapePeakPoint = static_cast<quint8>(ui->spinBox_trapShapePeakPoint->value());
    //下降沿
    detParameter.trapShapeFallPoint = static_cast<quint8>(ui->spinBox_trapShapeFallPoint->value());
    //高压电源
    //是否启用
    detParameter.highVoltageEnable = ui->highVoltageEnable->isChecked();
    //输出高压，单位：V
    detParameter.highVoltageOutLevel = static_cast<quint16>(ui->spinBox_highVoltageOutLevel->value());

    settings->sync();
}

void DetSettingWindow::loadAt(quint8 detId)
{
    //从HDF5加载配置信息
    HDF5Settings *settings = HDF5Settings::instance();
    QMap<quint8, DetParameter>& detParameters = settings->detParameters();
    DetParameter& detParameter = detParameters[detId];

    //心跳指令间隔
    ui->spinBox_pluseCheckTime->setValue(detParameter.pluseCheckTime);

    //时间服务器
    //IP地址
    ui->lineEdit_timerSrvIp->setText(QString::fromStdString(detParameter.timerSrvIp));

    //基本设置
    //增益, 获取文本转double
    ui->comboBox_gain->setCurrentText(QString::number(detParameter.gain));

    //死时间
    ui->spinBox_deathTime->setValue(detParameter.deathTime);

    //触发阈值
    ui->spinBox_triggerThold->setValue(detParameter.triggerThold);

    //网络设置
    //IP地址

    //能谱设置
    //能谱刷新时间（毫秒）
    ui->spinBox_spectrumRefreshTime->setValue(detParameter.spectrumRefreshTime);
    //能谱长度
    ui->comboBox_spectrumLength->setCurrentText(QString::number(detParameter.spectrumLength));

    //波形设置
    //触发模式
    ui->comboBox_waveformTriggerMode->setCurrentIndex(detParameter.waveformTriggerMode);
    //波形长度
    ui->comboBox_waveformLength->setCurrentText(QString::number(detParameter.waveformLength));

    //梯形成型
    //是否启用
    ui->trapShapeEnable->setChecked(detParameter.trapShapeEnable);
    //时间常数D1
    ui->spinBox_trapShapeTimeConstD1->setValue(detParameter.trapShapeTimeConstD1);
    //时间常数D2
    ui->spinBox_trapShapeTimeConstD2->setValue(detParameter.trapShapeTimeConstD2);
    //上升沿
    ui->spinBox_trapShapeRisePoint->setValue(detParameter.trapShapeRisePoint);
    //平顶
    ui->spinBox_trapShapePeakPoint->setValue(detParameter.trapShapePeakPoint);
    //下降沿
    ui->spinBox_trapShapeFallPoint->setValue(detParameter.trapShapeFallPoint);

    //高压电源
    //是否启用
    ui->highVoltageEnable->setChecked(detParameter.highVoltageEnable);
    //DAC高压输出电平
    ui->spinBox_highVoltageOutLevel->setValue(detParameter.highVoltageOutLevel);
}

void DetSettingWindow::on_pushButton_clicked()
{

}

