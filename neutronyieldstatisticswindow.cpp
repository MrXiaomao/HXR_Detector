/*
 * @Author: MrPan
 * @Date: 2025-04-20 09:21:28
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2026-06-15 16:57:18
 * @Description: 离线数据分析 中子产额分析
 */
#include "neutronyieldstatisticswindow.h"
#include "ui_neutronyieldstatisticswindow.h"
#include "globalsettings.h"

#include <QButtonGroup>
#include <QFileDialog>
#include <QAction>
#include <QToolButton>
#include <QHeaderView>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMessageBox>
#include <math.h>
#include <algorithm>
#include <cstring>

// #include "H5Cpp.h"
#include "CrasterParabolicHeatmapWidget.h"
#include "MollweideHeatmapWidget.h"

namespace {
enum FileListColumn {
    ColIndex = 0,
    ColFileName,
    ColStartTime,
    ColEndTime,
    ColDuration,
    ColSpectrumCount,
    ColSpecTimeInterval,
    ColValidChannels,
    ColGap,
    ColCount
};

QDateTime measureStartTimeFromPath(const QString &filePath)
{
    QTextCodec *gbk_codec = QTextCodec::codecForName("GBK");
    const std::string filePathStd = gbk_codec->fromUnicode(filePath).toStdString();
    const QDateTime fromConfig = HDF5Settings::readConfigStartTime(filePathStd);
    if (fromConfig.isValid())
        return fromConfig;
    return HDF5Settings::measureStartTimeFromFileName(filePath);
}

/** 将 H5Spectrum 中的 UTC 时间戳转为 QDateTime（含毫秒），逻辑同 DataProcessor::extractTimestampData_Thread */
QDateTime h5UtcToDateTime(quint32 timeUTCs, quint32 timeUTCms)
{
    const quint64 msecond = static_cast<quint64>(timeUTCms) * 1000 / (1ULL << 32);
    const quint64 tmUnix = static_cast<quint64>(timeUTCs) - 2208988800ULL;
    return QDateTime::fromMSecsSinceEpoch(tmUnix * 1000 + msecond);
}

bool readH5SpectrumRow(H5::DataSet &dataset, hsize_t cols, hsize_t rowIndex, H5Spectrum &out)
{
    H5::DataSpace file_space = dataset.getSpace();
    hsize_t mem_dims[2] = {1, cols};
    H5::DataSpace mem_space(2, mem_dims);
    uint32_t *row_data = new uint32_t[cols];

    hsize_t start[2] = {rowIndex, 0};
    hsize_t count[2] = {1, cols};
    file_space.selectHyperslab(H5S_SELECT_SET, count, start);
    dataset.read(row_data, H5::PredType::NATIVE_UINT, mem_space, file_space);
    std::memcpy(&out, row_data, sizeof(H5Spectrum));
    delete[] row_data;
    file_space.close();
    return true;
}

/** 将有效通道号格式化为区间列表，如 1-32 或 1-5,7-32 */
QString formatChannelRanges(const QMap<int, quint32> &specCount)
{
    if (specCount.isEmpty())
        return QString();

    QList<int> ids = specCount.keys();
    std::sort(ids.begin(), ids.end());

    QStringList parts;
    int rangeStart = ids.first();
    int rangeEnd = ids.first();

    const auto flushRange = [&]() {
        if (rangeStart == rangeEnd)
            parts.append(QString::number(rangeStart));
        else
            parts.append(QStringLiteral("%1-%2").arg(rangeStart).arg(rangeEnd));
    };

    for (int i = 1; i < ids.size(); ++i) {
        if (ids[i] == rangeEnd + 1) {
            rangeEnd = ids[i];
        } else {
            flushRange();
            rangeStart = rangeEnd = ids[i];
        }
    }
    flushRange();
    return parts.join(QStringLiteral(","));
}

bool isDetectorTableEditableColumn(int column)
{
    return column == 2 || column == 3 || column == 4;
}

void applyDetectorTableItemFlags(QTableWidgetItem *item, int column)
{
    if (!item)
        return;
    item->setTextAlignment(Qt::AlignCenter);
    Qt::ItemFlags flags = item->flags();
    if (isDetectorTableEditableColumn(column))
        flags |= Qt::ItemIsEditable;
    else
        flags &= ~Qt::ItemIsEditable;
    item->setFlags(flags);
}
} // namespace

NeutronYieldStatisticsWindow::NeutronYieldStatisticsWindow(bool isDarkTheme, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::NeutronYieldStatisticsWindow)
    , mIsDarkTheme(isDarkTheme)
    , mainWindow(static_cast<QGoodWindowHelper*>(parent))
    , w(nullptr)
{
    for (int i = 0; i < DET_NUM; ++i)
        dealFile[i] = nullptr;

    ui->setupUi(this);

    // 初始化界面
    initUi();
    restoreSettings();
    applyColorTheme();
    connect(this, SIGNAL(reporWriteLog(const QString&,QtMsgType)), this, SLOT(replyWriteLog(const QString&,QtMsgType)));
    connect(this, SIGNAL(sigWriteLog(const QString&,QtMsgType)), this, SLOT(replyWriteLog(const QString&,QtMsgType)));
    qRegisterMetaType<std::vector<FullSpectrum>>("std::vector<FullSpectrum>");
    connect(this, SIGNAL(sigStart()), this, SLOT(slotStart()));
    connect(this, SIGNAL(sigFail()), this, SLOT(slotFail()));//, Qt::QueuedConnection);
    // 使用函数指针语法连接信号和槽，确保连接成功
    connect(this, &NeutronYieldStatisticsWindow::sigSuccess, this, &NeutronYieldStatisticsWindow::slotSuccess, Qt::QueuedConnection);

    QTimer::singleShot(0, this, [&](){
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
        QGoodWindow::setAppCustomTheme(mIsDarkTheme,this->mThemeColor); // Must be >96
    });

    QTimer::singleShot(0, this, [&](){
        if(mainWindow) {
            mainWindow->fixMenuBarWidth();
        }
    });
}


NeutronYieldStatisticsWindow::~NeutronYieldStatisticsWindow()
{
    for (int i = 0; i < DET_NUM; ++i) {
        delete dealFile[i];
        dealFile[i] = nullptr;
    }
    delete ui;
}

void NeutronYieldStatisticsWindow::initUi()
{
    ui->widget_flowLayoutContainer->hide();

    // 任务栏时钟信息
    {
        // 设置任务栏信息 - 系统时间
        QLabel *label_systemtime = new QLabel(ui->statusbar);
        label_systemtime->setObjectName("label_systemtime");
        label_systemtime->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        ui->statusbar->setContentsMargins(5, 0, 5, 0);
        ui->statusbar->addWidget(new QLabel(ui->statusbar), 1);
        ui->statusbar->addWidget(nullptr, 1);
        ui->statusbar->addPermanentWidget(label_systemtime);

        QTimer* systemClockTimer = new QTimer(this);
        systemClockTimer->setObjectName("systemClockTimer");
        connect(systemClockTimer, &QTimer::timeout, this, [=](){
            // 获取当前时间
            QDateTime currentDateTime = QDateTime::currentDateTime();

            // 获取星期几的数字（1代表星期日，7代表星期日）
            int dayOfWeekNumber = currentDateTime.date().dayOfWeek();

            // 星期几的中文名称列表
            QStringList dayNames = {
                tr("星期日"), QObject::tr("星期一"), QObject::tr("星期二"), QObject::tr("星期三"), QObject::tr("星期四"), QObject::tr("星期五"), QObject::tr("星期六"), QObject::tr("星期日")
            };

            // 根据数字获取中文名称
            QString dayOfWeekString = dayNames.at(dayOfWeekNumber);
            this->findChild<QLabel*>("label_systemtime")->setText(QString(QObject::tr("系统时间：")) + currentDateTime.toString("yyyy/MM/dd hh:mm:ss ") + dayOfWeekString);
        });
        systemClockTimer->start(900);
    }

    // 布局
    {
        QSplitter *splitterH1 = new QSplitter(Qt::Horizontal,this);
        splitterH1->setObjectName("splitterH1");
        splitterH1->setHandleWidth(1);
        splitterH1->addWidget(ui->leftStackedWidget);
        splitterH1->addWidget(ui->centralHboxWidget);
        splitterH1->addWidget(ui->rightVboxWidget);
        splitterH1->setSizes(QList<int>() << 100000 << 600000 << 100000);
        splitterH1->setCollapsible(0,false);
        splitterH1->setCollapsible(1,false);
        splitterH1->setCollapsible(2,false);
        ui->centralwidget->layout()->addWidget(splitterH1);
        ui->centralwidget->layout()->addWidget(ui->rightSidewidget);
    }

    // 左侧栏
    QPushButton* detectorStatusButton = nullptr;
    {
        detectorStatusButton = new QPushButton();
        detectorStatusButton->setText(tr("统计结果"));
        detectorStatusButton->setFixedSize(250,29);
        detectorStatusButton->setCheckable(true);

        QHBoxLayout* sideHboxLayout = new QHBoxLayout();
        sideHboxLayout->setObjectName("sideHboxLayout");
        sideHboxLayout->setContentsMargins(0,0,0,0);
        sideHboxLayout->setSpacing(2);

        QWidget* sideProxyWidget = new QWidget();
        sideProxyWidget->setObjectName("sideProxyWidget");
        sideProxyWidget->setLayout(sideHboxLayout);
        sideHboxLayout->addWidget(detectorStatusButton);

        QGraphicsScene *scene = new QGraphicsScene(this);
        QGraphicsProxyWidget *w = scene->addWidget(sideProxyWidget);
        w->setPos(0,0);
        w->setRotation(-90);
        ui->graphicsView->setScene(scene);
        ui->graphicsView->setFrameStyle(0);
        ui->graphicsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->graphicsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->graphicsView->setFixedSize(30, 250);
        ui->leftSidewidget->setFixedWidth(30);

        connect(detectorStatusButton,&QPushButton::clicked,this,[=](){
            if(ui->leftStackedWidget->isHidden()) {
                ui->leftStackedWidget->setCurrentWidget(ui->detectorStatusWidget);
                ui->leftStackedWidget->show();

                detectorStatusButton->setChecked(true);

                GlobalSettings settingsSystem;
                settingsSystem.setValue("Global/DefaultPage", "detectorStatus");
            } else {
                if(ui->leftStackedWidget->currentWidget() == ui->detectorStatusWidget) {
                    ui->leftStackedWidget->hide();
                    detectorStatusButton->setChecked(false);

                    GlobalSettings settingsSystem;
                    settingsSystem.setValue("Global/DefaultPage", "");
                } else {
                    ui->leftStackedWidget->setCurrentWidget(ui->detectorStatusWidget);
                    detectorStatusButton->setChecked(true);

                    GlobalSettings settingsSystem;
                    settingsSystem.setValue("Global/DefaultPage", "detectorStatus");
                }
            }
        });

        connect(ui->toolButton_closeDetectorStatusWidget,&QPushButton::clicked,this,[=](){
            ui->leftStackedWidget->hide();
            detectorStatusButton->setChecked(false);

            GlobalSettings settingsSystem;
            settingsSystem.setValue("Global/DefaultPage", "");
        });
    }

    // 右侧栏
    QPushButton* labPrametersButton = nullptr;
    {
        labPrametersButton = new QPushButton();
        labPrametersButton->setText(tr("参数信息"));
        labPrametersButton->setFixedSize(250,29);
        labPrametersButton->setCheckable(true);

        connect(labPrametersButton,&QPushButton::clicked,this,[=](){
            if(ui->rightVboxWidget->isHidden()) {
                ui->rightVboxWidget->show();

                GlobalSettings settingsSystem;
                settingsSystem.setValue("Global/ShowRightSide", "true");
            } else {
                ui->rightVboxWidget->hide();

                GlobalSettings settingsSystem;
                settingsSystem.setValue("Global/ShowRightSide", "false");
            }
        });

        QGraphicsScene *scene = new QGraphicsScene(this);
        QGraphicsProxyWidget *w = scene->addWidget(labPrametersButton);
        w->setPos(0,0);
        w->setRotation(-90);
        ui->rightGraphicsView->setScene(scene);
        ui->rightGraphicsView->setFrameStyle(0);
        ui->rightGraphicsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->rightGraphicsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->rightGraphicsView->setFixedSize(30, 250);
        ui->rightSidewidget->setFixedWidth(30);
    }

    //存储路径设置
    {
        QAction *action = ui->lineEdit_savepath->addAction(QIcon(":/open.png"), QLineEdit::TrailingPosition);
        QToolButton* button = qobject_cast<QToolButton*>(action->associatedWidgets().last());
        button->setCursor(QCursor(Qt::PointingHandCursor));
        connect(button, &QToolButton::pressed, this, [=](){
            QString dir = QFileDialog::getExistingDirectory(this);
            ui->lineEdit_savepath->setText(dir);
        });
    }
    
    ui->tableWidget->setEditTriggers(QAbstractItemView::DoubleClicked
                                     | QAbstractItemView::SelectedClicked
                                     | QAbstractItemView::EditKeyPressed);
    ui->tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    ui->tableWidget->setColumnWidth(0, 40);

    ui->tableWidget->blockSignals(true);
    for (int column=0; column<ui->tableWidget->columnCount(); ++column)
    {
        for (int row=0; row<ui->tableWidget->rowCount(); ++row)
        {
            auto *item = new QTableWidgetItem("");
            applyDetectorTableItemFlags(item, column);
            ui->tableWidget->setItem(row, column, item);
        }
    }

    for (int row=0; row<ui->tableWidget->rowCount(); ++row)
    {
        ui->tableWidget->setRowHeight(row, 25);
        auto *indexItem = new QTableWidgetItem(QString("#%1").arg(row+1));
        applyDetectorTableItemFlags(indexItem, 0);
        ui->tableWidget->setItem(row, 0, indexItem);
    }
    ui->tableWidget->blockSignals(false);
    ui->tableWidget->setFixedHeight((32+1)*25); //表头1行加32行探测器
    ui->leftStackedWidget->setMinimumWidth(200);

    initFileListTable();

    // 连接表格选择变化信号（图表通道改由 spinBox_pictureDet 控制，不再联动 tableWidget 行选择）
    // connect(ui->tableWidget, &QTableWidget::itemSelectionChanged, this, &NeutronYieldStatisticsWindow::on_tableWidget_selectionChanged);

    {
        QButtonGroup *shotTimeModeGroup = new QButtonGroup(this);
        shotTimeModeGroup->addButton(ui->radioButton_unifiedShotTime);
        shotTimeModeGroup->addButton(ui->radioButton_perChannelShotTime);
        connect(ui->radioButton_unifiedShotTime, &QRadioButton::toggled, this, [this](bool checked) {
            if (checked)
                updateShotTimeModeUi();
        });
        connect(ui->radioButton_perChannelShotTime, &QRadioButton::toggled, this, [this](bool checked) {
            if (checked)
                updateShotTimeModeUi();
        });
        connect(ui->spinBox_channelId, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int detId) {
            if (isPerChannelShotTimeMode()) {
                syncMeasureDateTimeForDetector(detId);
                updatePictureForDetector(detId);
            }
        });
        connect(ui->spinBox_pictureDet, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int detId) {
            if (!isPerChannelShotTimeMode())
                updatePictureForDetector(detId);
        });
        updateShotTimeModeUi();
    }

    initSpectrumCustomPlot();
    initCountCustomPlot();
    initYieldCustomPlot();

    connect(ui->tableWidget, &QTableWidget::itemChanged, this,
            &NeutronYieldStatisticsWindow::on_tableWidget_itemChanged);

    // 显示计数/能谱
    QButtonGroup *grp = new QButtonGroup(this);
    grp->addButton(ui->radioButton_spectrum, 0);
    grp->addButton(ui->radioButton_count, 1);
    grp->addButton(ui->radioButton_neutronYield, 2);
    connect(grp, QOverload<int>::of(&QButtonGroup::idClicked), this, [=](int index){
        ui->stackedWidget->setCurrentIndex(index);
    });

    //恢复页面布局
    {
        GlobalSettings settingsSystem;
        QString defaultPage = settingsSystem.value("Global/DefaultPage").toString();
        if (defaultPage == "detectorStatus")
            detectorStatusButton->clicked();

        if (settingsSystem.contains("Global/MainWindows-State")){
            this->restoreState(settingsSystem.value("Global/MainWindows-State").toByteArray());
        }

        if (settingsSystem.value("Global/ShowRightSide").toString() == "false")
            ui->rightVboxWidget->hide();

        if (settingsSystem.contains("Global/splitterH1/State")){
            QSplitter *splitterH1 = this->findChild<QSplitter*>("splitterH1");
            if (splitterH1)
            {
                splitterH1->restoreState(settingsSystem.value("Global/splitterH1/State").toByteArray());
            }
        }

        if (settingsSystem.contains("Global/splitterV2/State")){
            QSplitter *splitterV2 = this->findChild<QSplitter*>("splitterV2");
            if (splitterV2)
            {
                splitterV2->restoreState(settingsSystem.value("Global/splitterV2/State").toByteArray());
            }
        }
    }
}

