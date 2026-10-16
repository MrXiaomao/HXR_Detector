/*
 * @Author: MrPan
 * @Date: 2026-01-14 09:21:28
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2026-07-07 15:52:02
 * @Description: 计数率统计
 */
#include "countratestatisticswindow.h"
#include "ui_countratestatisticswindow.h"
#include "globalsettings.h"

#include <QButtonGroup>
#include <QFileDialog>
#include <QAction>
#include <QToolButton>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QGridLayout>
#include <QLabel>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QProgressDialog>
#include <QTextCodec>
#include <QTableWidget>
#include <cmath>
#include <vector>
#include <limits>

#include "H5Cpp.h"

namespace {
constexpr int kDetectorsPerPlot = 16;
constexpr int kSpectrumPointCount = 8192;
/** 统计表每行高度（与 setFixedHeight 计算一致） */
constexpr int kStatsTableRowHeight = 25;
constexpr hsize_t kH5SpectrumColumnCount = sizeof(H5Spectrum) / sizeof(quint32);
/** 单通道解析结果点数上限（约 24h @ 50ms/帧），超出则中止该通道并提示 */
constexpr int kMaxCountPointsPerDetector = 2'000'000;

class H5ErrorStackSilencer {
public:
    H5ErrorStackSilencer()
    {
        H5Eget_auto(H5E_DEFAULT, &mOldFunc, &mOldClientData);
        H5Eset_auto(H5E_DEFAULT, nullptr, nullptr);
    }
    ~H5ErrorStackSilencer()
    {
        H5Eset_auto(H5E_DEFAULT, mOldFunc, mOldClientData);
    }

private:
    H5E_auto2_t mOldFunc = nullptr;
    void *mOldClientData = nullptr;
};

std::string h5FilePathStd(const QString &filePath)
{
    QTextCodec *gbk_codec = QTextCodec::codecForName("GBK");
    return gbk_codec->fromUnicode(filePath).toStdString();
}

QString detectorGroupName(int detectorIndex)
{
    return QStringLiteral("Detector#%1").arg(detectorIndex);
}

bool h5GroupExists(const H5::H5File &file, const std::string &groupName)
{
    return H5Lexists(file.getId(), groupName.c_str(), H5P_DEFAULT) > 0;
}

void clearDetectorStatisticsRow(QTableWidget *table, int detectorIndex)
{
    const int row = detectorIndex + 1;
    if (!table || row <= 1 || row >= table->rowCount())
        return;
    for (int col = 1; col <= 9; ++col) {
        QTableWidgetItem *item = table->item(row, col);
        if (!item) {
            item = new QTableWidgetItem;
            table->setItem(row, col, item);
        }
        item->setText(QString());
        item->setTextAlignment(Qt::AlignCenter);
    }
}

void clearAllDetectorStatisticsRows(QTableWidget *table)
{
    for (int index = 1; index <= DET_NUM; ++index)
        clearDetectorStatisticsRow(table, index);
}

/** @return 首个存在 Spectrum 数据集且行数>0 的通道号，0 表示未找到 */
int firstDetectorWithSpectrumRows(H5::H5File &file)
{
    for (int index = 1; index <= DET_NUM; ++index) {
        const std::string groupName = detectorGroupName(index).toStdString();
        if (!h5GroupExists(file, groupName))
            continue;
        try {
            H5::Group group = file.openGroup(groupName);
            H5::DataSet dataset = group.openDataSet("Spectrum");
            H5::DataSpace fileSpace = dataset.getSpace();
            hsize_t dims[2] = {0, 0};
            fileSpace.getSimpleExtentDims(dims, nullptr);
            dataset.close();
            group.close();
            if (dims[0] > 0)
                return index;
        } catch (H5::Exception &) {
        }
    }
    return 0;
}

/** spinBox 时间步长下限（s）：measureTime<=1s 时为 1，否则按 ms 向上取整到秒 */
int minTimeStepSecFromMeasureTimeMs(quint32 measureTimeMs)
{
    if (measureTimeMs <= 1000)
        return 1;
    return static_cast<int>((measureTimeMs + 999) / 1000);
}

/** @return 首条能谱的 measureTime（ms），0 表示读取失败 */
quint32 firstSpectrumMeasureTimeMs(H5::H5File &file)
{
    const int probeIndex = firstDetectorWithSpectrumRows(file);
    if (probeIndex <= 0)
        return 0;
    try {
        const std::string groupName = detectorGroupName(probeIndex).toStdString();
        H5::Group group = file.openGroup(groupName);
        H5::DataSet dataset = group.openDataSet("Spectrum");
        H5::CompType fileType = dataset.getCompType();
        if (fileType != H5::PredType::NATIVE_UINT) {
            dataset.close();
            group.close();
            return 0;
        }
        H5::DataSpace fileSpace = dataset.getSpace();
        hsize_t dims[2] = {0, 0};
        fileSpace.getSimpleExtentDims(dims, nullptr);
        if (dims[0] <= 0 || dims[1] != kH5SpectrumColumnCount) {
            dataset.close();
            group.close();
            return 0;
        }
        const hsize_t cols = dims[1];
        hsize_t memDims[2] = {1, cols};
        H5::DataSpace memSpace(2, memDims);
        std::vector<uint32_t> rowData(cols);
        hsize_t start[2] = {0, 0};
        hsize_t count[2] = {1, cols};
        fileSpace.selectHyperslab(H5S_SELECT_SET, count, start);
        dataset.read(rowData.data(), H5::PredType::NATIVE_UINT, memSpace, fileSpace);
        const H5Spectrum *spec = reinterpret_cast<const H5Spectrum *>(rowData.data());
        const quint32 measureTimeMs = spec->measureTime;
        dataset.close();
        group.close();
        return measureTimeMs;
    } catch (H5::Exception &) {
        return 0;
    }
}
}

