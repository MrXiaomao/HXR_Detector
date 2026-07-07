/*
 * @Author: MrPan
 * @Date: 2025-11-13 11:36:00
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2026-03-11 14:41:44
 * @Description: 配置文件窗口，用来配置硬件参数、网络参数等
 */
#include "localsettingwindow.h"
#include "ui_localsettingwindow.h"
#include <QHostInfo>
#include <QNetworkInterface>
#include "globalsettings.h"
#include "commhelper.h"
#include "useractionlogger.h"

#include <QStyledItemDelegate>
#include <QApplication>
#include <QStyleOptionButton>
#include <QPainter>
#include <QButtonGroup>
#include <QTimer>

class CheckBoxDelegate : public QStyledItemDelegate {
protected:
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        // 先绘制背景
        bool allowEdit = index.data(Qt::UserRole).toBool();
        if (!allowEdit){
            QStyleOptionViewItem opt = option;
            initStyleOption(&opt, index);
            painter->fillRect(opt.rect, Qt::red);
        }

        // 绘制选框
        QStyleOptionButton checkBoxOption;
        QRect checkBoxRect = QApplication::style()->subElementRect(
            QStyle::SE_CheckBoxIndicator, &checkBoxOption, nullptr);

        // 计算居中位置
        checkBoxOption.rect = QRect(
            option.rect.center().x() - checkBoxRect.width()/2,
            option.rect.center().y() - checkBoxRect.height()/2,
            checkBoxRect.width(),
            checkBoxRect.height()
            );
        checkBoxOption.state = index.data(Qt::CheckStateRole) == Qt::Checked
                                   ? QStyle::State_On : QStyle::State_Off;

        QApplication::style()->drawControl(QStyle::CE_CheckBox, &checkBoxOption, painter);
    }

    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                                       const QStyleOptionViewItem &option, const QModelIndex &index) {
        // 1. 检查是否允许用户修改（通过UserRole标记）
        bool allowEdit = model->data(index, Qt::UserRole).toBool();
        if (!allowEdit) {
            return false; // 不允许则拦截事件
        }

        if (event->type() == QEvent::MouseButtonRelease) {
            // 切换勾选状态
            Qt::CheckState currentState = static_cast<Qt::CheckState>(model->data(index, Qt::CheckStateRole).toInt());
            model->setData(index, currentState == Qt::Checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
            return true; // 拦截事件，避免触发默认编辑
        }

        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }
};