void NeutronYieldStatisticsWindow::initFileListTable()
{
    QTableWidget *table = ui->tableWidget_fileList;
    table->setColumnCount(ColCount);
    table->setHorizontalHeaderLabels({
        tr("序号"), tr("文件名"), tr("开始时刻"), tr("结束时刻"),
        tr("时长(s)"), tr("能谱数"), tr("能谱时间间隔"), tr("有效通道"), tr("与上段间隔")
    });
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setDragEnabled(true);
    table->setAcceptDrops(true);
    table->setDropIndicatorShown(true);
    table->setDragDropMode(QAbstractItemView::InternalMove);
    table->setDefaultDropAction(Qt::MoveAction);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(ColIndex, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(ColFileName, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(ColStartTime, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(ColEndTime, QHeaderView::Fixed);
    table->setColumnWidth(ColIndex, 40);
    table->setColumnWidth(ColStartTime, 190);
    table->setColumnWidth(ColEndTime, 190);
    table->horizontalHeader()->setSectionResizeMode(ColSpecTimeInterval, QHeaderView::Fixed);
    table->setColumnWidth(ColSpecTimeInterval, 90);
    table->horizontalHeader()->setSectionResizeMode(ColValidChannels, QHeaderView::Fixed);
    table->setColumnWidth(ColValidChannels, 100);

    connect(table->model(), &QAbstractItemModel::rowsMoved, this, [this]() {
        refreshFileListBasicColumns();
        updateMeasurementInfoFromFirstFile();
    });
}

void NeutronYieldStatisticsWindow::sortFileListByStartTime()
{
    QTableWidget *table = ui->tableWidget_fileList;
    const int rowCount = table->rowCount();
    if (rowCount <= 1)
        return;

    struct FileListRow {
        QDateTime startTime;
        QVector<QTableWidgetItem*> items;
    };

    const auto startTimeForRow = [&](int row) -> QDateTime {
        if (const QTableWidgetItem *item = table->item(row, ColStartTime)) {
            const QDateTime dt = item->data(Qt::UserRole).toDateTime();
            if (dt.isValid())
                return dt;
        }
        if (const QTableWidgetItem *nameItem = table->item(row, ColFileName)) {
            const QString path = nameItem->data(Qt::UserRole).toString();
            if (!path.isEmpty()) {
                const QDateTime dt = measureStartTimeFromPath(path);
                if (dt.isValid())
                    return dt;
            }
        }
        return QDateTime();
    };

    QVector<FileListRow> rows;
    rows.reserve(rowCount);
    for (int row = 0; row < rowCount; ++row) {
        FileListRow entry;
        entry.startTime = startTimeForRow(row);
        entry.items.resize(ColCount);
        for (int col = 0; col < ColCount; ++col) {
            QTableWidgetItem *srcItem = table->item(row, col);
            if (srcItem) {
                entry.items[col] = srcItem->clone();
            } else {
                auto *newItem = new QTableWidgetItem();
                newItem->setTextAlignment(Qt::AlignCenter);
                entry.items[col] = newItem;
            }
        }
        rows.append(entry);
    }

    std::stable_sort(rows.begin(), rows.end(), [](const FileListRow &a, const FileListRow &b) {
        if (a.startTime.isValid() && b.startTime.isValid())
            return a.startTime < b.startTime;
        if (a.startTime.isValid())
            return true;
        if (b.startTime.isValid())
            return false;
        const QString pathA = a.items[ColFileName]->data(Qt::UserRole).toString();
        const QString pathB = b.items[ColFileName]->data(Qt::UserRole).toString();
        return pathA < pathB;
    });

    table->setRowCount(0);
    table->setRowCount(rowCount);
    for (int row = 0; row < rowCount; ++row) {
        for (int col = 0; col < ColCount; ++col)
            table->setItem(row, col, rows[row].items[col]);
    }

    updateFileListGapColumns();
    refreshFileListBasicColumns();
    updateMeasurementInfoFromFirstFile();
}

void NeutronYieldStatisticsWindow::refreshFileListBasicColumns()
{
    QTableWidget *table = ui->tableWidget_fileList;
    for (int row = 0; row < table->rowCount(); ++row) {
        QTableWidgetItem *nameItem = table->item(row, ColFileName);
        const QString filePath = nameItem ? nameItem->data(Qt::UserRole).toString() : QString();

        QTableWidgetItem *indexItem = table->item(row, ColIndex);
        if (!indexItem) {
            indexItem = new QTableWidgetItem();
            indexItem->setTextAlignment(Qt::AlignCenter);
            table->setItem(row, ColIndex, indexItem);
        }
        indexItem->setText(QString::number(row + 1));

        if (!nameItem) {
            nameItem = new QTableWidgetItem();
            table->setItem(row, ColFileName, nameItem);
        }
        if (!filePath.isEmpty()) {
            nameItem->setText(QFileInfo(filePath).fileName());
            nameItem->setData(Qt::UserRole, filePath);
            nameItem->setToolTip(filePath);
        }
        nameItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
    mFilePaths = filePathsFromTable();
}

QStringList NeutronYieldStatisticsWindow::filePathsFromTable() const
{
    QStringList paths;
    const QTableWidget *table = ui->tableWidget_fileList;
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem *nameItem = table->item(row, ColFileName);
        if (!nameItem)
            continue;
        const QString filePath = nameItem->data(Qt::UserRole).toString();
        if (!filePath.isEmpty())
            paths.append(filePath);
    }
    return paths;
}

void NeutronYieldStatisticsWindow::addFilesToList(const QStringList &paths)
{
    if (paths.isEmpty())
        return;

    QTableWidget *table = ui->tableWidget_fileList;
    QStringList knownPaths = filePathsFromTable();
    QStringList newPaths;

    for (const QString &filePath : paths) {
        if (filePath.isEmpty() || !QFileInfo::exists(filePath))
            continue;
        if (knownPaths.contains(filePath))
            continue;
        newPaths.append(filePath);
        knownPaths.append(filePath);
    }

    if (newPaths.isEmpty())
        return;

    std::sort(newPaths.begin(), newPaths.end(), [](const QString &a, const QString &b) {
        const QDateTime ta = measureStartTimeFromPath(a);
        const QDateTime tb = measureStartTimeFromPath(b);
        if (ta.isValid() && tb.isValid())
            return ta < tb;
        if (ta.isValid())
            return true;
        if (tb.isValid())
            return false;
        return a < b;
    });

    for (const QString &filePath : newPaths) {
        const int row = table->rowCount();
        table->insertRow(row);

        auto *indexItem = new QTableWidgetItem(QString::number(row + 1));
        indexItem->setTextAlignment(Qt::AlignCenter);
        table->setItem(row, ColIndex, indexItem);

        auto *nameItem = new QTableWidgetItem(QFileInfo(filePath).fileName());
        nameItem->setData(Qt::UserRole, filePath);
        nameItem->setToolTip(filePath);
        nameItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        table->setItem(row, ColFileName, nameItem);

        for (int col = ColStartTime; col < ColCount; ++col) {
            auto *item = new QTableWidgetItem();
            item->setTextAlignment(Qt::AlignCenter);
            table->setItem(row, col, item);
        }

        emit reporWriteLog(tr("添加测量文件：%1").arg(filePath));

        H5FileChannelMeta meta;
        if (readH5FileChannelMeta(filePath, meta))
            applyMeasurementMetaToFileListRow(row, meta);
        else
            applyMeasurementMetaToFileListRow(row, H5FileChannelMeta{});
    }

    updateFileListGapColumns();
    refreshFileListBasicColumns();
    updateMeasurementInfoFromFirstFile();
}

void NeutronYieldStatisticsWindow::computeCommonTimeInterval(const H5FileChannelMeta &meta,
                                                             QDateTime &commonStart,
                                                             QDateTime &commonEnd)
{
    commonStart = QDateTime();
    commonEnd = QDateTime();
    for (auto it = meta.firstDateTime.cbegin(); it != meta.firstDateTime.cend(); ++it) {
        const int detId = it.key();
        if (!meta.lastDateTime.contains(detId))
            continue;
        const QDateTime first = it.value();
        const QDateTime last = meta.lastDateTime.value(detId);
        if (!first.isValid() || !last.isValid())
            continue;
        if (!commonStart.isValid()) {
            commonStart = first;
            commonEnd = last;
        } else {
            if (first > commonStart)
                commonStart = first;
            if (last < commonEnd)
                commonEnd = last;
        }
    }
}

bool NeutronYieldStatisticsWindow::readH5FileChannelMeta(const QString &filePath,
                                                         H5FileChannelMeta &out,
                                                         bool applyDetectorLonLat,
                                                         bool showErrorDialog)
{
    out = H5FileChannelMeta{};

    try {
        QTextCodec *gbk_codec = QTextCodec::codecForName("GBK");
        QByteArray filePathBytes = gbk_codec->fromUnicode(filePath);
        const std::string filePathStd = filePathBytes.toStdString();
        H5::H5File file(filePathStd, H5F_ACC_RDONLY);

        qint64 fileStartMs = 0;
        if (H5Lexists(file.getId(), "Config", H5P_DEFAULT) > 0) {
            H5::Group configGroup = file.openGroup("Config");
            out.configStartTime = HDF5Settings::parseConfigTimeString(
                HDF5Settings::readConfigStringFromGroup(configGroup, "StartTime"));
            out.configShotTime = HDF5Settings::parseConfigTimeString(
                HDF5Settings::readConfigStringFromGroup(configGroup, "ShotTime"));

            try {
                H5::DataSet configDataset = configGroup.openDataSet("Configuration");
                H5::CompType cfgType = HDF5Settings::createCfgDataType();
                DetParameter cfg[DET_NUM];
                configDataset.read(&cfg, cfgType);
                configDataset.close();

                if (applyDetectorLonLat) {
                    for (int i = 0; i < DET_NUM; ++i)
                        setLonLat(i, cfg[i].longitude, cfg[i].latitude);
                }
            } catch (H5::Exception &) {
                emit reporWriteLog(tr("文件 %1 缺少 Config/Configuration，跳过探测器参数读取")
                                       .arg(QFileInfo(filePath).fileName()),
                                   QtWarningMsg);
            }
            configGroup.close();
        }

        if (!out.configStartTime.isValid())
            out.configStartTime = HDF5Settings::measureStartTimeFromFileName(filePath);
        if (out.configStartTime.isValid())
            fileStartMs = out.configStartTime.toMSecsSinceEpoch();
        const bool useSyntheticTime = fileStartMs != 0;

        for (int i = 0; i < DET_NUM; ++i) {
            const int detId = i + 1;
            const QString detName = QString("Detector#%1").arg(detId);
            const std::string detNameStd = detName.toStdString();

            if (H5Lexists(file.getId(), detNameStd.c_str(), H5P_DEFAULT) <= 0)
                continue;

            try {
                H5::Group group = file.openGroup(detNameStd);
                H5::DataSet dataset = group.openDataSet("Spectrum");

                H5::CompType fileType = dataset.getCompType();
                if (fileType != H5::PredType::NATIVE_UINT) {
                    emit reporWriteLog(tr("谱仪%1数据类型不匹配，跳过").arg(detId), QtWarningMsg);
                    group.close();
                    dataset.close();
                    continue;
                }

                H5::DataSpace file_space = dataset.getSpace();
                hsize_t current_dims[2];
                file_space.getSimpleExtentDims(current_dims, NULL);
                const hsize_t rows = current_dims[0];
                const hsize_t cols = current_dims[1];

                if (rows <= 0) {
                    emit reporWriteLog(tr("谱仪%1无能谱数据，跳过").arg(detId), QtWarningMsg);
                    group.close();
                    dataset.close();
                    file_space.close();
                    continue;
                }

                H5Spectrum firstSpec{};
                H5Spectrum lastSpec{};
                readH5SpectrumRow(dataset, cols, 0, firstSpec);
                readH5SpectrumRow(dataset, cols, rows - 1, lastSpec);

                QDateTime firstDt;
                QDateTime lastDt;
                if (useSyntheticTime && firstSpec.measureTime != 0) {
                    const quint32 seq0 = firstSpec.sequence;
                    const qint64 mt = static_cast<qint64>(firstSpec.measureTime);
                    const qint64 firstMs = fileStartMs
                        + static_cast<qint64>(firstSpec.sequence - seq0) * mt;
                    const qint64 lastEndMs = fileStartMs
                        + static_cast<qint64>(lastSpec.sequence - seq0) * mt
                        + static_cast<qint64>(lastSpec.measureTime);
                    firstDt = QDateTime::fromMSecsSinceEpoch(firstMs);
                    lastDt = QDateTime::fromMSecsSinceEpoch(lastEndMs);
                } else {
                    firstDt = h5UtcToDateTime(firstSpec.timeUTCs, firstSpec.timeUTCms);
                    lastDt = h5UtcToDateTime(lastSpec.timeUTCs, lastSpec.timeUTCms);
                    if (lastSpec.measureTime != 0)
                        lastDt = lastDt.addMSecs(static_cast<qint64>(lastSpec.measureTime));
                }
                out.firstDateTime[detId] = firstDt;
                out.lastDateTime[detId] = lastDt;
                out.specCount[detId] = static_cast<quint32>(rows);

                if (!out.hasMeasureTime) {
                    out.measureTimeMs = firstSpec.measureTime;
                    out.hasMeasureTime = true;
                }

                emit reporWriteLog(tr("谱仪%1时间范围：%2 ~ %3")
                                       .arg(detId)
                                       .arg(firstDt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")))
                                       .arg(lastDt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"))));

                if (applyDetectorLonLat && i >= 0 && i < ui->tableWidget->rowCount()) {
                    auto *specCountItem = new QTableWidgetItem(QString::number(rows));
                    applyDetectorTableItemFlags(specCountItem, 6);
                    ui->tableWidget->setItem(i, 6, specCountItem);
                }

                group.close();
                dataset.close();
                file_space.close();
            } catch (H5::Exception &) {
                qDebug() << "Detector" << detId << "read failed in" << QFileInfo(filePath).fileName();
            }
        }
        file.close();
    } catch (H5::FileIException &) {
        if (showErrorDialog) {
            QMessageBox::critical(this, tr("错误"), tr("无法打开HDF5文件，可能被占用。"));
        } else {
            emit reporWriteLog(tr("无法打开HDF5文件：%1").arg(filePath), QtWarningMsg);
        }
        return false;
    } catch (H5::Exception &) {
        if (showErrorDialog) {
            QMessageBox::critical(this, tr("错误"), tr("HDF5读取异常。"));
        } else {
            emit reporWriteLog(tr("HDF5读取异常：%1").arg(filePath), QtWarningMsg);
        }
        return false;
    }

    computeCommonTimeInterval(out, out.commonStart, out.commonEnd);
    out.valid = out.commonStart.isValid() && out.commonEnd.isValid();
    if (out.valid && out.commonStart > out.commonEnd) {
        emit reporWriteLog(tr("文件 %1 各通道时间无重叠区间").arg(QFileInfo(filePath).fileName()),
                           QtWarningMsg);
        out.valid = false;
    } else if (out.valid) {
        const double durationSec = out.commonStart.msecsTo(out.commonEnd) / 1000.0;
        emit reporWriteLog(tr("文件 %1 共有测量区间：%2 ~ %3，时长 %4s")
                               .arg(QFileInfo(filePath).fileName())
                               .arg(out.commonStart.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")))
                               .arg(out.commonEnd.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")))
                               .arg(durationSec, 0, 'f', 1));
    }

    return out.valid || !out.firstDateTime.isEmpty();
}

void NeutronYieldStatisticsWindow::applyMeasurementMetaToFileListRow(int row,
                                                                     const H5FileChannelMeta &meta)
{
    QTableWidget *table = ui->tableWidget_fileList;
    if (row < 0 || row >= table->rowCount())
        return;

    const auto setCell = [&](int col, const QString &text, const QVariant &userData = {}) {
        QTableWidgetItem *item = table->item(row, col);
        if (!item) {
            item = new QTableWidgetItem();
            item->setTextAlignment(Qt::AlignCenter);
            table->setItem(row, col, item);
        }
        item->setText(text);
        if (userData.isValid())
            item->setData(Qt::UserRole, userData);
    };

    if (!meta.valid) {
        setCell(ColStartTime, QString());
        setCell(ColEndTime, QString());
        setCell(ColDuration, QString());
        setCell(ColSpectrumCount, QString());
        setCell(ColSpecTimeInterval, meta.hasMeasureTime ? tr("%1 ms").arg(meta.measureTimeMs) : QString());
        setCell(ColValidChannels, formatChannelRanges(meta.specCount));
        return;
    }

    const QString timeFmt = QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz");
    const double durationSec = meta.commonStart.msecsTo(meta.commonEnd) / 1000.0;
    quint32 totalSpec = 0;
    for (quint32 n : meta.specCount)
        totalSpec += n;

    setCell(ColStartTime, meta.commonStart.toString(timeFmt), meta.commonStart);
    setCell(ColEndTime, meta.commonEnd.toString(timeFmt), meta.commonEnd);
    setCell(ColDuration, QString::number(durationSec, 'f', 1));
    setCell(ColSpectrumCount, QString::number(totalSpec));
    if (meta.hasMeasureTime)
        setCell(ColSpecTimeInterval, tr("%1 ms").arg(meta.measureTimeMs));
    setCell(ColValidChannels, formatChannelRanges(meta.specCount));
}

void NeutronYieldStatisticsWindow::updateFileListGapColumns()
{
    QTableWidget *table = ui->tableWidget_fileList;
    for (int row = 0; row < table->rowCount(); ++row) {
        QTableWidgetItem *gapItem = table->item(row, ColGap);
        if (!gapItem) {
            gapItem = new QTableWidgetItem();
            gapItem->setTextAlignment(Qt::AlignCenter);
            table->setItem(row, ColGap, gapItem);
        }
        if (row == 0) {
            gapItem->setText(QStringLiteral("-"));
            continue;
        }
        const QTableWidgetItem *prevEndItem = table->item(row - 1, ColEndTime);
        const QTableWidgetItem *curStartItem = table->item(row, ColStartTime);
        const QDateTime prevEnd = prevEndItem ? prevEndItem->data(Qt::UserRole).toDateTime() : QDateTime();
        const QDateTime curStart = curStartItem ? curStartItem->data(Qt::UserRole).toDateTime() : QDateTime();
        if (!prevEnd.isValid() || !curStart.isValid()) {
            gapItem->setText(QString());
            continue;
        }
        const double gapSec = prevEnd.msecsTo(curStart) / 1000.0;
        gapItem->setText(QString::number(gapSec, 'f', 1));
    }
}

void NeutronYieldStatisticsWindow::updateMeasurementInfoFromFirstFile()
{
    const QStringList paths = filePathsFromTable();
    if (paths.isEmpty())
        return;

    const QString filePath = paths.first();
    mDetectorSpecCount.clear();
    mDealSucess.clear();
    mDetectorFirstDateTime.clear();
    mDetectorLastDateTime.clear();

    H5FileChannelMeta meta;
    if (!readH5FileChannelMeta(filePath, meta, true, true))
        return;

    applyMeasurementMetaToFileListRow(0, meta);
    updateFileListGapColumns();

    mDetectorSpecCount = meta.specCount;
    mDealSucess.clear();
    for (int detId : meta.specCount.keys())
        mDealSucess[detId] = false;
    mDetectorFirstDateTime = meta.firstDateTime;
    mDetectorLastDateTime = meta.lastDateTime;

    if (meta.valid && meta.commonStart.isValid())
        ui->messureDateTime->setDateTime(meta.commonStart);
    else if (meta.configStartTime.isValid())
        ui->messureDateTime->setDateTime(meta.configStartTime);

    if (meta.configShotTime.isValid())
        ui->fusionDateTime->setDateTime(meta.configShotTime);
    else if (paths.size() == 1 && meta.commonStart.isValid())
        ui->fusionDateTime->setDateTime(meta.commonStart);
    
    //取消时间范围的限制，采用交集方式计算时间范围
    // if (meta.valid && meta.commonStart.isValid() && meta.commonEnd.isValid()) {
    //     const qint64 durationSec = qMax<qint64>(0, meta.commonStart.secsTo(meta.commonEnd));
    //     ui->spinBox_timeEnd->setMaximum(static_cast<int>(durationSec / 60));
    // }
}

void NeutronYieldStatisticsWindow::updateDetectorSpecCountFromAllFiles()
{
    const QStringList paths = filePathsFromTable();
    mDetectorSpecCount.clear();
    mDetectorFirstDateTime.clear();
    mDetectorLastDateTime.clear();

    for (const QString &filePath : paths) {
        H5FileChannelMeta meta;
        if (!readH5FileChannelMeta(filePath, meta, false, false))
            continue;

        for (auto it = meta.specCount.cbegin(); it != meta.specCount.cend(); ++it) {
            const int detId = it.key();
            if (!mDetectorSpecCount.contains(detId) || it.value() > mDetectorSpecCount[detId])
                mDetectorSpecCount[detId] = it.value();

            if (!mDetectorFirstDateTime.contains(detId)
                || meta.firstDateTime.value(detId) < mDetectorFirstDateTime[detId]) {
                mDetectorFirstDateTime[detId] = meta.firstDateTime.value(detId);
            }
            if (!mDetectorLastDateTime.contains(detId)
                || meta.lastDateTime.value(detId) > mDetectorLastDateTime[detId]) {
                mDetectorLastDateTime[detId] = meta.lastDateTime.value(detId);
            }
        }
    }

    for (int detId : mDetectorSpecCount.keys())
        mDealSucess[detId] = false;
}

bool NeutronYieldStatisticsWindow::isPerChannelShotTimeMode() const
{
    return ui->radioButton_perChannelShotTime->isChecked();
}

void NeutronYieldStatisticsWindow::updateShotTimeModeUi()
{
    const bool perChannel = isPerChannelShotTimeMode();
    ui->label_channelId->setVisible(perChannel);
    ui->spinBox_channelId->setVisible(perChannel);
    ui->spinBox_pictureDet->setEnabled(!perChannel);
    if (perChannel)
        syncMeasureDateTimeForDetector(ui->spinBox_channelId->value());
}

int NeutronYieldStatisticsWindow::currentPictureDetId() const
{
    if (isPerChannelShotTimeMode())
        return ui->spinBox_channelId->value();
    return ui->spinBox_pictureDet->value();
}

void NeutronYieldStatisticsWindow::updatePictureForDetector(int detID)
{
    if (detID < 1 || detID > DET_NUM)
        return;

    const QVector<ParseData::mergeSpecData> mergeSpecData = dealFile[detID - 1]->GetMergeSpec();
    const QVector<specStripData> count909_picture = dealFile[detID - 1]->GetCount909Data();
    const bool hasMergeSpec = !mergeSpecData.isEmpty();
    const bool hasFitData = !count909_picture.isEmpty();

    if (!hasMergeSpec && !hasFitData) {
        if (!mDealSucess.value(detID, false)) {
            QMessageBox::information(this, tr("提示"), tr("谱仪%1不存在解析成功的数据！").arg(detID));
        }
        return;
    }

    // 分段合并能谱：合并成功即可绘制，不依赖拟合结果
    if (hasMergeSpec) {
        std::vector<ParseData::mergeSpecData> allSpec_vec;
        allSpec_vec.reserve(static_cast<size_t>(mergeSpecData.size()));
        for (const auto &data : mergeSpecData)
            allSpec_vec.push_back(data);
        slotUpdateMultiSegmentPlotDatas(allSpec_vec);
    }

    // 909 计数曲线与 909 段剥谱图：仅在有成功拟合的分段时绘制
    if (!hasFitData)
        return;

    std::vector<double> time;
    std::vector<double> count;
    std::vector<double> fitCount;
    std::vector<double> residual;

    for (int i = 0; i < count909_picture.size(); i++) {
        time.push_back(count909_picture.at(i).x);
        count.push_back(count909_picture.at(i).y);
        fitCount.push_back(count909_picture.at(i).fit_y);
        residual.push_back(count909_picture.at(i).residual_rate);
    }
    slotUpdate_Count909_time(time, count, fitCount, residual);

    QVector<specStripData> allSpec = dealFile[detID - 1]->GetStripData(0);
    if (!allSpec.isEmpty()) {
        std::vector<double> energy;
        std::vector<double> specCount;
        std::vector<double> fitSpecCount;
        std::vector<double> specResidual;

        for (auto data : allSpec) {
            energy.push_back(data.x);
            specCount.push_back(data.y);
            fitSpecCount.push_back(data.fit_y);
            specResidual.push_back(data.residual_rate);
        }
        slotUpdateSpec_909keV(energy, specCount, fitSpecCount, specResidual);
    }
}

void NeutronYieldStatisticsWindow::syncMeasureDateTimeForDetector(int detId)
{
    if (detId < 1 || !mDetectorFirstDateTime.contains(detId))
        return;

    const QDateTime firstDt = mDetectorFirstDateTime.value(detId);
    if (firstDt.isValid())
        ui->messureDateTime->setDateTime(firstDt);
}

qint64 NeutronYieldStatisticsWindow::beginTimeForDetector(int detId, const QDateTime &fusionTime) const
{
    if (isPerChannelShotTimeMode()) {
        const QDateTime measureStart = mDetectorFirstDateTime.value(detId);
        if (measureStart.isValid() && fusionTime.isValid())
            return measureStart.toSecsSinceEpoch() - fusionTime.toSecsSinceEpoch();
    }

    const qint64 fusionSec = fusionTime.toSecsSinceEpoch();
    const qint64 measureSec = ui->messureDateTime->dateTime().toSecsSinceEpoch();
    return measureSec - fusionSec;
}

void NeutronYieldStatisticsWindow::on_action_lightTheme_triggered()
{
    if(!mIsDarkTheme) return;
    mIsDarkTheme = false;
    qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    if(mThemeColorEnable) QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
    GlobalSettings settings;
    settings.setValue("Global/Startup/darkTheme","false");
    applyColorTheme();
}


void NeutronYieldStatisticsWindow::on_action_darkTheme_triggered()
{
    if(mIsDarkTheme) return;
    mIsDarkTheme = true;
    qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    if(mThemeColorEnable) QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
    GlobalSettings settings;
    settings.setValue("Global/Startup/darkTheme","true");
    applyColorTheme();
}


void NeutronYieldStatisticsWindow::on_action_colorTheme_triggered()
{
    GlobalSettings settings;
    QColor color = QColorDialog::getColor(mThemeColor, this, tr("选择颜色"));
    if (color.isValid()) {
        mThemeColor = color;
        mThemeColorEnable = true;
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
        QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
        settings.setValue("Global/Startup/themeColor",mThemeColor);
    } else {
        mThemeColorEnable = false;
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    }
    settings.setValue("Global/Startup/themeColorEnable",mThemeColorEnable);
    applyColorTheme();
}

void NeutronYieldStatisticsWindow::applyColorTheme()
{
    QList<QCustomPlot*> customPlots = this->findChildren<QCustomPlot*>();
    for (auto customPlot : customPlots){
        QPalette palette = customPlot->palette();
        if (mIsDarkTheme)
        {
            if (this->mThemeColorEnable)
            {
                CustomColorDarkStyle darkStyle(mThemeColor);
                darkStyle.polish(palette);
            }
            else
            {
                DarkStyle darkStyle;
                darkStyle.polish(palette);
            }

            ui->plainTextEdit_log->setStyleSheet(
            "QPlainTextEdit { background-color: black; color: white; }");
        }
        else
        {
            if (this->mThemeColorEnable)
            {
                CustomColorLightStyle lightStyle(mThemeColor);
                lightStyle.polish(palette);
            }
            else
            {
                LightStyle lightStyle;
                lightStyle.polish(palette);
            }

            ui->plainTextEdit_log->setStyleSheet(
            "QPlainTextEdit { background-color: white; color: black; }");
        }
        //日志窗体
        QString styleSheet = mIsDarkTheme ?
                                 QString("background-color:rgb(%1,%2,%3);color:white;")
                                     .arg(palette.color(QPalette::Dark).red())
                                     .arg(palette.color(QPalette::Dark).green())
                                     .arg(palette.color(QPalette::Dark).blue())
                                          : QString("background-color:white;color:black;");

        //更新样式表
        QList<QCheckBox*> checkBoxs = customPlot->findChildren<QCheckBox*>();
        int i = 0;
        for (auto checkBox : checkBoxs){
            checkBox->setStyleSheet(styleSheet);
        }

        QCPColorMap *colorMap = qobject_cast<QCPColorMap*>(customPlot->plottable("colorMap"));
        if (colorMap){
            colorMap->colorScale()->axis()->axisRect()->axis(QCPAxis::atBottom)->setTickLabelColor(mIsDarkTheme ? Qt::white : Qt::black); // 设置底部轴的刻度标签颜色
            colorMap->colorScale()->axis()->axisRect()->axis(QCPAxis::atRight)->setTickLabelColor(mIsDarkTheme ? Qt::white : Qt::black); // 设置右侧轴的刻度标签颜色
        }

        // 窗体背景色
        customPlot->setBackground(QBrush(mIsDarkTheme ? palette.color(QPalette::Dark) : Qt::white));
        // 四边安装轴并显示
        customPlot->axisRect()->setupFullAxesBox();
        customPlot->axisRect()->setBackground(QBrush(mIsDarkTheme ? palette.color(QPalette::Dark) : Qt::white));
        // 坐标轴线颜色
        // customPlot->xAxis->setBasePen(QPen(palette.color(QPalette::WindowText)));
        // customPlot->xAxis2->setBasePen(QPen(palette.color(QPalette::WindowText)));
        // customPlot->yAxis->setBasePen(QPen(palette.color(QPalette::WindowText)));
        // customPlot->yAxis2->setBasePen(QPen(palette.color(QPalette::WindowText)));
        // // 刻度线颜色
        // customPlot->xAxis->setTickPen(QPen(palette.color(QPalette::WindowText)));
        // customPlot->xAxis2->setTickPen(QPen(palette.color(QPalette::WindowText)));
        // customPlot->yAxis->setTickPen(QPen(palette.color(QPalette::WindowText)));
        // customPlot->yAxis2->setTickPen(QPen(palette.color(QPalette::WindowText)));
        // // 子刻度线颜色
        // customPlot->xAxis->setSubTickPen(QPen(palette.color(QPalette::WindowText)));
        // customPlot->xAxis2->setSubTickPen(QPen(palette.color(QPalette::WindowText)));
        // customPlot->yAxis->setSubTickPen(QPen(palette.color(QPalette::WindowText)));
        // customPlot->yAxis2->setSubTickPen(QPen(palette.color(QPalette::WindowText)));
        // // 坐标轴文本标签颜色
        // customPlot->xAxis->setLabelColor(palette.color(QPalette::WindowText));
        // customPlot->xAxis2->setLabelColor(palette.color(QPalette::WindowText));
        // customPlot->yAxis->setLabelColor(palette.color(QPalette::WindowText));
        // customPlot->yAxis2->setLabelColor(palette.color(QPalette::WindowText));
        // // 坐标轴刻度文本标签颜色
        // customPlot->xAxis->setTickLabelColor(palette.color(QPalette::WindowText));
        // customPlot->xAxis2->setTickLabelColor(palette.color(QPalette::WindowText));
        // customPlot->yAxis->setTickLabelColor(palette.color(QPalette::WindowText));
        // customPlot->yAxis2->setTickLabelColor(palette.color(QPalette::WindowText));
        // // 隐藏x2、y2刻度线
        // customPlot->xAxis2->setTicks(false);
        // customPlot->yAxis2->setTicks(false);
        // customPlot->xAxis2->setSubTicks(false);
        // customPlot->yAxis2->setSubTicks(false);

        customPlot->replot();
    }
}

void NeutronYieldStatisticsWindow::restoreSettings()
{
    GlobalSettings settings;
    if(mainWindow) {
        mainWindow->restoreGeometry(settings.value("MainWindow/Geometry").toByteArray());
        mainWindow->restoreState(settings.value("MainWindow/State").toByteArray());
    } else {
        restoreGeometry(settings.value("MainWindow/Geometry").toByteArray());
        restoreState(settings.value("MainWindow/State").toByteArray());
    }
    mThemeColor = settings.value("Global/Startup/themeColor",QColor(30,30,30)).value<QColor>();
    mThemeColorEnable = settings.value("Global/Startup/themeColorEnable",true).toBool();
    if(mThemeColorEnable) {
        QTimer::singleShot(0, this, [&](){
            qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
            QGoodWindow::setAppCustomTheme(mIsDarkTheme,this->mThemeColor); // Must be >96
        });
    }
}

void NeutronYieldStatisticsWindow::replyWriteLog(const QString &msg, QtMsgType msgType)
{
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

#include "QFlowLayout.h"
void NeutronYieldStatisticsWindow::initSpectrumCustomPlot()
{
    QCustomPlot* customPlot = ui->spectorMeter_Spectrum;
    QFlowLayout* flowLayout = new QFlowLayout(ui->widget_flowLayoutContainer, 40, 20, 20);
    flowLayout->setObjectName("flowLayout");
    flowLayout->setContentsMargins(80, 20, 20, 0);
    ui->widget_flowLayoutContainer->setLayout(flowLayout);

    QCustomPlotHelper* customPlotHelper = new QCustomPlotHelper(customPlot, this);
    customPlot->setAntialiasedElements(QCP::aeAll);
    customPlot->legend->setVisible(false);
    customPlot->xAxis->setTickLabelRotation(-45);
    customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
    customPlot->xAxis->rescale(true);
    customPlot->yAxis->rescale(true);

    // 设置刻度范围
    customPlot->xAxis->setRange(0, 2048);
    customPlot->yAxis->setRange(0, 10000);
    customPlot->yAxis->ticker()->setTickCount(5);
    customPlot->xAxis->ticker()->setTickCount(10);

    //设置轴标签名称
    customPlot->xAxis->setLabel(tr("道址"));
    customPlot->yAxis->setLabel(tr("计数率/cps"));

    customPlot->xAxis2->setTicks(false);
    customPlot->xAxis2->setSubTicks(false);
    customPlot->yAxis2->setTicks(false);
    customPlot->yAxis2->setSubTicks(false);

    customPlot->replot();
    connect(customPlot->xAxis, SIGNAL(rangeChanged(const QCPRange &)), customPlot->xAxis2, SLOT(setRange(const QCPRange &)));
    connect(customPlot->yAxis, SIGNAL(rangeChanged(const QCPRange &)), customPlot->yAxis2, SLOT(setRange(const QCPRange &)));
    connect(customPlot, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(slotShowTracer(QMouseEvent*)));
    connect(customPlot, SIGNAL(mouseRelease(QMouseEvent*)), this, SLOT(slotRestorePlot(QMouseEvent*)));
}

void NeutronYieldStatisticsWindow::initCountCustomPlot()
{
    QCustomPlot* customPlot = ui->spectorMeter_Count;
    QCustomPlotHelper* customPlotHelper = new QCustomPlotHelper(customPlot, this);
    customPlotHelper->setResetActionVisible(true);
    customPlotHelper->setClearMarkerActionVisible(false);
    customPlotHelper->setStraightLineActionVisible(true);
    customPlotHelper->setRangeSelectActionVisible(false);

    customPlot->setAntialiasedElements(QCP::aeAll);
    customPlot->legend->setVisible(false);
    customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
    customPlot->xAxis->rescale(true);
    customPlot->yAxis->rescale(true);
    customPlot->plotLayout()->clear();

    // 909全能峰计数随时间变化曲线
    QCPAxisRect *timeCountAxisRect = new QCPAxisRect(customPlot);
    timeCountAxisRect->setObjectName("timeCountAxisRect");
    {
        timeCountAxisRect->setupFullAxesBox();
        timeCountAxisRect->setMinimumMargins(QMargins(0,0,0,0));
        timeCountAxisRect->setMargins(QMargins(0,0,0,0));
        timeCountAxisRect->axis(QCPAxis::AxisType::atBottom)->setPadding(0);
        timeCountAxisRect->axis(QCPAxis::AxisType::atLeft)->setLabel(tr("计数率/cps"));
        timeCountAxisRect->axis(QCPAxis::AxisType::atBottom)->setLabel(tr(""));
        timeCountAxisRect->axis(QCPAxis::AxisType::atBottom)->setTickLabels(false);
        timeCountAxisRect->axis(QCPAxis::AxisType::atBottom)->setRange(790, 1150);
        timeCountAxisRect->axis(QCPAxis::AxisType::atLeft)->setRange(0, 1000);
        timeCountAxisRect->axis(QCPAxis::AxisType::atBottom)->grid()->setZeroLinePen(Qt::NoPen);
    }

    // 909计数随时间变化的拟合曲线
    {
        QCPAxis *keyAxis = timeCountAxisRect->axis(QCPAxis::AxisType::atBottom);
        QCPAxis *valueAxis = timeCountAxisRect->axis(QCPAxis::AxisType::atLeft);
        QCPGraph *graph = customPlot->addGraph(keyAxis, valueAxis);
        graph->setName("timeCountFitGraph");
        graph->setProperty("isTimeCountFitGraph", true);
        graph->setAntialiased(false);
        graph->setPen(QPen(QColor(Qt::red)));
        //graph->setSelectable(QCP::SelectionType::stNone);
        graph->setLineStyle(QCPGraph::lsLine);
        graph->setSmooth(true);
    }

    // 909计数拟合残差
    {
        QCPGraph *graph = customPlot->addGraph(timeCountAxisRect->axis(QCPAxis::AxisType::atBottom), timeCountAxisRect->axis(QCPAxis::AxisType::atLeft));
        graph->setName("timeCountGraph");
        graph->setProperty("isTimeCountGraph", true);
        graph->setAntialiased(false);
        graph->setPen(QPen(QColor(Qt::black)));
        //graph->selectionDecorator()->setPen(QPen(clrLine));
        graph->setLineStyle(QCPGraph::lsNone);
        graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, 5));//显示散点图
    }

    QCPAxisRect *timeCountRemainAxisRect = new QCPAxisRect(customPlot);
    timeCountRemainAxisRect->setObjectName("timeCountRemainAxisRect");
    {
        timeCountRemainAxisRect->setupFullAxesBox();
        timeCountRemainAxisRect->setMinimumMargins(QMargins(0,0,0,0));
        timeCountRemainAxisRect->setMargins(QMargins(0,0,0,0));
        timeCountRemainAxisRect->axis(QCPAxis::AxisType::atTop)->setPadding(0);
        timeCountRemainAxisRect->axis(QCPAxis::AxisType::atLeft)->setLabel(tr("残差(%)"));
        timeCountRemainAxisRect->axis(QCPAxis::AxisType::atBottom)->setLabel(tr("时间(分钟)"));
        timeCountRemainAxisRect->axis(QCPAxis::AxisType::atBottom)->setRange(790, 1150);
        timeCountRemainAxisRect->axis(QCPAxis::AxisType::atLeft)->setRange(-10, 10);

        QCPGraph *graph = customPlot->addGraph(timeCountRemainAxisRect->axis(QCPAxis::AxisType::atBottom), timeCountRemainAxisRect->axis(QCPAxis::AxisType::atLeft));
        graph->setName("timeCountRemainGraph");
        graph->setProperty("isTimeCountRemainGraph", true);
        graph->setAntialiased(false);
        graph->setPen(QPen(Qt::blue));
        //graph->setSelectable(QCP::SelectionType::stNone);
        graph->setLineStyle(QCPGraph::lsNone);
        graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, 5));//显示散点图
    }

    // 设置左边边界联动对齐
    {
        QCPMarginGroup* timeCountMarginGroup = new QCPMarginGroup(customPlot);
        timeCountAxisRect->setMarginGroup(QCP::msLeft | QCP::msRight, timeCountMarginGroup);
        timeCountRemainAxisRect->setMarginGroup(QCP::msLeft | QCP::msRight, timeCountMarginGroup);
    }

    // 布局
    QCPLayoutGrid *timeCountLayout = new QCPLayoutGrid;
    {
        timeCountLayout->setRowSpacing(0);
        timeCountLayout->addElement(0, 0, timeCountAxisRect);
        timeCountLayout->addElement(1, 0, timeCountRemainAxisRect);
        timeCountLayout->setRowStretchFactor(0, 2);
        timeCountLayout->setRowStretchFactor(1, 1);
    }

    QCPAxisRect *spec909_AxisRect = new QCPAxisRect(customPlot);
    spec909_AxisRect->setObjectName("spec909_AxisRect");
    {
        spec909_AxisRect->setupFullAxesBox();
        spec909_AxisRect->setMinimumMargins(QMargins(0,0,0,0));
        spec909_AxisRect->setMargins(QMargins(0,0,0,0));
        spec909_AxisRect->axis(QCPAxis::AxisType::atBottom)->setPadding(0);
        spec909_AxisRect->axis(QCPAxis::AxisType::atLeft)->setLabel(tr("能谱计数"));
        //spec909_AxisRect->axis(QCPAxis::AxisType::atBottom)->setLabel(tr("道址"));
        spec909_AxisRect->axis(QCPAxis::AxisType::atBottom)->setLabel(tr(""));
        spec909_AxisRect->axis(QCPAxis::AxisType::atBottom)->setTickLabels(false);
        spec909_AxisRect->axis(QCPAxis::AxisType::atBottom)->setRange(0, 3600);
        spec909_AxisRect->axis(QCPAxis::AxisType::atLeft)->setRange(0, 2048);
        spec909_AxisRect->axis(QCPAxis::AxisType::atBottom)->grid()->setZeroLinePen(Qt::NoPen);
    }

    // 创建Graph-曲线-阶梯 909keV能段的能谱
    {
        QCPGraph * graph = customPlot->addGraph(spec909_AxisRect->axis(QCPAxis::AxisType::atBottom), spec909_AxisRect->axis(QCPAxis::AxisType::atLeft));
        graph->setName("Spec909_Graph");
        graph->setProperty("isSpec909_Graph", true);
        graph->setAntialiased(false);
        graph->setPen(QPen(QColor(Qt::black)));
        //graph->setSelectable(QCP::SelectionType::stNone);
        //graph->selectionDecorator()->setPen(QPen(clrLine));
        graph->setLineStyle(QCPGraph::lsStepCenter);
        graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, 5));//显示散点图
    }

    // 创建Graph-曲线-拟合 909keV能段的能谱拟合曲线
    {
        QCPGraph * graph = customPlot->addGraph(spec909_AxisRect->axis(QCPAxis::AxisType::atBottom), spec909_AxisRect->axis(QCPAxis::AxisType::atLeft));
        graph->setName("spec909_FitGraph");
        graph->setProperty("isSpec909_FitGraph", true);
        graph->setAntialiased(false);
        graph->setPen(QPen(QColor(Qt::red)));
        //graph->setSelectable(QCP::SelectionType::stNone);
        //graph->selectionDecorator()->setPen(QPen(clrLine));
        graph->setLineStyle(QCPGraph::lsLine);
        graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, 5));//显示散点图
    }

    QCPAxisRect *Spec909_residualAxisRect = new QCPAxisRect(customPlot);
    Spec909_residualAxisRect->setObjectName("Spec909_residualAxisRect");
    {
        Spec909_residualAxisRect->setupFullAxesBox();
        Spec909_residualAxisRect->setMinimumMargins(QMargins(0,0,0,0));
        Spec909_residualAxisRect->setMargins(QMargins(0,0,0,0));
        Spec909_residualAxisRect->axis(QCPAxis::AxisType::atTop)->setPadding(0);
        Spec909_residualAxisRect->axis(QCPAxis::AxisType::atLeft)->setLabel(tr("残差(%)"));
        Spec909_residualAxisRect->axis(QCPAxis::AxisType::atBottom)->setLabel(tr("能量/keV"));
        Spec909_residualAxisRect->axis(QCPAxis::AxisType::atBottom)->setRange(0, 3600);
        Spec909_residualAxisRect->axis(QCPAxis::AxisType::atLeft)->setRange(-10, 10);

        QCPGraph *graph = customPlot->addGraph(Spec909_residualAxisRect->axis(QCPAxis::AxisType::atBottom), Spec909_residualAxisRect->axis(QCPAxis::AxisType::atLeft));
        graph->setName("Spec909_residualGraph");
        graph->setProperty("isSpec909_residualGraph", true);
        graph->setAntialiased(false);
        graph->setPen(QPen(QColor(Qt::black)));
        //graph->selectionDecorator()->setPen(QPen(clrLine));
        graph->setLineStyle(QCPGraph::lsNone);
        graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, 5));//显示散点图
    }

    // 设置左边边界联动对齐
    {
        QCPMarginGroup* energyCountMarginGroup = new QCPMarginGroup(customPlot);
        spec909_AxisRect->setMarginGroup(QCP::msLeft | QCP::msRight, energyCountMarginGroup);
        Spec909_residualAxisRect->setMarginGroup(QCP::msLeft | QCP::msRight, energyCountMarginGroup);
    }

    // 布局
    QCPLayoutGrid *energyCountLayout = new QCPLayoutGrid;
    {
        energyCountLayout->setRowSpacing(0);
        energyCountLayout->addElement(0, 0, spec909_AxisRect);
        energyCountLayout->addElement(1, 0, Spec909_residualAxisRect);
        energyCountLayout->setRowStretchFactor(0, 2);
        energyCountLayout->setRowStretchFactor(1, 1);
    }


    // 关联信号槽函数
    {
        connect(spec909_AxisRect->axis(QCPAxis::AxisType::atBottom), SIGNAL(rangeChanged(QCPRange)), spec909_AxisRect->axis(QCPAxis::AxisType::atTop), SLOT(setRange(QCPRange)));
        connect(spec909_AxisRect->axis(QCPAxis::AxisType::atLeft), SIGNAL(rangeChanged(QCPRange)), spec909_AxisRect->axis(QCPAxis::AxisType::atRight), SLOT(setRange(QCPRange)));
        connect(spec909_AxisRect->axis(QCPAxis::AxisType::atBottom), SIGNAL(rangeChanged(QCPRange)), Spec909_residualAxisRect->axis(QCPAxis::AxisType::atBottom), SLOT(setRange(QCPRange)));
        connect(Spec909_residualAxisRect->axis(QCPAxis::AxisType::atBottom), SIGNAL(rangeChanged(QCPRange)), Spec909_residualAxisRect->axis(QCPAxis::AxisType::atTop), SLOT(setRange(QCPRange)));
        connect(Spec909_residualAxisRect->axis(QCPAxis::AxisType::atLeft), SIGNAL(rangeChanged(QCPRange)), Spec909_residualAxisRect->axis(QCPAxis::AxisType::atRight), SLOT(setRange(QCPRange)));
        connect(Spec909_residualAxisRect->axis(QCPAxis::AxisType::atBottom), SIGNAL(rangeChanged(QCPRange)), spec909_AxisRect->axis(QCPAxis::AxisType::atBottom), SLOT(setRange(QCPRange)));

        connect(timeCountAxisRect->axis(QCPAxis::AxisType::atBottom), SIGNAL(rangeChanged(QCPRange)), timeCountAxisRect->axis(QCPAxis::AxisType::atTop), SLOT(setRange(QCPRange)));
        connect(timeCountAxisRect->axis(QCPAxis::AxisType::atLeft), SIGNAL(rangeChanged(QCPRange)), timeCountAxisRect->axis(QCPAxis::AxisType::atRight), SLOT(setRange(QCPRange)));
        connect(timeCountAxisRect->axis(QCPAxis::AxisType::atBottom), SIGNAL(rangeChanged(QCPRange)), timeCountRemainAxisRect->axis(QCPAxis::AxisType::atBottom), SLOT(setRange(QCPRange)));
        connect(timeCountRemainAxisRect->axis(QCPAxis::AxisType::atBottom), SIGNAL(rangeChanged(QCPRange)), timeCountRemainAxisRect->axis(QCPAxis::AxisType::atTop), SLOT(setRange(QCPRange)));
        connect(timeCountRemainAxisRect->axis(QCPAxis::AxisType::atLeft), SIGNAL(rangeChanged(QCPRange)), timeCountRemainAxisRect->axis(QCPAxis::AxisType::atRight), SLOT(setRange(QCPRange)));
        connect(timeCountRemainAxisRect->axis(QCPAxis::AxisType::atBottom), SIGNAL(rangeChanged(QCPRange)), timeCountAxisRect->axis(QCPAxis::AxisType::atBottom), SLOT(setRange(QCPRange)));
    }

    QList<QCPAxis*> allAxes;
    allAxes << timeCountAxisRect->axes() << timeCountRemainAxisRect->axes() <<  spec909_AxisRect->axes() << Spec909_residualAxisRect->axes();
    foreach (QCPAxis *axis, allAxes)
    {
        axis->setLayer("axes");
        axis->grid()->setLayer("grid");
    }

    customPlot->plotLayout()->addElement(0, 0, timeCountLayout);//上面显示计数
    customPlot->plotLayout()->addElement(1, 0, energyCountLayout);//下面显示能量

    customPlot->replot();
    connect(customPlot->xAxis, SIGNAL(rangeChanged(const QCPRange &)), customPlot->xAxis2, SLOT(setRange(const QCPRange &)));
    connect(customPlot->yAxis, SIGNAL(rangeChanged(const QCPRange &)), customPlot->yAxis2, SLOT(setRange(const QCPRange &)));
    connect(customPlot, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(slotShowTracer(QMouseEvent*)));
    connect(customPlot, SIGNAL(mouseRelease(QMouseEvent*)), this, SLOT(slotRestorePlot(QMouseEvent*)));

    connect(customPlotHelper, &QCustomPlotHelper::itemActived, this, [=](QCPGraph *graph, double key, double value){
        const int detID = currentPictureDetId();

        if (graph->name() != "timeCountGraph")
            return;

        // 显示散点、计数率和残差%
        int index = 0; //当前选中的计数点序次号，从0开始计数
        double eps = 0.00001; // 单位min

        //索引给出该点属于第几个点，下标从0开始
        QVector<specStripData> count909 = dealFile[detID-1]->GetCount909Data();
        for(auto data:count909)
        {
            if(abs(key - data.x)<eps) {
                break;
            }
            index++;
        }

        QVector<specStripData> allSpec = dealFile[detID-1]->GetStripData(index);

        std::vector<double> energy; //能量
        std::vector<double> specCount; // 能谱计数
        std::vector<double> fitSpecCount; // 拟合曲线
        std::vector<double> residual;// 残差
        int num = energy.size();
        energy.reserve(num);
        for(auto data:allSpec)
        {
            energy.push_back(data.x);
            specCount.push_back(data.y);
            fitSpecCount.push_back(data.fit_y);
            residual.push_back(data.residual_rate);
        }

        slotUpdateSpec_909keV(energy, specCount, fitSpecCount, residual);
    });
}

