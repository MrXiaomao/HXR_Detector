#include "otaupgradewindow.h"
#include "ui_otaupgradewindow.h"
#include <QToolButton>
#include <QFileDialog>
#include <QAction>
#include <QMessageBox>
#include <QDateTime>

// 枚举目录下的 .rpd 文件并存储到 QMap
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QString>

// 枚举目录下符合 "数字_xxx.rpd" 规则的文件并存储到 QMap<int, QString>
QMap<int, QString> enumRpdFilesByNumber(const QString& dirPath) {
    QMap<int, QString> rpdMap;
    QDir dir(dirPath);

    // 筛选 .rpd 文件（忽略大小写）
    QStringList filters;
    filters << "*.rpd" << "*.RPD";
    dir.setNameFilters(filters);

    // 正则表达式：匹配 "数字_xxx.rpd"（数字部分捕获为分组1）
    QRegularExpression regex(R"(^(\d+)_.*\.rpd$)", QRegularExpression::CaseInsensitiveOption);

    // 遍历文件信息列表
    QFileInfoList fileList = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& fileInfo : fileList) {
        QString fileName = fileInfo.fileName();
        QRegularExpressionMatch match = regex.match(fileName);

        // 若匹配成功，提取数字键并存储
        if (match.hasMatch()) {
            int key = match.captured(1).toInt(); // 提取分组1的数字
            rpdMap[key] = fileInfo.absoluteFilePath();
        }
    }

    return rpdMap;
}


OTAUpgradeWindow::OTAUpgradeWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::OTAUpgradeWindow)
{
    ui->setupUi(this);

    // 初始化进度条范围
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);

    ui->pushButton_ok->setEnabled(false);
    ui->pushButton_switch->setEnabled(false);

    ui->tableWidget_detector->verticalHeader()->setDefaultSectionSize(25); // 设置默认行高
    ui->tableWidget_detector->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed); // 固定高度
    ui->tableWidget_detector->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget_detector->horizontalHeader()->setFixedHeight(25);
    ui->tableWidget_detector->setRowHeight(0, 50);
    ui->tableWidget_detector->verticalHeaderItem(0)->setText("时间同步\n触发模块");
    for (int row=0; row<ui->tableWidget_detector->rowCount(); ++row){
        QLabel* cell = new QLabel();
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::gray));
        cell->setAlignment(Qt::AlignCenter);
        ui->tableWidget_detector->setCellWidget(row, 0, cell);
    }

    QAction *action = ui->lineEdit_filePath->addAction(QIcon(":/open.png"), QLineEdit::TrailingPosition);
    QToolButton* button = qobject_cast<QToolButton*>(action->associatedWidgets().last());
    button->setCursor(QCursor(Qt::PointingHandCursor));
    connect(button, &QToolButton::pressed, this, [=](){
        QString dirPath = QFileDialog::getExistingDirectory(this);
        ui->lineEdit_filePath->setText(dirPath);
        ui->pushButton_ok->setEnabled(true);
        ui->pushButton_switch->setEnabled(true);

        QMap<int, QString> rpdFiles = enumRpdFilesByNumber(dirPath);
        // 输出结果（按数字键升序排列）
        for (auto it = rpdFiles.begin(); it != rpdFiles.end(); ++it) {
            qDebug() << "数字键：" << it.key() << " 文件路径：" << it.value();
            emit sigWriteLog(QString(tr("谱仪[#%1] 更新文件\"%2\"")).arg(it.key()).arg(it.value()));
        }

        // QString filter = "更新文件 (*.rpd);;所有文件 (*.*)";
        // QString filePath = QFileDialog::getOpenFileName(this, tr("打开文件"), "", filter);
        // if (filePath.isEmpty() || !QFileInfo::exists(filePath))
        //     return;

        // ui->lineEdit_filePath->setText(filePath);
        // const int sectorSize = 262144; // 256KB
        // qint64 fileSize = QFileInfo(filePath).size();
        // if (fileSize % sectorSize != 0){
        //     QMessageBox::warning(this, tr("提示"), tr("文件大小校验错误，请重新选择文件！"));
        //     ui->lineEdit_filePath->clear();
        //     ui->pushButton_ok->setEnabled(false);
        //     ui->pushButton_switch->setEnabled(false);
        // }
        // else{
        //     emit sigWriteLog(QString(tr("选择的更新文件:%1")).arg(filePath));
        //     emit sigWriteLog(QString(tr("文件大小:%1 Bytes")).arg(fileSize));
        //     ui->pushButton_ok->setEnabled(true);
        //     ui->pushButton_switch->setEnabled(true);
        // }
    });

    commHelper = CommHelper::instance();
    // 探测器
    connect(commHelper, &CommHelper::detectorOnline, this, [=](quint8 index){
        int row = index;
        QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 0));
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::green));
        //记录联网的探测器ID
        mOnlineDetectors.append(index);
    });

    connect(commHelper, &CommHelper::detectorOffline, this, [=](quint8 index){
        int row = index;
        QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 0));
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::gray));

        //清除联网的探测器ID
        if (mOnlineDetectors.contains(index)){
            mOnlineDetectors.removeOne(index);
        }
    });

    connect(commHelper, &CommHelper::reportOTAUpgradeData, this, [=](quint8 index, const QByteArray& data){
        m_returnDataSize[index] += data.size();
        if (m_returnDataSize[index] >= m_currentFileSize[index])
        {
            if (index == 0)
                emit sigWriteLog(tr("[时钟同步模块]升级包更新成功！"));
            else
                emit sigWriteLog(tr("谱仪[#%1]升级包更新成功！").arg(index));

            m_waitLoop.quit();
        }
    });

    connect(this, SIGNAL(sigWriteLog(const QString&,QtMsgType)), this, SLOT(slotWriteLog(const QString&,QtMsgType)));
}