LocalSettingWindow::LocalSettingWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LocalSettingWindow)
{
    ui->setupUi(this);

    commHelper = CommHelper::instance();

    //获取本机IP地址列表
    //获取主机名称
    QString HostName = QHostInfo::localHostName();
    //根据主机名获取主机IP地址
    QHostInfo hostinfo = QHostInfo::fromName(HostName);
    QList<QHostAddress> AddressList = hostinfo.addresses();
    if(!AddressList.isEmpty())
    {
        for(int i = 0;i<AddressList.size();i++)
        {
            QHostAddress hostaddress = AddressList.at(i);
            if(hostaddress.protocol() == QAbstractSocket::IPv4Protocol)
            {
                ui->comboBox_ip->addItem(hostaddress.toString());
            }
        }
    }

    if (ui->comboBox_ip->findText(QStringLiteral("127.0.0.1")) == -1)
        ui->comboBox_ip->addItem("127.0.0.1");
    ui->comboBox_ip->addItem("0.0.0.0");
    ui->spinBox->setMaximum(DET_NUM);

    GlobalSettings settings(DEVICE_CONFIG_FILE);
    //心跳检测
    ui->spinBox_pluseCheckTime->setValue(settings.value("Global/PluseTime", 10).toUInt());

    ui->comboBox_ip->setCurrentText(settings.value("Local/ServerIp", "0.0.0.0").toString());
    ui->spinBox_port->setValue(settings.value("Local/ServerPort", 6000).toUInt());

    ui->tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    ui->tableWidget->setColumnCount(DET_NUM);

    for (int column = 0; column < DET_NUM; ++column)
    {
        QTableWidgetItem *__qtablewidgetitem = new QTableWidgetItem(QString::number(column+1));
        ui->tableWidget->setHorizontalHeaderItem(column, __qtablewidgetitem);

        // 使用委托
        ui->tableWidget->setItemDelegateForColumn(column, new CheckBoxDelegate()); // 为第0列设置
        ui->tableWidget->setColumnWidth(column, 25);
        for (int row = 0; row < 3; ++row)
        {
            QTableWidgetItem* item = new QTableWidgetItem();
            item->setCheckState(Qt::CheckState::Unchecked);
            item->setTextAlignment(Qt::AlignCenter);
            ui->tableWidget->setItem(row, column, item);
            item->setData(Qt::UserRole, true);
        }
    }

    // 加载配置文件
    ui->lineEdit_switcherIp_1->setText(settings.value("Switcher/1/ip", "172.18.41.94").toString());
    ui->lineEdit_switcherIp_2->setText(settings.value("Switcher/2/ip").toString());
    ui->lineEdit_switcherIp_3->setText(settings.value("Switcher/3/ip").toString());
    QRegExp regExp(R"(^((25[0-5]|2[0-4]\d|1\d{2}|[1-9]\d|\d)\.){3}(25[0-5]|2[0-4]\d|1\d{2}|[1-9]\d|\d)$)");
    regExp.setPatternSyntax(QRegExp::RegExp2);
    ui->lineEdit_switcherIp_1->setValidator(new QRegExpValidator(regExp, ui->lineEdit_switcherIp_1));
    ui->lineEdit_switcherIp_2->setValidator(new QRegExpValidator(regExp, ui->lineEdit_switcherIp_2));
    ui->lineEdit_switcherIp_3->setValidator(new QRegExpValidator(regExp, ui->lineEdit_switcherIp_3));

    for (int row = 0; row < 3; ++row)
    {
        ui->tableWidget->blockSignals(true);
        QString S1 = settings.value(QString("Switcher/%1/detector").arg(row+1), "").toString();
        QStringList det1S = S1.split(",");
        for (auto& det : det1S)
        {
            int column = det.toInt();
            if (column >= 1 && column <= DET_NUM)
            {
                auto *item = ui->tableWidget->item(row, column - 1);
                if (item)
                {
                    item->setCheckState(Qt::Checked);
                }
            }
        }
        ui->tableWidget->blockSignals(false);
    }

    ui->label_tip->hide();
    ui->checkBox->setChecked(settings.value("Switcher/count").toInt() >= 2);
    ui->checkBox_2->setChecked(settings.value("Switcher/count").toInt() >= 3);
    ui->checkBox_2->setEnabled(settings.value("Switcher/count").toInt() >= 3);

    mSwitcher2Enabled = ui->checkBox->isChecked();
    mSwitcher3Enabled = ui->checkBox_2->isChecked();

    // 时钟同步模块
    {
        commHelper->mExtendTimeSynModule.reload();

        ui->radioButton->blockSignals(true);
        ui->radioButton_2->blockSignals(true);
        ui->radioButton_3->blockSignals(true);
        ui->lineEdit->setText(commHelper->mExtendTimeSynModule.ip);
        if (commHelper->mExtendTimeSynModule.switcherIndex == 1)
            ui->radioButton->setChecked(true);
        else if (commHelper->mExtendTimeSynModule.switcherIndex == 2)
            ui->radioButton_2->setChecked(true);
        else
            ui->radioButton_3->setChecked(true);
        ui->spinBox->setValue(commHelper->mExtendTimeSynModule.poeIndex);
        ui->radioButton->blockSignals(false);
        ui->radioButton_2->blockSignals(false);
        ui->radioButton_3->blockSignals(false);

        auto *item = ui->tableWidget->item(commHelper->mExtendTimeSynModule.switcherIndex-1, commHelper->mExtendTimeSynModule.poeIndex-1);
        if (item)
        {
            item->setCheckState(Qt::Checked);
            item->setData(Qt::UserRole, false);
            //item->setBackgroundColor(Qt::red);
        }        
    }

    QButtonGroup *grp = new QButtonGroup(this);
    grp->addButton(ui->radioButton, 0);
    grp->addButton(ui->radioButton_2, 1);
    grp->addButton(ui->radioButton_3, 2);

    connect(grp, &QButtonGroup::idToggled, this, [=](int id, bool checked){
        on_spinBox_valueChanged(ui->spinBox->value());
    });
    connect(ui->tableWidget, &QTableWidget::itemChanged, this, [=](QTableWidgetItem *item) {
        if (item->checkState() == Qt::Checked)
        {
            ui->tableWidget->blockSignals(true);

            for (int i = 0; i < ui->tableWidget->rowCount(); ++i)
            {
                if (i != item->row())
                {
                    if (ui->tableWidget->item(i, item->column())->data(Qt::UserRole).toBool())
                        ui->tableWidget->item(i, item->column())->setCheckState(Qt::Unchecked);
                }
                else
                {
                    ui->tableWidget->item(item->row(), item->column())->setCheckState(Qt::Checked);
                }
            }

            ui->tableWidget->blockSignals(false);
        }
        else {
            ui->tableWidget->item(item->row(), item->column())->setCheckState(Qt::Unchecked);
        }

        updateData();
    });
    connect(ui->comboBox_ip, &QComboBox::currentTextChanged, this, &LocalSettingWindow::updateData);
    connect(ui->spinBox_port, &QSpinBox::textChanged, this, &LocalSettingWindow::updateData);
    connect(ui->checkBox, &QCheckBox::stateChanged, this, &LocalSettingWindow::updateData);
    connect(ui->checkBox_2, &QCheckBox::stateChanged, this, &LocalSettingWindow::updateData);
    connect(ui->lineEdit_switcherIp_1, &QLineEdit::editingFinished, this, &LocalSettingWindow::updateData);
    connect(ui->lineEdit_switcherIp_2, &QLineEdit::editingFinished, this, &LocalSettingWindow::updateData);
    connect(ui->lineEdit_switcherIp_3, &QLineEdit::editingFinished, this, &LocalSettingWindow::updateData);

    ui->comboBox_ip->installEventFilter(this);
    ui->lineEdit->installEventFilter(this);
    ui->lineEdit_switcherIp_1->installEventFilter(this);
    ui->lineEdit_switcherIp_2->installEventFilter(this);
    ui->lineEdit_switcherIp_3->installEventFilter(this);
    ui->pushButton_ok->setEnabled(!commHelper->isOpen());
}

