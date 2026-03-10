/*
 * @Author: MrPan
 * @Date: 2026-01-14 09:21:28
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2026-03-09 10:02:07
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
#include <math.h>

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
    connect(this, SIGNAL(reporWriteLog(const QString&,QtMsgType)), this, SLOT(replyWriteLog(const QString&,QtMsgType)));

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

    //initCustomPlot(ui->spectorMeter, tr("时间/s"), tr("计数率/cps"));
    initCustomPlot(ui->spectorMeter, tr("道址"), tr("能量keV"));

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


void CountRateStatisticsWindow::initCustomPlot(QCustomPlot* customPlot, QString axisXLabel, QString axisYLabel)
{
    QCustomPlotHelper* customPlotHelper = new QCustomPlotHelper(customPlot, this);
    customPlot->setAntialiasedElements(QCP::aeAll);
    customPlot->legend->setVisible(false);
    customPlot->xAxis->setTickLabelRotation(-45);
    customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
    customPlot->xAxis->rescale(true);
    customPlot->yAxis->rescale(true);

    // 设置刻度范围
    customPlot->xAxis->setRange(0, 8192);
    customPlot->yAxis->setRange(0, 10000);
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

    // 添加散点图
    for (int i=0; i<2; ++i){
        QCPGraph * graph = customPlot->addGraph(customPlot->xAxis, customPlot->yAxis);
        graph->setName(i==0 ? "修正前" : "修正后");
        graph->setAntialiased(false);
        graph->setPen(QPen(i==0 ? Qt::red : Qt::blue));
        graph->setLineStyle(QCPGraph::lsLine);
        graph->setSelectable(QCP::SelectionType::stNone);
    }

    customPlotHelper->setGraphCheckBox(customPlot);
    customPlot->replot();
    connect(customPlot->xAxis, SIGNAL(rangeChanged(const QCPRange &)), customPlot->xAxis2, SLOT(setRange(const QCPRange &)));
    connect(customPlot->yAxis, SIGNAL(rangeChanged(const QCPRange &)), customPlot->yAxis2, SLOT(setRange(const QCPRange &)));
    connect(customPlot, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(slotShowTracer(QMouseEvent*)));
    connect(customPlot, SIGNAL(mouseRelease(QMouseEvent*)), this, SLOT(slotRestorePlot(QMouseEvent*)));
}


#include "H5Cpp.h"
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


void CountRateStatisticsWindow::on_action_startMeasure_triggered()
{
    emit reporWriteLog(tr("开始解析..."));

    ui->spectorMeter->graph(0)->data()->clear();
    ui->spectorMeter->graph(1)->data()->clear();
    ui->spectorMeter->replot(QCustomPlot::rpQueuedReplot);
    int index = 1;
    if (ui->tableWidget->selectedItems().count() > 0)
        index = ui->tableWidget->selectedItems()[0]->row() - 1;

    if (mMapSpectrum.contains(index))
    {
        QVector<double> spectrumTotal(8192, 0); // 8192道完整数据
        QVector<double> spectrumTotalAdjust(8192, 0); // 8192道完整数据
        QVector<double> keys;
        for (int i=0; i<8192; ++i)
        {
            keys << i;
            spectrumTotal[i] = mMapSpectrum[index][i];
            spectrumTotalAdjust[i] = mMapSpectrumAdjust[index][i];
        }

        ui->spectorMeter->graph(0)->setData(keys, spectrumTotal);
        ui->spectorMeter->graph(1)->setData(keys, spectrumTotalAdjust);
        ui->spectorMeter->replot(QCustomPlot::rpQueuedReplot);
        return;
    }

    qApp->setOverrideCursor(QCursor(Qt::WaitCursor));
    quint32 tmStart = ui->spinBox_timeStart->value();
    quint32 tmEnd = ui->spinBox_timeEnd->value();
    QString filePath = ui->textBrowser_filepath->toPlainText();

    // 1. 打开文件
    QTextCodec* gbk_codec = QTextCodec::codecForName("GBK");
    QByteArray filePathBytes = gbk_codec->fromUnicode(filePath);
    H5::H5File file(filePathBytes.toStdString(), H5F_ACC_RDONLY);

    //for (int index = 1; index <= DET_NUM; ++index)
    {
        // 2. 打开分组核数据集
        H5::Group group = file.openGroup(QString("Detector#%1").arg(index).toStdString().c_str());
        H5::DataSet dataset = group.openDataSet("Spectrum");

        // 3. 确认数据类型匹配（可选，用于错误检查）
        H5::CompType fileType = dataset.getCompType();
        if (fileType != H5::PredType::NATIVE_UINT)
        {
            file.close();
            qApp->restoreOverrideCursor();
            return;
        }

        // 4. 初始文件空间
        H5::DataSpace file_space = dataset.getSpace();
        hsize_t current_dims[2];
        file_space.getSimpleExtentDims(current_dims, NULL);
        hsize_t rows = current_dims[0];
        hsize_t cols = current_dims[1];
        if (rows<=0)
        {
            file.close();
            qApp->restoreOverrideCursor();
            return;
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
        quint32 measureTime = 0;
        double totalAdjust = 0;// 修正后
        QVector<double> spectrumTotal(8192, 0); // 8192道完整数据
        QVector<double> spectrumTotalAdjust(8192, 0); // 8192道完整数据

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
                QVector<quint32> spectrum(8192, 0);// 能谱/s
                quint32 deathTime = 0;// 死时间/s
                for (int t = 0; t < step; ++t)
                {
                    for (int j=0; j<8192; ++j)
                    {
                        spectrum[j] += data->spectrum[j];
                    }

                    deathTime += data->deathTime;
                }
                i += step;

                // 9. 能谱统计
                for (int j=0; j<8192; ++j)
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

            measureTime = data->measureTime;

            // 释放文件空间
            file_space.selectNone();
        }

        QVector<double> keys;
        for (int j=0; j<8192; ++j)
            keys << j;

        ui->spectorMeter->graph(0)->setData(keys, spectrumTotal);
        ui->spectorMeter->graph(1)->setData(keys, spectrumTotalAdjust);
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
        delete[] row_data;
        dataset.close();
        group.close();
    }

    file.close();
    ui->spectorMeter->rescaleAxes(true);
    ui->spectorMeter->replot(QCustomPlot::rpQueuedReplot);
    qApp->restoreOverrideCursor();

    emit reporWriteLog(tr("解析结束"));
}


void CountRateStatisticsWindow::on_action_stopMeasure_triggered()
{
    emit reporWriteLog(tr("中断解析"));
    mInterrupted = true;
}


void CountRateStatisticsWindow::on_tableWidget_cellClicked(int row, int column)
{
    if (row <= 1 || column != 0)
        return;

    int index = row - 1;
    if (mMapSpectrum.contains(index))
    {
        QVector<double> spectrumTotal(8192, 0); // 8192道完整数据
        QVector<double> spectrumTotalAdjust(8192, 0); // 8192道完整数据
        QVector<double> keys;
        for (int i=0; i<8192; ++i)
        {
            keys << i;
            spectrumTotal[i] = mMapSpectrum[index][i];
            spectrumTotalAdjust[i] = mMapSpectrumAdjust[index][i];
        }

        ui->spectorMeter->graph(0)->setData(keys, spectrumTotal);
        ui->spectorMeter->graph(1)->setData(keys, spectrumTotalAdjust);
        ui->spectorMeter->replot(QCustomPlot::rpQueuedReplot);
    }
}

