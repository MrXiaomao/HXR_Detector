/*
 * @Author: MrPan
 * @Date: 2025-04-20 09:21:28
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2026-02-04 17:18:06
 * @Description: 离线数据分析
 */
#include "neutronyieldstatisticswindow.h"
#include "ui_neutronyieldstatisticswindow.h"
#include "globalsettings.h"

#include <QButtonGroup>
#include <QFileDialog>
#include <QAction>
#include <QToolButton>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMessageBox>
#include <math.h>

// #include "H5Cpp.h"
#include "CrasterParabolicHeatmapWidget.h"
#include "MollweideHeatmapWidget.h"

NeutronYieldStatisticsWindow::NeutronYieldStatisticsWindow(bool isDarkTheme, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::NeutronYieldStatisticsWindow)
    , mIsDarkTheme(isDarkTheme)
    , mainWindow(static_cast<QGoodWindowHelper*>(parent))
{
    ui->setupUi(this);

    // 初始化界面
    initUi();
    restoreSettings();
    applyColorTheme();
    connect(this, SIGNAL(reporWriteLog(const QString&,QtMsgType)), this, SLOT(replyWriteLog(const QString&,QtMsgType)));
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
        detectorStatusButton->setText(tr("设备信息"));
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

                GlobalSettings settingsGlobal;
                settingsGlobal.setValue("Global/DefaultPage", "detectorStatus");
            } else {
                if(ui->leftStackedWidget->currentWidget() == ui->detectorStatusWidget) {
                    ui->leftStackedWidget->hide();
                    detectorStatusButton->setChecked(false);

                    GlobalSettings settingsGlobal;
                    settingsGlobal.setValue("Global/DefaultPage", "");
                } else {
                    ui->leftStackedWidget->setCurrentWidget(ui->detectorStatusWidget);
                    detectorStatusButton->setChecked(true);

                    GlobalSettings settingsGlobal;
                    settingsGlobal.setValue("Global/DefaultPage", "detectorStatus");
                }
            }
        });

        connect(ui->toolButton_closeDetectorStatusWidget,&QPushButton::clicked,this,[=](){
            ui->leftStackedWidget->hide();
            detectorStatusButton->setChecked(false);

            GlobalSettings settingsGlobal;
            settingsGlobal.setValue("Global/DefaultPage", "");
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

                GlobalSettings settingsGlobal;
                settingsGlobal.setValue("Global/ShowRightSide", "true");
            } else {
                ui->rightVboxWidget->hide();

                GlobalSettings settingsGlobal;
                settingsGlobal.setValue("Global/ShowRightSide", "false");
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
    ui->tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    ui->tableWidget->setColumnWidth(0, 40);

    for (int column=0; column<ui->tableWidget->columnCount(); ++column)
    {
        for (int row=0; row<ui->tableWidget->rowCount(); ++row)
        {
            ui->tableWidget->setItem(row, column, new QTableWidgetItem(""));
            ui->tableWidget->item(row, column)->setTextAlignment(Qt::AlignCenter);
        }
    }

    for (int row=0; row<ui->tableWidget->rowCount(); ++row)
    {
        ui->tableWidget->setRowHeight(row, 25);
        ui->tableWidget->setItem(row, 0, new QTableWidgetItem(QString("#%1").arg(row+1)));
        ui->tableWidget->item(row, 0)->setTextAlignment(Qt::AlignCenter);
    }
    ui->tableWidget->setFixedHeight(625);
    ui->leftStackedWidget->setMinimumWidth(200);

    // 连接表格选择变化信号
    connect(ui->tableWidget, &QTableWidget::itemSelectionChanged, this, &NeutronYieldStatisticsWindow::on_tableWidget_selectionChanged);

    initSpectrumCustomPlot();
    initCountCustomPlot();
    initYieldCustomPlot();

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
        GlobalSettings settingsGlobal;
        QString defaultPage = settingsGlobal.value("Global/DefaultPage").toString();
        if (defaultPage == "detectorStatus")
            detectorStatusButton->clicked();

        if (settingsGlobal.contains("Global/MainWindows-State")){
            this->restoreState(settingsGlobal.value("Global/MainWindows-State").toByteArray());
        }

        if (settingsGlobal.value("Global/ShowRightSide").toString() == "false")
            ui->rightVboxWidget->hide();

        if (settingsGlobal.contains("Global/splitterH1/State")){
            QSplitter *splitterH1 = this->findChild<QSplitter*>("splitterH1");
            if (splitterH1)
            {
                splitterH1->restoreState(settingsGlobal.value("Global/splitterH1/State").toByteArray());
            }
        }

        if (settingsGlobal.contains("Global/splitterV2/State")){
            QSplitter *splitterV2 = this->findChild<QSplitter*>("splitterV2");
            if (splitterV2)
            {
                splitterV2->restoreState(settingsGlobal.value("Global/splitterV2/State").toByteArray());
            }
        }
    }
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

            // 创建一个 QTextCursor
            QTextCursor cursor = ui->textEdit_log->textCursor();
            QTextDocument *document = cursor.document();
            QString html = document->toHtml();
            html = html.replace("color:#000000", "color:#ffffff");
            document->setHtml(html);
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

            QTextCursor cursor = ui->textEdit_log->textCursor();
            QTextDocument *document = cursor.document();
            QString html = document->toHtml();
            html = html.replace("color:#ffffff", "color:#000000");
            document->setHtml(html);
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
        cursor.insertHtml(QString("<span style='color:red;'>%1</span>").arg(msg));
    else
        cursor.insertHtml(QString("<span style='color:green;'>%1</span>").arg(msg));

    // 最后插入换行符
    cursor.insertHtml("<br>");

    // 确保 QTextEdit 显示了光标的新位置
    ui->textEdit_log->setTextCursor(cursor);

    //限制行数
    QTextDocument *document = ui->textEdit_log->document(); // 获取文档对象，想象成打开了一个TXT文件
    int rowCount = document->blockCount(); // 获取输出区的行数
    int maxRowNumber = 2000;//设定最大行
    if(rowCount > maxRowNumber){//超过最大行则开始删除
        QTextCursor cursor = QTextCursor(document); // 创建光标对象
        cursor.movePosition(QTextCursor::Start); //移动到开头，就是TXT文件开头

        for (int var = 0; var < rowCount - maxRowNumber; ++var) {
            cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor); // 向下移动并选中当前行
        }
        cursor.removeSelectedText();//删除选择的文本
    }
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
        int detID = 1; //默认读取探测器1的数据
        if (ui->tableWidget->selectedItems().count() > 0)
            detID = ui->tableWidget->selectedItems()[0]->row() + 1;

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
    // 打开历史测量数据文件...
    GlobalSettings settings;
    QString lastPath = settings.value("mainWindow/LastFilePath", QDir::homePath()).toString();
    QString filter = "测量文件 (*.H5);;所有文件 (*.*)";
    QString filePath = QFileDialog::getOpenFileName(this, tr("打开测量数据文件"), lastPath, filter);

    if (filePath.isEmpty() || !QFileInfo::exists(filePath))
        return;

    // 判断一下文件名有效性，提取测量时间
    QString timePart = QFileInfo(filePath).baseName().left(17).replace('_', ' ');
    QDateTime datetime = QDateTime::fromString(timePart, "yyyy-MM-dd HHmmss");
    if (datetime.isValid()) {
        ui->messureDateTime->setDateTime(datetime);
        ui->fusionDateTime->setDateTime(datetime);
    } else {
        qDebug().nospace() << "文件名解析到的时间格式无效";
        QMessageBox::information(this, tr("提示"), tr("文件名解析到的时间格式无效。"));
        return;
    }

    emit reporWriteLog(tr("选择测量文件：%1").arg(filePath));

    settings.setValue("mainWindow/LastFilePath", filePath);
    ui->textBrowser_filepath->setText(filePath);

    // 解析文件，获取能谱范围时长
    {
        mDetectorSpecCount.clear();
        mDealSucess.clear();

        // 1. 打开文件
        H5::H5File file(filePath.toStdString(), H5F_ACC_RDONLY);

        // 解析给出当前有能谱数据的通道编号集合
        for(int i=0; i<24; i++){
            // 2. 打开分组核数据集
            QString detName = QString("Detector#%1").arg(i+1);
            H5::Group group = file.openGroup(detName.toStdString());
            H5::DataSet dataset = group.openDataSet("Spectrum");

            // 3. 确认数据类型匹配（可选，用于错误检查）
            H5::CompType fileType = dataset.getCompType();
            if (fileType != H5::PredType::NATIVE_UINT) {
                file.close();
                continue;
            }

            // 4. 初始文件空间
            H5::DataSpace file_space = dataset.getSpace();
            hsize_t current_dims[2];
            file_space.getSimpleExtentDims(current_dims, NULL);
            hsize_t rows = current_dims[0];
            hsize_t cols = current_dims[1];

            if(rows>0){
                mDetectorSpecCount[i+1] = rows;
                mDealSucess[i+1] = false;
            }
            if(i >= 0 && i <= ui->tableWidget->rowCount()){
                ui->tableWidget->setItem(i, 1, new QTableWidgetItem(QString::number(rows)));
            }
        }
        
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
        H5Spectrum* data = reinterpret_cast<H5Spectrum*>(row_data);

        quint32 measureTimelength = rows * data->measureTime / 1000;
        ui->spinBox_timeEnd->setMaximum(measureTimelength / 60);

        emit reporWriteLog(tr("测量时长/s：%1").arg(measureTimelength));

        delete[] row_data;
        row_data = nullptr;

        group.close();
        dataset.close();
        file_space.close();
    }
}