CountRateStatisticsWindow::CountRateStatisticsWindow(bool isDarkTheme, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::CountRateStatisticsWindow)
    , mIsDarkTheme(isDarkTheme)
    , mainWindow(static_cast<QGoodWindowHelper*>(parent))
{
    ui->setupUi(this);

    // 初始化界面
    initUi();
    restoreSettings();
    applyColorTheme();
    connect(this, &CountRateStatisticsWindow::reporWriteLog,
        this, &CountRateStatisticsWindow::replyWriteLog);
    connect(this, &CountRateStatisticsWindow::sigWriteLog, this, &CountRateStatisticsWindow::replyWriteLog);

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


CountRateStatisticsWindow::~CountRateStatisticsWindow()
{
    delete ui;
}

void CountRateStatisticsWindow::initUi()
{
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
        splitterH1->addWidget(ui->centralHboxTabWidget);
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

    //中间栏
    {
        // 关键：两个都允许“垂直方向扩展”
        ui->picture_setting_group->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
        ui->plot_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        QHBoxLayout *layout = new QHBoxLayout;
        layout->setContentsMargins(0,0,0,0);
        layout->addWidget(ui->plot_widget);
        layout->addWidget(ui->picture_setting_group);
        // 设置拉伸因子（关键）
        // layout->setStretch(0, 0);  // groupBox 不拉伸
        // layout->setStretch(1, 1);  // 右侧占满
        ui->centralHboxTabWidget->setLayout(layout);
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

        if (QVBoxLayout *paramLayout = qobject_cast<QVBoxLayout*>(ui->labParametersWidget->layout())) {
            paramLayout->setStretch(0, 0);
            paramLayout->setStretch(1, 0);
            paramLayout->setStretch(2, 0);
            paramLayout->setStretch(3, 1);
        }
        if (QVBoxLayout *logLayout = qobject_cast<QVBoxLayout*>(ui->gbLog->layout())) {
            logLayout->setStretch(0, 1);
            logLayout->setStretch(1, 0);
        }
    }

    ui->tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    ui->tableWidget->setColumnWidth(0, 40);

    updateTableRowCount();
    for (int column=0; column<ui->tableWidget->columnCount(); ++column)
    {
        //ui->tableWidget->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Fixed);
        //ui->tableWidget->setColumnWidth(column, column==0 ? 40 : 60);

        for (int row=0; row<ui->tableWidget->rowCount(); ++row)
        {
            if (row>=0 && row<=1)
            {
                if (nullptr == ui->tableWidget->item(row, column))
                    ui->tableWidget->setItem(row, column, new QTableWidgetItem(""));
                ui->tableWidget->item(row, column)->setBackground(Qt::gray);
            }
            else
            {
                ui->tableWidget->setItem(row, column, new QTableWidgetItem(""));
            }

            ui->tableWidget->item(row, column)->setTextAlignment(Qt::AlignCenter);
        }
    }

    for (int row=0; row<ui->tableWidget->rowCount(); ++row)
    {
        ui->tableWidget->setRowHeight(row, kStatsTableRowHeight);
        if (row>=2)
        {
            ui->tableWidget->setItem(row, 0, new QTableWidgetItem(QString("#%1").arg(row-1)));
            ui->tableWidget->item(row, 0)->setTextAlignment(Qt::AlignCenter);
        }
    }
    ui->tableWidget->setFixedHeight((DET_NUM + 2) * kStatsTableRowHeight);
    ui->leftStackedWidget->setMinimumWidth(582);

    ui->tableWidget->setSpan(0, 0, 2, 1);
    ui->tableWidget->setSpan(0, 1, 2, 1);
    ui->tableWidget->setItem(0, 1, new QTableWidgetItem("最小值(计数率cps)"));
    ui->tableWidget->setSpan(0, 2, 2, 1);
    ui->tableWidget->setItem(0, 2, new QTableWidgetItem("最大值(计数率cps)"));
    ui->tableWidget->setSpan(0, 3, 2, 1);
    ui->tableWidget->setItem(0, 3, new QTableWidgetItem("均值(计数率cps)"));
    ui->tableWidget->setSpan(0, 4, 1, 3);
    ui->tableWidget->setItem(0, 4, new QTableWidgetItem("死时间率％"));
    ui->tableWidget->setSpan(0, 7, 1, 3);
    ui->tableWidget->setItem(0, 7, new QTableWidgetItem("死时间(ms)"));

    ui->tableWidget->item(0, 1)->setTextAlignment(Qt::AlignCenter);
    ui->tableWidget->item(0, 2)->setTextAlignment(Qt::AlignCenter);
    ui->tableWidget->item(0, 3)->setTextAlignment(Qt::AlignCenter);
    ui->tableWidget->item(0, 4)->setTextAlignment(Qt::AlignCenter);
    ui->tableWidget->item(0, 7)->setTextAlignment(Qt::AlignCenter);

    ui->tableWidget->item(0, 1)->setBackground(Qt::gray);
    ui->tableWidget->item(0, 2)->setBackground(Qt::gray);
    ui->tableWidget->item(0, 3)->setBackground(Qt::gray);
    ui->tableWidget->item(0, 4)->setBackground(Qt::gray);
    ui->tableWidget->item(0, 7)->setBackground(Qt::gray);

    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        for (int column = 0; column < ui->tableWidget->columnCount(); ++column) {
            if (QTableWidgetItem *item = ui->tableWidget->item(row, column))
                item->setTextAlignment(Qt::AlignCenter);
        }
    }

    initCustomPlot(ui->plot_up, tr("时间/s"), tr("计数率/cps"), 1, kDetectorsPerPlot,
                   tr("计数曲线（通道1-16）"), 1000.0);
    initCustomPlot(ui->plot_down, tr("时间/s"), tr("计数率/cps"), kDetectorsPerPlot + 1, DET_NUM,
                   tr("计数曲线（通道17-32）"), 1000.0);
    initCustomPlot(ui->spec_plot_up, tr("道址"), tr("计数"), 1, kDetectorsPerPlot,
                   tr("累积能谱（通道1-16）"), kSpectrumPointCount);
    initCustomPlot(ui->spec_plot_down, tr("道址"), tr("计数"), kDetectorsPerPlot + 1, DET_NUM,
                   tr("累积能谱（通道17-32）"), kSpectrumPointCount);

    // 默认：32通道原始/修正曲线都开启
    mOriginalEnabled = QVector<bool>(DET_NUM + 1, true);
    mCorrectEnabled = QVector<bool>(DET_NUM + 1, true);
    mOriginalLegendChecks = QVector<QCheckBox*>(DET_NUM + 1, nullptr);
    mCorrectLegendChecks = QVector<QCheckBox*>(DET_NUM + 1, nullptr);

    ui->cb_countOriginalPlotShow->setChecked(true);
    ui->cb_countCorrectPlotShow->setChecked(true);
    connect(ui->cb_countOriginalPlotShow, &QCheckBox::toggled, this, &CountRateStatisticsWindow::applyOriginalGlobalState);
    connect(ui->cb_countCorrectPlotShow, &QCheckBox::toggled, this, &CountRateStatisticsWindow::applyCorrectGlobalState);
    createDetectorLegendCheckboxes();
    syncDetectorCheckboxes();

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

void CountRateStatisticsWindow::createDetectorLegendCheckboxes()
{
    QLayout* groupLayout = ui->picture_setting_group->layout();
    QHBoxLayout* oldTopRow = qobject_cast<QHBoxLayout*>(groupLayout);
    if (!oldTopRow)
        return;

    oldTopRow->removeWidget(ui->cb_countOriginalPlotShow);
    oldTopRow->removeWidget(ui->cb_countCorrectPlotShow);
    delete oldTopRow;

    QVBoxLayout* containerLayout = new QVBoxLayout();
    containerLayout->setContentsMargins(6, 4, 6, 6);
    containerLayout->setSpacing(4);
    ui->picture_setting_group->setLayout(containerLayout);

    QHBoxLayout* topGlobalLayout = new QHBoxLayout();
    topGlobalLayout->setContentsMargins(0, 0, 0, 0);
    topGlobalLayout->addWidget(ui->cb_countOriginalPlotShow);
    topGlobalLayout->addWidget(ui->cb_countCorrectPlotShow);
    topGlobalLayout->addStretch();
    containerLayout->addLayout(topGlobalLayout);

    QWidget* legendPanel = new QWidget(ui->picture_setting_group);
    QGridLayout* legendGrid = new QGridLayout(legendPanel);
    legendGrid->setContentsMargins(4, 2, 4, 2);
    legendGrid->setHorizontalSpacing(18);
    legendGrid->setVerticalSpacing(2);

    for (int detector = 1; detector <= DET_NUM; ++detector)
    {
        const QColor detectorColor = QColor::fromHsv((detector * 27) % 360, 220, 230);
        const QString detectorText = QString("%1").arg(detector, 2, 10, QChar('0'));

        QCheckBox* originalCheck = new QCheckBox(detectorText, legendPanel);
        originalCheck->setIcon(QIcon(lineLegendPixmap(detectorColor, Qt::SolidLine, 1)));
        originalCheck->setIconSize(QSize(32, 12));
        originalCheck->setChecked(true);
        connect(originalCheck, &QCheckBox::toggled, this, [this, detector](bool checked){
            mOriginalEnabled[detector] = checked;
            updateAllPlotVisibility();
        });
        mOriginalLegendChecks[detector] = originalCheck;
        legendGrid->addWidget(originalCheck, detector, 0);

        QCheckBox* correctCheck = new QCheckBox(detectorText, legendPanel);
        correctCheck->setIcon(QIcon(lineLegendPixmap(detectorColor, Qt::DashLine, 2)));
        correctCheck->setIconSize(QSize(32, 12));
        correctCheck->setChecked(true);
        connect(correctCheck, &QCheckBox::toggled, this, [this, detector](bool checked){
            mCorrectEnabled[detector] = checked;
            updateAllPlotVisibility();
        });
        mCorrectLegendChecks[detector] = correctCheck;
        legendGrid->addWidget(correctCheck, detector, 1);
    }

    QScrollArea* legendScroll = new QScrollArea(ui->picture_setting_group);
    legendScroll->setWidgetResizable(true);
    legendScroll->setFrameShape(QFrame::NoFrame);
    legendScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    legendScroll->setWidget(legendPanel);
    legendScroll->setMinimumHeight(360);
    containerLayout->addWidget(legendScroll);
}

void CountRateStatisticsWindow::syncDetectorCheckboxes()
{
    for (int detector = 1; detector <= DET_NUM; ++detector)
    {
        if (detector < mOriginalEnabled.size() && detector < mOriginalLegendChecks.size() && mOriginalLegendChecks[detector])
        {
            const QSignalBlocker b(mOriginalLegendChecks[detector]);
            mOriginalLegendChecks[detector]->setChecked(mOriginalEnabled[detector]);
        }
        if (detector < mCorrectEnabled.size() && detector < mCorrectLegendChecks.size() && mCorrectLegendChecks[detector])
        {
            const QSignalBlocker b(mCorrectLegendChecks[detector]);
            mCorrectLegendChecks[detector]->setChecked(mCorrectEnabled[detector]);
        }
    }
}

void CountRateStatisticsWindow::applyOriginalGlobalState(bool enableAll)
{
    for (int det = 1; det <= DET_NUM; ++det)
    {
        mOriginalEnabled[det] = enableAll;
        if (det < mOriginalLegendChecks.size() && mOriginalLegendChecks[det])
        {
            const QSignalBlocker b(mOriginalLegendChecks[det]);
            mOriginalLegendChecks[det]->setChecked(enableAll);
        }
    }
    updateAllPlotVisibility();
}

void CountRateStatisticsWindow::applyCorrectGlobalState(bool enableAll)
{
    for (int det = 1; det <= DET_NUM; ++det)
    {
        mCorrectEnabled[det] = enableAll;
        if (det < mCorrectLegendChecks.size() && mCorrectLegendChecks[det])
        {
            const QSignalBlocker b(mCorrectLegendChecks[det]);
            mCorrectLegendChecks[det]->setChecked(enableAll);
        }
    }
    updateAllPlotVisibility();
}


void CountRateStatisticsWindow::on_action_lightTheme_triggered()
{
    if(!mIsDarkTheme) return;
    mIsDarkTheme = false;
    qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    if(mThemeColorEnable) QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
    GlobalSettings settings;
    settings.setValue("Global/Startup/darkTheme","false");
    applyColorTheme();
}


void CountRateStatisticsWindow::on_action_darkTheme_triggered()
{
    if(mIsDarkTheme) return;
    mIsDarkTheme = true;
    qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    if(mThemeColorEnable) QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
    GlobalSettings settings;
    settings.setValue("Global/Startup/darkTheme","true");
    applyColorTheme();
}


void CountRateStatisticsWindow::on_action_colorTheme_triggered()
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


void CountRateStatisticsWindow::applyColorTheme()
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
        customPlot->xAxis->setBasePen(QPen(palette.color(QPalette::WindowText)));
        customPlot->xAxis2->setBasePen(QPen(palette.color(QPalette::WindowText)));
        customPlot->yAxis->setBasePen(QPen(palette.color(QPalette::WindowText)));
        customPlot->yAxis2->setBasePen(QPen(palette.color(QPalette::WindowText)));
        // 刻度线颜色
        customPlot->xAxis->setTickPen(QPen(palette.color(QPalette::WindowText)));
        customPlot->xAxis2->setTickPen(QPen(palette.color(QPalette::WindowText)));
        customPlot->yAxis->setTickPen(QPen(palette.color(QPalette::WindowText)));
        customPlot->yAxis2->setTickPen(QPen(palette.color(QPalette::WindowText)));
        // 子刻度线颜色
        customPlot->xAxis->setSubTickPen(QPen(palette.color(QPalette::WindowText)));
        customPlot->xAxis2->setSubTickPen(QPen(palette.color(QPalette::WindowText)));
        customPlot->yAxis->setSubTickPen(QPen(palette.color(QPalette::WindowText)));
        customPlot->yAxis2->setSubTickPen(QPen(palette.color(QPalette::WindowText)));
        // 坐标轴文本标签颜色
        customPlot->xAxis->setLabelColor(palette.color(QPalette::WindowText));
        customPlot->xAxis2->setLabelColor(palette.color(QPalette::WindowText));
        customPlot->yAxis->setLabelColor(palette.color(QPalette::WindowText));
        customPlot->yAxis2->setLabelColor(palette.color(QPalette::WindowText));
        // 坐标轴刻度文本标签颜色
        customPlot->xAxis->setTickLabelColor(palette.color(QPalette::WindowText));
        customPlot->xAxis2->setTickLabelColor(palette.color(QPalette::WindowText));
        customPlot->yAxis->setTickLabelColor(palette.color(QPalette::WindowText));
        customPlot->yAxis2->setTickLabelColor(palette.color(QPalette::WindowText));
        // 隐藏x2、y2刻度线
        customPlot->xAxis2->setTicks(false);
        customPlot->yAxis2->setTicks(false);
        customPlot->xAxis2->setSubTicks(false);
        customPlot->yAxis2->setSubTicks(false);

        if (QCPItemText *titleItem = customPlot->findChild<QCPItemText*>("title"))
            titleItem->setColor(palette.color(QPalette::WindowText));

        customPlot->replot();
    }
}