LocalSettingWindow::~LocalSettingWindow()
{
    delete ui;
}

#include <QMessageBox>
bool LocalSettingWindow::eventFilter(QObject *watched, QEvent *event) {
    // 1. 过滤目标 QLineEdit 控件
    QLineEdit* ip_edit = nullptr;
    if (watched == ui->comboBox_ip){
        QComboBox *comboBox = static_cast<QComboBox *>(watched);
        ip_edit = comboBox->lineEdit();
    }
    else{
        ip_edit = qobject_cast<QLineEdit*>(watched);
    }

    if (!ip_edit || !(watched == ui->lineEdit || watched == ui->lineEdit_switcherIp_1 ||
                      watched == ui->lineEdit_switcherIp_2 || watched == ui->lineEdit_switcherIp_3 || watched == ui->comboBox_ip)) {
        return QWidget::eventFilter(watched, event);
    }

    // 2. 监听焦点失去事件（FocusOut）
    if (event->type() == QEvent::FocusOut) {
        QRegExp ip_port_regex(R"(^((25[0-5]|2[0-4]\d|1\d{2}|[1-9]\d|\d)\.){3}(25[0-5]|2[0-4]\d|1\d{2}|[1-9]\d|\d)?$)");
        if (!ip_port_regex.exactMatch(ip_edit->text())) {
            QMessageBox::warning(this, "错误", "请输入合法的 IP 格式！\r\n如 192.168.1.1");
            
            // 延迟恢复焦点（加延时，防止陷入死循环）
            QTimer::singleShot(0, ip_edit, [ip_edit](){
                ip_edit->setFocus();
            });
            return true; // 拦截事件，阻止焦点转移
        }
    }

    return QWidget::eventFilter(watched, event);
}


