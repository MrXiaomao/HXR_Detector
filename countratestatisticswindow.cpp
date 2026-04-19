/*
 * @Author: MrPan
 * @Date: 2026-01-14 09:21:28
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2026-04-17 17:56:57
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
#include <QSignalBlocker>
#include <math.h>

namespace {
constexpr int kDetectorsPerPlot = 16;
constexpr int kSpectrumPointCount = 8192;
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
    }

    ui->tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    ui->tableWidget->setColumnWidth(0, 40);

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

    updateTableRowCount();
    for (int row=0; row<ui->tableWidget->rowCount(); ++row)
    {
        ui->tableWidget->setRowHeight(row, 25);
        if (row>=2)
        {
            ui->tableWidget->setItem(row, 0, new QTableWidgetItem(QString("#%1").arg(row-1)));
            ui->tableWidget->item(row, 0)->setTextAlignment(Qt::AlignCenter);
        }
    }
    ui->tableWidget->setFixedHeight(650);
    ui->leftStackedWidget->setMinimumWidth(582);

    ui->tableWidget->setSpan(0, 0, 2, 1);
    ui->tableWidget->setSpan(0, 1, 1, 2);
    ui->tableWidget->setItem(0, 1, new QTableWidgetItem("最小值(计数率cps)"));
    ui->tableWidget->setSpan(0, 3, 1, 2);
    ui->tableWidget->setItem(0, 3, new QTableWidgetItem("最大值(计数率cps)"));
    ui->tableWidget->setSpan(0, 5, 1, 2);
    ui->tableWidget->setItem(0, 5, new QTableWidgetItem("均值(计数率cps)"));
    ui->tableWidget->setSpan(0, 7, 1, 3);
    ui->tableWidget->setItem(0, 7, new QTableWidgetItem("死时间率％"));

    ui->tableWidget->item(0, 1)->setTextAlignment(Qt::AlignCenter);
    ui->tableWidget->item(0, 3)->setTextAlignment(Qt::AlignCenter);
    ui->tableWidget->item(0, 5)->setTextAlignment(Qt::AlignCenter);
    ui->tableWidget->item(0, 7)->setTextAlignment(Qt::AlignCenter);

    ui->tableWidget->item(0, 1)->setBackground(Qt::gray);
    ui->tableWidget->item(0, 3)->setBackground(Qt::gray);
    ui->tableWidget->item(0, 5)->setBackground(Qt::gray);
    ui->tableWidget->item(0, 7)->setBackground(Qt::gray);

    initCustomPlot(ui->plot_up, tr("时间/s"), tr("计数率/cps"), 1, kDetectorsPerPlot);
    initCustomPlot(ui->plot_down, tr("时间/s"), tr("计数率/cps"), kDetectorsPerPlot + 1, DET_NUM);

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
    // legendGrid->addWidget(new QLabel(tr("左列原始"), legendPanel), 0, 0);
    // legendGrid->addWidget(new QLabel(tr("右列修正"), legendPanel), 0, 1);

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
    for (QCustomPlot* plot : {ui->plot_up, ui->plot_down})
    {
        for (int i = 0; i < plot->graphCount(); ++i)
            plot->graph(i)->data()->clear();
        plot->replot(QCustomPlot::rpQueuedReplot);
    }
}


void CountRateStatisticsWindow::updatePlotVisibility(QCustomPlot* customPlot, int startDetector, int endDetector)
{
    const bool showOriginalGlobal = ui->cb_countOriginalPlotShow->isChecked();
    const bool showCorrectedGlobal = ui->cb_countCorrectPlotShow->isChecked();

    const int localCount = endDetector - startDetector + 1; // 16
    for (int localIndex = 0; localIndex < localCount; ++localIndex)
    {
        const int det = startDetector + localIndex;
        const int originalGraphIndex = localIndex; // 原始曲线：先加
        const int correctedGraphIndex = kDetectorsPerPlot + localIndex; // 修正曲线：后加

        if (originalGraphIndex < customPlot->graphCount())
            customPlot->graph(originalGraphIndex)->setVisible(showOriginalGlobal && mOriginalEnabled[det]);

        if (correctedGraphIndex < customPlot->graphCount())
            customPlot->graph(correctedGraphIndex)->setVisible(showCorrectedGlobal && mCorrectEnabled[det]);
    }
}


void CountRateStatisticsWindow::updateAllPlotVisibility()
{
    updatePlotVisibility(ui->plot_up, 1, kDetectorsPerPlot);
    updatePlotVisibility(ui->plot_down, kDetectorsPerPlot + 1, DET_NUM);
    ui->plot_up->replot(QCustomPlot::rpQueuedReplot);
    ui->plot_down->replot(QCustomPlot::rpQueuedReplot);
}


void CountRateStatisticsWindow::plotAllDetectors()
{
    QVector<double> keys;
    keys.reserve(kSpectrumPointCount);
    for (int i = 0; i < kSpectrumPointCount; ++i)
        keys << i;

    for (int det = 1; det <= DET_NUM; ++det)
    {
        if (!mMapSpectrum.contains(det) || !mMapSpectrumAdjust.contains(det))
            continue;

        QCustomPlot* targetPlot = det <= kDetectorsPerPlot ? ui->plot_up : ui->plot_down;
        const int localIndex = (det <= kDetectorsPerPlot ? det - 1 : det - kDetectorsPerPlot - 1);
        const int originalGraphIndex = localIndex; // 原始曲线：先加
        const int correctedGraphIndex = kDetectorsPerPlot + localIndex; // 修正曲线：后加

        if (correctedGraphIndex >= targetPlot->graphCount())
            continue;

        targetPlot->graph(originalGraphIndex)->setData(keys, mMapSpectrum[det]);
        targetPlot->graph(correctedGraphIndex)->setData(keys, mMapSpectrumAdjust[det]);
    }

    ui->plot_up->rescaleAxes(true);
    ui->plot_down->rescaleAxes(true);
    updateAllPlotVisibility();
}


void CountRateStatisticsWindow::initCustomPlot(QCustomPlot* customPlot, QString axisXLabel, QString axisYLabel, int startDetector, int endDetector)
{
    // QCustomPlotHelper* customPlotHelper = new QCustomPlotHelper(customPlot, this);
    customPlot->setAntialiasedElements(QCP::aeAll);
    customPlot->legend->setVisible(false);
    customPlot->xAxis->setTickLabelRotation(-45);
    customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables | QCP::iSelectLegend);
    customPlot->xAxis->rescale(true);
    customPlot->yAxis->rescale(true);

    // 设置刻度范围
    customPlot->xAxis->setRange(0, 1000);
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

    // 解析文件，获取能谱范围时长
    {
        // 1. 打开文件
        QTextCodec* gbk_codec = QTextCodec::codecForName("GBK");
        QByteArray filePathBytes = gbk_codec->fromUnicode(filePath);
        H5::H5File file(filePathBytes.toStdString(), H5F_ACC_RDONLY);

        // 2. 打开分组核数据集
        H5::Group group = file.openGroup("Detector#1");
        H5::DataSet dataset = group.openDataSet("Spectrum");

        // 3. 确认数据类型匹配（可选，用于错误检查）
        H5::CompType fileType = dataset.getCompType();
        if (fileType != H5::PredType::NATIVE_UINT) {
            file.close();
            return;
        }

        // 4. 初始文件空间
        H5::DataSpace file_space = dataset.getSpace();
        hsize_t current_dims[2];
        file_space.getSimpleExtentDims(current_dims, NULL);
        hsize_t rows = current_dims[0];
        hsize_t cols = current_dims[1];
        if (rows<=0){
            group.close();
            dataset.close();
            file_space.close();
            file.close();
            return;
        }

        // 5. 内存空间：只读取一行
        hsize_t mem_dims[2] = {1, cols};
        H5::DataSpace mem_space(2, mem_dims);
        uint32_t* row_data = new uint32_t[cols];

        // 6. 指定数据读取位置
        hsize_t start[2] = {0, 0};
        hsize_t count[2] = {1, cols};
        file_space.selectHyperslab(H5S_SELECT_SET, count, start);

        // 7. 读取数据
        dataset.read(row_data, H5::PredType::NATIVE_UINT, mem_space, file_space);
        FullSpectrum* data = reinterpret_cast<FullSpectrum*>(row_data);

        ui->line_measure_endT->setText(QString::number(rows * data->measureTime / 1000));
        emit reporWriteLog(tr("测量时长/s：%1").arg(ui->line_measure_endT->text()));

        delete[] row_data;
        row_data = nullptr;

        group.close();
        dataset.close();
        file_space.close();
    }
}


void CountRateStatisticsWindow::on_action_exit_triggered()
{
    mainWindow->close();
}


void CountRateStatisticsWindow::on_action_startParse_triggered()
{
    emit reporWriteLog(tr("开始解析..."));
    mInterrupted = false;
    mMapSpectrum.clear();
    mMapSpectrumAdjust.clear();
    clearAllPlots();
    qApp->setOverrideCursor(QCursor(Qt::WaitCursor));
    quint32 tmStart = ui->spinBox_timeStart->value();
    quint32 tmEnd = ui->spinBox_timeEnd->value();
    QString filePath = ui->textBrowser_filepath->toPlainText();

    // 1. 打开文件
    QTextCodec* gbk_codec = QTextCodec::codecForName("GBK");
    QByteArray filePathBytes = gbk_codec->fromUnicode(filePath);
    H5::H5File file(filePathBytes.toStdString(), H5F_ACC_RDONLY);

    for (int index = 1; index <= DET_NUM; ++index)
    {
        if (mInterrupted)
        {
            emit reporWriteLog(tr("解析被中断！"));
            break;
        }

        try
        {
            // 2. 打开分组核数据集
            H5::Group group = file.openGroup(QString("Detector#%1").arg(index).toStdString().c_str());
            H5::DataSet dataset = group.openDataSet("Spectrum");

            // 3. 确认数据类型匹配（可选，用于错误检查）
            H5::CompType fileType = dataset.getCompType();
            if (fileType != H5::PredType::NATIVE_UINT)
            {
                emit reporWriteLog(tr("通道#%1 数据类型不匹配，跳过").arg(index), QtWarningMsg);
                continue;
            }

            // 4. 初始文件空间
            H5::DataSpace file_space = dataset.getSpace();
            hsize_t current_dims[2];
            file_space.getSimpleExtentDims(current_dims, NULL);
            hsize_t rows = current_dims[0];
            hsize_t cols = current_dims[1];
            if (rows <= 0)
            {
                emit reporWriteLog(tr("通道#%1 无可用数据，跳过").arg(index), QtWarningMsg);
                continue;
            }

            // 5. 内存空间：每次读取一行
            hsize_t mem_dims[2] = {1, cols};
            H5::DataSpace mem_space(2, mem_dims);
            uint32_t* row_data = new uint32_t[cols];
            quint64 minV = quint32(-1);
            quint64 maxV = 0;
            double minVAdjust = quint32(-1);
            double maxVAdjust = 0;
            double minVDeathTime = quint32(-1);
            double maxVDeathTime = 0;
            quint32 ref = 0;
            quint64 total = 0;
            double totalDeathTime = 0;
            double totalAdjust = 0;// 修正后
            QVector<double> spectrumTotal(kSpectrumPointCount, 0);
            QVector<double> spectrumTotalAdjust(kSpectrumPointCount, 0);

            for (hsize_t i = 0; i < rows;/* ++i*/) {
                if (mInterrupted)
                {
                    emit reporWriteLog(tr("解析被中断！"));
                    break;
                }

                // 6. 指定数据读取位置
                hsize_t start[2] = {i, 0};
                hsize_t count[2] = {1, cols};
                file_space.selectHyperslab(H5S_SELECT_SET, count, start);

                // 7. 读取数据
                dataset.read(row_data, H5::PredType::NATIVE_UINT, mem_space, file_space);
                FullSpectrum* data = reinterpret_cast<FullSpectrum*>(row_data);
                if (data->sequence >= tmStart && data->sequence <= tmEnd)
                {
                    // 8. 对数据按秒进行重新分类
                    quint64 totalTemp = 0;//计数
                    double totalTempDeathT = 0;//死时间
                    quint8 step = qMax<quint8>(1, 1000 / data->measureTime);
                    QVector<quint32> spectrum(kSpectrumPointCount, 0);// 能谱/s
                    quint32 deathTime = 0;// 死时间/s
                    for (int t = 0; t < step; ++t)
                    {
                        for (int j=0; j<kSpectrumPointCount; ++j)
                        {
                            spectrum[j] += data->spectrum[j];
                        }

                        deathTime += data->deathTime;
                    }
                    spectrum[kSpectrumPointCount-1]=0; //最后一道置零
                    i += step;

                    // 9. 能谱统计
                    for (int j=0; j<kSpectrumPointCount; ++j)
                    {
                        // 9. 对数据进行累加，计数率
                        totalTemp += spectrum[j];
                        spectrumTotal[j] += spectrum[j];
                        spectrumTotalAdjust[j] += (double)spectrum[j] * data->measureTime * 10e6 / (data->measureTime*10e6 - data->deathTime*10);
                    }

                    // 死时间率统计
                    totalTempDeathT = (double)deathTime*10 / (data->measureTime*10e6);
                    minVDeathTime = qMin(minVDeathTime, (double)totalTempDeathT);
                    maxVDeathTime = qMax(maxVDeathTime, (double)totalTempDeathT);
                    totalDeathTime += totalTempDeathT;

                    // 9. 修正后10e6
                    double totalSAdjust = 0;
                    totalSAdjust = (double)totalTemp * data->measureTime * 10e6 / (data->measureTime*10e6 - data->deathTime*10);

                    ++ref;
                    minV = qMin(minV, (quint64)totalTemp);
                    maxV = qMax(maxV, (quint64)totalTemp);
                    total += (quint64)totalTemp;

                    minVAdjust = qMin((double)minVAdjust, (double)totalSAdjust);
                    maxVAdjust = qMax((double)maxVAdjust, (double)totalSAdjust);
                    totalAdjust += totalSAdjust;
                }
                else{
                    ++i;
                }

                // 释放文件空间
                file_space.selectNone();
            }

            if (ref > 0)
            {
                if (!ui->tableWidget->item(index + 1, 1)) ui->tableWidget->setItem(index + 1, 1, new QTableWidgetItem(""));
                if (!ui->tableWidget->item(index + 1, 2)) ui->tableWidget->setItem(index + 1, 2, new QTableWidgetItem(""));
                if (!ui->tableWidget->item(index + 1, 3)) ui->tableWidget->setItem(index + 1, 3, new QTableWidgetItem(""));
                if (!ui->tableWidget->item(index + 1, 4)) ui->tableWidget->setItem(index + 1, 4, new QTableWidgetItem(""));
                if (!ui->tableWidget->item(index + 1, 5)) ui->tableWidget->setItem(index + 1, 5, new QTableWidgetItem(""));
                if (!ui->tableWidget->item(index + 1, 6)) ui->tableWidget->setItem(index + 1, 6, new QTableWidgetItem(""));
                if (!ui->tableWidget->item(index + 1, 7)) ui->tableWidget->setItem(index + 1, 7, new QTableWidgetItem(""));
                if (!ui->tableWidget->item(index + 1, 8)) ui->tableWidget->setItem(index + 1, 8, new QTableWidgetItem(""));
                if (!ui->tableWidget->item(index + 1, 9)) ui->tableWidget->setItem(index + 1, 9, new QTableWidgetItem(""));

                ui->tableWidget->item(index + 1, 1)->setText(QString::number(minV));
                ui->tableWidget->item(index + 1, 2)->setText(QString::number(minVAdjust));
                ui->tableWidget->item(index + 1, 3)->setText(QString::number(maxV));
                ui->tableWidget->item(index + 1, 4)->setText(QString::number(maxVAdjust));
                ui->tableWidget->item(index + 1, 5)->setText(QString::number(total / ref));
                ui->tableWidget->item(index + 1, 6)->setText(QString::number(totalAdjust / ref));

                ui->tableWidget->item(index + 1, 7)->setText(QString::number(minVDeathTime, 'e', 2));
                ui->tableWidget->item(index + 1, 8)->setText(QString::number(maxVDeathTime, 'e', 2));
                ui->tableWidget->item(index + 1, 9)->setText(QString::number(totalDeathTime * 100 / ref, 'e', 2));
                mMapSpectrum[index] = spectrumTotal;
                mMapSpectrumAdjust[index] = spectrumTotalAdjust;
            }

            delete[] row_data;
            dataset.close();
            group.close();
        }
        catch (...)
        {
            emit reporWriteLog(tr("通道#%1 读取失败，已跳过").arg(index), QtWarningMsg);
        }
    }

    file.close();
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

    QCustomPlot* targetPlot = detector <= kDetectorsPerPlot ? ui->plot_up : ui->plot_down;
    const int plotStart = detector <= kDetectorsPerPlot ? 1 : (kDetectorsPerPlot + 1);
    const int plotEnd = detector <= kDetectorsPerPlot ? kDetectorsPerPlot : DET_NUM;

    const bool newEnabled = !mOriginalEnabled[detector];
    mOriginalEnabled[detector] = newEnabled;
    mCorrectEnabled[detector] = newEnabled;

    syncDetectorCheckboxes();
    updatePlotVisibility(targetPlot, plotStart, plotEnd);
    targetPlot->replot(QCustomPlot::rpQueuedReplot);
}