void NeutronYieldStatisticsWindow::initYieldCustomPlot()
{
   // auto *w = new MollweideHeatmapWidget(ui->heatmapContainer);
    w = new CrasterParabolicHeatmapWidget(ui->yieldSpatialDistribution);
    QVBoxLayout *layout = new QVBoxLayout(ui->yieldSpatialDistribution);
    layout->setContentsMargins(0,0,0,0);
    layout->addWidget(w);

    // 示例：塞 30 个点数据（你替换成真实数据）
    std::vector<Sensor> sensors = {
            {-180, 0, 120}, {-150, 0, 110},{-120, 0, 113},{-90, 0, 112},{-60, 0, 94},{-30, 0, 103},
            {0, 0, 112}, {30, 0, 112}, {60, 0, 102}, {120, 0, 124}, {150, 0, 96},
            {-180, 45, 76}, {-90, 45, 82}, {0, 45, 73}, {90, 45, 60}, {156, 45, 75},
            {-180, -45, 86}, {-90, -45, 83}, {0, -45, 90}, {90, -45, 76}, {156, -45, 76},
            {0, -90, 60}, {0, 90, 40}};
    // ...补齐你的30个
    w->setSensors(sensors);

    // Mollweide投影
    /*{
        //方式1 球谐函数插值
        w->setInterpMode(MollweideHeatmapWidget::InterpMode::SphericalHarmonics);
        w->setSHOrder(4);       // 3或4
        w->setSHLambda(1e-4);   // 稳一点
        w->rebuild();

        //方式2
        // w->setInterpMode(MollweideHeatmapWidget::InterpMode::IDW);
        // w->setPower(2.0);
        // w->rebuild();
    }
    */

    // Craster Parabolic 投影
    {
        //方式1 球谐函数插值
        w->setInterpMode(CrasterParabolicHeatmapWidget::InterpMode::SphericalHarmonics);
        w->setSHOrder(4);       // 3或4
        w->setSHLambda(1e-4);   // 稳一点
        w->rebuild();

        //方式2
        // w->setInterpMode(CrasterParabolicHeatmapWidget::InterpMode::IDW);
        // w->setPower(2.0);
        // w->rebuild();
    }
    // 如果你后续数据会变：更新 sensors 后再 rebuild()
}