void LocalSettingWindow::updateData()
{
    if (commHelper->isOpen())
        return ;

    if (!ui->checkBox->isChecked()){
        ui->checkBox_2->setChecked(false);
    }
    ui->checkBox_2->setEnabled(ui->checkBox->isChecked());

    ui->radioButton_2->setEnabled(ui->checkBox->isChecked());
    ui->radioButton_3->setEnabled(ui->checkBox_2->isChecked());

    if (!ui->radioButton_3->isEnabled() && ui->radioButton_3->isChecked())
    {
        if (ui->radioButton_2->isEnabled())
            ui->radioButton_2->setChecked(true);
        else
            ui->radioButton->setChecked(true);
    }
    if (!ui->radioButton_2->isEnabled() && ui->radioButton_2->isChecked())
    {
        ui->radioButton->setChecked(true);
    }

    if (mSwitcher2Enabled != ui->checkBox->isChecked() || mSwitcher3Enabled != ui->checkBox_2->isChecked())
    {
        ui->label_tip->show();
    }
    else{
        ui->label_tip->hide();
    }

    GlobalSettings settings(DEVICE_CONFIG_FILE);
    //心跳检测
    settings.setValue("Global/PluseTime", ui->spinBox_pluseCheckTime->value());
    HDF5Settings::instance()->setPluseTime(ui->spinBox_pluseCheckTime->value());

    //本地服务配置
    settings.setValue("Local/ServerIp", ui->comboBox_ip->currentText());
    settings.setValue("Local/ServerPort", ui->spinBox_port->text());

    //交换机Telnet配置
    settings.setValue("Switcher/count", ui->checkBox->isChecked() ? (ui->checkBox_2->isChecked() ? 3 : 2) : 1);
    QCheckBox* checkBoxs[] = {nullptr, ui->checkBox, ui->checkBox_2};
    for (auto checkBox : checkBoxs)
    {
        int index = (checkBox == nullptr) ? 1 : ((checkBox == ui->checkBox) ? 2 : 3);
        QLineEdit* lineEdit = this->findChild<QLineEdit*>(QString("lineEdit_switcherIp_%1").arg(index));
        if (lineEdit)
        {
            settings.setValue(QString("Switcher/%1/ip").arg(index), lineEdit->text());
        }

        QString dets;
        for (int column = 0; column < DET_NUM; ++column)
        {
            auto *item = ui->tableWidget->item(index-1, column);
            if (item)
            {
                if (item->checkState() == Qt::Checked && item->data(Qt::UserRole).toBool())
                {
                    dets += QString("%1,").arg(column+1);
                }
            }

            settings.setValue(QString("Switcher/%1/detector").arg(index), dets);
        }
    }

    // 时钟同步模块
    settings.setValue("TimeSynModule/ip", ui->lineEdit->text());
    settings.setValue("TimeSynModule/poeIndex", ui->spinBox->value());
    settings.setValue("TimeSynModule/switcherIndex", ui->radioButton->isChecked() ? 1 : (ui->radioButton_2->isChecked() ? 2: 3));
}

#include <QMessageBox>
void LocalSettingWindow::on_pushButton_ok_clicked()
{
    if (ui->lineEdit_switcherIp_1->text().isEmpty())
    {
        QMessageBox::information(this, tr("提示"), tr("交换机#1的IP地址不能为空！"));
        return;
    }
    if (ui->checkBox->isChecked() && ui->lineEdit_switcherIp_2->text().isEmpty())
    {
        QMessageBox::information(this, tr("提示"), tr("交换机#2的IP地址不能为空！"));
        return;
    }
    if (ui->checkBox_2->isChecked() && ui->lineEdit_switcherIp_3->text().isEmpty())
    {
        QMessageBox::information(this, tr("提示"), tr("交换机#3的IP地址不能为空！"));
        return;
    }

    updateData();
    UserActionLogger::logMessage(QStringLiteral("保存网络配置 本地服务 ip=%1 port=%2 交换机1=%3")
                                     .arg(ui->comboBox_ip->currentText())
                                     .arg(ui->spinBox_port->value())
                                     .arg(ui->lineEdit_switcherIp_1->text()));
    this->close();
}


void LocalSettingWindow::on_pushButton_cancel_clicked()
{
    this->close();
}


void LocalSettingWindow::on_spinBox_valueChanged(int arg1)
{
    CommHelper* commHelper = CommHelper::instance();
    commHelper->mExtendTimeSynModule.reload();
    int poeIndex = commHelper->mExtendTimeSynModule.poeIndex;
    int switcherIndex = commHelper->mExtendTimeSynModule.switcherIndex;
    auto *item = ui->tableWidget->item(switcherIndex-1, poeIndex-1);
    if (item)
    {
        item->setCheckState(Qt::Unchecked);
        item->setData(Qt::UserRole, true);
    }

    int index = ui->radioButton->isChecked() ? 1 : (ui->radioButton_2->isChecked() ? 2 : 3);
    int column = arg1 - 1;
    item = ui->tableWidget->item(index-1, column);
    if (item)
    {
        item->setCheckState(Qt::Checked);
        // 关键：设置UserRole为false，禁止用户修改
        item->setData(Qt::UserRole, false);
    }
}