OTAUpgradeWindow::~OTAUpgradeWindow()
{
    // 清理任务监控器
    for (auto watcher : m_watchers) {
        if (watcher->isRunning()) {
            watcher->cancel();
            watcher->waitForFinished();
        }
        delete watcher;
    }

    delete ui;
}

#include <QPainter>
#include <QPainterPath>
QPixmap OTAUpgradeWindow::dblroundPixmap(QSize sz, QColor clrIn, QColor clrOut)
{
    QPixmap result(sz);
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing,true);

    QPainterPath bigCirclePath;
    bigCirclePath.addEllipse(1, 1, sz.width()-2, sz.height()-2);
    painter.setPen(QPen(QBrush(clrOut), 2, Qt::SolidLine));
    painter.drawPath(bigCirclePath);

    QPainterPath smallCirclePath;
    smallCirclePath.addEllipse(4, 4, sz.width() - 8, sz.height() - 8);
    painter.fillPath(smallCirclePath, QBrush(clrIn));

    return result;
}

void OTAUpgradeWindow::slotWriteLog(const QString &msg, QtMsgType msgType)
{
    // 创建一个 QTextCursor
    QTextCursor cursor = ui->textEdit_log->textCursor();
    // 将光标移动到文本末尾
    cursor.movePosition(QTextCursor::End);

    // 先插入时间
    QString color = "black";
    if (mIsDarkTheme)
        color = "white";
    cursor.insertHtml(QString("<span style='color:%1;'>%2</span>").arg(color, QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz >> ")));
    // 再插入文本
    if (msgType == QtDebugMsg || msgType == QtInfoMsg)
        cursor.insertHtml(QString("<span style='color:%1;'>%2</span>").arg(color, msg));
    else if (msgType == QtCriticalMsg || msgType == QtFatalMsg)
        cursor.insertHtml(QString("<span style='color:#FF0000;'>%1</span>").arg(msg));
    else
        cursor.insertHtml(QString("<span style='color:#0000F0;'>%1</span>").arg(msg));

    // 最后插入换行符
    cursor.insertHtml("<br>");

    // 确保 QTextEdit 显示了光标的新位置
    ui->textEdit_log->setTextCursor(cursor);

    //限制行数
    QTextDocument *document = ui->textEdit_log->document(); // 获取文档对象，想象成打开了一个TXT文件
    int rowCount = document->blockCount(); // 获取输出区的行数
    const int maxRowNumber = 2000;//设定最大行
    if(rowCount > maxRowNumber){//超过最大行则开始删除
        QTextCursor cursor = QTextCursor(document); // 创建光标对象
        cursor.movePosition(QTextCursor::Start); //移动到开头，就是TXT文件开头

        for (int var = 0; var < rowCount - 1000; ++var) {
            cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor); // 向下移动并选中当前行
        }
        cursor.removeSelectedText();//删除选择的文本
    }
}

#if MULI_THREAD // 同时升级
void OTAUpgradeWindow::on_pushButton_ok_clicked()
{
    auto rows  = ui->tableWidget_detector->selectionModel()->selectedRows();
    if (rows .isEmpty()) {
        // 没有选中任何行
        QMessageBox::warning(
            this,
            tr("自定义通道测量——提示"),
            tr("请先选择一个探测器通道，再执行此操作。")
            );
        return;  // 或者给个提示
    }
    // 为了稳定，排序一下（可选）
    std::sort(rows.begin(), rows.end(), [](const QModelIndex &a, const QModelIndex &b){ return a.row() < b.row(); });
    // 过滤掉没有上线的探测器id
    QModelIndexList validIndexes;
    for (const auto &mi : rows) {
        int row = mi.row();
        if (0 == row)
            continue;

        quint8 index = row;
        if (mOnlineDetectors.contains(index))
            validIndexes.append(mi);
    }

    QString selectedPartition;
    if (ui->comboBox_partition->currentIndex() == 0){
        selectedPartition = QStringLiteral("主分区程序");
    } else {
        selectedPartition = QStringLiteral("备用分区程序");
    }

    ui->pushButton_ok->setEnabled(false);
    ui->pushButton_switch->setEnabled(false);
    QString dirPath = ui->lineEdit_filePath->text();
    QMap<int, QString> rpdFiles = enumRpdFilesByNumber(dirPath);

    m_runningTasks = 0;

    for (const auto &mi : validIndexes) {
        int index = mi.row();
        QString filePath = rpdFiles[index];//ui->lineEdit_filePath->text();
        QFileInfo fileInfo(filePath);
        qint64 fileSize = fileInfo.size();
        quint8 sectorCount = fileSize / 262144; // 扇区大小262144

        // 1. 发送扇区数和更新地址（原业务逻辑保留）
        QByteArray sectorData = QByteArray::fromHex("12 34 00 0F CA 11 00 00 00 00 AB CD");
        sectorData[9] = sectorCount & 0xFF;
        if (!commHelper->sendOTAUpgradeData(index, sectorData)) {
            emit sigWriteLog(tr("谱仪[#%1]发送扇区数失败！").arg(index), QtCriticalMsg);
            continue;
        }

        QByteArray addrData = QByteArray::fromHex("12 34 00 0F CA 10 00 00 00 00 AB CD");
        addrData[7] = (ui->comboBox_partition->currentIndex() == 0) ? 0x00 : 0x08;
        if (!commHelper->sendOTAUpgradeData(index, addrData)) {
            emit sigWriteLog(tr("谱仪[#%1]发送更新地址失败！").arg(index), QtCriticalMsg);
            continue;
        }

        // 2. 启动异步发送任务
        m_runningTasks++;
        QFuture<void> future = QtConcurrent::run(this, &OTAUpgradeWindow::asyncSendOTAData,
                                                 index, filePath, selectedPartition);

        // 监控任务状态（支持取消）
        QFutureWatcher<void>* watcher = new QFutureWatcher<void>(this);
        connect(watcher, &QFutureWatcher<void>::finished, [this, watcher]() {
            m_watchers.removeOne(watcher);
            delete watcher;
        });
        watcher->setFuture(future);
        m_watchers.append(watcher);
    }
}
#else
void OTAUpgradeWindow::on_pushButton_ok_clicked()
{
    auto rows  = ui->tableWidget_detector->selectionModel()->selectedRows();
    if (rows .isEmpty()) {
        // 没有选中任何行
        QMessageBox::warning(
            this,
            tr("自定义通道测量——提示"),
            tr("请先选择一个探测器通道，再执行此操作。")
            );
        return;  // 或者给个提示
    }
    // 为了稳定，排序一下（可选）
    std::sort(rows.begin(), rows.end(), [](const QModelIndex &a, const QModelIndex &b){ return a.row() < b.row(); });
    // 过滤掉没有上线的探测器id
    QModelIndexList validIndexes;
    for (const auto &mi : rows) {
        int row = mi.row();
        if (0 == row)
            continue;

        quint8 index = row;
        if (mOnlineDetectors.contains(index))
            validIndexes.append(mi);
    }

    QString selectedPartition;
    if (ui->comboBox_partition->currentIndex() == 0){
        selectedPartition = QStringLiteral("主分区程序");
    } else {
        selectedPartition = QStringLiteral("备用分区程序");
    }

    ui->pushButton_ok->setEnabled(false);
    ui->pushButton_switch->setEnabled(false);
    QString dirPath = ui->lineEdit_filePath->text();
    m_rpdFiles = enumRpdFilesByNumber(dirPath);

    m_runningTasks = 0;
    m_taskQueue.clear();
    m_currentTaskIndex = 0;

    for (const auto &mi : validIndexes) {
        int index = mi.row();
        // 将任务加入队列（每个设备对应一个任务）
        m_taskQueue.append(qMakePair(index, selectedPartition));
    }

    // 2. 启动第一个任务（若队列非空）
    if (!m_taskQueue.isEmpty()) {
        startNextTask();
    } else {
        ui->pushButton_ok->setEnabled(true);
        ui->pushButton_switch->setEnabled(true);
    }
}
#endif // 同时升级

void OTAUpgradeWindow::startNextTask()
{
    if (m_currentTaskIndex >= m_taskQueue.size()) {
        // 所有任务完成
        sigWriteLog("=== 所有OTA任务执行完毕 ===", QtInfoMsg);
        ui->pushButton_ok->setEnabled(true);
        ui->pushButton_switch->setEnabled(true);
        ui->progressBar->setValue(0);
        return;
    }

    // 获取当前任务的设备索引和分区
    auto currentTask = m_taskQueue[m_currentTaskIndex];
    int index = currentTask.first;
    QString partition = currentTask.second;
    QString filePath = m_rpdFiles[index];

    sigWriteLog(tr("=== 开始执行任务%1：谱仪#%2（分区：%3） ===")
                    .arg(m_currentTaskIndex + 1).arg(index).arg(partition));

    // 1. 发送更新数据包和更新地址（原业务逻辑保留）
    QByteArray sectorData = QByteArray::fromHex("12 34 00 0F CA 11 00 00 00 00 AB CD");
    qint64 fileSize = QFileInfo(filePath).size();
    quint8 sectorCount = fileSize / 262144; // 扇区大小262144
    sectorData[9] = sectorCount & 0xFF;

    if (!commHelper->sendOTAUpgradeData(index, sectorData)) {
        if (index == 0)
            sigWriteLog(tr("时钟同步模块发送更新数据包失败，跳过该任务").arg(index), QtCriticalMsg);
        else
            sigWriteLog(tr("谱仪[#%1]发送更新数据包失败，跳过该任务").arg(index), QtCriticalMsg);
        m_currentTaskIndex++;
        startNextTask(); // 直接执行下一个任务
        return;
    }
    QThread::msleep(500); // 子线程休眠，不阻塞主线程

    QByteArray addrData = QByteArray::fromHex("12 34 00 0F CA 10 00 00 00 00 AB CD");
    addrData[7] = (ui->comboBox_partition->currentIndex() == 0) ? 0x00 : 0x08;
    if (!commHelper->sendOTAUpgradeData(index, addrData)) {
        sigWriteLog(tr("谱仪[#%1]发送更新地址失败，跳过该任务").arg(index), QtCriticalMsg);
        m_currentTaskIndex++;
        startNextTask();
        return;
    }
    QThread::msleep(500); // 子线程休眠，不阻塞主线程

    // 2. 启动当前任务的异步发送（子线程执行）
    QFuture<void> future = QtConcurrent::run(this, &OTAUpgradeWindow::asyncSendOTAData,
                                             index, filePath, partition);

    // 监控当前任务完成，自动启动下一个任务
    QFutureWatcher<void>* watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, [this, watcher]() {
        m_currentTaskIndex++;
        startNextTask(); // 任务完成后启动下一个
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}

void OTAUpgradeWindow::on_pushButton_exit_clicked()
{
    for (int index = 0; index <=24; ++index)
        commHelper->endOTAUpgrade(index);

    ui->pushButton_ok->setEnabled(true);
    ui->pushButton_switch->setEnabled(true);
    this->close();
}


void OTAUpgradeWindow::on_pushButton_switch_clicked()
{
    auto rows  = ui->tableWidget_detector->selectionModel()->selectedRows();
    if (rows .isEmpty()) {
        // 没有选中任何行
        QMessageBox::warning(
            this,
            tr("自定义通道测量——提示"),
            tr("请先选择一个探测器通道，再执行此操作。")
            );
        return;  // 或者给个提示
    }
    // 为了稳定，排序一下（可选）
    std::sort(rows.begin(), rows.end(),
              [](const QModelIndex &a, const QModelIndex &b){ return a.row() < b.row(); });
    // 过滤掉没有上线的探测器id
    QModelIndexList validIndexes;
    for (const auto &mi : rows) {
        int row = mi.row();
        if (0 == row)
            continue;

        quint8 index = row;
        if (mOnlineDetectors.contains(index))
            validIndexes.append(mi);
    }

    QString selectedPartition;
    if (ui->comboBox_partition->currentIndex() == 0){
        selectedPartition = QStringLiteral("主分区程序");
    } else {
        selectedPartition = QStringLiteral("备用分区程序");
    }

    for (const auto &mi : validIndexes) {
        int row = mi.row();
        quint8 index = row;

        if (index == 0)
            emit sigWriteLog(tr("开始更新[时间同步触发模块][%1]......").arg(selectedPartition));
        else
            emit sigWriteLog(tr("开始更新谱仪[#%1][%2]......").arg(index).arg(selectedPartition));

        if (ui->comboBox_partition->currentIndex() == 0){
            selectedPartition = QStringLiteral("主分区程序");
        } else {
            selectedPartition = QStringLiteral("备用分区程序");
        }

        // 切换程序指令
        QByteArray data = QByteArray::fromHex("12 34 00 0F CA 12 00 00 00 00 AB CD");
        if (ui->comboBox_partition->currentIndex() == 0){
            data[7] = 0x00;
        } else {
            data[7] = 0x01;
        }

        if (!commHelper->sendOTAUpgradeData(index, data)){
            if (index == 0)
                emit sigWriteLog(tr("[时间同步触发模块]程序切换失败！！！"));
            else
                emit sigWriteLog(tr("谱仪[#%1]程序切换失败！！！").arg(index).arg(selectedPartition));

            return;
        }

        if (index == 0)
            emit sigWriteLog(tr("[时间同步触发模块]程序切换完成！！！"));
        else
            emit sigWriteLog(tr("谱仪[#%1]程序切换完成！！！").arg(index).arg(selectedPartition));
    }
}

void OTAUpgradeWindow::asyncSendOTAData(int index, const QString& filePath, const QString& selectedPartition)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit onSendFinished(index, selectedPartition, false);
        return;
    }

    qint64 fileSize = QFileInfo(filePath).size();
    int sendCount = fileSize / 256;
    bool sendSuccess = true;
    m_currentFileSize[index] = fileSize;
    m_returnDataSize[index] = 0;

    commHelper->startOTAUpgrade(index);
    for (int i = 0; i < sendCount && sendSuccess; ++i) {
        // 发送进度信号（主线程更新UI）
        emit onSendProgress(index, selectedPartition, i + 1, sendCount);

        QByteArray buf = file.read(256);
        if (buf.size() < 256 && i != sendCount - 1) {
            sendSuccess = false;
            break;
        }

        if (!commHelper->sendOTAUpgradeData(index, buf)) {
            sendSuccess = false;
            break;
        }

        QThread::msleep(50); // 子线程休眠，不阻塞主线程
    }

    file.close();
    onSendFinished(index, selectedPartition, sendSuccess);

    // 等待返回数据完成
    QTimer::singleShot(30000, [=](){
        m_waitLoop.exit(-1);
    });

    if (-1 == m_waitLoop.exec())
    {
        if (0 == index)
            emit sigWriteLog(tr("[时间同步触发模块]返回数据超时！！！"));
        else
            emit sigWriteLog(tr("谱仪[#1]返回数据超时！！！").arg(index));
    }

    commHelper->endOTAUpgrade(index); // 发送结束指令
}