void NeutronYieldStatisticsWindow::on_action_open_triggered()
{
    on_bt_addFiles_clicked();
}

void NeutronYieldStatisticsWindow::on_bt_addFiles_clicked()
{
    GlobalSettings settings;
    const QString lastPath = settings.value("mainWindow/LastFilePath", QDir::homePath()).toString();
    const QString filter = tr("测量文件 (*.H5);;所有文件 (*.*)");
    const QStringList filePaths = QFileDialog::getOpenFileNames(
        this, tr("添加测量数据文件（可多选）"), lastPath, filter);

    if (filePaths.isEmpty())
        return;

    addFilesToList(filePaths);
    settings.setValue("mainWindow/LastFilePath", QFileInfo(filePaths.last()).absolutePath());
}

void NeutronYieldStatisticsWindow::on_bt_removeFile_clicked()
{
    QTableWidget *table = ui->tableWidget_fileList;
    const int row = table->currentRow();
    if (row < 0)
        return;

    table->removeRow(row);
    refreshFileListBasicColumns();
    updateMeasurementInfoFromFirstFile();
}

void NeutronYieldStatisticsWindow::on_bt_clearFiles_clicked()
{
    ui->tableWidget_fileList->setRowCount(0);
    mFilePaths.clear();
    mDetectorSpecCount.clear();
    mDealSucess.clear();
    mDetectorFirstDateTime.clear();
    mDetectorLastDateTime.clear();
}

