#include "otaupgradewindow.h"
#include "ui_otaupgradewindow.h"
#include "commhelper.h"
#include <QToolButton>
#include <QFileDialog>
#include <QAction>
#include <QMessageBox>
#include <QDateTime>
#include <QtConcurrent>

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

    ui->tableWidget_detector->verticalHeader()->setDefaultSectionSize(25); // 设置默认行高
    ui->tableWidget_detector->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed); // 固定高度
    ui->tableWidget_detector->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget_detector->horizontalHeader()->setFixedHeight(25);
    ui->tableWidget_detector->setRowCount(DET_NUM+1);
    ui->tableWidget_detector->setRowHeight(0, 50);
    ui->tableWidget_detector->verticalHeaderItem(0)->setText("时间同步\n触发模块");
    for (int row=0; row<ui->tableWidget_detector->rowCount(); ++row){
        QTableWidgetItem* item = nullptr;
        if (row == 0)
            item = new QTableWidgetItem(tr("时间同步\n触发模块"));
        else
            item = new QTableWidgetItem(QString("谱仪#%1").arg(row));
        ui->tableWidget_detector->setItem(row, 0, item);
        item->setCheckState(Qt::Unchecked);
        item->setTextAlignment(Qt::AlignCenter);

        QLabel* cell = new QLabel();
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::red));
        cell->setAlignment(Qt::AlignCenter);
        ui->tableWidget_detector->setCellWidget(row, 1, cell);
    }

    QAction *action = ui->lineEdit_filePath->addAction(QIcon(":/open.png"), QLineEdit::TrailingPosition);
    QToolButton* button = qobject_cast<QToolButton*>(action->associatedWidgets().last());
    button->setCursor(QCursor(Qt::PointingHandCursor));
    connect(button, &QToolButton::pressed, this, [=](){
        QString dirPath = QFileDialog::getExistingDirectory(this);
        if (dirPath.isEmpty())
            return;

        ui->lineEdit_filePath->setText(dirPath);
        ui->pushButton_ok->setEnabled(true);
        ui->pushButton_switch->setEnabled(true);

        QMap<int, QString> rpdFiles = enumRpdFilesByNumber(dirPath);
        // 输出结果（按数字键升序排列）
        for (auto it = rpdFiles.begin(); it != rpdFiles.end(); ++it) {
            if (it.key() == 0)
                emit sigWriteLog(QString(tr("[时间同步模块] 更新文件名称\"%1\"")).arg(it.value()));
            else
                emit sigWriteLog(QString(tr("谱仪[#%1] 更新文件名称\"%2\"")).arg(it.key()).arg(it.value()));
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
        QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 1));
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::green));
        //记录联网的探测器ID
        mOnlineDetectors.append(index);
    });

    connect(commHelper, &CommHelper::detectorOffline, this, [=](quint8 index){
        int row = index;
        QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 1));
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::red));

        //清除联网的探测器ID
        if (mOnlineDetectors.contains(index)){
            mOnlineDetectors.removeOne(index);
        }
    });

    connect(commHelper, &CommHelper::reportOTAUpgradeData, this, [=](quint8 index, const QByteArray& data){
        qint64 oldSize = m_returnDataSize.fetch_add(data.size());
        qDebug() << "累加前的大小是:" << oldSize << " 新接收大小:"<< data.size() << " 累加后的大小是:" << m_returnDataSize;

        mRecvTimeoutTimer.start();

        mAckUpgradePkg.push_back(data);
    });

    connect(this, SIGNAL(sigWriteLog(const QString&,QtMsgType)), this, SLOT(slotWriteLog(const QString&,QtMsgType)), Qt::QueuedConnection);
    connect(this, SIGNAL(sigSendProgress(int,const QString&,int,int)), this, SLOT(slotSendProgress(int,const QString&,int,int)), Qt::QueuedConnection);
    connect(this, SIGNAL(sigSendFinished(int,const QString&,bool)), this, SLOT(slotSendFinished(int,const QString&,bool)), Qt::QueuedConnection);
    connect(this, SIGNAL(sigUpgradeFinished()), this, SLOT(slotUpgradeFinished()), Qt::QueuedConnection);
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