void CountRateStatisticsWindow::restoreSettings()
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


void CountRateStatisticsWindow::replyWriteLog(const QString &msg, QtMsgType msgType)
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


void CountRateStatisticsWindow::updateTableRowCount()
{
    const int rowCount = DET_NUM + 2;
    if (ui->tableWidget->rowCount() < rowCount)
        ui->tableWidget->setRowCount(rowCount);
}


void CountRateStatisticsWindow::clearAllPlots()
{
    for (QCustomPlot* plot : {ui->plot_up, ui->plot_down, ui->spec_plot_up, ui->spec_plot_down})
    {
        for (int i = 0; i < plot->graphCount(); ++i)
            plot->graph(i)->data()->clear();
        plot->replot(QCustomPlot::rpQueuedReplot);
    }
}


void CountRateStatisticsWindow::updatePlotVisibility(QCustomPlot* customPlot, int startDetector, int endDetector)
{
    // 可见性仅由每通道勾选决定；顶部 cb_countOriginalPlotShow / cb_countCorrectPlotShow 只做全选/全不选
    const int localCount = endDetector - startDetector + 1; // 16
    for (int localIndex = 0; localIndex < localCount; ++localIndex)
    {
        const int det = startDetector + localIndex;
        const int originalGraphIndex = localIndex; // 原始曲线：先加
        const int correctedGraphIndex = kDetectorsPerPlot + localIndex; // 修正曲线：后加

        if (originalGraphIndex < customPlot->graphCount())
            customPlot->graph(originalGraphIndex)->setVisible(mOriginalEnabled[det]);

        if (correctedGraphIndex < customPlot->graphCount())
            customPlot->graph(correctedGraphIndex)->setVisible(mCorrectEnabled[det]);
    }
}


void CountRateStatisticsWindow::updateAllPlotVisibility()
{
    updatePlotVisibility(ui->plot_up, 1, kDetectorsPerPlot);
    updatePlotVisibility(ui->plot_down, kDetectorsPerPlot + 1, DET_NUM);
    updatePlotVisibility(ui->spec_plot_up, 1, kDetectorsPerPlot);
    updatePlotVisibility(ui->spec_plot_down, kDetectorsPerPlot + 1, DET_NUM);
    ui->plot_up->replot(QCustomPlot::rpQueuedReplot);
    ui->plot_down->replot(QCustomPlot::rpQueuedReplot);
    ui->spec_plot_up->replot(QCustomPlot::rpQueuedReplot);
    ui->spec_plot_down->replot(QCustomPlot::rpQueuedReplot);
}


