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
        QTableWidgetItem* item = new QTableWidgetItem();
        item->setCheckState(Qt::CheckState::Unchecked);
        item->setTextAlignment(Qt::AlignCenter);
        ui->tableWidget->setItem(0, column, item);
        item->setData(Qt::UserRole, true);
    }

    ui->lineEdit_switcherIp_1->setText(settings.value("Switcher/1/ip", "172.18.41.94").toString());
    QRegExp regExp(R"(^((25[0-5]|2[0-4]\d|1\d{2}|[1-9]\d|\d)\.){3}(25[0-5]|2[0-4]\d|1\d{2}|[1-9]\d|\d)$)");
    regExp.setPatternSyntax(QRegExp::RegExp2);
    ui->lineEdit_switcherIp_1->setValidator(new QRegExpValidator(regExp, ui->lineEdit_switcherIp_1));

    ui->tableWidget->blockSignals(true);
    const QString detectorList = settings.value("Switcher/1/detector").toString();
    const QStringList detectors = detectorList.split(',');
    for (const QString &det : detectors)
    {
        const int column = det.toInt();
        if (column >= 1 && column <= DET_NUM)
        {
            auto *item = ui->tableWidget->item(0, column - 1);
            if (item)
                item->setCheckState(Qt::Checked);
        }
    }
    ui->tableWidget->blockSignals(false);
    QString loadedDets;
    for (int column = 0; column < DET_NUM; ++column)
    {
        auto *item = ui->tableWidget->item(0, column);
        if (item && item->checkState() == Qt::Checked)
            loadedDets += QString("%1,").arg(column + 1);
    }
    mLoadedSwitcherIp = ui->lineEdit_switcherIp_1->text();
    mLoadedSwitcherDetectors = loadedDets;
    ui->label_tip->hide();

    connect(ui->tableWidget, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *) {
        updateData();
    });
    connect(ui->comboBox_ip, &QComboBox::currentTextChanged, this, &LocalSettingWindow::updateData);
    connect(ui->spinBox_port, &QSpinBox::textChanged, this, &LocalSettingWindow::updateData);
    connect(ui->lineEdit_switcherIp_1, &QLineEdit::editingFinished, this, &LocalSettingWindow::updateData);

    ui->comboBox_ip->installEventFilter(this);
    ui->lineEdit_switcherIp_1->installEventFilter(this);
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

    if (!ip_edit || !(watched == ui->lineEdit_switcherIp_1 || watched == ui->comboBox_ip)) {
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

    QString dets;
    for (int column = 0; column < DET_NUM; ++column)
    {
        auto *item = ui->tableWidget->item(0, column);
        if (item && item->checkState() == Qt::Checked)
            dets += QString("%1,").arg(column + 1);
    }
    const bool switcherChanged = ui->lineEdit_switcherIp_1->text() != mLoadedSwitcherIp
                                 || dets != mLoadedSwitcherDetectors;
    ui->label_tip->setVisible(switcherChanged);

    GlobalSettings settings(DEVICE_CONFIG_FILE);
    settings.setValue("Global/PluseTime", ui->spinBox_pluseCheckTime->value());
    HDF5Settings::instance()->setPluseTime(ui->spinBox_pluseCheckTime->value());

    settings.setValue("Local/ServerIp", ui->comboBox_ip->currentText());
    settings.setValue("Local/ServerPort", ui->spinBox_port->text());

    settings.setValue("Switcher/count", 1);
    settings.setValue("Switcher/1/ip", ui->lineEdit_switcherIp_1->text());
    settings.setValue("Switcher/1/detector", dets);
    settings.remove("Switcher/2/ip");
    settings.remove("Switcher/2/detector");
    settings.remove("Switcher/3/ip");
    settings.remove("Switcher/3/detector");
}

#include <QMessageBox>
void LocalSettingWindow::on_pushButton_ok_clicked()
{
    if (ui->lineEdit_switcherIp_1->text().isEmpty())
    {
        QMessageBox::information(this, tr("提示"), tr("交换机地址不能为空！"));
        return;
    }

    updateData();
    UserActionLogger::logMessage(QStringLiteral("保存网络配置 本地服务 ip=%1 port=%2 交换机=%3")
                                     .arg(ui->comboBox_ip->currentText())
                                     .arg(ui->spinBox_port->value())
                                     .arg(ui->lineEdit_switcherIp_1->text()));
    this->close();
}


void LocalSettingWindow::on_pushButton_cancel_clicked()
{
    this->close();
}