void NeutronYieldStatisticsWindow::on_action_exit_triggered()
{
    mainWindow->close();
}


void NeutronYieldStatisticsWindow::on_action_startCalculate_triggered()
{
    mInterrupted = false;

    // 获取测量的起始时刻，以打靶时刻为零时刻
    // 获取两个时间的时间戳（秒）
    qint64 seconds1 = ui->fusionDateTime->dateTime().toSecsSinceEpoch();
    qint64 seconds2 = ui->messureDateTime->dateTime().toSecsSinceEpoch();
    qint64 beginTime = seconds2 - seconds1;

    // 设定待分析的时间区间以及步长
    int startTime = ui->spinBox_timeStart->value() * 60; //单位：s
    int endTime = ui->spinBox_timeEnd->value() * 60; //单位：s
    int timeStep = ui->spinBox_step->value() * 60;//单位：s

    if(startTime >= endTime)
    {
        QMessageBox::information(this, tr("提示"), tr("退出解析！输入的解析时间起点>=解析时间终点。"));
        return;
    }

    QString tmStart = ui->fusionDateTime->dateTime().addSecs(startTime).toString("yyyy-MM-dd hh:mm:ss");
    QString tmEnd = ui->fusionDateTime->dateTime().addSecs(endTime).toString("yyyy-MM-dd hh:mm:ss");
    QString step = QString::number(timeStep/60);
    QString f_segment = QString::number((endTime - startTime)*1.0/timeStep, 'f', 1);
    QString i_segment = QString::number((endTime - startTime)/timeStep);
    QString msgTip = QString("您选择的"
                             "\n起点时间：%1"
                             "\n结束时间：%2"
                             "\n时间步长：%3分钟"
                             "\n计算结果：%4条曲线，根据整除原则，将只显示%5条曲线"
                             "\n\n点击[确认]开始解析"
                             "\n点击[否]返回重新修改").arg(tmStart, tmEnd, step, f_segment, i_segment);
    if (QMessageBox::question(this, tr("提示"), msgTip, QMessageBox::Yes, QMessageBox::No)!=QMessageBox::Yes)
        return;

    emit reporWriteLog(tr("开始解析..."));

    QString detectorList;
    for(int detID:mDetectorSpecCount.keys()){
        detectorList += QString::number(detID) + ",";

        if(mInterrupted) {
            break;
        }
        if(!dealFile[detID-1]) delete dealFile[detID-1];
        dealFile[detID-1] = new ParseData();

        quint32 specCount = 0;
        dealFile[detID-1]->setStartTime(beginTime);

        QString filePath = ui->textBrowser_filepath->toPlainText();
        if (QFileInfo(filePath).suffix() == "H5")
        {
            int index = 1; //默认读取探测器1的数据
            if (ui->tableWidget->selectedItems().count() > 0)
                index = ui->tableWidget->selectedItems()[0]->row() + 1;
            specCount = dealFile[detID-1]->parseH5File(filePath, index);
        }
        else//处理网口原始数据，暂时搁置，后续有空再处理
        {
            specCount = dealFile[detID-1]->parseDatFile(filePath);
        }

        if (specCount <= 0)
        {
            qInfo().nospace() << tr("文件中未找到完整能谱数据。");
        }
        else {
            if (dealFile[detID-1]->getResult_offline(timeStep, startTime, endTime))
            {
                mDealSucess[detID] = true;
                emit sigSuccess(detID);
            }
            else
            {
                emit reporWriteLog(QString("谱仪%1解析失败").arg(detID));
                // emit sigFail();
            }
        }

        emit reporWriteLog(QString("谱仪%1解析完成").arg(detID));
    }

    // 去掉最后一个逗号
    if (!detectorList.isEmpty())
        detectorList.chop(1);
    QString msg = QString("谱仪：%1解析完成").arg(detectorList);
    emit reporWriteLog(msg);

    QMessageBox::information(this, tr("提示"), msg);
}