void CountRateStatisticsWindow::plotAllDetectors()
{
    static QVector<double> channelKeys;
    if (channelKeys.size() != kSpectrumPointCount) {
        channelKeys.resize(kSpectrumPointCount);
        for (int j = 0; j < kSpectrumPointCount; ++j)
            channelKeys[j] = j + 1;
    }

    for (int det = 1; det <= DET_NUM; ++det)
    {
        const bool hasCount = mMapCount.contains(det) && !mMapCount[det].isEmpty()
            && mMapCountCorrect.contains(det) && mMapCountCorrect[det].size() == mMapCount[det].size();
        const bool hasSpectrum = mMapSpectrum.contains(det) && mMapSpectrum[det].size() == kSpectrumPointCount
            && mMapSpectrumCorrect.contains(det) && mMapSpectrumCorrect[det].size() == kSpectrumPointCount;

        QCustomPlot* countPlot = det <= kDetectorsPerPlot ? ui->plot_up : ui->plot_down;
        QCustomPlot* specPlot = det <= kDetectorsPerPlot ? ui->spec_plot_up : ui->spec_plot_down;
        const int localIndex = (det <= kDetectorsPerPlot ? det - 1 : det - kDetectorsPerPlot - 1);
        const int originalGraphIndex = localIndex;
        const int correctedGraphIndex = kDetectorsPerPlot + localIndex;

        if (hasCount)
        {
            const QVector<QPair<double, double>> &origPairs = mMapCount[det];
            const QVector<QPair<double, double>> &corrPairs = mMapCountCorrect[det];
            const int n = origPairs.size();

            QVector<double> timeSec;
            QVector<double> yOriginal;
            QVector<double> yCorrect;
            timeSec.reserve(n);
            yOriginal.reserve(n);
            yCorrect.reserve(n);
            for (int i = 0; i < n; ++i)
            {
                timeSec << origPairs[i].first;
                yOriginal << origPairs[i].second;
                yCorrect << corrPairs[i].second;
            }

            if (correctedGraphIndex < countPlot->graphCount())
            {
                countPlot->graph(originalGraphIndex)->setData(timeSec, yOriginal);
                countPlot->graph(correctedGraphIndex)->setData(timeSec, yCorrect);
            }
        }

        if (hasSpectrum && correctedGraphIndex < specPlot->graphCount())
        {
            QVector<double> yOriginalSpec;
            QVector<double> yCorrectSpec;
            yOriginalSpec.reserve(kSpectrumPointCount);
            yCorrectSpec.reserve(kSpectrumPointCount);
            for (int j = 0; j < kSpectrumPointCount; ++j)
            {
                yOriginalSpec << static_cast<double>(mMapSpectrum[det][j]);
                yCorrectSpec << static_cast<double>(mMapSpectrumCorrect[det][j]);
            }
            specPlot->graph(originalGraphIndex)->setData(channelKeys, yOriginalSpec);
            specPlot->graph(correctedGraphIndex)->setData(channelKeys, yCorrectSpec);
        }
    }

    ui->plot_up->rescaleAxes(true);
    ui->plot_down->rescaleAxes(true);
    ui->spec_plot_up->rescaleAxes(true);
    ui->spec_plot_down->rescaleAxes(true);

    const double countRangeStart = ui->spinBox_timeStart->value();
    const double countRangeEnd = ui->spinBox_timeEnd->value();
    if (countRangeStart <= countRangeEnd) {
        ui->plot_up->xAxis->setRange(countRangeStart, countRangeEnd);
        ui->plot_down->xAxis->setRange(countRangeStart, countRangeEnd);
    }

    updateAllPlotVisibility();
}


void CountRateStatisticsWindow::initCustomPlot(QCustomPlot* customPlot, QString axisXLabel, QString axisYLabel, int startDetector, int endDetector,
                                               const QString &plotTitle, double xAxisMax)
{
    QCustomPlotHelper* customPlotHelper = new QCustomPlotHelper(customPlot, this);
    customPlotHelper->setRangeSelectActionVisible(false); // 禁用右键的选择范围
    customPlot->setAntialiasedElements(QCP::aeAll);
    customPlot->legend->setVisible(false);
    customPlot->xAxis->setTickLabelRotation(-45);
    customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables | QCP::iSelectLegend);
    customPlot->xAxis->rescale(true);
    customPlot->yAxis->rescale(true);

    if (!plotTitle.isEmpty())
    {
        QCPItemText *textLabel = new QCPItemText(customPlot);
        textLabel->setObjectName("title");
        textLabel->setPositionAlignment(Qt::AlignTop | Qt::AlignHCenter);
        textLabel->position->setType(QCPItemPosition::ptAxisRectRatio);
        textLabel->position->setCoords(0.5, 0);
        textLabel->setText(plotTitle);
        textLabel->setColor(mIsDarkTheme ? Qt::white : Qt::black);
    }

    // 设置刻度范围
    customPlot->xAxis->setRange(0, xAxisMax);
    customPlot->yAxis->setRange(0, 2000);
    customPlot->yAxis->ticker()->setTickCount(5);
    customPlot->xAxis->ticker()->setTickCount(10);
    customPlot->yAxis2->ticker()->setTickCount(5);
    customPlot->xAxis2->ticker()->setTickCount(10);

    //设置轴标签名称
    customPlot->xAxis->setLabel(axisXLabel);
    customPlot->yAxis->setLabel(axisYLabel);

    customPlot->xAxis2->setTicks(false);
    customPlot->xAxis2->setSubTicks(false);
    customPlot->yAxis2->setTicks(false);
    customPlot->yAxis2->setSubTicks(false);

    // 添加曲线：先原始（16条），再修正（16条）
    // 左列原始 / 右列修正」。
    for (int detector = startDetector; detector <= endDetector; ++detector)
    {
        const QColor color = QColor::fromHsv((detector * 27) % 360, 220, 230);
        QCPGraph * graph = customPlot->addGraph(customPlot->xAxis, customPlot->yAxis);
        graph->setName(QString("CH%1").arg(detector, 2, 10, QChar('0')));
        graph->setAntialiased(false);
        QPen pen(color);
        pen.setWidth(1);
        pen.setStyle(Qt::SolidLine);
        graph->setPen(pen);
        graph->setLineStyle(QCPGraph::lsLine);
        graph->setSelectable(QCP::SelectionType::stSingleData);
    }
    for (int detector = startDetector; detector <= endDetector; ++detector)
    {
        const QColor color = QColor::fromHsv((detector * 27) % 360, 220, 230);
        QCPGraph * graph = customPlot->addGraph(customPlot->xAxis, customPlot->yAxis);
        graph->setName(QString("CH%1").arg(detector, 2, 10, QChar('0')));
        graph->setAntialiased(false);
        QPen pen(color);
        pen.setWidth(2);
        pen.setStyle(Qt::DashLine);
        graph->setPen(pen);
        graph->setLineStyle(QCPGraph::lsLine);
        graph->setSelectable(QCP::SelectionType::stSingleData);
    }
    //将图例添加到picture_setting_group中,这个控件里面已经有两个QCheckBox了
    // 添加可选项
    /*for (int i=0; i<customPlot->graphCount(); ++i){
        QCheckBox* checkBox = new QCheckBox(legendWidget);
        checkBox->setText(customPlot->graph(i)->name());
        checkBox->setObjectName(tr(""));
        QIcon actionIcon = roundPixmap(QSize(16,16), customPlot->graph(i)->pen().color());
        checkBox->setIcon(actionIcon);
        checkBox->setChecked(true);
        connect(checkBox, &QCheckBox::stateChanged, [=](int state){
            QCPGraph *graph = customPlot->graph(i);
            if (graph){
                graph->setVisible(Qt::CheckState::Checked == state ? true : false);
                customPlot->replot(QCustomPlot::rpQueuedReplot);
            }
        });
    }*/
    customPlot->replot();

    connect(customPlot->xAxis, SIGNAL(rangeChanged(const QCPRange &)), customPlot->xAxis2, SLOT(setRange(const QCPRange &)));
    connect(customPlot->yAxis, SIGNAL(rangeChanged(const QCPRange &)), customPlot->yAxis2, SLOT(setRange(const QCPRange &)));
    connect(customPlot, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(slotShowTracer(QMouseEvent*)));
    connect(customPlot, SIGNAL(mouseRelease(QMouseEvent*)), this, SLOT(slotRestorePlot(QMouseEvent*)));
}