void NeutronYieldStatisticsWindow::on_btn_sort_clicked()
{
    sortFileListByStartTime();
    emit reporWriteLog(tr("文件列表已按开始时刻排序"));
}


void NeutronYieldStatisticsWindow::on_action_exit_triggered()
{
    mainWindow->close();
}


void NeutronYieldStatisticsWindow::on_action_startCalculate_triggered()
{
    mInterrupted = false;

    const int startTime = ui->spinBox_timeStart->value() * 60;
    const int endTime = ui->spinBox_timeEnd->value() * 60;
    const int timeStep = ui->spinBox_step->value() * 60;

    if (startTime >= endTime) {
        QMessageBox::information(this, tr("提示"), tr("退出解析！输入的解析时间起点>=解析时间终点。"));
        return;
    }

    const QStringList allFilePaths = filePathsFromTable();
    if (allFilePaths.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("请先添加测量文件。"));
        return;
    }

    QStringList h5FilePaths;
    for (const QString &filePath : allFilePaths) {
        if (QFileInfo(filePath).suffix().compare(QStringLiteral("H5"), Qt::CaseInsensitive) == 0)
            h5FilePaths.append(filePath);
    }
    if (h5FilePaths.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("当前仅支持 H5 测量文件的多文件分段合并解析。"));
        return;
    }

    updateDetectorSpecCountFromAllFiles();
    if (mDetectorSpecCount.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("所选文件中没有可用的探测器能谱数据。"));
        return;
    }

    const bool perChannelMode = isPerChannelShotTimeMode();
    const QDateTime fusionTime = ui->fusionDateTime->dateTime();
    if (!fusionTime.isValid()) {
        QMessageBox::information(this, tr("提示"), tr("打靶时刻无效，请先设置打靶时刻。"));
        return;
    }

    QList<int> detIds;
    if (perChannelMode) {
        const int detID = ui->spinBox_channelId->value();
        if (detID < 1 || detID > DET_NUM) {
            QMessageBox::information(this, tr("提示"), tr("通道号无效，请输入 1~%1。").arg(DET_NUM));
            return;
        }
        if (!mDetectorSpecCount.contains(detID)) {
            QMessageBox::information(this, tr("提示"), tr("谱仪%1无有效能谱数据。").arg(detID));
            return;
        }
        detIds.append(detID);
        syncMeasureDateTimeForDetector(detID);
    } else {
        detIds = mDetectorSpecCount.keys();
        std::sort(detIds.begin(), detIds.end());
    }

    const QDateTime windowLeft = fusionTime.addSecs(startTime);
    const QDateTime windowRight = fusionTime.addSecs(endTime);

    const QString tmStart = windowLeft.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    const QString tmEnd = windowRight.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    const QString step = QString::number(timeStep / 60);
    const QString f_segment = QString::number((endTime - startTime) * 1.0 / timeStep, 'f', 1);
    const QString i_segment = QString::number((endTime - startTime) / timeStep);

    QString msgTip;
    if (perChannelMode) {
        msgTip = QString(tr("模式：分通道打靶时刻"
                            "\n通道：谱仪 %1"
                            "\n打靶时刻：%2"
                            "\n起点时间：%3"
                            "\n结束时间：%4"
                            "\n时间步长：%5分钟"
                            "\n计算结果：%6条曲线，根据整除原则，将只显示%7条曲线"
                            "\n\n点击[Yes]开始解析"
                            "\n点击[No]返回重新修改"))
                   .arg(detIds.first())
                   .arg(fusionTime.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")))
                   .arg(tmStart, tmEnd, step, f_segment, i_segment);
    } else {
        msgTip = QString(tr("模式：统一打靶时刻"
                            "\n打靶时刻：%1"
                            "\n起点时间：%2"
                            "\n结束时间：%3"
                            "\n时间步长：%4分钟"
                            "\n计算结果：%5条曲线，根据整除原则，将只显示%6条曲线"
                            "\n\n点击[Yes]开始解析"
                            "\n点击[No]返回重新修改"))
                   .arg(fusionTime.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")))
                   .arg(tmStart, tmEnd, step, f_segment, i_segment);
    }
    if (QMessageBox::question(this, tr("提示"), msgTip, QMessageBox::Yes, QMessageBox::No) != QMessageBox::Yes)
        return;

    emit reporWriteLog(tr("开始解析..."));
    emit reporWriteLog(tr("解析窗口：%1 ~ %2，步长 %3 s，全局 %4 段")
                           .arg(tmStart, tmEnd)
                           .arg(timeStep)
                           .arg((endTime - startTime) / timeStep));
    if (perChannelMode) {
        emit reporWriteLog(tr("分通道模式：谱仪 %1，打靶时刻 %2")
                               .arg(detIds.first())
                               .arg(fusionTime.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))));
    } else {
        emit reporWriteLog(tr("统一打靶时刻：%1")
                               .arg(fusionTime.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))));
    }

    QString detectorList;
    for (int detID : detIds) {
        if (detID < 1 || detID > DET_NUM) {
            emit reporWriteLog(tr("无效通道号 %1，已跳过").arg(detID), QtWarningMsg);
            continue;
        }

        detectorList += QString::number(detID) + ",";

        if (mInterrupted)
            break;

        mDealSucess[detID] = false;
        delete dealFile[detID - 1];
        dealFile[detID - 1] = new ParseData();
        dealFile[detID - 1]->setStartTime(static_cast<int>(beginTimeForDetector(detID, fusionTime)));

        bool mergeOk = dealFile[detID - 1]->getResult_offlineMultiFileUtc(
            h5FilePaths,
            static_cast<quint32>(detID),
            fusionTime,
            windowLeft,
            windowRight,
            timeStep);

        const QStringList fitErrors = dealFile[detID - 1]->fitFailureMessages();
        for (const QString &msg : fitErrors)
            emit reporWriteLog(msg, QtWarningMsg);

        if (!mergeOk) {
            emit reporWriteLog(tr("谱仪%1解析失败").arg(detID));
            emit reporWriteLog(QString("谱仪%1解析完成").arg(detID));
            continue;
        }

        mDealSucess[detID] = true;
        emit sigSuccess(detID);

        if (dealFile[detID - 1]->successfulFitSegmentCount() == 0 && fitErrors.isEmpty()) {
            emit reporWriteLog(tr("谱仪%1能谱拟合失败，解析失败").arg(detID), QtWarningMsg);
        }

        emit reporWriteLog(QString("谱仪%1解析完成").arg(detID));
    }

    QString msg;
    if (perChannelMode) {
        const int detID = detIds.isEmpty() ? 0 : detIds.first();
        msg = mDealSucess.value(detID, false)
            ? tr("谱仪%1解析成功。").arg(detID)
            : tr("谱仪%1解析失败。").arg(detID);
    } else {
        if (!detectorList.isEmpty())
            detectorList.chop(1);
        msg = QString(tr("文件全部解析完成，包含谱仪：%1")).arg(detectorList);
    }
    emit reporWriteLog(msg);
    QMessageBox::information(this, tr("提示"), msg);
}