#include <QScrollBar>
void OTAUpgradeWindow::slotWriteLog(const QString &msg, QtMsgType msgType)
{
    if (msgType == QtCriticalMsg)
        qCritical() << msg;
    else if (msgType == QtWarningMsg)
        qWarning() << msg;
    else if (msgType == QtInfoMsg)
        qInfo() << msg;
    else if (msgType == QtDebugMsg)
        qDebug() << msg;
    else
        return;

    QPlainTextEdit* edit = ui->plainTextEdit_log;

    // 可选：减少刷新（高频日志建议保留）
    edit->setUpdatesEnabled(false);

    QTextCursor cursor = edit->textCursor();
    cursor.movePosition(QTextCursor::End);

    // ===== 定义颜色 =====
    QTextCharFormat fmtNormal;
    fmtNormal.setForeground(mIsDarkTheme ? Qt::white : Qt::black);

    QTextCharFormat fmtError;
    fmtError.setForeground(Qt::red);

    QTextCharFormat fmtWarning;
    fmtWarning.setForeground(Qt::green);

    // ===== 时间 =====
    cursor.setCharFormat(fmtNormal);
    cursor.insertText(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz >> "));

    // ===== 内容 =====
    if (msgType == QtCriticalMsg || msgType == QtFatalMsg) {
        cursor.setCharFormat(fmtError);
    }
    else if (msgType == QtWarningMsg) {
        cursor.setCharFormat(fmtWarning);
    }
    else {
        cursor.setCharFormat(fmtNormal);
    }

    cursor.insertText(msg);
    cursor.insertText("\n");

    edit->setTextCursor(cursor);

    // 自动滚动
    edit->verticalScrollBar()->setValue(edit->verticalScrollBar()->maximum());
    // 限制最大行数
    ui->plainTextEdit_log->document()->setMaximumBlockCount(2000);
    edit->setUpdatesEnabled(true);
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
            emit sigWriteLog(tr("谱仪[#%1]发送扇区数失败！（原因：设备未连接或通信写入失败）").arg(index), QtCriticalMsg);
            continue;
        }

        QByteArray addrData = QByteArray::fromHex("12 34 00 0F CA 10 00 00 00 00 AB CD");
        addrData[7] = (ui->comboBox_partition->currentIndex() == 0) ? 0x00 : 0x08;
        if (!commHelper->sendOTAUpgradeData(index, addrData)) {
            emit sigWriteLog(tr("谱仪[#%1]发送更新地址失败！（原因：设备未连接或通信写入失败）").arg(index), QtCriticalMsg);
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
    QVector<int> validIndexes;
    for (int i=0; i<ui->tableWidget_detector->rowCount(); ++i){
        QTableWidgetItem* item = ui->tableWidget_detector->item(i, 0);
        if (item->checkState() == Qt::Checked){
            int index = i;
            if (mOnlineDetectors.contains(index)){
                validIndexes.append(index);
            }
        }
    }

    if (validIndexes.size() == 0) {
        // 没有选中任何行
        QMessageBox::warning(
            this,
            tr("自定义通道测量——提示"),
            tr("请先选择一个探测器通道，再执行此操作。")
            );
        return;  // 或者给个提示
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

    for (const auto index : validIndexes) {
        m_runningTasks++;
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
        return;
    }

    // 获取当前任务的设备索引和分区
    auto currentTask = m_taskQueue[m_currentTaskIndex];
    int index = currentTask.first;
    QString partition = currentTask.second;
    QString filePath = m_rpdFiles.value(index);

    QString detectorName = (index == 0) ? QStringLiteral("[时钟同步模块]")
                                        : QStringLiteral("谱仪[#%1]").arg(index);

    emit sigWriteLog(tr("=== 开始执行任务%1：%2（分区：%3） ===")
                    .arg(m_currentTaskIndex + 1).arg(detectorName).arg(partition));

    if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
        emit sigWriteLog(tr("%1升级失败：未找到对应的升级文件（期望文件名格式：%2_xxx.rpd）")
                         .arg(detectorName).arg(index), QtCriticalMsg);
        m_runningTasks--;
        m_currentTaskIndex++;
        if (m_runningTasks == 0)
            emit sigUpgradeFinished();
        else
            startNextTask();
        return;
    }

    // 1. 发送更新数据包和更新地址（原业务逻辑保留）
    QByteArray sectorData = QByteArray::fromHex("12 34 00 0F CA 11 00 00 00 00 AB CD");
    qint64 fileSize = QFileInfo(filePath).size();
    quint8 sectorCount = qCeil((double)fileSize / 262144); // 扇区大小262144
    sectorData[9] = sectorCount & 0xFF;

    if (!commHelper->sendOTAUpgradeData(index, sectorData)) {
        emit sigWriteLog(tr("%1发送更新数据包失败，跳过该任务（原因：设备未连接或通信写入失败）")
                         .arg(detectorName), QtCriticalMsg);
        m_runningTasks--;
        m_currentTaskIndex++;
        if (m_runningTasks == 0)
            emit sigUpgradeFinished();
        else
            startNextTask(); // 直接执行下一个任务
        return;
    }
    QThread::msleep(1000); // 子线程休眠，不阻塞主线程

    QByteArray addrData = QByteArray::fromHex("12 34 00 0F CA 10 00 00 00 00 AB CD");
    addrData[7] = (ui->comboBox_partition->currentIndex() == 0) ? 0x00 : 0x80;
    if (!commHelper->sendOTAUpgradeData(index, addrData)) {
        emit sigWriteLog(tr("%1发送更新地址失败，跳过该任务（原因：设备未连接或通信写入失败）")
                         .arg(detectorName), QtCriticalMsg);
        m_runningTasks--;
        m_currentTaskIndex++;
        if (m_runningTasks == 0)
            emit sigUpgradeFinished();
        else
            startNextTask();
        return;
    }
    QThread::msleep(1000); // 子线程休眠，不阻塞主线程

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
    for (int index = 0; index <=DET_NUM; ++index)
        commHelper->endOTAUpgrade(index);

    ui->pushButton_ok->setEnabled(true);
    ui->pushButton_switch->setEnabled(true);
    this->close();
}


void OTAUpgradeWindow::on_pushButton_switch_clicked()
{
    QVector<int> validIndexes;
    for (int i=0; i<ui->tableWidget_detector->rowCount(); ++i){
        QTableWidgetItem* item = ui->tableWidget_detector->item(i, 0);
        if (item->checkState() == Qt::Checked){
            int index = i;
            if (mOnlineDetectors.contains(index)){
                validIndexes.append(index);
            }
        }
    }
    if (validIndexes.size() == 0) {
        // 没有选中任何行
        QMessageBox::warning(
            this,
            tr("自定义通道测量——提示"),
            tr("请先选择一个探测器通道，再执行此操作。")
            );
        return;  // 或者给个提示
    }

    QString selectedPartition;
    if (ui->comboBox_partition->currentIndex() == 0){
        selectedPartition = QStringLiteral("主分区程序");
    } else {
        selectedPartition = QStringLiteral("备用分区程序");
    }

    for (const auto index : validIndexes) {
        if (index == 0){
            emit sigWriteLog(tr("开始切换[时间同步触发模块]程序至[%1]......").arg(selectedPartition));
        }
        else{
            emit sigWriteLog(tr("开始切换谱仪[#%1]程序至[%2]......").arg(index).arg(selectedPartition));
        }

        // 切换程序指令
        QByteArray data = QByteArray::fromHex("12 34 00 0F CA 12 00 00 00 00 AB CD");
        if (ui->comboBox_partition->currentIndex() == 0){
            data[9] = 0x00;
        } else {
            data[9] = 0x01;
        }

        if (!commHelper->sendOTAUpgradeData(index, data)){
            if (index == 0){
                emit sigWriteLog(tr("[时间同步触发模块]程序切换失败！！！（原因：设备未连接或通信写入失败）"), QtCriticalMsg);
            }
            else{
                emit sigWriteLog(tr("谱仪[#%1]程序切换失败！！！（原因：设备未连接或通信写入失败）").arg(index), QtCriticalMsg);
            }

            continue;
        }

        if (index == 0){
            emit sigWriteLog(tr("[时间同步触发模块]程序切换成功！！！"));
        }
        else{
            emit sigWriteLog(tr("谱仪[#%1]程序切换成功！！！").arg(index));
        }
    }
}

void OTAUpgradeWindow::asyncSendOTAData(int index, const QString& filePath, const QString& selectedPartition)
{
    QString detectorName;
    if (index == 0){
        detectorName = QStringLiteral("[时钟同步模块]");
    } else {
        detectorName = QStringLiteral("谱仪[#%1]").arg(index);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit sigWriteLog(tr("%1[%2]升级失败：无法打开升级文件\"%3\"（原因：%4）")
                         .arg(detectorName, selectedPartition, filePath, file.errorString()), QtCriticalMsg);
        emit sigSendFinished(index, selectedPartition, false);
        return;
    }

    qint64 fileSize = QFileInfo(filePath).size();
    int sendCount = qCeil((double)fileSize / 256);
    bool sendSuccess = true;
    QString failReason;

    m_currentFileSize = fileSize;
    m_returnDataSize = 0;

    commHelper->startOTAUpgrade(index);

    QDateTime now = QDateTime::currentDateTime();
    mFileUpgrade = new QFile(QString("./logs/%1_%2_snd.rpd").arg(index).arg(now.toString("yyyy-MM-dd-HHmmsss")));
    if (!mFileUpgrade->open(QIODevice::WriteOnly)){
        emit sigWriteLog(tr("%1发送日志文件创建失败：%2（不影响升级流程）")
                         .arg(detectorName, mFileUpgrade->errorString()), QtWarningMsg);
        mFileUpgrade->deleteLater();
        mFileUpgrade = nullptr;
    }

    mAckUpgradePkg.clear();
    mFileUpgradeAck = new QFile(QString("./logs/%1_%2_rcv.rpd").arg(index).arg(now.toString("yyyy-MM-dd-HHmmsss")));
    if (!mFileUpgradeAck->open(QIODevice::WriteOnly)){
        emit sigWriteLog(tr("%1接收日志文件创建失败：%2（不影响升级流程）")
                         .arg(detectorName, mFileUpgradeAck->errorString()), QtWarningMsg);
        mFileUpgradeAck->deleteLater();
        mFileUpgradeAck = nullptr;
    }

    mOTAUpgradePkg = file.readAll();
    file.seek(0);

    for (int i = 0; i < sendCount && sendSuccess; ++i) {
        // 发送进度信号（主线程更新UI）
        emit sigSendProgress(index, selectedPartition, i + 1, sendCount);

        QByteArray buf = file.read(256);
        if (buf.size() < 256 && i != sendCount - 1) {
            failReason = tr("读取升级文件数据包不完整（包序号：%1/%2，期望256字节，实际%3字节）")
                             .arg(i + 1).arg(sendCount).arg(buf.size());
            sendSuccess = false;
            break;
        }

        if (!commHelper->sendOTAUpgradeData(index, buf)) {
            failReason = tr("发送升级数据包失败（包序号：%1/%2，原因：设备未连接或通信写入失败）")
                             .arg(i + 1).arg(sendCount);
            sendSuccess = false;
            break;
        }

        if (mFileUpgrade && mFileUpgrade->isOpen()){
            mFileUpgrade->write(buf);
        }

        QThread::msleep(50); // 子线程休眠，不阻塞主线程
    }

    file.close();
    if (!sendSuccess)
    {
        emit sigWriteLog(tr("%1[%2]升级失败：%3").arg(detectorName, selectedPartition, failReason), QtCriticalMsg);
        emit sigSendFinished(index, selectedPartition, sendSuccess);
    }
    else{
        // 这里是否需要等到数据收完了再发送最后分区切换指令？？？
        /*
            正常逻辑：数据发送完毕，探测器会一次性将所有数据返回，这里我们可以设定如果3s返回数据没更新了，表示数据接收完毕，此时就可以对返回数据进行校验
        */
        mRecvTimeoutTimer.start();
        QFuture<bool> future = QtConcurrent::run([&] {
            while (mRecvTimeoutTimer.elapsed() < 5000) { // 最长等待5秒
                QThread::msleep(10); // 每10ms检查一次
            }

            mRecvTimeoutTimer.invalidate();
            bool upgradeSuccess = false;
            qDebug() << m_returnDataSize << m_currentFileSize;
            qDebug() << mAckUpgradePkg.size() << mOTAUpgradePkg.size();
            if (mAckUpgradePkg.size() >= mOTAUpgradePkg.size()) {
                // 数据长度满足要求，再进一步对数据进行校验
                // 在这里可以对返回值与发送OTA升级文件内容对比，判断升级是否成功
                //通过分治交换相邻位，三步完成字节内翻转，算法效率更高：
                // auto reverseByte = [=](unsigned char b) {
                //     // 交换相邻1位 -> 交换相邻2位 -> 交换相邻4位，完成翻转
                //     b = ((b & 0b10101010) >> 1) | ((b & 0b01010101) << 1);
                //     b = ((b & 0b11001100) >> 2) | ((b & 0b00110011) << 2);
                //     b = ((b & 0b11110000) >> 4) | ((b & 0b00001111) << 4);
                //     return b;
                // };

                // auto reverseBitsInEachByte = [=](const QByteArray& input) {
                //     QByteArray result;
                //     result.reserve(input.size());
                //     for (char byte : input) {
                //         result.append(static_cast<char>(reverseByte(static_cast<unsigned char>(byte))));
                //     }
                //     return result;
                // };

                auto reverseBitsInEachByte = [=](const QByteArray& input) {
                    // 预生成0-255的翻转查找表
                    static unsigned char reverseTable[256] = {};
                    static bool tableInitialized = false;
                    if (!tableInitialized) {
                        for (int i = 0; i < 256; ++i) {
                            unsigned char reversed = 0;
                            for (int j = 0; j < 8; ++j) {
                                if (i & (1 << j)) reversed |= (1 << (7 - j));
                            }
                            reverseTable[i] = reversed;
                        }
                        tableInitialized = true;
                    }

                    QByteArray result;
                    result.reserve(input.size());
                    for (char byte : input) {
                        result.append(static_cast<char>(reverseTable[static_cast<unsigned char>(byte)]));
                    }
                    return result;
                };


                mAckUpgradePkg = reverseBitsInEachByte(mAckUpgradePkg);
                if (mFileUpgradeAck && mFileUpgradeAck->isOpen())
                {
                    mFileUpgradeAck->write(mAckUpgradePkg);
                }

                // 仅比较发送长度部分，避免多收字节导致误判
                QByteArray ackCompare = mAckUpgradePkg.left(mOTAUpgradePkg.size());
                upgradeSuccess = (mOTAUpgradePkg == ackCompare);
                if (!upgradeSuccess) {
                    int mismatchOffset = -1;
                    for (int i = 0; i < mOTAUpgradePkg.size(); ++i) {
                        if (mOTAUpgradePkg.at(i) != ackCompare.at(i)) {
                            mismatchOffset = i;
                            break;
                        }
                    }
                    emit sigWriteLog(tr("%1[%2]升级失败：返回数据内容校验不一致（发送%3字节，接收%4字节，首个差异偏移：%5）")
                                     .arg(detectorName, selectedPartition)
                                     .arg(mOTAUpgradePkg.size())
                                     .arg(mAckUpgradePkg.size())
                                     .arg(mismatchOffset), QtCriticalMsg);
                }
            }
            else{
                emit sigWriteLog(tr("%1[%2]升级失败：返回数据大小不足（发送%3字节，接收%4字节，可能原因：等待超时或通信中断）")
                                 .arg(detectorName, selectedPartition)
                                 .arg(mOTAUpgradePkg.size())
                                 .arg(mAckUpgradePkg.size()), QtCriticalMsg);
            }

            if (upgradeSuccess)
            {
                emit sigWriteLog(tr("%1[%2]升级成功！").arg(detectorName, selectedPartition));
            }

            // 发送结束指令之后，探测器会返回原升级包内容，所以字节校对需要发送指令之前判断
            emit sigSendFinished(index, selectedPartition, upgradeSuccess);
            return true;
        });

        bool result = future.result(); // 阻塞等待结果
        Q_UNUSED(result);
    }

    if (mFileUpgrade)
    {
        mFileUpgrade->flush();
        mFileUpgrade->close();
        mFileUpgrade->deleteLater();
        mFileUpgrade = nullptr;
    }

    if (mFileUpgradeAck)
    {
        mFileUpgradeAck->flush();
        mFileUpgradeAck->close();
        mFileUpgradeAck->deleteLater();
        mFileUpgradeAck = nullptr;
    }

    commHelper->endOTAUpgrade(index); // 发送结束指令
}

void OTAUpgradeWindow::slotSendProgress(int index, const QString& partition, int current, int total)
{
    // 更新进度条（主线程安全）
    int progress = (current * 100) / total;
    ui->progressBar->setValue(progress);

    // 更新日志
    // sigWriteLog(tr("谱仪[#%1][%2]更新进度：%3/%4（%5%）")
    //              .arg(index).arg(partition).arg(current).arg(total).arg(progress));
}

void OTAUpgradeWindow::slotSendFinished(int index, const QString& partition, bool success)
{
    m_runningTasks--;

    if (success) {
        // 发送完成指令（原业务逻辑保留）
        QByteArray finishData = QByteArray::fromHex("12 34 00 0F CA 10 00 00 00 00 AB CD");
        finishData[7] = (ui->comboBox_partition->currentIndex() == 0) ? 0x00 : 0x80;

        if (commHelper->sendOTAUpgradeData(index, finishData)) {
        } else {
            QString detectorName = (index == 0) ? QStringLiteral("[时钟同步模块]")
                                                : QStringLiteral("谱仪[#%1]").arg(index);
            emit sigWriteLog(tr("%1[%2]发送完成指令失败！（原因：设备未连接或通信写入失败）")
                             .arg(detectorName).arg(partition), QtCriticalMsg);
        }
    } else {
        // 具体失败原因已在 asyncSendOTAData 中打印，此处仅做汇总提示
        QString detectorName = (index == 0) ? QStringLiteral("[时钟同步模块]")
                                            : QStringLiteral("谱仪[#%1]").arg(index);
        emit sigWriteLog(tr("%1[%2]更新失败（详见上方失败原因）").arg(detectorName).arg(partition), QtCriticalMsg);
    }

    // 按钮状态
    if (m_runningTasks == 0) {
        emit sigUpgradeFinished();
    }
}

void OTAUpgradeWindow::slotUpgradeFinished()
{
    // 所有任务完成
    emit sigWriteLog("=== 所有OTA任务执行完毕【注意：升级成功一定要断电重启！！！】 ===", QtInfoMsg);
    ui->pushButton_ok->setEnabled(true);
    ui->pushButton_switch->setEnabled(true);
    ui->progressBar->setValue(0);
}