QPixmap CountRateStatisticsWindow::roundPixmap(QSize sz, QColor clrOut)
{
    QPixmap result(sz);
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing,true);

    QPainterPath bigCirclePath;
    bigCirclePath.addEllipse(0, 0, sz.width(), sz.height());
    painter.fillPath(bigCirclePath, QBrush(clrOut));

    return result;
}

QPixmap CountRateStatisticsWindow::lineLegendPixmap(const QColor &color, Qt::PenStyle style, int penWidth)
{
    const QSize sz(32, 12);
    QPixmap result(sz);
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPen pen(color);
    pen.setWidth(penWidth);
    pen.setStyle(style);
    pen.setCapStyle(Qt::FlatCap);
    painter.setPen(pen);

    const int y = sz.height() / 2;
    painter.drawLine(2, y, sz.width() - 2, y);
    return result;
}

// #include "H5Cpp.h"
void CountRateStatisticsWindow::on_action_open_triggered()
{
    // 打开历史测量数据文件...
    GlobalSettings settings;
    QString lastPath = settings.value("mainWindow/LastFilePath", QDir::homePath()).toString();
    QString filter = "测量文件 (*.H5);;所有文件 (*.*)";
    QString filePath = QFileDialog::getOpenFileName(this, tr("打开测量数据文件"), lastPath, filter);

    if (filePath.isEmpty() || !QFileInfo::exists(filePath))
        return;

    emit reporWriteLog(tr("选择测量文件：%1").arg(filePath));

    settings.setValue("mainWindow/LastFilePath", filePath);
    ui->textBrowser_filepath->setText(filePath);
    mMeasureStartTime = QDateTime();
    mMeasureStartSecond = 0.0;
    mMeasureDurationSecond = 0.0;

    // 解析文件，获取能谱范围时长（从首个有数据的通道读取）
    try {
        H5ErrorStackSilencer silence;
        H5::H5File file(h5FilePathStd(filePath), H5F_ACC_RDONLY);

        const int probeIndex = firstDetectorWithSpectrumRows(file);
        if (probeIndex <= 0) {
            ui->line_measure_endT->clear();
            ui->line_measure_startT->clear();
            emit reporWriteLog(tr("文件中未找到任何有效探测器通道数据"), QtWarningMsg);
            file.close();
            return;
        }

        const std::string groupName = detectorGroupName(probeIndex).toStdString();
        H5::Group group = file.openGroup(groupName);
        H5::DataSet dataset = group.openDataSet("Spectrum");

        H5::CompType fileType = dataset.getCompType();
        if (fileType != H5::PredType::NATIVE_UINT) {
            emit reporWriteLog(tr("通道#%1 数据类型不匹配，无法读取测量时长").arg(probeIndex), QtWarningMsg);
            dataset.close();
            group.close();
            file.close();
            return;
        }

        H5::DataSpace file_space = dataset.getSpace();
        hsize_t current_dims[2];
        file_space.getSimpleExtentDims(current_dims, NULL);
        const hsize_t rows = current_dims[0];
        const hsize_t cols = current_dims[1];
        if (rows <= 0) {
            emit reporWriteLog(tr("通道#%1 无可用数据").arg(probeIndex), QtWarningMsg);
            dataset.close();
            group.close();
            file.close();
            return;
        }

        hsize_t mem_dims[2] = {1, cols};
        H5::DataSpace mem_space(2, mem_dims);
        std::vector<uint32_t> row_data(cols);

        hsize_t start[2] = {0, 0};
        hsize_t count[2] = {1, cols};
        file_space.selectHyperslab(H5S_SELECT_SET, count, start);
        dataset.read(row_data.data(), H5::PredType::NATIVE_UINT, mem_space, file_space);
        const FullSpectrum *data = reinterpret_cast<const FullSpectrum *>(row_data.data());

        mMeasureDurationSecond = rows * data->measureTime / 1000.0;
        QDateTime measureStartTime = HDF5Settings::measureStartTimeFromFileName(filePath);
        QDateTime shotTime;
        if (H5Lexists(file.getId(), "Config", H5P_DEFAULT) > 0) {
            H5::Group cfgGroup = file.openGroup("Config");
            const QDateTime configStartTime = HDF5Settings::parseConfigTimeString(
                HDF5Settings::readConfigStringFromGroup(cfgGroup, "StartTime"));
            if (configStartTime.isValid())
                measureStartTime = configStartTime;

            if (H5Lexists(cfgGroup.getId(), "ShotTime", H5P_DEFAULT) > 0) {
                shotTime = HDF5Settings::parseConfigTimeString(
                    HDF5Settings::readConfigStringFromGroup(cfgGroup, "ShotTime"));
            } else {
                emit reporWriteLog(tr("当前 H5 文件 Config 中未找到 ShotTime，暂以开测时刻为时间零点；可在打靶时刻控件中补填后点「导入打靶时刻」"), QtWarningMsg);
            }
            cfgGroup.close();
        }
        mMeasureStartTime = measureStartTime;
        if (shotTime.isValid()) {
            ui->fusionDateTime->setDateTime(shotTime);
            if (updateMeasureStartTimeFromShotTime(shotTime)) {
                emit reporWriteLog(tr("已按 H5 中 ShotTime 设置时间零点（相对开测偏移 %1 s）；仍可通过打靶时刻控件覆盖")
                                       .arg(mMeasureStartSecond));
            }
        } else if (mMeasureStartTime.isValid()) {
            // 无 ShotTime：时间零点=开测时刻，fusionDateTime 预填开测时刻便于手工改成真实打靶时刻
            updateMeasureStartSecond(0);
            ui->fusionDateTime->setDateTime(mMeasureStartTime);
            emit reporWriteLog(tr("未读取到有效打靶时刻，暂以开测时刻为时间零点；请在打靶时刻控件中补填后点「导入打靶时刻」"), QtWarningMsg);
        } else {
            updateMeasureStartSecond(0);
            emit reporWriteLog(tr("未读取到有效开测时刻与打靶时刻，时间轴从 0 起；请打开有效测量文件"), QtWarningMsg);
        }

        // 设置时间步长下限
        if (data->measureTime == 0) {
            emit reporWriteLog(tr("参考通道#%1，测量时长/s：%2").arg(probeIndex).arg(mMeasureDurationSecond), QtWarningMsg);
            emit reporWriteLog(tr("首行 measureTime 为 0，无法设置时间步长下限"), QtWarningMsg);
        } else {
            const int minStepSec = minTimeStepSecFromMeasureTimeMs(data->measureTime);
            int currentValue = ui->spinBox->value();
            if (ui->spinBox->value() < minStepSec)
                ui->spinBox->setValue(minStepSec);
            ui->spinBox->setMinimum(minStepSec);
            emit reporWriteLog(tr("参考通道#%1，测量时长/s：%2，单帧 measureTime=%3 ms，时间步长下限=%4 s")
                                   .arg(probeIndex)
                                   .arg(mMeasureDurationSecond)
                                   .arg(data->measureTime)
                                   .arg(minStepSec));
        }

        dataset.close();
        group.close();
        file.close();
    } catch (H5::Exception &e) {
        emit reporWriteLog(tr("打开文件失败：%1").arg(QString::fromUtf8(e.getDetailMsg().c_str())), QtWarningMsg);
    } catch (...) {
        emit reporWriteLog(tr("打开文件失败"), QtWarningMsg);
    }
}


void CountRateStatisticsWindow::on_action_exit_triggered()
{
    mainWindow->close();
}

bool CountRateStatisticsWindow::updateMeasureStartTimeFromShotTime(const QDateTime &shotTime)
{
    if (!mMeasureStartTime.isValid() || !shotTime.isValid())
        return false;

    QDateTime measureStartTime = mMeasureStartTime;
    QDateTime fusionShotTime = shotTime;
    measureStartTime.setTimeSpec(Qt::UTC);
    fusionShotTime.setTimeSpec(Qt::UTC);

    updateMeasureStartSecond(fusionShotTime.msecsTo(measureStartTime) / 1000.0);
    return true;
}