void NeutronYieldStatisticsWindow::on_action_stopCalculate_triggered()
{
    emit reporWriteLog(tr("中断解析"));
    mInterrupted = true;
}

void NeutronYieldStatisticsWindow::updateYieldSpatialDistributionForRow(int row)
{
    if (row < 0 || row >= ui->tableWidget->rowCount())
        return;

    const int detID = row + 1;
    QTableWidgetItem *yieldItem = ui->tableWidget->item(row, 1);
    QTableWidgetItem *longitudeItem = ui->tableWidget->item(row, 3);
    QTableWidgetItem *latitudeItem = ui->tableWidget->item(row, 4);

    const double yield = yieldItem ? yieldItem->text().toDouble() : 0.0;
    const double longitude = longitudeItem ? longitudeItem->text().toDouble() : 0.0;
    const double latitude = latitudeItem ? latitudeItem->text().toDouble() : 0.0;

    myield[detID] = Sensor{longitude, latitude, yield};
    slotUpdate_yieldDis(myield);
}

void NeutronYieldStatisticsWindow::on_tableWidget_itemChanged(QTableWidgetItem *item)
{
    if (!item)
        return;

    const int col = item->column();
    if (col != 3 && col != 4)
        return;

    updateYieldSpatialDistributionForRow(item->row());
}