void NeutronYieldStatisticsWindow::on_action_stopCalculate_triggered()
{
    emit reporWriteLog(tr("中断解析"));
    mInterrupted = true;
}

void NeutronYieldStatisticsWindow::on_tableWidget_selectionChanged()
{
    // 获取当前选中的行
    QList<QTableWidgetItem*> selectedItems = ui->tableWidget->selectedItems();
    if (selectedItems.isEmpty())
        return;

    int row = selectedItems[0]->row();
    if (row < 0)
        return;

    int detID = row + 1; // 探测器ID从1开始

    // 检查数据是否已经解析完成
    if (detID > 0 && detID <= 24)
    {
        if(!mDealSucess[detID]){
            QMessageBox::information(this, tr("提示"), tr("谱仪%1不存在解析成功的数据！").arg(detID));
            return; 
        }
        
        // 更新计数衰减曲线
        QVector<specStripData> count909_picture = dealFile[detID-1]->GetCount909Data();
        if (!count909_picture.isEmpty())
        {
            std::vector<double> time;
            std::vector<double> count;
            std::vector<double> fitCount;
            std::vector<double> residual;

            for(int i=0; i<count909_picture.size(); i++)
            {
                time.push_back(count909_picture.at(i).x);
                count.push_back(count909_picture.at(i).y);
                fitCount.push_back(count909_picture.at(i).fit_y);
                residual.push_back(count909_picture.at(i).residual_rate);
            }
            slotUpdate_Count909_time(time, count, fitCount, residual);

            // 更新第一个时间点的909keV能谱
            QVector<specStripData> allSpec = dealFile[detID-1]->GetStripData(0);
            if (!allSpec.isEmpty())
            {
                std::vector<double> energy;
                std::vector<double> specCount;
                std::vector<double> fitSpecCount;
                std::vector<double> residual;

                for(auto data : allSpec)
                {
                    energy.push_back(data.x);
                    specCount.push_back(data.y);
                    fitSpecCount.push_back(data.fit_y);
                    residual.push_back(data.residual_rate);
                }
                slotUpdateSpec_909keV(energy, specCount, fitSpecCount, residual);
            }

            // 更新多段能谱
            QVector<ParseData::mergeSpecData> mergeSpecData = dealFile[detID-1]->GetMergeSpec();
            if (!mergeSpecData.isEmpty())
            {
                std::vector<ParseData::mergeSpecData> allSpec_vec;
                for(auto data : mergeSpecData)
                {
                    allSpec_vec.push_back(data);
                }
                slotUpdateMultiSegmentPlotDatas(allSpec_vec);
            }
        }
    }
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
        ui->tableWidget->setItem(detID-1, 2, new QTableWidgetItem(QString::number(count909_N0)));

        //读取灵敏度标定数据，给出中子产额
        QSettings mSettings("./config/NeutronYield.ini", QSettings::IniFormat);
        double yield_c = mSettings.value(QString("Table/yield%1").arg(detID), 1.0).toDouble();
        double initN_c = mSettings.value(QString("Table/initial909Count%1").arg(detID), 1.0).toDouble();
        double yield = 0.0;
        if(initN_c>0.0){
            yield = count909_N0 / initN_c * yield_c;
        }

        ui->tableWidget->setItem(detID-1, 3, new QTableWidgetItem(QString::number(yield)));

        //获取探测器经纬度
        //从HDF5加载配置信息
        HDF5Settings *settings = HDF5Settings::instance();
        QMap<quint8, DetParameter>& detParameters = settings->detParameters();
        DetParameter& detParameter = detParameters[detID];
        myield[detID] = Sensor{detParameter.longitude, detParameter.latitude, yield};

        //更新中子产额分布图
        slotUpdate_yieldDis(myield);
    }

    //绘制中子产额空间分布

    //显示计数衰减曲线、显示计数率和残差%
    std::vector<double> time;// x
    std::vector<double> count;// y
    std::vector<double> fitCount; // fity 拟合曲线
    std::vector<double> residual; // 残差

    QVector<specStripData> count909_picture = dealFile[detID-1]->GetCount909Data();
    specStripData tempData;
    for(int i=0; i<count909_picture.size(); i++)
    {
        time.push_back(count909_picture.at(i).x);
        count.push_back(count909_picture.at(i).y);
        fitCount.push_back(count909_picture.at(i).fit_y);
        residual.push_back(count909_picture.at(i).residual_rate);
    }
    slotUpdate_Count909_time(time, count, fitCount, residual);

    // 先默认绘制第一幅图
    {
        QVector<specStripData> allSpec = dealFile[detID-1]->GetStripData(0);
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
    }

    // 绘制多段能谱
    {
        QVector<ParseData::mergeSpecData> allSpec = dealFile[detID-1]->GetMergeSpec();
        std::vector<ParseData::mergeSpecData> allSpec_vec;
        for(auto data:allSpec)
        {
            allSpec_vec.push_back(data);
        }

        slotUpdateMultiSegmentPlotDatas(allSpec_vec);
    }

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
    std::vector<Sensor> sensors;
    for(auto yield:yields)
    {
        sensors.push_back({yield.lonDeg,yield.latDeg,yield.value});
    }
    w->setSensors(sensors);
    w->rebuild();
}