void CountRateStatisticsWindow::updateMeasureStartSecond(double startSecond)
{
    mMeasureStartSecond = startSecond;
    ui->line_measure_startT->setText(QString::number(startSecond));
    const double endSecond = mMeasureDurationSecond + startSecond;
    ui->line_measure_endT->setText(QString::number(endSecond));

    const double intMin = static_cast<double>(std::numeric_limits<int>::min());
    const double intMax = static_cast<double>(std::numeric_limits<int>::max());
    const int minimumStartSecond = static_cast<int>(std::ceil(qBound(intMin, startSecond, intMax)));
    const int maximumEndSecond = static_cast<int>(std::floor(qBound(intMin, endSecond, intMax)));

    // 先放宽范围，再设值，避免 spinBox 钳制导致起止错乱
    ui->spinBox_timeStart->setMinimum(minimumStartSecond);
    ui->spinBox_timeStart->setMaximum(qMax(minimumStartSecond, maximumEndSecond));
    ui->spinBox_timeEnd->setMinimum(minimumStartSecond);
    ui->spinBox_timeEnd->setMaximum(qMax(minimumStartSecond, maximumEndSecond));

    // 导入打靶时刻后，解析区间默认对齐整段测量范围
    ui->spinBox_timeStart->setValue(minimumStartSecond);
    ui->spinBox_timeEnd->setValue(maximumEndSecond);
}

void CountRateStatisticsWindow::on_ConfirmTime_clicked()
{
    const QDateTime shotTime = ui->fusionDateTime->dateTime();
    if (!mMeasureStartTime.isValid()) {
        emit reporWriteLog(tr("未读取到有效开测时刻，请先打开测量文件"), QtWarningMsg);
        return;
    }

    if (!shotTime.isValid()) {
        emit reporWriteLog(tr("导入打靶时刻失败：打靶时刻无效"), QtWarningMsg);
        return;
    }

    if (updateMeasureStartTimeFromShotTime(shotTime)) {
        emit reporWriteLog(tr("已按打靶时刻控件更新时间零点（相对开测偏移 %1 s，可为负），可开始解析")
                               .arg(mMeasureStartSecond));
    }
}