void NeutronYieldStatisticsWindow::on_tableWidget_selectionChanged()
{
    QList<QTableWidgetItem*> selectedItems = ui->tableWidget->selectedItems();
    if (selectedItems.isEmpty())
        return;

    const int row = selectedItems[0]->row();
    if (row < 0)
        return;

    updatePictureForDetector(row + 1);
}

void NeutronYieldStatisticsWindow::slotFail()
{
    // QTimer::singleShot(1, this, [=](){
    //     SplashWidget::instance()->hide();
    // });
    QMessageBox::information(this, tr("提示"), tr("文件解析失败！"));
}

void NeutronYieldStatisticsWindow::slotSuccess(int detID)
{
    qDebug() << "slotSuccess called with detID:" << detID;
    // 显示909初始计数
    double count909_N0 = dealFile[detID-1]->GetInitialCount();
    if(detID > 0 && detID <= ui->tableWidget->rowCount()){
        auto *countItem = new QTableWidgetItem(QString::number(count909_N0));
        applyDetectorTableItemFlags(countItem, 5);
        ui->tableWidget->setItem(detID-1, 5, countItem);

        //读取灵敏度标定数据，给出中子产额
        QSettings mSettings("./config/NeutronYield.ini", QSettings::IniFormat);
        double yield_c = mSettings.value(QString("Table/yield%1").arg(detID), 1.0).toDouble();
        double initN_c = mSettings.value(QString("Table/initial909Count%1").arg(detID), 1.0).toDouble();
        double yield = 0.0;
        if(initN_c>0.0){
            yield = count909_N0 / initN_c * yield_c;
        }

        auto *yieldItem = new QTableWidgetItem(QString::number(yield));
        applyDetectorTableItemFlags(yieldItem, 1);
        ui->tableWidget->setItem(detID-1, 1, yieldItem);

        //从界面左侧表格读取探测器经纬度(这意味着用户可以离线分析中手动修改各个探测器的坐标位置）
        updateYieldSpatialDistributionForRow(detID - 1);
    }

    if (detID == currentPictureDetId())
        updatePictureForDetector(detID);

    // QTimer::singleShot(1, this, [=](){
    //     SplashWidget::instance()->hide();
    // });
}

//更新多段能谱数据
void NeutronYieldStatisticsWindow::slotUpdateMultiSegmentPlotDatas(std::vector<ParseData::mergeSpecData> allSpectrum)
{
    QCustomPlot *customPlot = ui->spectorMeter_Spectrum;
    customPlot->clearGraphs();

    //清空布局内所有的控件
    QFlowLayout *flowLayout = this->findChild<QFlowLayout*>("flowLayout");
    QWidget* flowLayoutContainer = ui->widget_flowLayoutContainer;
    while (QLayoutItem* item = flowLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        if (QLayout* childLayout = item->layout()) {
            delete childLayout;
        }
        delete item;
    }

    QList<QColor> color ={ Qt::red, Qt::green, Qt::blue, Qt::cyan, Qt::darkCyan};
    //根据能谱个数重新生成控件
    QCheckBox *checkBoxAll = new QCheckBox(flowLayoutContainer);
    checkBoxAll->setObjectName("checkBoxAll");
    checkBoxAll->setVisible(true);
    checkBoxAll->setProperty("checkedCount", allSpectrum.size());
    checkBoxAll->setText("显示所有曲线");
    checkBoxAll->setFixedWidth(80);
    checkBoxAll->setChecked(true);
    flowLayout->addWidget(checkBoxAll);
    for (size_t i=0; i<allSpectrum.size(); ++i){
        //用横向布局包裹起来
        QWidget* w = new QWidget(flowLayoutContainer);
        w->setContentsMargins(0,0,0,0);
        QHBoxLayout * hBoxLayout = new QHBoxLayout(w);
        //hBoxLayout->setSpacing(9);
        hBoxLayout->setMargin(0);
        w->setLayout(hBoxLayout);
        w->setFixedWidth(80);

        QCheckBox *checkBox = new QCheckBox(w);
        checkBox->setProperty("index", i+1);
        checkBox->setVisible(true);
        checkBox->setChecked(true);
        QLabel *labelColor = new QLabel(w);
        labelColor->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        labelColor->setFixedWidth(20);
        labelColor->setFixedHeight(20);
        labelColor->setVisible(true);
        labelColor->setStyleSheet(QString("background-color:rgb(%1,%2,%3)").arg(color[i%5].red()).arg(color[i%5].green()).arg(color[i%5].blue()));
        QLabel *label = new QLabel(w);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        label->setText(QString("#%1").arg(i+1));
        label->setVisible(true);
        //label->setFixedWidth(50);
        hBoxLayout->addWidget(checkBox);
        hBoxLayout->addWidget(labelColor);
        hBoxLayout->addWidget(label);
        hBoxLayout->addStretch();
        flowLayout->addWidget(w);
        connect(checkBox, &QCheckBox::stateChanged, this, [=](int state){
            int index = checkBox->property("index").toInt();
            QCPGraph *graph = customPlot->graph(QString("energyGraph#%1").arg(index));
            if (graph){
                if (state == Qt::CheckState::Checked && !graph->visible()){
                    graph->setVisible(state == Qt::CheckState::Checked ? true : false);
                    customPlot->replot(QCustomPlot::rpQueuedReplot);
                } else if (state == Qt::CheckState::Unchecked && graph->visible()) {
                    graph->setVisible(state == Qt::CheckState::Checked ? true : false);
                    customPlot->replot(QCustomPlot::rpQueuedReplot);
                }
            }
        });

        ui->widget_flowLayoutContainer->show();
    }
    connect(checkBoxAll, &QCheckBox::stateChanged, this, [=](int state){
        // QWidget* flowLayoutContainer = ui->widget_flowLayoutContainer;
        QList<QCheckBox *>checkBoxs = flowLayoutContainer->findChildren<QCheckBox*>();
        for (auto checkBox : checkBoxs){
            checkBox->setChecked(state == Qt::CheckState::Checked ? true : false);
        }
        // checkBoxAll->blockSignals(false);
    });

    //能谱
    {
        for (size_t i=0; i<allSpectrum.size(); ++i){
            //QCPGraph *graph = customPlot->addGraph(customPlot->xAxis, customPlot->yAxis);
            QCPGraph *graph = customPlot->addGraph();

            graph->setName(QString("energyGraph#%1").arg(i+1));
            graph->setAntialiased(true);
            graph->setPen(QPen(QBrush(color[i%5]), 2, Qt::SolidLine));
            //graph->setSelectable(QCP::SelectionType::stNone);
            graph->setLineStyle(QCPGraph::lsNone);
            graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCross, 5));//显示散点图
            graph->setVisible(true);

            QVector<double> keys, values;
            QVector<QColor> colors;
            for (int j=0; j<2048; ++j){
                keys << j + 1;
                values << allSpectrum[i].spectrum[j] * 1.0;
                colors << color[i%5];
            }
            graph->setData(keys, values, colors);
        }
    }

    customPlot->rescaleAxes(true);
    customPlot->replot();
}