void OTAUpgradeWindow::onSendProgress(int index, const QString& partition, int current, int total)
{
    // 更新进度条（主线程安全）
    int progress = (current * 100) / total;
    ui->progressBar->setValue(progress);

    // 更新日志
    // sigWriteLog(tr("谱仪[#%1][%2]更新进度：%3/%4（%5%）")
    //                 .arg(index).arg(partition).arg(current).arg(total).arg(progress));
}

void OTAUpgradeWindow::onSendFinished(int index, const QString& partition, bool success)
{
    m_runningTasks--;

    if (success) {
        // 发送完成指令（原业务逻辑保留）
        QByteArray finishData = QByteArray::fromHex("12 34 00 0F CA 10 00 00 00 00 AB CD");
        finishData[7] = (ui->comboBox_partition->currentIndex() == 0) ? 0x00 : 0x08;

        if (commHelper->sendOTAUpgradeData(index, finishData)) {
            sigWriteLog(tr("谱仪[#%1][%2]更新完成！").arg(index).arg(partition));
        } else {
            sigWriteLog(tr("谱仪[#%1][%2]发送完成指令失败！").arg(index).arg(partition), QtCriticalMsg);
        }
    } else {
        sigWriteLog(tr("谱仪[#%1][%2]更新失败！").arg(index).arg(partition), QtCriticalMsg);
    }

    // 按钮状态
    if (m_runningTasks == 0) {
        // 所有任务完成后恢复
        ui->pushButton_ok->setEnabled(true);
        ui->pushButton_switch->setEnabled(true);
        ui->progressBar->setValue(0); // 重置进度条
    }
}