void CountRateStatisticsWindow::on_action_startParse_triggered()
{
    emit reporWriteLog(tr("开始解析..."));
    mInterrupted = false;
    mMapCount.clear();
    mMapCountCorrect.clear();
    mMapSpectrum.clear();
    mMapSpectrumCorrect.clear();
    clearAllPlots();
    clearAllDetectorStatisticsRows(ui->tableWidget);
    qApp->setOverrideCursor(QCursor(Qt::WaitCursor));

    // 解析前再次读取打靶时刻控件：支持无 ShotTime 时补全，或覆盖 H5 中已有 ShotTime。
    // 只更新零点偏移与测量范围显示，不强制重置用户已选的解析起止（完整重置请点「导入打靶时刻」）。
    if (mMeasureStartTime.isValid()) {
        const QDateTime shotFromUi = ui->fusionDateTime->dateTime();
        if (shotFromUi.isValid()) {
            QDateTime measureStartTime = mMeasureStartTime;
            QDateTime fusionShotTime = shotFromUi;
            measureStartTime.setTimeSpec(Qt::UTC);
            fusionShotTime.setTimeSpec(Qt::UTC);
            mMeasureStartSecond = fusionShotTime.msecsTo(measureStartTime) / 1000.0;
            ui->line_measure_startT->setText(QString::number(mMeasureStartSecond));
            ui->line_measure_endT->setText(QString::number(mMeasureDurationSecond + mMeasureStartSecond));
        }
    }

    const QString filePath = ui->textBrowser_filepath->toPlainText();
    if (filePath.isEmpty()) {
        qApp->restoreOverrideCursor();
        emit reporWriteLog(tr("请先打开测量文件"), QtWarningMsg);
        return;
    }
    if (mMeasureDurationSecond <= 0) {
        qApp->restoreOverrideCursor();
        emit reporWriteLog(tr("测量时长无效，请重新打开测量文件"), QtWarningMsg);
        return;
    }

    // 测量时间范围（相对打靶）：[mMeasureStartSecond, mMeasureStartSecond + duration]
    const double measureRangeStart = mMeasureStartSecond;
    const double measureRangeEnd = mMeasureStartSecond + mMeasureDurationSecond;
    const double intMin = static_cast<double>(std::numeric_limits<int>::min());
    const double intMax = static_cast<double>(std::numeric_limits<int>::max());
    const int measureMinSec = static_cast<int>(std::ceil(qBound(intMin, measureRangeStart, intMax)));
    const int measureMaxSec = static_cast<int>(std::floor(qBound(intMin, measureRangeEnd, intMax)));
    if (measureMinSec > measureMaxSec) {
        qApp->restoreOverrideCursor();
        emit reporWriteLog(tr("测量时间范围无效：[%1, %2] s").arg(measureRangeStart).arg(measureRangeEnd), QtWarningMsg);
        return;
    }

    // 先读用户输入，再把 spinBox 合法区间同步到测量范围
    const qint32 rawStart = ui->spinBox_timeStart->value();
    const qint32 rawEnd = ui->spinBox_timeEnd->value();
    ui->spinBox_timeStart->setMinimum(measureMinSec);
    ui->spinBox_timeStart->setMaximum(measureMaxSec);
    ui->spinBox_timeEnd->setMinimum(measureMinSec);
    ui->spinBox_timeEnd->setMaximum(measureMaxSec);

    const bool startOutOfRange = (rawStart < measureMinSec || rawStart > measureMaxSec);
    const bool endOutOfRange = (rawEnd < measureMinSec || rawEnd > measureMaxSec);
    if (startOutOfRange || endOutOfRange) {
        qApp->restoreOverrideCursor();
        emit reporWriteLog(tr("解析时间区间 [%1, %2] s 超出测量时间范围 [%3, %4] s，请调整到测量范围内后再解析")
                               .arg(rawStart)
                               .arg(rawEnd)
                               .arg(measureMinSec)
                               .arg(measureMaxSec),
                           QtWarningMsg);
        // 将控件收回到合法范围，便于用户下一次直接改
        if (startOutOfRange)
            ui->spinBox_timeStart->setValue(qBound(measureMinSec, rawStart, measureMaxSec));
        if (endOutOfRange)
            ui->spinBox_timeEnd->setValue(qBound(measureMinSec, rawEnd, measureMaxSec));
        return;
    }

    // t = (StartTime - ShotTime) + (sequence - sequence0) * measureTime_s；无有效打靶时刻时偏移为 0（开测为零点）
    const double tmStartBase = mMeasureStartSecond;
    const qint32 tmStart = ui->spinBox_timeStart->value();
    const qint32 tmEnd = ui->spinBox_timeEnd->value();
    const int stepSec = ui->spinBox->value();
    if (tmStart > tmEnd) {
        qApp->restoreOverrideCursor();
        emit reporWriteLog(tr("解析时间起点不能大于终点"), QtWarningMsg);
        return;
    }
    if (stepSec <= 0) {
        qApp->restoreOverrideCursor();
        emit reporWriteLog(tr("时间步长须大于 0"), QtWarningMsg);
        return;
    }
    emit reporWriteLog(tr("解析时间区间：[%1, %2] s（测量范围 [%3, %4] s，相对打靶零点偏移 %5 s），时间步长：%6 s")
                           .arg(tmStart)
                           .arg(tmEnd)
                           .arg(measureMinSec)
                           .arg(measureMaxSec)
                           .arg(tmStartBase)
                           .arg(stepSec));

    try {
        H5ErrorStackSilencer silence;
        H5::H5File file(h5FilePathStd(filePath), H5F_ACC_RDONLY);

        const quint32 fileMeasureTimeMs = firstSpectrumMeasureTimeMs(file);
        if (fileMeasureTimeMs == 0) {
            qApp->restoreOverrideCursor();
            emit reporWriteLog(tr("无法读取首条能谱 measureTime，请检查文件"), QtWarningMsg);
            file.close();
            return;
        }
        const int minStepSec = minTimeStepSecFromMeasureTimeMs(fileMeasureTimeMs);
        ui->spinBox->setMinimum(minStepSec);
        if (stepSec < minStepSec) {
            qApp->restoreOverrideCursor();
            emit reporWriteLog(tr("时间步长不能小于 %1 s（单帧 measureTime=%2 ms）")
                                   .arg(minStepSec)
                                   .arg(fileMeasureTimeMs),
                               QtWarningMsg);
            file.close();
            return;
        }

        // 预扫描各通道行数，用于进度条总量
        hsize_t totalRowsAll = 0;
        int skippedChannels = 0;
        for (int index = 1; index <= DET_NUM; ++index) {
            const std::string groupName = detectorGroupName(index).toStdString();
            if (!h5GroupExists(file, groupName)) {
                ++skippedChannels;
                continue;
            }
            try {
                H5::Group group = file.openGroup(groupName);
                H5::DataSet dataset = group.openDataSet("Spectrum");
                H5::DataSpace fileSpace = dataset.getSpace();
                hsize_t dims[2] = {0, 0};
                fileSpace.getSimpleExtentDims(dims, nullptr);
                totalRowsAll += dims[0];
                dataset.close();
                group.close();
            } catch (H5::Exception &) {
                ++skippedChannels;
            }
        }

        if (totalRowsAll == 0) {
            qApp->restoreOverrideCursor();
            emit reporWriteLog(tr("文件中未找到可解析的探测器通道数据"), QtWarningMsg);
            file.close();
            return;
        }
        if (skippedChannels > 0) {
            emit reporWriteLog(tr("预扫描：%1 个通道无数据，解析时将跳过").arg(skippedChannels), QtWarningMsg);
        }

        const int progressMax = static_cast<int>(qMin(totalRowsAll, static_cast<hsize_t>(std::numeric_limits<int>::max())));
        QProgressDialog progress(tr("正在解析测量数据..."), tr("取消"), 0,
                                 qMax(1, progressMax), this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.setAutoClose(true);
        progress.setAutoReset(true);
        progress.setValue(0);

        hsize_t processedRows = 0;
        auto updateProgress = [&](int detectorIndex, hsize_t rowIndex, hsize_t rowTotal) {
            progress.setLabelText(tr("通道 #%1：%2 / %3")
                                      .arg(detectorIndex)
                                      .arg(rowIndex)
                                      .arg(rowTotal));
            progress.setValue(qMin(static_cast<int>(processedRows), progressMax));
            if (progress.wasCanceled()) {
                mInterrupted = true;
            }
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        };

        int channelsWithDataInRange = 0;
        for (int index = 1; index <= DET_NUM; ++index)
        {
            if (mInterrupted)
            {
                emit reporWriteLog(tr("解析被中断！"));
                break;
            }

            const std::string groupName = detectorGroupName(index).toStdString();
            if (!h5GroupExists(file, groupName)) {
                emit reporWriteLog(tr("通道#%1 无数据（%2 不存在），已跳过")
                                       .arg(index)
                                       .arg(QString::fromStdString(groupName)),
                                   QtWarningMsg);
                continue;
            }

            try
            {
                H5::Group group = file.openGroup(groupName);
                H5::DataSet dataset = group.openDataSet("Spectrum");

                H5::CompType fileType = dataset.getCompType();
                if (fileType != H5::PredType::NATIVE_UINT)
                {
                    emit reporWriteLog(tr("通道#%1 数据类型不匹配，跳过").arg(index), QtWarningMsg);
                    continue;
                }

                H5::DataSpace file_space = dataset.getSpace();
                hsize_t current_dims[2];
                file_space.getSimpleExtentDims(current_dims, NULL);
                const hsize_t rows = current_dims[0];
                const hsize_t cols = current_dims[1];
                if (rows <= 0)
                {
                    emit reporWriteLog(tr("通道#%1 无可用数据，跳过").arg(index), QtWarningMsg);
                    continue;
                }
                if (cols != kH5SpectrumColumnCount) {
                    emit reporWriteLog(tr("通道#%1 列数异常（%2，期望 %3），跳过")
                                           .arg(index).arg(cols).arg(kH5SpectrumColumnCount), QtWarningMsg);
                    continue;
                }
                if (rows > static_cast<hsize_t>(std::numeric_limits<int>::max())) {
                    emit reporWriteLog(tr("通道#%1 数据行数超过程序上限，跳过").arg(index), QtWarningMsg);
                    continue;
                }

                hsize_t mem_dims[2] = {1, cols};
                H5::DataSpace mem_space(2, mem_dims);
                std::vector<uint32_t> row_data(cols);

                quint32 measureInterval = 0;
                quint32 sequence0 = 0;
                bool haveSequence0 = false;
                {
                    hsize_t firstStart[2] = {0, 0};
                    hsize_t firstCount[2] = {1, cols};
                    file_space.selectHyperslab(H5S_SELECT_SET, firstCount, firstStart);
                    dataset.read(row_data.data(), H5::PredType::NATIVE_UINT, mem_space, file_space);
                    const H5Spectrum* firstRow = reinterpret_cast<const H5Spectrum*>(row_data.data());
                    measureInterval = firstRow->measureTime;
                    if (firstRow->measureTime != 0) {
                        sequence0 = firstRow->sequence;
                        haveSequence0 = true;
                    }
                    file_space.selectNone();
                }

                // 相对打靶：t = offset + (sequence - sequence0)×dt，offset = StartTime - ShotTime（秒）
                const auto relativeTimeSec = [&](quint32 sequence, quint32 measureTimeMs) -> double {
                    const qint64 deltaSeq = static_cast<qint64>(sequence) - static_cast<qint64>(sequence0);
                    return tmStartBase + static_cast<double>(deltaSeq) * measureTimeMs / 1000.0;
                };

                double minV = 0.0;
                double maxV = 0.0;
                double minVDeathTime = 0.0;
                double maxVDeathTime = 0.0;
                quint32 ref = 0;
                double totalCountRate = 0.0;
                double totalDeathTime = 0.0;

                mMapCount[index].clear();
                mMapCountCorrect[index].clear();
                mMapSpectrum[index] = QVector<quint64>(kSpectrumPointCount, 0);
                mMapSpectrumCorrect[index] = QVector<quint64>(kSpectrumPointCount, 0);
                mMapCount[index].reserve(static_cast<int>(qMin(rows, static_cast<hsize_t>(1024))));
                mMapCountCorrect[index].reserve(static_cast<int>(qMin(rows, static_cast<hsize_t>(1024))));

                for (hsize_t i = 0; i < rows;) {
                    if (mInterrupted)
                    {
                        emit reporWriteLog(tr("解析被中断！"));
                        break;
                    }

                    hsize_t start[2] = {i, 0};
                    hsize_t count[2] = {1, cols};
                    file_space.selectHyperslab(H5S_SELECT_SET, count, start);

                    dataset.read(row_data.data(), H5::PredType::NATIVE_UINT, mem_space, file_space);
                    const H5Spectrum* data = reinterpret_cast<const H5Spectrum*>(row_data.data());
                    file_space.selectNone();

                    hsize_t rowAdvance = 1;
                    if (data->measureTime == 0) {
                        emit reporWriteLog(tr("通道#%1 第 %2 行 measureTime 为 0，跳过").arg(index).arg(i + 1), QtWarningMsg);
                    } else {
                        if (!haveSequence0) {
                            sequence0 = data->sequence;
                            haveSequence0 = true;
                        }
                        const double rowTimeSec = relativeTimeSec(data->sequence, data->measureTime);
                        if (rowTimeSec > tmEnd) {
                            break;
                        }
                        if (rowTimeSec >= tmStart)
                        {
                            const quint32 measureTimeMs = data->measureTime;
                            const int stepTarget = qMax(1, static_cast<int>(
                                static_cast<quint64>(stepSec) * 1000 / measureTimeMs));

                            QVector<quint32> spectrum(kSpectrumPointCount, 0);
                            quint64 deathTimeSum = 0;
                            quint32 mergedRows = 0;
                            quint32 lastSequence = data->sequence;

                            for (int t = 0; t < stepTarget; ++t) {
                                const hsize_t rowIdx = i + static_cast<hsize_t>(t);
                                if (rowIdx >= rows)
                                    break;

                                if (t > 0) {
                                    hsize_t binStart[2] = {rowIdx, 0};
                                    hsize_t binCount[2] = {1, cols};
                                    file_space.selectHyperslab(H5S_SELECT_SET, binCount, binStart);
                                    dataset.read(row_data.data(), H5::PredType::NATIVE_UINT, mem_space, file_space);
                                    file_space.selectNone();
                                }
                                const H5Spectrum *row = reinterpret_cast<const H5Spectrum *>(row_data.data());
                                if (row->measureTime == 0 || row->measureTime != measureTimeMs)
                                    break;

                                const double binTimeSec = relativeTimeSec(row->sequence, row->measureTime);
                                if (binTimeSec > tmEnd)
                                    break;
                                if (binTimeSec < tmStart)
                                    continue;

                                for (int j = 0; j < kSpectrumPointCount; ++j)
                                    spectrum[j] += row->spectrum[j];
                                deathTimeSum += row->deathTime;
                                lastSequence = row->sequence;
                                ++mergedRows;
                            }

                            if (mergedRows > 0) {
                                spectrum[kSpectrumPointCount - 1] = 0;
                                rowAdvance = mergedRows;

                                double totalTemp = 0.0;
                                for (int j = 0; j < kSpectrumPointCount; ++j)
                                    totalTemp += spectrum[j];

                                const double sumMeasureMs = mergedRows * measureTimeMs;
                                const double countRate = totalTemp / sumMeasureMs * 1000.0;
                                const double totalTempDeathT =
                                    deathTimeSum * 10.0 / (sumMeasureMs * 10e6) * 100;

                                const double countRateCorrect =
                                    countRate * sumMeasureMs * 10e6
                                    / (sumMeasureMs * 10e6 - deathTimeSum * 10);

                                const double deathTimeCorrectFactor =
                                    (sumMeasureMs * 10e6 > deathTimeSum * 10)
                                        ? sumMeasureMs * 10e6 / (sumMeasureMs * 10e6 - deathTimeSum * 10)
                                        : 1.0;
                                for (int j = 0; j < kSpectrumPointCount; ++j)
                                {
                                    mMapSpectrum[index][j] += spectrum[j];
                                    mMapSpectrumCorrect[index][j] +=
                                        static_cast<quint64>(spectrum[j] * deathTimeCorrectFactor);
                                }

                                if (ref == 0) {
                                    minV = countRate;
                                    maxV = countRate;
                                    minVDeathTime = totalTempDeathT;
                                    maxVDeathTime = totalTempDeathT;
                                } else {
                                    minV = qMin(minV, countRate);
                                    maxV = qMax(maxV, countRate);
                                    minVDeathTime = qMin(minVDeathTime, totalTempDeathT);
                                    maxVDeathTime = qMax(maxVDeathTime, totalTempDeathT);
                                }
                                totalCountRate += countRate;
                                totalDeathTime += totalTempDeathT;
                                ++ref;

                                if (mMapCount[index].size() >= kMaxCountPointsPerDetector) {
                                    emit reporWriteLog(tr("通道#%1 解析点数超过上限 %2，已截断")
                                                           .arg(index).arg(kMaxCountPointsPerDetector), QtWarningMsg);
                                    i += rowAdvance;
                                    processedRows += rowAdvance;
                                    break;
                                }

                                const double timeSec = relativeTimeSec(lastSequence, measureTimeMs);
                                mMapCount[index].push_back(qMakePair(timeSec, countRate));
                                mMapCountCorrect[index].push_back(qMakePair(timeSec, countRateCorrect));
                            }
                        }
                    }

                    i += rowAdvance;
                    processedRows += rowAdvance;
                    if ((processedRows & 0xFF) == 0 || i >= rows) {
                        updateProgress(index, qMin(i, rows), rows);
                    }
                }

                if (ref > 0)
                {
                    auto ensureCell = [&](int row, int col) -> QTableWidgetItem* {
                        if (!ui->tableWidget->item(row, col))
                            ui->tableWidget->setItem(row, col, new QTableWidgetItem(""));
                        QTableWidgetItem *item = ui->tableWidget->item(row, col);
                        item->setTextAlignment(Qt::AlignCenter);
                        return item;
                    };

                    ensureCell(index + 1, 1)->setText(QString::number(minV));
                    ensureCell(index + 1, 2)->setText(QString::number(maxV));
                    ensureCell(index + 1, 3)->setText(QString::number(totalCountRate / ref));
                    ensureCell(index + 1, 4)->setText(QString::number(minVDeathTime, 'e', 2));
                    ensureCell(index + 1, 5)->setText(QString::number(maxVDeathTime, 'e', 2));
                    ensureCell(index + 1, 6)->setText(QString::number(totalDeathTime / ref, 'e', 2));

                    const double deadTimeScale = measureInterval / 100.0;
                    ensureCell(index + 1, 7)->setText(QString::number(minVDeathTime * deadTimeScale, 'f', 3));
                    ensureCell(index + 1, 8)->setText(QString::number(maxVDeathTime * deadTimeScale, 'f', 3));
                    ensureCell(index + 1, 9)->setText(QString::number(totalDeathTime / ref * deadTimeScale, 'f', 3));
                    ++channelsWithDataInRange;
                } else {
                    mMapCount.remove(index);
                    mMapCountCorrect.remove(index);
                    mMapSpectrum.remove(index);
                    mMapSpectrumCorrect.remove(index);
                    clearDetectorStatisticsRow(ui->tableWidget, index);
                    emit reporWriteLog(tr("通道#%1 在时间区间 [%2, %3] 内无数据，已跳过")
                                           .arg(index)
                                           .arg(tmStart)
                                           .arg(tmEnd),
                                       QtWarningMsg);
                }

                dataset.close();
                group.close();
            }
            catch (H5::Exception &e)
            {
                emit reporWriteLog(tr("通道#%1 读取失败：%2，已跳过")
                                       .arg(index)
                                       .arg(QString::fromUtf8(e.getDetailMsg().c_str())),
                                   QtWarningMsg);
            }
            catch (...)
            {
                emit reporWriteLog(tr("通道#%1 读取失败，已跳过").arg(index), QtWarningMsg);
            }
        }

        file.close();
        if (channelsWithDataInRange > 0) {
            emit reporWriteLog(tr("时间区间 [%1, %2] 内成功解析 %3 个通道")
                                   .arg(tmStart)
                                   .arg(tmEnd)
                                   .arg(channelsWithDataInRange));
        } else {
            emit reporWriteLog(tr("所有通道在时间区间 [%1, %2] 内均无数据")
                                   .arg(tmStart)
                                   .arg(tmEnd),
                               QtWarningMsg);
        }
    } catch (H5::Exception &e) {
        emit reporWriteLog(tr("打开或解析文件失败：%1")
                               .arg(QString::fromUtf8(e.getDetailMsg().c_str())),
                           QtWarningMsg);
    } catch (...) {
        emit reporWriteLog(tr("打开或解析文件失败"), QtWarningMsg);
    }

    plotAllDetectors();
    qApp->restoreOverrideCursor();

    emit reporWriteLog(tr("解析结束"));
}


void CountRateStatisticsWindow::on_action_stopParse_triggered()
{
    emit reporWriteLog(tr("中断解析"));
    mInterrupted = true;
}


void CountRateStatisticsWindow::on_tableWidget_cellClicked(int row, int column)
{
    if (row <= 1 || column != 0)
        return;

    const int detector = row - 1;
    if (detector < 1 || detector > DET_NUM)
        return;

    const bool newEnabled = !mOriginalEnabled[detector];
    mOriginalEnabled[detector] = newEnabled;
    mCorrectEnabled[detector] = newEnabled;

    syncDetectorCheckboxes();
    updateAllPlotVisibility();
}