void NeutronYieldStatisticsWindow::slotUpdate_Count909_time(std::vector<double> time/*时刻*/, std::vector<double> count/*散点*/,
                              std::vector<double> fitcount/*拟合曲线*/, std::vector<double> residual/*残差*/)
{
    QCustomPlot *customPlot = ui->spectorMeter_Count;

    //计数随时间变化的散点
    {
        QCPAxisRect *timeCountAxisRect = this->findChild<QCPAxisRect*>("timeCountAxisRect");
        QCPAxis *keyAxis = timeCountAxisRect->axis(QCPAxis::AxisType::atBottom);
        QCPAxis *valueAxis = timeCountAxisRect->axis(QCPAxis::AxisType::atLeft);
        QCPGraph *graph = customPlot->graph("timeCountGraph");
        if (!graph){
            graph = customPlot->addGraph(keyAxis, valueAxis);

            graph->setName(QString("timeCountGraph"));
            graph->setProperty("isTimeCountGraph", true);
            graph->setAntialiased(true);
            graph->setPen(QPen(QBrush(Qt::black), 2, Qt::SolidLine));
            //graph->setSelectable(QCP::SelectionType::stSingleData);
            graph->setLineStyle(QCPGraph::lsNone);
            graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, 5));//显示散点图
            graph->setVisible(true);
        }
        QVector<double> keys, values;
        double minY = count[0], maxY=0.0;
        for (int i=0; i<time.size(); ++i){
            keys << time[i];
            values << count[i];
            minY = qMin(minY, count[i]);
            maxY = qMax(maxY, count[i]);
        }
        graph->setData(keys, values);
        keyAxis->rescale(true);

        //设置留白区域，1.1则表示左右各留白10%
        keyAxis->scaleRange(1.05, keyAxis->range().center());
        valueAxis->setRange(minY*0.95, maxY*1.05);
        // valueAxis->scaleRange(1.05, valueAxis->range().center());
    }

    //计数拟合曲线
    {
        QCPAxisRect *timeCountAxisRect = this->findChild<QCPAxisRect*>("timeCountAxisRect");
        QCPAxis *keyAxis = timeCountAxisRect->axis(QCPAxis::AxisType::atBottom);
        QCPAxis *valueAxis = timeCountAxisRect->axis(QCPAxis::AxisType::atLeft);
        QCPGraph *graph = customPlot->graph("timeCountFitGraph");
        if (!graph){
            graph = customPlot->addGraph(keyAxis, valueAxis);

            graph->setName(QString("timeCountFitGraph"));
            graph->setProperty("isTimeCountFitGraph", true);
            graph->setAntialiased(true);
            graph->setPen(QPen(QBrush(Qt::red), 2, Qt::SolidLine));
            //graph->setSelectable(QCP::SelectionType::stNone);
            graph->setLineStyle(QCPGraph::lsLine);
            graph->setSmooth(true);
            graph->setVisible(true);
        }

        QVector<double> keys, values;
        for (int i=0; i<time.size(); ++i){
            keys << time[i];
            values << fitcount[i];
        }

        //拟合
        graph->setData(keys, values);
        keyAxis->rescale(true);
        // valueAxis->rescale(true);
        //设置留白区域，1.1则表示左右各留白10%
        keyAxis->scaleRange(1.05, keyAxis->range().center());
        // valueAxis->scaleRange(1.05, valueAxis->range().center());
    }

    //909峰位计数残差散点
    {
        QCPAxisRect *timeCountRemainAxisRect = this->findChild<QCPAxisRect*>("timeCountRemainAxisRect");
        QCPAxis *keyAxis = timeCountRemainAxisRect->axis(QCPAxis::AxisType::atBottom);
        QCPAxis *valueAxis = timeCountRemainAxisRect->axis(QCPAxis::AxisType::atLeft);
        QCPGraph *graph = customPlot->graph("timeCountRemainGraph");
        if (!graph){
            graph = customPlot->addGraph(keyAxis, valueAxis);

            graph->setName(QString("timeCountRemainGraph"));
            graph->setProperty("isTimeCountRemainGraph", true);
            graph->setAntialiased(true);
            graph->setPen(QPen(QBrush(QColor(0,0,0)), 2, Qt::SolidLine));
            graph->setLineStyle(QCPGraph::lsNone);
            graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, 5));//显示散点图
            graph->setVisible(true);
        }

        QVector<double> keys, values;
        double maxAbs = 0.0;
        for (int i=0; i<time.size(); ++i){
            keys << time[i];
            values << residual[i];
            maxAbs = qMax(maxAbs, qAbs(residual[i]));
        }
        graph->setData(keys, values);

        keyAxis->rescale(true);
        //设置留白区域，如1.1则表示左右各留白10%
        keyAxis->scaleRange(1.05, keyAxis->range().center());
        if (maxAbs < 1e-12) maxAbs = 1.0;
        maxAbs *= 1.05;
        valueAxis->setRange(-maxAbs, maxAbs);

        keyAxis->scaleRange(1.05, keyAxis->range().center());
    }

    //customPlot->rescaleAxes(true);
    customPlot->replot();
}

void NeutronYieldStatisticsWindow::slotUpdateSpec_909keV(std::vector<double> channel, std::vector<double> count, std::vector<double> fitcount, std::vector<double>residual)
{
    QCustomPlot *customPlot = ui->spectorMeter_Count;

    //909段原始能谱
    {
        QCPAxisRect *spec909_AxisRect = this->findChild<QCPAxisRect*>("spec909_AxisRect");
        QCPAxis *keyAxis = spec909_AxisRect->axis(QCPAxis::AxisType::atBottom);
        QCPAxis *valueAxis = spec909_AxisRect->axis(QCPAxis::AxisType::atLeft);
        QCPGraph *graph = customPlot->graph("Spec909_Graph");
        if (!graph){
            graph = customPlot->addGraph(keyAxis, valueAxis);

            graph->setName(QString("Spec909_Graph"));
            graph->setProperty("isSpec909_Graph", true);
            graph->setAntialiased(true);
            graph->setPen(QPen(QBrush(Qt::black), 1, Qt::SolidLine));
            graph->setLineStyle(QCPGraph::lsStepCenter);
            //graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, 5));//显示散点图
            graph->setVisible(true);
        }
        QVector<double> keys, values;
        for (int i=0; i<channel.size(); ++i){
            keys << channel[i];
            values << count[i];
        }
        graph->setData(keys, values);
        keyAxis->rescale(true);
        valueAxis->rescale(true);
        //设置留白区域，1.1则表示左右各留白10%
        keyAxis->scaleRange(1.05, keyAxis->range().center());
        valueAxis->scaleRange(1.05, valueAxis->range().center());
    }

    { // 909段拟合能谱
        QCPAxisRect *spec909_AxisRect = this->findChild<QCPAxisRect*>("spec909_AxisRect");
        QCPAxis *keyAxis = spec909_AxisRect->axis(QCPAxis::AxisType::atBottom);
        QCPAxis *valueAxis = spec909_AxisRect->axis(QCPAxis::AxisType::atLeft);
        QCPGraph *graph = customPlot->graph("spec909_FitGraph");
        if (!graph){
            graph = customPlot->addGraph(keyAxis, valueAxis);

            graph->setName(QString("spec909_FitGraph"));
            graph->setProperty("isSpec909_FitGraph", true);
            graph->setAntialiased(true);
            graph->setPen(QPen(QBrush(Qt::red), 1, Qt::SolidLine));
            graph->setLineStyle(QCPGraph::lsLine);
            graph->setSmooth(true);
            //graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, 5));//显示散点图
            graph->setVisible(true);
        }
        QVector<double> keys, values;
        for (int i=0; i<channel.size(); ++i){
            keys << channel[i];
            values << fitcount[i];
        }

        //拟合
        graph->setData(keys, values);
        keyAxis->rescale(true);
        valueAxis->rescale(true);
        //设置留白区域，1.1则表示左右各留白10%
        keyAxis->scaleRange(1.05, keyAxis->range().center());
        valueAxis->scaleRange(1.05, valueAxis->range().center());
    }

    { //909段能谱拟合残差，相对残差
        QCPAxisRect *Spec909_residualAxisRect = this->findChild<QCPAxisRect*>("Spec909_residualAxisRect");
        QCPAxis *keyAxis = Spec909_residualAxisRect->axis(QCPAxis::AxisType::atBottom);
        QCPAxis *valueAxis = Spec909_residualAxisRect->axis(QCPAxis::AxisType::atLeft);
        QCPGraph *graph = customPlot->graph("Spec909_residualGraph");
        if (!graph){
            graph = customPlot->addGraph(keyAxis, valueAxis);

            graph->setName(QString("Spec909_residualGraph"));
            graph->setProperty("isSpec909_residualGraph", true);
            graph->setAntialiased(true);
            graph->setPen(QPen(QBrush(Qt::black), 1, Qt::SolidLine));
            graph->setLineStyle(QCPGraph::lsNone);
            graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, 5));//显示散点图
            graph->setVisible(true);
        }

        double maxAbs = 0.0;
        QVector<double> keys, values;
        for (int i=0; i<channel.size(); ++i){
            keys << channel[i];
            values << residual[i];
            maxAbs = qMax(maxAbs, qAbs(residual[i]));
        }
        graph->setData(keys, values);

        keyAxis->rescale(true);
        //设置留白区域，1.1则表示左右各留白10%
        keyAxis->scaleRange(1.05, keyAxis->range().center());

        // 让 y 轴关于 0 对称
        if (maxAbs < 1e-12) maxAbs = 1.0;          // 避免全 0 时范围塌缩
        maxAbs *= 1.05;                            // 上下留 5% 空白
        valueAxis->setRange(-maxAbs, maxAbs);
    }

    //customPlot->rescaleAxes(true);
    customPlot->replot(QCustomPlot::rpQueuedReplot);
}

void NeutronYieldStatisticsWindow::slotUpdate_yieldDis(QMap<int, Sensor> yields)
{
    if (!w)
        return;

    std::vector<Sensor> sensors;
    for(auto yield:yields)
    {
        sensors.push_back({yield.lonDeg,yield.latDeg,yield.value});
    }
    w->setSensors(sensors);
    w->rebuild();
}

bool NeutronYieldStatisticsWindow::setLonLat(int row, double lon, double lat)
{
    // 行号合法性
    if (row < 0 || row >= ui->tableWidget->rowCount())
        return false;

    // 范围校验（与你前面的规则一致）
    if (lon < 0.0 || lon > 360.0)
        return false;
    if (lat < 0.0 || lat > 180.0)
        return false;

    ui->tableWidget->blockSignals(true); // 防止触发 itemChanged

    // 经度（第四列）
    QTableWidgetItem* lonItem = ui->tableWidget->item(row, 3);
    if (!lonItem) {
        lonItem = new QTableWidgetItem;
        applyDetectorTableItemFlags(lonItem, 3);
        ui->tableWidget->setItem(row, 3, lonItem);
    }
    lonItem->setText(QString::number(lon, 'f', 2));

    // 纬度（第五列）
    QTableWidgetItem* latItem = ui->tableWidget->item(row, 4);
    if (!latItem) {
        latItem = new QTableWidgetItem;
        applyDetectorTableItemFlags(latItem, 4);
        ui->tableWidget->setItem(row, 4, latItem);
    }
    latItem->setText(QString::number(lat, 'f', 2));

    ui->tableWidget->blockSignals(false);
    return true;
}

void NeutronYieldStatisticsWindow::on_bt_saveResult_clicked()
{
    //先检查文件路径是否存在
    QString filePath = ui->lineEdit_savepath->text().trimmed();
    if (!QFileInfo::exists(filePath))
    {
        QMessageBox::information(this, tr("提示"), tr("文件路径不存在，请重新选择！"));
        return;
    }

    //检查文件名，当存在后缀.txt时，直接使用，否则自动添加后缀
    QString fileName = ui->lineEdit_fileName->text();
    if (!fileName.endsWith(".txt", Qt::CaseInsensitive))
    {        fileName += ".txt";
    }

    //拼接文件全路径
    QString wholeFileName = QDir(filePath).filePath(fileName);

    //文件存在时，提示用户是否覆盖
    if (QFileInfo::exists(wholeFileName))
    {        
        if (QMessageBox::question(this, tr("提示"), tr("文件已存在，是否覆盖？"), 
                QMessageBox::Yes, QMessageBox::No) != QMessageBox::Yes)        
        {
            return;
        }
    }

    //保存文件,将tableWidget数据全部导出
    QFile file(wholeFileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text))
    {        
        QTextStream out(&file);
        //写入表头
        out << "DetectorID\tYield\tUncertainty\tLongitude\tLatitude\tInitial909Count\tSpecCount\n";
        //写入数据
        for (int row = 0; row < ui->tableWidget->rowCount(); row++)
        {
            QStringList rowData;
            //如果最后一列的数据为空，则认为该行没有数据，跳过
            QTableWidgetItem* lastItem = ui->tableWidget->item(row, ui->tableWidget->columnCount() - 1);
            if (!lastItem || lastItem->text().isEmpty())
                continue;
            for (int col = 0; col < ui->tableWidget->columnCount(); col++)
            {
                QTableWidgetItem* item = ui->tableWidget->item(row, col);
                rowData << (item ? item->text() : ""); //如果单元格没有数据，则写入空字符串
            }
            out << rowData.join("\t") << "\n";
        }
        //弹出对话框提示保存成功，并打印日志
        QMessageBox::information(this, tr("提示"), tr("文件保存成功！"));
        emit reporWriteLog(tr("文件保存成功，路径：%1").arg(wholeFileName));
    }
    else
    {
        QMessageBox::information(this, tr("提示"), tr("文件保存失败，请检查路径和文件名！"));
        emit reporWriteLog(tr("文件保存失败，路径：%1").arg(wholeFileName));
    }
    file.close();
}
