#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "switchbutton.h"
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include "energycalibration.h"
#include "qcustomplothelper.h"

MainWindow::MainWindow(bool isDarkTheme, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , mIsDarkTheme(isDarkTheme)
    , mainWindow(static_cast<QGoodWindowHelper *>(parent))
{
    ui->setupUi(this);
    setWindowTitle(QApplication::applicationName()+" - "+APP_VERSION);

    commHelper = CommHelper::instance();

    // 测量计时器
    mMeasureTimer = new QElapsedTimer();
    // 自动化测量计时器
    mAutoMeasureDelayTimer = new QTimer(this);
    mAutoMeasureDelayTimer->setInterval(1000); // 每秒触发一次
    connect(mAutoMeasureDelayTimer, &QTimer::timeout, this, &MainWindow::onAutoMeasureDelayTimeout);

    // 初始化连接按钮禁用定时器
    mConnectButtonDisableTimer = new QTimer(this);
    mConnectButtonDisableTimer->setSingleShot(true);
    mConnectButtonDisableTimer->setInterval(10000); // 10秒
    connect(mConnectButtonDisableTimer, &QTimer::timeout, this, [=](){
        // 定时器超时后，根据连接状态决定是否启用按钮
        if (!mSwitcherConnected) {
            qInfo().noquote().nospace() << tr("交换机连接失败");
            ui->action_connectSwitch->setEnabled(true);
        }
    });

    initUi();
    initNet();

    restoreSettings();
    applyColorTheme();

    connect(this, &MainWindow::sigWriteLog, this, &MainWindow::slotWriteLog);

    ui->groupBox_3->setVisible(false); //隐藏统计结果，暂时没有在线中子产额统计结果
    ui->action_startServer->setEnabled(true);
    ui->action_stopServer->setEnabled(false);
    ui->action_connectSwitch->setEnabled(false);
    ui->action_powerOn->setEnabled(false);
    ui->action_powerOff->setEnabled(false);
    ui->action_disconnect->setEnabled(false);
    ui->action_startMeasure->setEnabled(false);
    ui->action_stopMeasure->setEnabled(false);
    ui->pushButton_startMeasure->setEnabled(false);
    ui->pushButton_stopMeasure->setEnabled(false);

    // 初始化连接按钮状态（未连接状态）
    updateConnectButtonState(false);

    // 初始化查找功能
    connect(ui->lineEdit_search, &QLineEdit::returnPressed, this, &MainWindow::on_lineEdit_search_returnPressed);
    connect(ui->lineEdit_search, &QLineEdit::textChanged, this, &MainWindow::on_lineEdit_search_textChanged);

    // 交换机连接状态更新(连接后必须登录才能使用)
    connect(commHelper, &CommHelper::switcherConnected, this, [=](QString ip){
        mSwitcherConnected = true;
        qDebug().nospace().noquote() << tr("交换机[%1]已连通").arg(ip);
    }, Qt::QueuedConnection);

    // 交换机登录状态更新
    connect(commHelper, &CommHelper::switcherLogged, this, &MainWindow::onSwitcherLogged);

    // 交换机断开连接
    connect(commHelper, &CommHelper::switcherDisconnected, this, [=](QString ip){
        mSwitcherConnected = false;
        updateConnectButtonState(false);
        ui->action_powerOn->setEnabled(false);
        ui->action_powerOff->setEnabled(false);
        qCritical().nospace().noquote() << tr("交换机[%1]连接已断开").arg(ip);
    }, Qt::QueuedConnection);

    // 交换机上POE电源状态更新
    connect(commHelper, &CommHelper::reportPoePowerStatus, this, &MainWindow::onPoePowerStatusReport, Qt::QueuedConnection);

    // 探测器上线
    connect(commHelper, &CommHelper::detectorOnline, this, &MainWindow::onDetectorOnline, Qt::QueuedConnection);

    // 探测器下线
    connect(commHelper, &CommHelper::detectorOffline, this, &MainWindow::onDetectorOffline, Qt::QueuedConnection);

    // 开始指令下发，测量正式开始
    connect(commHelper, &CommHelper::measureStart, this, [=](quint8 index){
        int row = index;
        QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 2));
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::green));

        // 重置该探测器的能谱数据
        resetDetectorSpectrum(index);

        // 记录探测器正在测量
        mDetectorMeasuring[index] = true;

        qInfo().nospace() << "谱仪[#" << index << "]测量已启动，准备接收数据";//开始实验，准备接收数据
    }, Qt::QueuedConnection);

    // 测量停止指令下发，测量结束
    connect(commHelper, &CommHelper::measureStop, this, [=](quint8 index){
        int row = index;
        QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 2));
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::red));

        qInfo().nospace() << "谱仪[#" << index << "]测量已停止";
    }, Qt::QueuedConnection);

    connect(ui->statusbar, &QStatusBar::messageChanged,this,[&](const QString &message){
        if(message.isEmpty()) {
            ui->statusbar->showMessage(tr("准备就绪"));
        } else {
            ui->statusbar->showMessage(message);
        }
    }, Qt::QueuedConnection);

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

MainWindow::~MainWindow()
{
    GlobalSettings settingsSystem;

    // 保存界面设置
    settingsSystem.setValue("mainWindow/TriggerMode", ui->com_triggerModel->currentIndex());
    settingsSystem.setValue("mainWindow/ShotNumIsAutoIncrease", ui->checkBox_autoIncrease->isChecked());
    settingsSystem.setValue("mainWindow/ShotNumStr", ui->lineEdit_shotNum->text());
    settingsSystem.setValue("mainWindow/SavePath", ui->lineEdit_filePath->text());
    settingsSystem.setValue("mainWindow/MeasureTime", ui->spinBox_measureTime->value());
    settingsSystem.setValue("mainWindow/MeasureMode", ui->cbb_measureMode->currentIndex());
    QSplitter *splitterH1 = this->findChild<QSplitter*>("splitterH1");// QSplitter(Qt::Horizontal,this);
    if (splitterH1){
        settingsSystem.setValue("Global/splitterH1/State", splitterH1->saveState());
        settingsSystem.setValue("Global/splitterH1/Geometry", splitterH1->saveGeometry());
    }

    QSplitter *splitterV2 = this->findChild<QSplitter*>("splitterV2");
    if (splitterV2){
        settingsSystem.setValue("Global/splitterV2/State", splitterV2->saveState());
        settingsSystem.setValue("Global/splitterV2/Geometry", splitterV2->saveGeometry());
    }

    settingsSystem.setValue("Global/MainWindows/State", this->saveState());
    delete ui;
}

void MainWindow::initUi()
{
    // 自动测量
    ui->dateTime_shotTime->setDateTime(QDateTime::currentDateTime());
    ui->dateTimeEdit_startTime->setDateTime(QDateTime::currentDateTime().addSecs(180));

    // 谱仪最大化按钮
    auto titleClicked = ([=](){
        QToolButton* btn = qobject_cast<QToolButton*>(sender());
        QString name = btn->objectName();
        int index = name.right(1).toInt();
        QWidget* spectroMeterWidget = this->findChild<QWidget*>(QString("spectroMeterWidget%1").arg(index));
        if (spectroMeterWidget->property("isZoomIn").toBool())
        {
            // 2行显示
            QGridLayout* layoutLeft = (QGridLayout*)ui->widget_left->layout();
            QGridLayout* layoutRight = (QGridLayout*)ui->widget_right->layout();

            for (int index=1; index<=DET_NUM; ++index)
            {
                QCheckBox* checkBox = findChild<QCheckBox*>(QLatin1String("CH ")+QString::number(index));
                if (index<=(DET_NUM/2))
                {
                    layoutLeft->addWidget(checkBox, index<=(DET_NUM/4) ? 0 : 1, (index-1) % (DET_NUM/4));
                }
                else
                {
                    layoutRight->addWidget(checkBox, index<=(DET_NUM*3/4) ? 0 : 1, (index-1) % (DET_NUM/4));
                }
            }

            layoutLeft->addWidget(ui->toolButton_title_1, 0, DET_NUM/2);
            layoutRight->addWidget(ui->toolButton_title_2, 0, DET_NUM/2);

            this->setProperty("isZoomIn", false);
            spectroMeterWidget->setProperty("isZoomIn", false);
            btn->setIcon(QIcon(":/zoom_in.png"));
            for (int i=1; i<=2; ++i)
            {
                spectroMeterWidget = this->findChild<QWidget*>(QString("spectroMeterWidget%1").arg(i));
                spectroMeterWidget->show();
            }

            ui->widget_left->setFixedHeight(50);
            ui->widget_right->setFixedHeight(50);
        }
        else
        {
            // 单行显示
            QGridLayout* layoutLeft = (QGridLayout*)ui->widget_left->layout();
            QGridLayout* layoutRight = (QGridLayout*)ui->widget_right->layout();

            for (int index=1; index<=DET_NUM; ++index)
            {
                QCheckBox* checkBox = findChild<QCheckBox*>(QLatin1String("CH ")+QString::number(index));
                if (index<=DET_NUM/2)
                {
                    layoutLeft->addWidget(checkBox, 0, (index-1) % (DET_NUM/2));
                }
                else
                {
                    layoutRight->addWidget(checkBox, 0, (index-1) % (DET_NUM/2));
                }
            }

            layoutLeft->addWidget(ui->toolButton_title_1, 0, DET_NUM/2);
            layoutRight->addWidget(ui->toolButton_title_2, 0, DET_NUM/2);

            this->setProperty("isZoomIn", true);
            this->setProperty("currentMeterIndex", index);
            spectroMeterWidget->setProperty("isZoomIn", true);
            btn->setIcon(QIcon(":/zoom_out.png"));
            for (int i=1; i<=2; ++i)
            {
                spectroMeterWidget = this->findChild<QWidget*>(QString("spectroMeterWidget%1").arg(i));
                if (i==index)
                    spectroMeterWidget->show();
                else
                    spectroMeterWidget->hide();
            }

            ui->widget_left->setFixedHeight(27);
            ui->widget_right->setFixedHeight(27);
        }
    });
    connect(ui->toolButton_title_1, &QToolButton::clicked, this, titleClicked);
    connect(ui->toolButton_title_2, &QToolButton::clicked, this, titleClicked);
    ui->leftStackedWidget->hide();

    // 隐藏页面【历史数据、数据分析、数据管理】
    ui->centralHboxTabWidget->setTabVisible(1, false);
    ui->centralHboxTabWidget->setTabVisible(2, false);
    ui->centralHboxTabWidget->setTabVisible(3, false);

    //加载界面参数
    {
        GlobalSettings deviceSettings(DEVICE_CONFIG_FILE);
        HDF5Settings::instance()->setPluseTime(deviceSettings.value("Global/PluseTime", 10).toUInt());
        GlobalSettings systemSettings(SYSTEM_CONFIG_FILE);
        systemSettings.beginGroup("mainWindow");

        // 测量模式
        int measureMode = systemSettings.value("MeasureMode", 0).toInt();
        ui->cbb_measureMode->setCurrentIndex(measureMode);
        onMeasureModeChanged(measureMode);

        // 测量发次号是否自动增加
        bool flag = systemSettings.value("ShotNumIsAutoIncrease", true).toBool();
        ui->checkBox_autoIncrease->setChecked(flag);

        // 发次号
        ui->lineEdit_shotNum->setText(systemSettings.value("ShotNumStr", "000").toString());

        // 数据保存路径
        ui->lineEdit_filePath->setText(systemSettings.value("SavePath", "./cache").toString());
        if (!QDir(ui->lineEdit_filePath->text()).exists())
            ui->lineEdit_filePath->setText("./cache");
        ui->spinBox_measureTime->setValue(systemSettings.value("MeasureTime", 10).toInt());

        // 触发模式
        int triggerMode = systemSettings.value("TriggerMode", 0).toInt();
        ui->com_triggerModel->setCurrentIndex(triggerMode); // 或者从配置文件中读取
        onTriggerModelChanged(triggerMode);
        systemSettings.endGroup();
    }

    connect(ui->cbb_measureMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onMeasureModeChanged);

    connect(ui->com_triggerModel, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onTriggerModelChanged);

    // 创建上线客户端页面
    mClientPeersWindow = new ClientPeersWindow();
    // mClientPeersWindow->setAttribute(Qt::WA_TranslucentBackground);
    mClientPeersWindow->setWindowFlags(Qt::Widget | Qt::WindowOverridesSystemGestures);
    mClientPeersWindow->hide();
    connect(commHelper, &CommHelper::connectPeerConnection, mClientPeersWindow, &ClientPeersWindow::connectPeerConnection);
    connect(commHelper, &CommHelper::disconnectPeerConnection, mClientPeersWindow, &ClientPeersWindow::disconnectPeerConnection);

    mOTAUpgradeWindow = new OTAUpgradeWindow();
    mOTAUpgradeWindow->setWindowFlags(Qt::Widget | Qt::WindowOverridesSystemGestures);
    mOTAUpgradeWindow->hide();

    mDetSettingWindow = new DetSettingWindow();
    mDetSettingWindow->setWindowFlags(Qt::Widget | Qt::WindowOverridesSystemGestures);
    mDetSettingWindow->hide();
    connect(commHelper, &CommHelper::connectPeerConnection, mDetSettingWindow, &DetSettingWindow::connectPeerConnection);
    connect(commHelper, &CommHelper::disconnectPeerConnection, mDetSettingWindow, &DetSettingWindow::disconnectPeerConnection);
    connect(mDetSettingWindow, &DetSettingWindow::settingfinished, commHelper, &CommHelper::settingfinished);
    connect(mDetSettingWindow, &DetSettingWindow::settingfinished, this, [this]{
        updateSpectrumPlotSettings();
    });

    connect(ui->centralHboxTabWidget,&QTabWidget::tabCloseRequested,this,[=](int index){
        if (index != 0){
            ui->centralHboxTabWidget->setTabVisible(index, false);
            QList<QAction*> actions;
            actions << ui->action_histroyData << ui->action_analyzeData << ui->action_managerData;
            actions[index-1]->setChecked(false);
        }
    });

    /*设置关闭按钮图标尺寸，QSS设置无效，所以只能在这里修改*/
    for (int i=0; i< ui->centralHboxTabWidget->count(); ++i)
    {
        ui->centralHboxTabWidget->tabBar()->tabButton(i, QTabBar::RightSide)->setFixedSize(QSize(12,12));
    }

    QActionGroup *themeActionGroup = new QActionGroup(this);
    ui->action_lightTheme->setActionGroup(themeActionGroup);
    ui->action_darkTheme->setActionGroup(themeActionGroup);
    ui->action_lightTheme->setChecked(!mIsDarkTheme);
    ui->action_darkTheme->setChecked(mIsDarkTheme);

    // 任务栏信息
    QLabel *label_Idle = new QLabel(ui->statusbar);
    label_Idle->setObjectName("label_Idle");
    label_Idle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    label_Idle->setFixedWidth(300);
    label_Idle->setText(tr("准备就绪"));
    connect(ui->statusbar,&QStatusBar::messageChanged,this,[&](const QString &message){
        label_Idle->setText(message);
    });

    // 设置任务栏信息 - 本地网络服务状态
    QLabel *label_LocalServer = new QLabel(ui->statusbar);
    label_LocalServer->setObjectName("label_LocalServer");
    label_LocalServer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    label_LocalServer->setFixedWidth(300);
    label_LocalServer->setText(tr("本地网络服务：未开启"));
    label_LocalServer->setCursor(Qt::PointingHandCursor);
    label_LocalServer->installEventFilter(this);

    // 设置任务栏信息 - 连接状态
    QLabel *label_Connected = new QLabel(ui->statusbar);
    label_Connected->setObjectName("label_Connected");
    label_Connected->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    label_Connected->setFixedWidth(300);
    //label_Connected->setText(tr("交换机网络状态：未开启"));
    label_Connected->installEventFilter(this);

    // 设置任务栏查询状态
    QLabel *label_Query = new QLabel(ui->statusbar);
    label_Query->setObjectName("label_Query");
    label_Query->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    label_Query->setFixedWidth(300);
    label_Query->setText(tr("查询状态：无"));

    // 设置任务栏信息 - 系统时间
    QLabel *label_systemtime = new QLabel(ui->statusbar);
    label_systemtime->setObjectName("label_systemtime");
    label_systemtime->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    ui->statusbar->setContentsMargins(5, 0, 5, 0);
    ui->statusbar->addWidget(label_Idle);
    ui->statusbar->addWidget(label_LocalServer);
    ui->statusbar->addWidget(label_Connected);
    ui->statusbar->addWidget(new QLabel(ui->statusbar), 1);
    ui->statusbar->addWidget(nullptr, 1);
    ui->statusbar->addPermanentWidget(label_Query);
    ui->statusbar->addPermanentWidget(label_systemtime);

    QTimer* systemClockTimer = new QTimer(this);
    systemClockTimer->setObjectName("systemClockTimer");
    connect(systemClockTimer, &QTimer::timeout, this, &MainWindow::onSystemTimer);
    systemClockTimer->start(900);

    {
        ui->widget__plot->setLayout(new QVBoxLayout(ui->widget__plot));

        QSplitter *splitterH1 = new QSplitter(Qt::Horizontal,this);
        splitterH1->setHandleWidth(1);
        splitterH1->addWidget(ui->spectroMeterWidget1);
        splitterH1->addWidget(ui->spectroMeterWidget2);
        splitterH1->setSizes(QList<int>() << 100000 << 100000);
        splitterH1->setCollapsible(0,false);
        splitterH1->setCollapsible(1,false);

        ui->widget__plot->layout()->addWidget(splitterH1);
    }

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
    }

    // 右侧栏
    QPushButton* labPrametersButton = nullptr;
    {
        labPrametersButton = new QPushButton();
        labPrametersButton->setText(tr("实验参数"));
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

    ui->tableWidget_detector->verticalHeader()->setDefaultSectionSize(25); // 设置默认行高
    ui->tableWidget_detector->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed); // 固定高度
    ui->tableWidget_detector->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget_detector->horizontalHeader()->setFixedHeight(25);
    ui->tableWidget_detector->setRowCount(DET_NUM+1);
    for (int row = 0; row <= DET_NUM; ++row){
        QTableWidgetItem *__qtablewidgetitem = new QTableWidgetItem(QStringLiteral("谱仪#%1").arg(row));
        ui->tableWidget_detector->setVerticalHeaderItem(row, __qtablewidgetitem);
        ui->tableWidget_detector->setRowHeight(row, 25);
    }
    ui->tableWidget_detector->setRowHeight(0, 50);
    ui->tableWidget_detector->verticalHeaderItem(0)->setText("时间同步\n触发模块");
    ui->tableWidget_detector->setMinimumHeight(DET_NUM*25+2);

    for (int row=0; row<ui->tableWidget_detector->rowCount(); ++row){
        for (int column=0; column<=3; ++column){
            if (column == 0){
                SwitchButton* cell = new SwitchButton();
                cell->setObjectName(QStringLiteral("poeSwitcher%1").arg(column));
                if (row == 0)
                    cell->setContentsMargins(5, 12, 5, 12);
                else
                    cell->setContentsMargins(5, 2, 5, 0);
                cell->setAutoChecked(false);
                ui->tableWidget_detector->setCellWidget(row, column, cell);
                connect(cell, &SwitchButton::clicked, this, [=](){
                    if (row == 0)
                    {
                        // 时间同步模块
                        if (!cell->getChecked())
                        {
                            commHelper->openSwitcherExtendPOEPower();
                            qInfo().nospace() << "时间同步触发模块手动打开POE供电";
                        }
                        else
                        {
                            commHelper->closeSwitcherExtendPOEPower();
                            qInfo().nospace() << "时间同步触发模块手动关闭POE供电";
                        }
                    }
                    else
                    {
                        if (!cell->getChecked()){
                            if (commHelper->openSwitcherPOEPower(row))
                            {
                                commHelper->manualOpenSwitcherPOEPower(row);
                                //cell->setChecked(true);
                                //打印日志
                                qInfo().nospace() << "谱仪[#" << row << "]手动打开POE供电";
                            }
                        }
                        else{
                            if (commHelper->closeSwitcherPOEPower(row))
                            {
                                commHelper->manualCloseSwitcherPOEPower(row);
                                //cell->setChecked(false);
                                //打印日志
                                qInfo().nospace() << "谱仪[#" << row << "]手动关闭POE供电";
                            }
                        }
                    }
                });
            }
            else {
                QLabel* cell = new QLabel();
                if (column == 3)
                    cell->setText("-℃");
                else
                    cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::red));
                cell->setAlignment(Qt::AlignCenter);
                ui->tableWidget_detector->setCellWidget(row, column, cell);
            }
        }
    }

    QAction *action = ui->lineEdit_filePath->addAction(QIcon(":/open.png"), QLineEdit::TrailingPosition);
    QToolButton* button = qobject_cast<QToolButton*>(action->associatedWidgets().last());
    button->setCursor(QCursor(Qt::PointingHandCursor));
    connect(button, &QToolButton::pressed, this, [=](){
        QString cacheDir = QFileDialog::getExistingDirectory(this);
        if (!cacheDir.isEmpty()){
            GlobalSettings settings(SYSTEM_CONFIG_FILE);
            settings.setValue("mainWindow/CacheDir", cacheDir);
            ui->lineEdit_filePath->setText(cacheDir);
        }
    });

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

    auto getRandomColor = []() {
        static std::mt19937 engine(std::chrono::system_clock::now().time_since_epoch().count());
        std::uniform_int_distribution<int> distR(0, 255); // 红色通道高值
        std::uniform_int_distribution<int> distG(0, 255);  // 绿色通道中值
        std::uniform_int_distribution<int> distB(0, 255);   // 蓝色通道低值
        return QColor(distR(engine), distG(engine), distB(engine));
    };
    mGraphisColor.push_back(QColor::fromRgb(0,47,167));
    mGraphisColor.push_back(QColor::fromRgb(255,0,77));
    mGraphisColor.push_back(QColor::fromRgb(255,204,51));
    mGraphisColor.push_back(QColor::fromRgb(10,186,181));
    mGraphisColor.push_back(QColor::fromRgb(5,75,58));
    mGraphisColor.push_back(QColor::fromRgb(129,216,208));
    mGraphisColor.push_back(QColor::fromRgb(0,140,141));
    mGraphisColor.push_back(QColor::fromRgb(128,0,32));
    mGraphisColor.push_back(QColor::fromRgb(142,76,41));
    mGraphisColor.push_back(QColor::fromRgb(205,0,0));
    mGraphisColor.push_back(QColor::fromRgb(0,102,128));
    mGraphisColor.push_back(QColor::fromRgb(32,88,39));
    mGraphisColor.push_back(QColor::fromRgb(132,88,39));
    mGraphisColor.push_back(QColor::fromRgb(32,188,39));
    mGraphisColor.push_back(QColor::fromRgb(32,88,139));
    mGraphisColor.push_back(QColor::fromRgb(32,188,139));

    for (int i=1; i<=2; ++i){
        QCustomPlot *spectroMeter_top = this->findChild<QCustomPlot*>(QString("spectroMeter%1_top").arg(i));
        initCustomPlot(i, spectroMeter_top, tr(""), tr("时间/s 计数率/cps"), tr("计数曲线"), DET_NUM/2);
        QCustomPlot *spectroMeter_bottom = this->findChild<QCustomPlot*>(QString("spectroMeter%1_bottom").arg(i));
        initCustomPlot(i, spectroMeter_bottom, tr(""), tr("道址 计数"), tr("累积能谱"), DET_NUM/2);
    }

    QCustomPlot *spectroMeter_left = this->findChild<QCustomPlot*>(QString("spectroMeter_left"));
    initCustomPlot(100, spectroMeter_left, tr(""), tr("时间/ns ADC采样值"), tr("实时波形"), 1);
    QCustomPlot *spectroMeter_right = this->findChild<QCustomPlot*>(QString("spectroMeter_right"));
    initCustomPlot(101, spectroMeter_right, tr(""), tr("道址 计数"), tr("累积能谱"), 1);

    {
        // 添加可选项
        QGridLayout* layoutLeft = new QGridLayout(ui->widget_left);
        layoutLeft->setSpacing(5);
        layoutLeft->setContentsMargins(9,3,9,3);
        QGridLayout* layoutRight = new QGridLayout(ui->widget_right);
        layoutRight->setSpacing(5);
        layoutRight->setContentsMargins(9,3,9,3);
        ui->widget_left->setLayout(layoutLeft);
        ui->widget_right->setLayout(layoutRight);

        for (int index=1; index<=DET_NUM; ++index)
        {
            QCheckBox* checkBox = new QCheckBox(index==1 ? ui->widget_left : ui->widget_right);
            checkBox->setText(QLatin1String("")+QString::number(index));
            checkBox->setObjectName(QLatin1String("CH ")+QString::number(index));
            checkBox->setIcon(QIcon(roundPixmap(QSize(16, 16), mGraphisColor.at((index-1) % (DET_NUM/2)))));
            checkBox->setProperty("index", index);
            checkBox->setChecked(true);
            connect(checkBox, &QCheckBox::stateChanged, this, [=](int state){
                int index = checkBox->property("index").toInt();

                // 能谱
                QCPGraph *graph = getGraph(index);
                if (graph){
                    graph->setVisible(Qt::CheckState::Checked == state ? true : false);
                    getCustomPlot(index)->replot();
                }

                // 计数率
                graph = getGraph(index, false);
                if (graph){
                    graph->setVisible(Qt::CheckState::Checked == state ? true : false);
                    getCustomPlot(index, false)->replot();
                }
            });

            if (index<=(DET_NUM/2))
            {
                layoutLeft->addWidget(checkBox, index<=(DET_NUM/4) ? 0 : 1, (index-1) % (DET_NUM/4));
            }
            else
            {
                layoutRight->addWidget(checkBox, index<=(DET_NUM*3/4) ? 0 : 1, (index-1) % (DET_NUM/4));
            }
        }

        layoutLeft->addWidget(ui->toolButton_title_1, 0, (DET_NUM/2)+1);
        layoutRight->addWidget(ui->toolButton_title_2, 0, (DET_NUM/2)+1);
    }

    //更新温度状态
    connect(commHelper, &CommHelper::reportDetectorTemperature, this, [=](quint8 index, float temperature){
        if (index < 0 || index > DET_NUM) //注意，index=0对应时间触发模块
            return;

        // 仅缓存数据：回调里不做文件 IO，由QTimer每10秒统一写盘
        QDateTime now = QDateTime::currentDateTime();
        m_mapTemperatur[index] = qMakePair(now.toMSecsSinceEpoch(), temperature);

        const QString ymd = now.toString("yyyy-MM-dd");
        const QString dtStr = now.toString("yyyy-MM-dd HH:mm:ss"); // 精确到秒
        TempPoint p{dtStr, temperature};

        QMutexLocker flushLocker(&mMutexTemperature);
        m_tempPointCache[index][ymd].push_back(p);
    }, Qt::DirectConnection);

    connect(commHelper, &CommHelper::reportTemperatureTimeout, this, [=](quint8 index){
        QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(index, 3));
        cell->setStyleSheet("color: gray;");//灰色字体
        mTemperatureTimeoutDetectors.append(index);
    }, Qt::QueuedConnection);

    // 接收完整的能谱数据
    connect(commHelper, &CommHelper::reportFullSpectrum, this, &MainWindow::onFullSpectrum, Qt::DirectConnection);

    // 接收波形曲线数据
    connect(commHelper, &CommHelper::reportWaveCurve, this, [=](quint8 index, QVector<quint16>& data){
        return; // 不显示波形
        QCustomPlot *customPlot = getCustomPlot(index);
        if (!customPlot)
            return;

        QVector<double> x, y;
        for(int i=0; i<data.size(); ++i)
        {
            x << i*10;
            y << data.at(i);
        }


        getGraph(index)->setData(x, y);
        customPlot->xAxis->rescale(true);
        customPlot->yAxis->rescale(true);
    });

    // 接收触发信号时间戳
    connect(commHelper, &CommHelper::reportTimestampe, this, [=](QDateTime& tm){
        // 只有启用了外触发才会接收打靶信号的时间戳，并且只在自动测量未激活时更新界面显示
        if (mEnableExtendTrigger && !mAutoMeasureTimerActive)
        {
            // 只有开启了自动化测量，才接收自动触发的时间戳，并更新界面显示
            ui->dateTime_shotTime->setDateTime(tm);
            ui->lineEdit_triggerflag->setText("已经触发");

            // 根据冷却时间设置自动测量的触发时间，单位为秒
            qint32 coolTimelength = ui->spinBox_coolTimeLength->value();
            ui->dateTimeEdit_startTime->setDateTime(tm.addSecs(coolTimelength));

            mAutoMeasureTimerActive = true;
            qInfo().noquote().nospace() << "收到打靶信号时间戳：" << tm.toString("yyyy-MM-dd HH:mm:ss") << "，自动测量将在" << coolTimelength << "秒后触发";
        }
    }, Qt::QueuedConnection);
}

QPixmap MainWindow::roundPixmap(QSize sz, QColor clrOut)
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

QPixmap MainWindow::dblroundPixmap(QSize sz, QColor clrIn, QColor clrOut)
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

void MainWindow::initNet()
{
    connect(commHelper, &CommHelper::connectPeerConnection, this, [=](QString peerAddress, quint16 peerPort){

    });
}

void MainWindow::initCustomPlot(int index, QCustomPlot* customPlot, QString axisXLabel, QString axisYLabel,
        QString str_title, int graphCount/* = 1*/)
{
    QCustomPlotHelper* customPlotHelper = new QCustomPlotHelper(customPlot, this);
    customPlot->setProperty("index", index);

    // 创建标题文本元素
    QCPItemText *textLabel = new QCPItemText(customPlot);
    textLabel->setObjectName("title");
    textLabel->setPositionAlignment(Qt::AlignTop|Qt::AlignHCenter);
    textLabel->position->setType(QCPItemPosition::ptAxisRectRatio);
    textLabel->position->setCoords(0.5, 0); // place position at center/top of axis rect
    textLabel->setText(str_title);
    if (mIsDarkTheme)
        textLabel->setColor(Qt::white);
    else
        textLabel->setColor(Qt::black);

    // 设置背景网格线是否显示
    //customPlot->xAxis->grid()->setVisible(true);
    //customPlot->yAxis->grid()->setVisible(true);
    // 设置背景网格线条颜色
    //customPlot->xAxis->grid()->setPen(QPen(palette.color(QPalette::WindowText),1,Qt::PenStyle::SolidLine));  // 垂直网格线条属性
    //customPlot->yAxis->grid()->setPen(QPen(palette.color(QPalette::WindowText),1,Qt::PenStyle::SolidLine)); // 水平网格线条属性
    //customPlot->xAxis->grid()->setSubGridPen(QPen(palette.color(QPalette::WindowText),1,Qt::PenStyle::DotLine));
    //customPlot->yAxis->grid()->setSubGridPen(QPen(palette.color(QPalette::WindowText),1,Qt::PenStyle::SolidLine));

    // 设置全局抗锯齿
    customPlot->setAntialiasedElements(QCP::aeAll);
    // 图例名称隐藏
    customPlot->legend->setVisible(false);
    // customPlot->legend->setFillOrder(QCPLayoutGrid::foRowsFirst);//设置图例在一列中显示
    // customPlot->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop|Qt::AlignRight);// 图例名称显示位置
    // customPlot->legend->setBrush(Qt::NoBrush);//设置背景颜色
    // customPlot->legend->setBorderPen(Qt::NoPen);//设置边框颜色

    // 设置边界
    //customPlot->setContentsMargins(0, 0, 0, 0);
    // 设置标签倾斜角度，避免显示不下
    customPlot->xAxis->setTickLabelRotation(-45);
    // 允许拖拽，缩放
    customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
    // 允许轴自适应大小
    customPlot->xAxis->rescale(true);
    customPlot->yAxis->rescale(false);

    // 读取探测器参数 ，能谱长度
    HDF5Settings *settings = HDF5Settings::instance();
    QMap<quint8, DetParameter>& detParameters = settings->detParameters();
    DetParameter& detParameter = detParameters[1]; //默认读取探测器1的参数
    int multiCh = detParameter.spectrumLength;

    // 设置刻度范围
    customPlot->xAxis->setRange(0, multiCh);
    customPlot->yAxis->setRange(0, 10000);
    customPlot->yAxis->ticker()->setTickCount(5);
    customPlot->xAxis->ticker()->setTickCount(graphCount == 1 ? 10 : 5);

    customPlot->yAxis2->ticker()->setTickCount(5);
    customPlot->xAxis2->ticker()->setTickCount(graphCount == 1 ? 10 : 5);

    //设置轴标签名称
    customPlot->xAxis->setLabel(axisXLabel);
    customPlot->yAxis->setLabel(axisYLabel);

    customPlot->xAxis2->setTicks(false);
    customPlot->xAxis2->setSubTicks(false);
    customPlot->yAxis2->setTicks(false);
    customPlot->yAxis2->setSubTicks(false);

    // 添加散点图
    for (int i=0; i<graphCount; ++i){
        QCPGraph * graph = customPlot->addGraph(customPlot->xAxis, customPlot->yAxis);
        //graph->setName("");
        graph->setAntialiased(false);
        graph->setPen(mGraphisColor.at(i % (DET_NUM/2)));
        //graph->selectionDecorator()->setPen(QPen(getRandomColor()));
        graph->setLineStyle(QCPGraph::lsLine);
        graph->setSelectable(QCP::SelectionType::stNone);
        //graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, colors[i], 2));//显示散点图
        //graph->setSmooth(true);
    }

    customPlot->replot();
    connect(customPlot->xAxis, SIGNAL(rangeChanged(const QCPRange &)), customPlot->xAxis2, SLOT(setRange(const QCPRange &)));
    connect(customPlot->yAxis, SIGNAL(rangeChanged(const QCPRange &)), customPlot->yAxis2, SLOT(setRange(const QCPRange &)));


    // 是否允许X轴自适应缩放
    connect(customPlot, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(slotShowTracer(QMouseEvent*)));
    connect(customPlot, SIGNAL(mouseRelease(QMouseEvent*)), this, SLOT(slotRestorePlot(QMouseEvent*)));
    //connect(customPlot, SIGNAL(mouseWheel(QWheelEvent*)), this, SLOT(slotRestorePlot(QWheelEvent*)));
    //connect(customPlot, SIGNAL(mouseMove(QMouseEvent*)), this,SLOT(slotShowTracer(QMouseEvent*)));
}

//清除全部图像数据（计数曲线、能谱曲线）
void MainWindow::clearCustomPlotData()
{
    for (int i = 0; i < DET_NUM; ++i)
    {
        // 计数率图
        {
            QCustomPlot* customPlot = getCustomPlot(i, false);

            if (customPlot)
            {
                for (int g = 0; g < customPlot->graphCount(); ++g)
                {
                    customPlot->graph(g)->data()->clear();  // 只清数据
                }
            }
        }

        // 能谱图
        QCustomPlot* customPlot = getCustomPlot(i, true);
        if (customPlot)
        {
            for (int g = 0; g < customPlot->graphCount(); ++g)
            {
                customPlot->graph(g)->data()->clear();  // 只清数据
            }
        }
    }
}

// 自动测量，定时器检测是否接近测量起始时刻。在起始时刻提前30s做好准备工作：
void MainWindow::onAutoMeasureDelayTimeout()
{
    if (!mAutoMeasureTimerActive) return;

    QDateTime now = QDateTime::currentDateTime();
    if (ui->cbb_measureMode->currentIndex() == 0){
        // 手动测量模式
        QDateTime tmStart = ui->dateTimeEdit_startTime->dateTime();
        if (now >= tmStart)
        {
            ui->lineEdit_triggerflag->setText("已经触发");
            mAutoMeasureDelayTimer->stop();

            // 直接开始测量
            this->startMeasure();
        }
    }
    else{
        // 自动化测量模式
        QDateTime tmStart = ui->dateTimeEdit_startTime->dateTime().addSecs(-180);// 提前180秒做好准备工作
        if (now >= tmStart)
        {
            ui->lineEdit_triggerflag->setText("已经触发");

            mAutoMeasureDelayTimer->stop();

            // 第一步 先启动服务
            if (!commHelper->isOpen())
            {
                emit ui->action_startServer->trigger();
            }

            // 第二步 先连接交换机，开启电源
            commHelper->connectSwitcher(false);
        }
    }
}

// 系统刷新定时器，暂定于每秒钟刷新1次，刷新内容包括：测量时间、能谱图像、计数率曲线等
void MainWindow::onSystemTimer()
{
    static uint64_t t = 0;

    // 获取当前时间
    QDateTime currentDateTime = QDateTime::currentDateTime();
    if (!mPendingLogs.isEmpty()) {
        const qint64 currentSec = currentDateTime.toMSecsSinceEpoch() / 1000;
        if (mLastLogFlushSecond < 0)
            mLastLogFlushSecond = currentSec;
        if (currentSec != mLastLogFlushSecond) {
            flushPendingLogs();
            mLastLogFlushSecond = currentSec;
        }
    }

    // 获取星期几的数字（1代表星期日，7代表星期日）
    int dayOfWeekNumber = currentDateTime.date().dayOfWeek();

    // 星期几的中文名称列表
    QStringList dayNames = {
        tr("星期日"), QObject::tr("星期一"), QObject::tr("星期二"), QObject::tr("星期三"), QObject::tr("星期四"), QObject::tr("星期五"), QObject::tr("星期六"), QObject::tr("星期日")
    };

    // 根据数字获取中文名称
    QString dayOfWeekString = dayNames.at(dayOfWeekNumber);
    this->findChild<QLabel*>("label_systemtime")->setText(QString(QObject::tr("系统时间：")) + currentDateTime.toString("yyyy/MM/dd hh:mm:ss ") + dayOfWeekString);

    if (mMeasureTimer && mMeasureTimer->isValid())
    {
        ui->edit_measureTime->setText(formatTimeString(mMeasureTimer->elapsed() / 1000));
    }

    // 温度刷新一次，时钟刷新2次，
    if ((t++%2) == 0){
        ui->tableWidget_detector->setUpdatesEnabled(false);
        for (int row=0; row<=DET_NUM; ++row){
            if (m_mapTemperatur.contains(row)){
                float temperature = m_mapTemperatur[row].second;
                QDateTime tmTrigger = QDateTime::fromMSecsSinceEpoch(m_mapTemperatur[row].first);
                bool isTimeout = false;
                if (tmTrigger.msecsTo(currentDateTime) >= 2000)
                    isTimeout = true;

                QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 3));
                if (isTimeout){
                    // 长时间没收到温度数据了
                    cell->setEnabled(false);
                }
                else{
                    cell->setText(QString::number(temperature, 'f', 1) + " ℃");
                    if (cell->isEnabled()){
                        // 判断温度是否达到报警阈值点
                        if (temperature > 70.0) {
                            cell->setStyleSheet("color: red;");
                        } else {
                            cell->setStyleSheet("color: green;");
                        }
                    }
                }
            }
        }
        ui->tableWidget_detector->setUpdatesEnabled(true);
    }

    // 每10s刷新一次温度缓存到文件，避免频繁IO
    if(t%10==0) flushTemperatureCache();

    // 能谱刷新需根据刷新时长来决定，最小1s刷新一次
    if (commHelper->isMeasuring() && (t % m_specPlotRefreshTime) == 0){
        //
        ui->spectroMeter1_top->replot(QCustomPlot::rpQueuedReplot);
        ui->spectroMeter2_top->replot(QCustomPlot::rpQueuedReplot);

        //能谱图像 y轴自动缩放
        ui->spectroMeter1_bottom->yAxis->rescale();
        ui->spectroMeter1_bottom->yAxis->setRangeUpper(
            ui->spectroMeter1_bottom->yAxis->range().upper * 1.1); // 留10%余量
        ui->spectroMeter1_bottom->replot(QCustomPlot::rpQueuedReplot);

        ui->spectroMeter2_bottom->yAxis->rescale();
        ui->spectroMeter2_bottom->yAxis->setRangeUpper(
            ui->spectroMeter2_bottom->yAxis->range().upper * 1.1); // 留10%余量
        ui->spectroMeter2_bottom->replot(QCustomPlot::rpQueuedReplot);
    }

    // 定时测量模式下，检测是否时长已满
    if(misTimedMeasure)
    {
        onMeasureCountdownTimeout();
    }
}

//交换机登录成功的回调
void MainWindow::onSwitcherLogged(const QString& ip)
{
    mSwitcherLogged = true;
    updateConnectButtonState(true);
    ui->action_powerOn->setEnabled(true);
    ui->action_powerOff->setEnabled(true);
    qInfo().nospace().noquote() << tr("交换机[%1]已登录").arg(ip);

    /// 再给设备上电
    if (mEnableAutoMeasure)
    {
        // 打开时钟同步模块电源
        commHelper->openSwitcherExtendPOEPower();
        // 打开谱仪设备电源
        commHelper->openPower();

        // 第三步 30s之后开始自动测量
        ui->action_stopMeasure->setEnabled(false);
        QTimer::singleShot(30000, this, [=](){
            ui->action_stopMeasure->setEnabled(true);

            /// 开始测量
            this->startMeasure();
        });
    }
}

//探测器上线
void MainWindow::onDetectorOnline(quint8 index)
{
    int row = index;
    QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 1));
    cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::green));
    cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 3));
    cell->setStyleSheet("color: green;");
    cell->setEnabled(true);

    //记录联网的探测器ID
    mOnlineDetectors.append(index);
    if(index==0){
        qInfo().nospace() << "时间同步触发模块上线";
    } 
    else
    {
        qInfo().nospace() << "谱仪[#" << index << "]上线"; 
    }
    //如果该探测器在温度超时报警列表中，则删除
    if (mTemperatureTimeoutDetectors.contains(index)){
        mTemperatureTimeoutDetectors.removeOne(index);
        //如果该探测器正在测量，则重新开始测量
        if (mDetectorMeasuring[index]){
            commHelper->startMeasure(QDateTime::currentDateTime(), CommandAdapter::MeasureMode::mmManual, CommandAdapter::WorkMode::wmSpectrum, index);
            //打印日志
            qInfo().nospace() << "谱仪[#" << index << "]自动重新开始测量";
        }
    }
}

//探测器离线
void MainWindow::onDetectorOffline(quint8 index)
{
    int row = index;
    QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 1));
    cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::red));
    cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 2));
    cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::red));
    cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 3));
    cell->setStyleSheet("color: gray;");
    cell->setEnabled(false);

    //清除联网的探测器ID
    if (mOnlineDetectors.contains(index)){
        mOnlineDetectors.removeOne(index);
        qInfo().nospace() << "谱仪[#" << index << "]离线";
    }
}

// 交换机POE电源状态报告
void MainWindow::onPoePowerStatusReport(quint8 index, bool on)
{
    int row = index;
    SwitchButton* cell =  qobject_cast<SwitchButton*>(ui->tableWidget_detector->cellWidget(row, 0));
    if(on)
    {
        if (row == 0)
            qInfo().nospace() << "时间同步触发模块POE电源状态: 开，模块开机中...";
        else
            qInfo().nospace() << "谱仪[#" << index << "]POE电源状态: 开，谱仪开机中...";
    }else{
        if (row == 0)
            qInfo().nospace() << "时间同步触发模块已关机，POE电源状态: 关";
        else
            qInfo().nospace() << "谱仪[#" << index << "]已关机，POE电源状态: 关";
    }
    cell->setChecked(on);
}

// 处理数据解析后的能谱数据，更新界面相关数据，子线程，而非界面线程
void MainWindow::onFullSpectrum(int index, const FullSpectrum& fullSpectrum)
{
    if (index < 1 || index > DET_NUM) {
        qWarning() << "Invalid detector ID:" << index;
        return;
    }

    DetectorData &data = m_detectorData[index];

    // 累积能谱
    quint32 currentCount = 0;
    for (int i = 0; i < 8192; ++i) {
        currentCount += fullSpectrum.spectrum[i];
        data.spectrum[i] += fullSpectrum.spectrum[i];
    }

    // 记录累积计数率
    data.lastAccumulateCount += currentCount;
    data.lastAccuCountFixed += currentCount * 1.0 * fullSpectrum.measureTime *1e6 
        / (fullSpectrum.measureTime *1e6 - fullSpectrum.deathTime*10); // 死时间修正，注意测量时间单位ms，死时间单位*10ns

    //最快每秒更新一次能谱、计数率,这里不考虑丢包带来的计数率修复
    quint32 accuTime = (fullSpectrum.sequence - data.lastSpectrumID) * fullSpectrum.measureTime;//单位ms
    double currenTime = 1.0 * fullSpectrum.sequence * fullSpectrum.measureTime / 1000; //单位s，这里最大数对应2^32/24/3600=49days
    if(accuTime >= 1000) {
        double countRate = 1000.0*data.lastAccumulateCount * 1.0 / accuTime; //cps
        double fixedCountRate = 1000.0*data.lastAccuCountFixed * 1.0 / accuTime; //修正后的cps
        data.lastSpectrumID = fullSpectrum.sequence;
        data.lastAccumulateCount = 0;
        data.lastAccuCountFixed = 0;
        data.countRateHistory.append(countRate);
        
        // 更新能谱图像数据
        updateSpecPlotData(index, data.spectrum);

        // 更新计数率图像数据
        updateCountRatePlotData(index, currenTime, fixedCountRate);
    }
}

// 将缓存的温度数据写入磁盘，避免丢数据
void MainWindow::flushTemperatureCache()
{
    QHash<quint8, QHash<QString, QVector<TempPoint>>> tempData;
    {
        QMutexLocker locker(&mMutexTemperature);
        tempData = m_tempPointCache;
        m_tempPointCache.clear();
    }

    if (tempData.isEmpty())
        return;

    const QString baseDirPath = QApplication::applicationDirPath() + "/temperature";

    const QList<quint8> indexKeys = tempData.keys();
    for (quint8 index : indexKeys)
    {
        auto& dayMap = tempData[index]; // index -> ymd -> points
        const QStringList dayKeys = dayMap.keys();
        for (const QString& ymd : dayKeys)
        {
            QVector<TempPoint>& points = dayMap[ymd];
            if (points.isEmpty())
                continue;

            const QString dayDirPath = baseDirPath + "/" + ymd;
            QDir dayDir(dayDirPath);
            if (!dayDir.exists())
                dayDir.mkpath(dayDirPath);

            const QString filePath = dayDirPath + "/" + QString::number(index) + ".txt";
            QFile temperatureFile(filePath);
            if (!temperatureFile.open(QIODevice::WriteOnly | QIODevice::Append))
            {
                qWarning().noquote().nospace()
                    << "Failed to open temperature file: " << filePath;
                continue; // 保留points，下一轮flush再写
            }

            QTextStream temperatureStream(&temperatureFile);
            for (const TempPoint& p : points)
            {
                temperatureStream << p.dtStr << "," << QString::number(p.temperature, 'f', 1) << "\n";
            }
            temperatureFile.close();

            points.clear();
        }
        dayMap.clear();
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    //确保子对话框都退出
    mDetSettingWindow->close();
    mClientPeersWindow->close();
    mOTAUpgradeWindow->close();

    //确认是否退出
    int ret = QMessageBox::question(this, tr("系统退出提示"), tr("确定要退出软件系统吗？"),
                                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret == QMessageBox::No) {
        event->ignore();
        return;
    }

    // 关闭前把缓存的温度点写入磁盘，避免丢数据
    flushTemperatureCache();

    commHelper->stopMeasure();
    commHelper->stopServer();
    event->accept();
}

bool MainWindow::checkStatusTipEvent(QEvent * event) {
    if(event->type() == QEvent::StatusTip) {
        QStatusTipEvent* statusTipEvent = static_cast<QStatusTipEvent *>(event);
        if (!statusTipEvent->tip().isEmpty()) {
            ui->statusbar->showMessage(statusTipEvent->tip(), 2000);
        }

        return true;
    }

    return false;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event){
    if (watched != this){
        if (event->type() == QEvent::MouseButtonPress){
            QMouseEvent *e = reinterpret_cast<QMouseEvent*>(event);
            if (watched->inherits("QLabel")){
                QLabel* label = qobject_cast<QLabel*>(watched);
                if (label->objectName() == "label_LocalServer"){
                    mClientPeersWindow->show();
                }
            }
        }

        else if (event->type() == QEvent::MouseButtonDblClick){
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::slotWriteLog(const QString &msg, QtMsgType msgType)
{
    PendingLogEntry item;
    item.timeText = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    item.msg = msg;
    item.msgType = msgType;
    mPendingLogs.push_back(item);
}

void MainWindow::flushPendingLogs()
{
    if (mPendingLogs.isEmpty())
        return;
    
    QPlainTextEdit* edit = ui->plainTextEdit_log;

    edit->setUpdatesEnabled(false);   // 减少重绘

    QTextCursor cursor = edit->textCursor();
    cursor.movePosition(QTextCursor::End);

    QTextCharFormat fmtNormal;
    fmtNormal.setForeground(mIsDarkTheme ? Qt::white : Qt::black);

    QTextCharFormat fmtError;
    fmtError.setForeground(Qt::red);

    QTextCharFormat fmtWarning;
    fmtWarning.setForeground(Qt::blue);

    // 批量插入（关键）
    for (const PendingLogEntry& item : qAsConst(mPendingLogs)) {

        // 时间部分（普通颜色）
        cursor.setCharFormat(fmtNormal);
        cursor.insertText(item.timeText);
        cursor.insertText(" >> ");

        // 内容部分（按类型着色）
        if (item.msgType == QtCriticalMsg || item.msgType == QtFatalMsg) {
            cursor.setCharFormat(fmtError);
        }
        else if (item.msgType == QtWarningMsg) {
            cursor.setCharFormat(fmtWarning);
        }
        else {
            cursor.setCharFormat(fmtNormal);
        }

        cursor.insertText(item.msg);
        cursor.insertText("\n");
    }

    mPendingLogs.clear();

    edit->setTextCursor(cursor);

    // 自动滚动到底部
    edit->verticalScrollBar()->setValue(edit->verticalScrollBar()->maximum());
    ui->plainTextEdit_log->document()->setMaximumBlockCount(2000);
    
    edit->setUpdatesEnabled(true);
}


void MainWindow::on_action_cfgParam_triggered()
{    
    mDetSettingWindow->show();
    mDetSettingWindow->raise();
    mDetSettingWindow->activateWindow();
}


void MainWindow::on_action_exit_triggered()
{
    //qApp->quit();
    mainWindow->close();
}


void MainWindow::on_action_startServer_triggered()
{
    // 连接网络
    if (commHelper->startServer()){
        ui->action_startServer->setEnabled(false);
        ui->action_stopServer->setEnabled(true);
        ui->action_startMeasure->setEnabled(true);
        ui->action_stopMeasure->setEnabled(true);
        ui->action_connectSwitch->setEnabled(true);
        ui->pushButton_startMeasure->setEnabled(true);
        ui->pushButton_stopMeasure->setEnabled(true);

        QLabel* label_LocalServer = this->findChild<QLabel*>("label_LocalServer");
        label_LocalServer->setStyleSheet("color:#00ff00;");
        label_LocalServer->setText(tr("本地网络服务：已开启"));
        //打印日志
        qInfo().noquote() << tr("本地网络服务已开启，等待探测器连接...");
    }
    else{
        QMessageBox::information(this, tr("提示"), tr("服务开启失败，请检测本地主机地址！"));
    }
}


void MainWindow::on_action_stopServer_triggered()
{
    //先记录下所有联网的探测器
    foreach (quint8 index, mOnlineDetectors){
        commHelper->manualCloseSwitcherPOEPower(index);
        //将响应在线图标设置为红色
        int row = index;
        QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 1));
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::red));
        cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 2));
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::red));
    }
    //清除联网的探测器ID
    mOnlineDetectors.clear();
    //清除温度超时的探测器ID
    mTemperatureTimeoutDetectors.clear();

    // 断开网络
    commHelper->stopMeasure();
    commHelper->stopServer();

    ui->action_startServer->setEnabled(true);
    ui->action_stopServer->setEnabled(false);
    ui->action_startMeasure->setEnabled(false);
    ui->action_stopMeasure->setEnabled(false);
    ui->pushButton_startMeasure->setEnabled(false);
    ui->pushButton_stopMeasure->setEnabled(false);

    QLabel* label_LocalServer = this->findChild<QLabel*>("label_LocalServer");
    label_LocalServer->setStyleSheet("color:#ff0000;");
    label_LocalServer->setText(tr("本地网络服务：断开"));
    qInfo().noquote() << tr("本地网络服务已断开");
    misTimedMeasure = false;
}

//视图
void MainWindow::on_actiontoolBar_triggered()
{
    ui->toolBar->setVisible(!ui->toolBar->isVisible());
}

void MainWindow::on_actionstatusBar_triggered()
{
    ui->statusbar->setVisible(!ui->statusbar->isVisible());
}

// 测量模式选择：手动测量/继续测量
void MainWindow::onMeasureModeChanged(int index)
{
    //手动测量模式，只允许软件触发，自动测量既可以软件触发也可以外触发
    if(index==0){
        ui->com_triggerModel->setCurrentIndex(0);
        ui->com_triggerModel->setEnabled(false);
    }
    else{
        ui->com_triggerModel->setEnabled(true);
    }
}

/**
 * @brief 触发模式选择：软件触发/外触发
 * @param index 下拉框序号，触发类型
 */
void MainWindow::onTriggerModelChanged(int index)
{
    if(index==0){
        if(ui->cbb_measureMode->currentIndex() ==  0){ 
            ui->spinBox_coolTimeLength->setEnabled(false);// 手动测量下，软件触发不允许设置冷却时间
        }
        //软件触发
        ui->dateTime_shotTime->setEnabled(true); // 软件触发才允许设置打靶时间
    }
    else
    {
        //外触发
        ui->dateTime_shotTime->setEnabled(false);   // 外触发不允许设置打靶时间
        ui->spinBox_coolTimeLength->setEnabled(true); // 外触发才允许设置冷却时间
    }
    // 实时保存触发模式
    GlobalSettings settings;
    settings.setValue("mainWindow/TriggerMode", index);
}

// 开始测量 全部通道测量
void MainWindow::on_action_startMeasure_triggered()
{
    //打印线程编号
    qDebug() << "MainWindow::on_action_startMeasure_triggered(), thread id:" << QThread::currentThreadId();
    clearCustomPlotData();
    updateSpectrumPlotSettings();
    //先检查路径有效性 lineEdit_filePath
    QString filePath = ui->lineEdit_filePath->text();
    QDir dir(filePath);
    if (!dir.exists()) {
        QMessageBox::warning(this, tr("警告"), tr("数据保存路径不存在，请检查数据保存路径设置！"));
        return;
    }

    mEnableAutoMeasure = ui->cbb_measureMode->currentIndex()==1;
    mEnableExtendTrigger = ui->com_triggerModel->currentIndex()==1;
    m_specPlotRefreshTime = qMax((quint32)1000, (quint32)HDF5Settings::instance()->detParameters()[1].spectrumRefreshTime) / 1000;

    if (mEnableAutoMeasure)
    {
        qInfo().nospace().noquote() << tr("开启自动化测量");
        mAutoMeasureTimerActive = false; // 等到触发信号后，再置为true
        //确保测量时刻距离当前时间大于240s，给自动测量模式预留足够的准备时间
        QDateTime tmStart = ui->dateTimeEdit_startTime->dateTime();
        QDateTime now = QDateTime::currentDateTime();
        if (now.secsTo(tmStart) < 240){
            QMessageBox::information(this, QStringLiteral("提示"), "请确保测量起始时间距离当前时间至少4分钟，以便自动测量模式有足够的准备时间！");
        }
    }
    else
    {
        if (mEnableExtendTrigger){
            // 提示时间同步触发模块电源是否接通
            SwitchButton* btn = this->findChild<SwitchButton*>("poeSwitcher0");
            if (btn && !btn->getChecked()){
                QMessageBox::information(this, QStringLiteral("提示"), "请开启时间同步模块电源开关！");
                return ;
            }
        }
        qInfo().nospace().noquote() << tr("开启手动测量");
    }

    ui->action_startMeasure->setEnabled(false);
    ui->action_stopMeasure->setEnabled(true);
    ui->dateTime_shotTime->setEnabled(false);
    ui->com_triggerModel->setEnabled(false);
    ui->lineEdit_shotNum->setEnabled(false);
    ui->checkBox_autoIncrease->setEnabled(false);
    ui->spinBox_measureTime->setEnabled(false);
    ui->checkBox_continueMeasure->setEnabled(false);
    ui->lineEdit_filePath->setEnabled(false);
    ui->dateTimeEdit_startTime->setEnabled(false);
    ui->cbb_measureMode->setEnabled(false);
    ui->pushButton_startMeasure->setEnabled(false);
    ui->pushButton_stopMeasure->setEnabled(false);
    ui->action_waveformMode->setEnabled(false);
    ui->action_particalMode->setEnabled(false);
    ui->spinBox_coolTimeLength->setEnabled(false);

    for (auto index : mOnlineDetectors){
        if (index != 0)
            m_selectedChannels.append(index);
    }

    // 启用外触发模式，界面上显示未触发，等待时间戳信号触发测量
    if (mEnableExtendTrigger){
        ui->lineEdit_triggerflag->setText("等待触发");
        ui->dateTimeEdit_startTime->setDateTime(QDateTime::fromString("2026-01-01 00:00:00"));
        ui->dateTime_shotTime->setDateTime(QDateTime::fromString("2026-01-01 00:00:00"));
    }
    else{ // 软件触发模式，开始时间=当前时间，外触发模式，开始时间=打靶时间+冷却时间
        ui->lineEdit_triggerflag->setText("手动触发");
        QDateTime now = QDateTime::currentDateTime();
        ui->dateTimeEdit_startTime->setDateTime(now);
        ui->spinBox_coolTimeLength->setValue(ui->dateTime_shotTime->dateTime().secsTo(now));
    }

    if (!mEnableAutoMeasure)
    {
        ui->lineEdit_triggerflag->setText("已经触发");
        mAutoMeasureDelayTimer->stop();

        // 直接开始测量
        this->startMeasure();
    }
    else{
        mAutoMeasureDelayTimer->start();
    }
}

//单通道测量，单独测量某个通道
void MainWindow::on_pushButton_startMeasure_clicked()
{
    m_specPlotRefreshTime = qMax((quint32)1000, (quint32)HDF5Settings::instance()->detParameters()[1].spectrumRefreshTime) / 1000;
    //清空图像中的曲线数据
    clearCustomPlotData();
    updateSpectrumPlotSettings();

    //先检查路径有效性 lineEdit_filePath
    QString filePath = ui->lineEdit_filePath->text();
    QDir dir1(filePath);
    if (!dir1.exists()) {
        QMessageBox::warning(this, tr("警告"), tr("数据保存路径不存在，请检查数据保存路径设置！"));
        return;
    }

    /*设置发次信息*/
    QString shotDir = ui->lineEdit_filePath->text();
    QString shotNumStr = ui->lineEdit_shotNum->text();
    if (shotDir.isEmpty()){
        shotDir = "./cache";
        ui->lineEdit_filePath->setText(shotDir);
    }

    // 保存测量数据
    QString savePath = QString(tr("%1/%2")).arg(shotDir).arg(shotNumStr);
    QDir dir(QString(tr("%1/%2")).arg(shotDir).arg(shotNumStr));
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    // 保存温度数据
    dir.setPath(QDir::currentPath() + "/temperature");
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    // 保存网络数据
    dir.setPath(QDir::currentPath() + "/cache/net");
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    {
        GlobalSettings settings(QString("%1/Settings.ini").arg(savePath));
        settings.setValue("mainWindow/MeasureMode", ui->cbb_measureMode->currentIndex());
        settings.setValue("mainWindow/TriggerMode", ui->com_triggerModel->currentText());
        settings.setValue("mainWindow/ShotNumStr", shotNumStr);
        settings.setValue("mainWindow/MeasureTimeSeconds(s)", ui->spinBox_measureTime->value());
    }

    {
        GlobalSettings settings(SYSTEM_CONFIG_FILE);
        settings.setValue("mainWindow/ShotNumStr", shotNumStr);
        settings.setValue("mainWindow/CacheDir", ui->lineEdit_filePath->text());
    }

    commHelper->setShotInformation(shotDir, shotNumStr);
    // 再发开始测量指令
    auto rows  = ui->tableWidget_detector->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
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

    if (validIndexes.size() <= 0){
        QMessageBox::warning(
            this,
            tr("自定义通道测量——提示"),
            tr("所选探测器通道未上线。")
            );
        return;
    }

    if (ui->action_waveformMode->isChecked())
    {
        QString str = QString("开始测量，通道号：");
        for (const auto &mi : validIndexes) {
            int row = mi.row();
            if (0 == row)
                continue;

            quint8 index = row;

            if (m_selectedChannels.contains(index))
                continue;

            // 再发送开始测量指令
            if (mOnlineDetectors.contains(index))
            {
                // 启动连续测量计时器
                if (!mMeasureTimer->isValid())
                    mMeasureTimer->start();

                qInfo().noquote().nospace() << QStringLiteral("谱仪[#%1]开始测量，波形测量模式").arg(index);
                commHelper->startMeasure(QDateTime::currentDateTime(), CommandAdapter::MeasureMode::mmManual, CommandAdapter::WorkMode::wmWaveform, index);
                //记录下所选的通道号，保存到成员变量中，停止测量的时候使用并清空
                m_selectedChannels.append(index);
            }
        }
    }
    else if (ui->action_spectrumMode->isChecked())
    {
        QString str = QString("开始测量，通道号：");
        for (const auto &mi : validIndexes) {
            int row = mi.row();
            if (0 == row)
                continue;

            quint8 index = row;

            if (m_selectedChannels.contains(index))
                continue;

            // 再发送开始测量指令
            if (mOnlineDetectors.contains(index))
            {
                // 启动测量计时器
                if (!mMeasureTimer->isValid())
                    mMeasureTimer->start();
                // 判断是否连续测量
                if (ui->checkBox_continueMeasure->isChecked())
                {
                    misTimedMeasure = false;
                    // 初始化测量时长显示为 00:00:00
                    ui->edit_measureTime->setText("00:00:00");
                    
                    qInfo().noquote().nospace() << QString("谱仪[#%1]开始测量，连续测量模式").arg(index);
                }
                else
                {
                    // 读取倒计时时间（秒）
                    int countdownSeconds = ui->spinBox_measureTime->value();
                    if (countdownSeconds > 0) {
                        mTotalCountdown = countdownSeconds;
                        misTimedMeasure = true;
                        // 初始化测量时长显示为 00:00:00
                        ui->edit_measureTime->setText("00:00:00");
                        qInfo().noquote().nospace() << QString("谱仪[#%1]开始测量，倒计时：%2 秒").arg(index).arg(countdownSeconds);
                    }
                }

                commHelper->startMeasure(QDateTime::currentDateTime(), CommandAdapter::MeasureMode::mmManual, CommandAdapter::WorkMode::wmSpectrum, index);
                //记录下所选的通道号，保存到成员变量中，停止测量的时候使用并清空
                m_selectedChannels.append(index);
            }
        }
    }

    // 测量中禁用参数配置
    ui->pushButton_stopMeasure->setEnabled(true);
    ui->action_startMeasure->setEnabled(false);
    ui->action_stopMeasure->setEnabled(false);
    ui->action_waveformMode->setEnabled(false);
    ui->action_particalMode->setEnabled(false);
}


//（自定义通道）停止测量，停止测量已选中的通道
void MainWindow::on_pushButton_stopMeasure_clicked()
{
    if(mEnableAutoMeasure){
        qInfo().noquote() << "自动化测量已停止";
    }
    else{
        qInfo().noquote() << "手动测量已停止";
    }

    // 显示最终测量时长
    int elapsedSeconds = mMeasureTimer->elapsed() / 1000;
    ui->edit_measureTime->setText(formatTimeString(elapsedSeconds));
    mMeasureTimer->invalidate();
    misTimedMeasure = false;

    // 获取所选通道
    // 再发开始测量指令
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

    //打印日志

    for (const auto &mi : validIndexes) {
        int row = mi.row();
        if (0 == row)
            continue;

        quint8 index = row;
        qInfo().noquote().nospace() << QString("谱仪[#%1]点击停止测量").arg(index);
        commHelper->stopMeasure(index);
        m_selectedChannels.removeOne(index);
    }

    if (m_selectedChannels.empty()){
        // 非测量中可用参数配置
        ui->action_startMeasure->setEnabled(true);
        ui->action_stopMeasure->setEnabled(false);

        ui->pushButton_stopMeasure->setEnabled(false);
        ui->action_waveformMode->setEnabled(true);
        ui->action_particalMode->setEnabled(true);
    }

    ui->pushButton_startMeasure->setEnabled(true);
    ui->pushButton_stopMeasure->setEnabled(false);
    ui->action_startMeasure->setEnabled(true);
    ui->action_stopMeasure->setEnabled(false);
}


void MainWindow::on_action_stopMeasure_triggered()
{
    ui->action_startMeasure->setEnabled(true);
    ui->action_stopMeasure->setEnabled(false);
    ui->com_triggerModel->setEnabled(true);
    ui->lineEdit_shotNum->setEnabled(true);
    ui->checkBox_autoIncrease->setEnabled(true);
    ui->checkBox_continueMeasure->setEnabled(true);
    ui->lineEdit_filePath->setEnabled(true);
    ui->dateTimeEdit_startTime->setEnabled(false);
    ui->cbb_measureMode->setEnabled(true);
    ui->pushButton_startMeasure->setEnabled(true);
    ui->pushButton_stopMeasure->setEnabled(false);
    ui->action_waveformMode->setEnabled(true);
    ui->action_particalMode->setEnabled(true);
    ui->spinBox_measureTime->setEnabled(!ui->checkBox_continueMeasure->isChecked());

    // 软件触发才允许设置打靶时间
    ui->dateTime_shotTime->setEnabled(ui->com_triggerModel->currentIndex() == 0);
    ui->lineEdit_triggerflag->setText("未触发");

    // 外触发才允许设置冷却时间
    ui->spinBox_coolTimeLength->setEnabled(mEnableExtendTrigger);

    if (mEnableAutoMeasure)
    {
        qInfo().nospace().noquote() << tr("人工停止自动化测量");
    }
    else
    {
        qInfo().nospace().noquote() << tr("停止手动测量");
    }

    ui->action_startMeasure->setEnabled(true);

    // 停止测量
    stopMeasure();
    m_selectedChannels.clear();

    mEnableAutoMeasure = false;
    mEnableExtendTrigger = false;
}

void MainWindow::onMeasureCountdownTimeout()
{
    // 计算已测量时长s（总时长 - 剩余倒计时）
    int mRemainingCountdown = mTotalCountdown - mMeasureTimer->elapsed() / 1000; 

    if (mRemainingCountdown <= 0) {
        misTimedMeasure = false;
        // 自动更新发次号
        if (ui->checkBox_autoIncrease->isChecked()){
            ui->lineEdit_shotNum->setText(increaseShotNumSuffix(ui->lineEdit_shotNum->text().trimmed()));
            GlobalSettings settings(DEVICE_CONFIG_FILE);
            settings.setValue("mainWindow/ShotNumStr", ui->lineEdit_shotNum->text());
        }

        if (mEnableAutoMeasure)
        {
            qInfo() << "测量倒计时结束，正在停止测量并关闭所有通道电源...";

            // 停止所有测量
            commHelper->stopMeasure();

            //记录下关闭的电源通道号
            foreach (quint8 index, mOnlineDetectors)
                commHelper->manualCloseSwitcherPOEPower(index);

            // 关闭所有通道电源
            commHelper->closePower(mEnableAutoMeasure);

            qInfo() << "所有测量已停止，所有通道电源已关闭";
        }
        else
        {
            qInfo() << "测量倒计时结束，正在停止测量...";

            // 停止所有测量
            commHelper->stopMeasure();
        }

        //停止测量定时器
        mMeasureTimer->invalidate();
        //清空温度超时报警的探测器ID
        mTemperatureTimeoutDetectors.clear();
        //清空探测器正在测量记录
        mDetectorMeasuring.clear();

        ui->action_startMeasure->setEnabled(true);
        ui->action_stopMeasure->setEnabled(false);
        ui->pushButton_startMeasure->setEnabled(true);
        ui->pushButton_stopMeasure->setEnabled(false);
        ui->dateTime_shotTime->setEnabled(false);
        ui->com_triggerModel->setEnabled(true);
        ui->lineEdit_shotNum->setEnabled(true);
        ui->checkBox_autoIncrease->setEnabled(true);
        ui->spinBox_measureTime->setEnabled(true);
        ui->checkBox_continueMeasure->setEnabled(true);
        ui->lineEdit_filePath->setEnabled(true);
        ui->cbb_measureMode->setEnabled(true);
        ui->dateTimeEdit_startTime->setEnabled(false);

        if (!mEnableExtendTrigger){
            ui->dateTime_shotTime->setEnabled(true);
        }
    }
}

// 对尾缀_1等数字进行加1操作。一定要有下划线
QString MainWindow::increaseShotNumSuffix(QString shotNumStr)
{
    QRegularExpression  rx(R"(^(.*?_)(\d+)$)"); // 匹配以数字结尾的字符串
    auto m = rx.match(shotNumStr);
    if (m.hasMatch()) {
        QString prefix = m.captured(1); // "20251121_"
        QString digits = m.captured(2); // "002"
        int numberLength = digits.length(); // 数字部分的长度
        int number = digits.toInt(); // 转换为整数
        number++; // 自增1
        QString newNumberStr = QString::number(number).rightJustified(numberLength, '0'); // 保持原有长度，前面补0
        return prefix + newNumberStr; // 返回新的发次字符串
    }
    else {
        // 如果没有下划线+数字结尾，直接在后面加上1
        return shotNumStr + "_1";
    }
}

void MainWindow::on_action_powerOn_triggered()
{
    //清除所有
    commHelper->manualOpenSwitcherPOEPower();

    // 打开电源
    commHelper->openPower();
}


void MainWindow::on_action_powerOff_triggered()
{
    // 关闭电源
    commHelper->closePower();
}

void MainWindow::on_action_about_triggered()
{
    QString filename = QFileInfo(QCoreApplication::applicationFilePath()).baseName();
    QMessageBox::about(this, tr("关于"),
                       QString("<p>") +
                           tr("版本") +
                           QString("</p><span style='color:blue;'>%1</span><p>").arg(filename, APP_VERSION) +
                           tr("提交") +
                           QString("</p><span style='color:blue;'>%1: %2</span><p>").arg(GIT_BRANCH, GIT_HASH) +
                           tr("日期") +
                           QString("</p><span style='color:blue;'>%1</span><p>").arg(GIT_DATE) +
                           tr("开发者") +
                           QString("</p><span style='color:blue;'>MaoXiaoqing</span><p>") +
                           "</p><p>四川大学物理学院 版权所有 (C) 2025</p>"
                       );
}

void MainWindow::on_action_aboutQt_triggered()
{
    QMessageBox::aboutQt(this);
}


void MainWindow::on_action_lightTheme_triggered()
{
    if(!mIsDarkTheme) return;
    mIsDarkTheme = false;
    qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    if(mThemeColorEnable) QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
    GlobalSettings settingsSystem;
    settingsSystem.setValue("Global/Startup/darkTheme","false");
    applyColorTheme();
}


void MainWindow::on_action_darkTheme_triggered()
{
    if(mIsDarkTheme) return;
    mIsDarkTheme = true;
    qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    if(mThemeColorEnable) QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
    GlobalSettings settingsSystem;
    settingsSystem.setValue("Global/Startup/darkTheme","true");
    applyColorTheme();
}


void MainWindow::on_action_colorTheme_triggered()
{
    GlobalSettings settingsSystem;
    QColor color = QColorDialog::getColor(mThemeColor, this, tr("选择颜色"));
    if (color.isValid()) {
        mThemeColor = color;
        mThemeColorEnable = true;
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
        QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
        settingsSystem.setValue("Global/Startup/themeColor",mThemeColor);
    } else {
        mThemeColorEnable = false;
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    }
    settingsSystem.setValue("Global/Startup/themeColorEnable",mThemeColorEnable);
    applyColorTheme();
}

void MainWindow::applyColorTheme()
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
        for (auto checkBox : checkBoxs){
            checkBox->setStyleSheet(styleSheet);
        }

        // 更新工作日志标签颜色
        QLabel* workLogLabel = this->findChild<QLabel*>("label_workLog");
        if (workLogLabel) {
            QString style = mIsDarkTheme ? "color: white;" : "color: black;";
            workLogLabel->setStyleSheet(style);
        }

        QCPItemText *textLabel = customPlot->findChild<QCPItemText*>("title");
        if (mIsDarkTheme)
            textLabel->setColor(Qt::white);
        else
            textLabel->setColor(Qt::black);

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

        customPlot->xAxis2->setTicks(false);
        customPlot->xAxis2->setSubTicks(false);
        customPlot->yAxis2->setTicks(false);
        customPlot->yAxis2->setSubTicks(false);

        customPlot->replot();
    }
}

void MainWindow::restoreSettings()
{
    GlobalSettings settingsSystem;
    if(mainWindow) {
        mainWindow->restoreGeometry(settingsSystem.value("MainWindow/Geometry").toByteArray());
        mainWindow->restoreState(settingsSystem.value("MainWindow/State").toByteArray());
    } else {
        restoreGeometry(settingsSystem.value("MainWindow/Geometry").toByteArray());
        restoreState(settingsSystem.value("MainWindow/State").toByteArray());
    }
    mThemeColor = settingsSystem.value("Global/Startup/themeColor",QColor(30,30,30)).value<QColor>();
    mThemeColorEnable = settingsSystem.value("Global/Startup/themeColorEnable",true).toBool();
    if(mThemeColorEnable) {
        QTimer::singleShot(0, this, [&](){
            qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
            QGoodWindow::setAppCustomTheme(mIsDarkTheme,this->mThemeColor); // Must be >96
        });
    }
}

// 重置能谱
void MainWindow::resetDetectorSpectrum(int detectorId) {
    auto it = m_detectorData.find(detectorId);
    if (it == m_detectorData.end())
        return;

    // 清空能谱
    memset(it->spectrum, 0, sizeof(it->spectrum));
    // 清空上次测量累积时间的能谱序号
    it->lastSpectrumID = 0;
    // 清空上次测量累积时间的计数率
    it->lastAccumulateCount = 0;
    // 清空计数率历史并释放内存
    QVector<double>().swap(it->countRateHistory);

    qDebug().noquote().nospace() << "Detector" << detectorId << "spectrum and countRateHistory reset";
}

// 获取探测器数据
DetectorData MainWindow::getDetectorData(int detectorId) const {
    return m_detectorData.value(detectorId, DetectorData{});
}

// 检查探测器是否在线
bool MainWindow::isDetectorOnline(int detectorId) const {
    return m_detectorData.contains(detectorId);
}

// 获取在线探测器列表
QList<int> MainWindow::getOnlineDetectors() const {
    return m_detectorData.keys();
}

// 更新能谱图像数据
void MainWindow::updateSpecPlotData(int detectorId, const quint32 spectrum[]) {
    // 计算在页面中的索引
    QCustomPlot *customPlot = getCustomPlot(detectorId, true);
    if (!customPlot)
        return;

    QVector<double> x, y;
    // 针对能量刻度做处理
    if(mEnScale)
    {
        //压缩多道
        int mergebin = 8192/m_spectrumPlotSettings[detectorId-1].multiChannel;
        for (int i = 0; i < 8192; ++i) {
            double mergeCount = 0.0;
            for(int mergeID = 0; mergeID<mergebin; mergeID++){
                if(i<8192){
                    mergeCount += spectrum[i];
                }
                else{
                    break;
                }
                i++;
            }
            int newCh = (i+1)/mergebin;
            double tempX;
            if (m_spectrumPlotSettings[detectorId-1].fitType == 1){
                tempX = m_spectrumPlotSettings[detectorId-1].c0*newCh + m_spectrumPlotSettings[detectorId-1].c1;
            } else if (m_spectrumPlotSettings[detectorId-1].fitType == 2){
                //拟合参数赋值
                tempX = m_spectrumPlotSettings[detectorId-1].c0*newCh*newCh + m_spectrumPlotSettings[detectorId-1].c1*newCh + m_spectrumPlotSettings[detectorId-1].c2;
            }
            x << tempX;
            y << mergeCount;
        }
    }
    else
    {
        //压缩多道
        int mergebin = 8192/m_spectrumPlotSettings[detectorId-1].multiChannel;
        for (int i = 0; i < 8192; ++i) {
            double mergeCount = 0.0;
            for(int mergeID = 0; mergeID<mergebin; mergeID++){
                if(i<8192){
                    mergeCount += spectrum[i];
                }
                else{
                    break;
                }
                i++;
            }
            int newCh = (i+1)/mergebin;
            x << newCh;
            y << mergeCount;
        }
    }

    // 更新图像数据
    getGraph(detectorId, true)->setData(x, y);
}

// 添加计数率显示更新函数
void MainWindow::updateCountRatePlotData(int detectorId, double fpgaTime, double countRate) {
    // 计算在页面中的索引
    QCustomPlot *customPlot = getCustomPlot(detectorId, false);
    if (!customPlot)
        return;

    DetectorData &data = m_detectorData[detectorId];

    // 获取当前时间（使用相对时间或绝对时间）
    static QElapsedTimer timer;
    if (!timer.isValid()) {
        timer.start();
    }

    getGraph(detectorId, false)->addData(fpgaTime, countRate);

    // 根据设置调整坐标轴范围
    if (customPlot->property("enableAutoScale").toBool())
        customPlot->rescaleAxes(true);
    else if (customPlot->property("enableFixedScale").toBool()){
        // 显示最近300秒
        const int WINDOW = customPlot->property("xAxisFixedRange").toUInt();
        const int halfDetNum = DET_NUM / 2; // 提取常量，避免重复计算DET_NUM/2
        // 1. 计算组起始索引（原代码中重复3次的部分）
        int groupStart = ((detectorId - 1) / halfDetNum) * halfDetNum + 1;
        // 2. 计算组结束索引（原代码中groupStart + halfDetNum，无需+1，因为循环是<=）
        int groupEnd = groupStart + halfDetNum - 1;

        // 3. 初始化x_max：取组内第一个探测器的第一个数据点的key
        QCPGraph* firstGraph = getGraph(groupStart, false);
        double x_max = (firstGraph && firstGraph->dataCount() > 0)
                           ? firstGraph->data()->at(0)->key
                           : 0.0; // 处理空数据的默认值

        // 4. 遍历组内所有探测器，更新x_max
        for (int detectorIndex = groupStart; detectorIndex <= groupEnd; ++detectorIndex) {
            QCPGraph* tempGraph = getGraph(detectorIndex, false);
            if (!tempGraph || tempGraph->dataCount() <= 0) {
                continue; // 提前判断空指针或无数据，避免后续错误
            }
            // 获取该探测器的最后一个数据点的key
            double lastKey = tempGraph->data()->at(tempGraph->dataCount() - 1)->key;
            x_max = qMax(x_max, lastKey);
        }
        x_max = qMax(x_max, (double)WINDOW);
        double x_min = qMax(0.0, x_max - WINDOW);
        customPlot->xAxis->setRange(x_min, x_max);

        // y轴范围只由最近300秒的y决定
        auto range = calcRecentYRange(data.countRateHistory, WINDOW);
        double y_min = range.first;
        double y_max = range.second;
        // 统计总曲线Y轴最大、最小值范围
        for (int detectorIndex = groupStart; detectorIndex <= groupEnd; ++detectorIndex) {
            DetectorData &data = m_detectorData[detectorIndex];
            auto range = calcRecentYRange(data.countRateHistory, WINDOW);
            y_min = qMin(y_min, range.first);
            y_max = qMax(y_max, range.second);
        }

        // 稍微留点头部空间
        double pad = (y_max - y_min) * 0.1;
        customPlot->yAxis->setRange(y_min - pad, y_max + pad);
    }
}

#include "localsettingwindow.h"
void MainWindow::on_action_localService_triggered()
{
    LocalSettingWindow *w = new LocalSettingWindow(this);
    w->setAttribute(Qt::WA_DeleteOnClose, true);
    w->setWindowFlags(Qt::WindowCloseButtonHint|Qt::Dialog);
    w->setWindowModality(Qt::ApplicationModal);
    w->showNormal();
}

QString MainWindow::formatTimeString(int totalSeconds)
{
    int days = totalSeconds / 86400;  // 86400秒 = 24小时
    int remainingSeconds = totalSeconds % 86400;
    int hours = remainingSeconds / 3600;
    int minutes = (remainingSeconds % 3600) / 60;
    int seconds = remainingSeconds % 60;

    if (days > 0) {
        // 超过24小时，显示天数
        return QString("%1day %2:%3:%4")
                .arg(days)
                .arg(hours, 2, 10, QChar('0'))
                .arg(minutes, 2, 10, QChar('0'))
                .arg(seconds, 2, 10, QChar('0'));
    } else {
        // 24小时以内，只显示时分秒
        return QString("%1:%2:%3")
                .arg(hours, 2, 10, QChar('0'))
                .arg(minutes, 2, 10, QChar('0'))
                .arg(seconds, 2, 10, QChar('0'));
    }
}

void MainWindow::on_action_connectSwitch_triggered()
{
     if (mSwitcherConnected) {
        // 当前已连接，执行断开操作
        commHelper->disconnectSwitcher();
        // 禁用按钮10秒
        ui->action_connectSwitch->setEnabled(false);
        mConnectButtonDisableTimer->start();
    } else {
        // 当前未连接，执行连接操作
        commHelper->connectSwitcher(true);
        // 禁用按钮10秒
        ui->action_connectSwitch->setEnabled(false);
        mConnectButtonDisableTimer->start();
    }
}

void MainWindow::updateConnectButtonState(bool connected)
{
    if (connected) {
        // 已连接状态：显示"断开交换机"，使用在线图标
        ui->action_connectSwitch->setText(tr("断开交换机"));
        ui->action_connectSwitch->setToolTip(tr("断开交换机"));
        ui->action_connectSwitch->setIcon(QIcon(":/switchOnline.png"));  // 使用在线图标
        // 连接成功后立即启用按钮（解除禁用）
        ui->action_connectSwitch->setEnabled(true);
        mConnectButtonDisableTimer->stop();
    } else {
        // 未连接状态：显示"连接交换机"，使用离线图标
        ui->action_connectSwitch->setText(tr("连接交换机"));
        ui->action_connectSwitch->setToolTip(tr("连接交换机"));
        ui->action_connectSwitch->setIcon(QIcon(":/switchOffline.png"));  // 使用离线图标
        // 断开后立即启用按钮（解除禁用）
        ui->action_connectSwitch->setEnabled(commHelper->isOpen());
        mConnectButtonDisableTimer->stop();
    }
}

// 清除日志
void MainWindow::on_bt_clearLog_clicked()
{
    ui->plainTextEdit_log->clear();
    mPendingLogs.clear();
    clearHighlights();
    mLastSearchText.clear();
    mCurrentSearchPosition = 0;
}

// 查找功能实现
void MainWindow::on_bt_search_clicked()
{
    performSearch(true, true);  // 向前查找，支持循环
}

void MainWindow::on_bt_searchPrevious_clicked()
{
    performSearch(false, true);  // 向后查找，支持循环
}

void MainWindow::on_bt_searchNext_clicked()
{
    performSearch(true, true);  // 向前查找，支持循环
}

void MainWindow::on_bt_highlightAll_toggled(bool checked)
{
    QString searchText = ui->lineEdit_search->text();
    if (checked && !searchText.isEmpty()) {
        highlightAllMatches(searchText);
    } else {
        clearHighlights();
    }
}

void MainWindow::on_lineEdit_search_returnPressed()
{
    performSearch(true, true);  // 按回车键执行查找
}

void MainWindow::on_lineEdit_search_textChanged(const QString &text)
{
    // 如果高亮全部已启用，更新高亮
    if (ui->bt_highlightAll->isChecked()) {
        if (!text.isEmpty()) {
            highlightAllMatches(text);
        } else {
            clearHighlights();
        }
    } else {
        clearHighlights();
    }
}

void MainWindow::performSearch(bool forward, bool wrap)
{
    QString searchText = ui->lineEdit_search->text();
    if (searchText.isEmpty()) {
        return;
    }

    QPlainTextEdit *textEdit = ui->plainTextEdit_log;
    QTextDocument *document = textEdit->document();
    QTextCursor cursor = textEdit->textCursor();

    // 如果搜索文本改变，重置位置
    if (searchText != mLastSearchText) {
        mLastSearchText = searchText;
        if (forward) {
            cursor.setPosition(0);  // 从开头开始
        } else {
            cursor.setPosition(document->characterCount());  // 从末尾开始
        }
        textEdit->setTextCursor(cursor);
    } else {
        // 如果搜索文本未改变，从当前位置继续查找
        // 对于向前查找，需要移动到下一个位置；对于向后查找，需要移动到上一个位置
        if (forward) {
            // 向前查找：从当前位置的下一个字符开始
            int pos = cursor.position();
            if (pos < document->characterCount()) {
                cursor.setPosition(pos + 1);
            } else if (wrap) {
                cursor.setPosition(0);  // 循环到开头
            }
        } else {
            // 向后查找：从当前位置的上一个字符开始
            int pos = cursor.position();
            if (pos > 0) {
                cursor.setPosition(pos - 1);
            } else if (wrap) {
                cursor.setPosition(document->characterCount());  // 循环到末尾
            }
        }
    }

    // 设置查找选项
    QTextDocument::FindFlags flags;
    if (!forward) {
        flags |= QTextDocument::FindBackward;
    }

    // 执行查找
    QTextCursor found = document->find(searchText, cursor, flags);

    QLabel* label_Query = this->findChild<QLabel*>("label_Query");
    if (found.isNull()) {
        // 未找到，如果支持循环，从头/尾继续查找
        if (wrap) {
            QTextCursor newCursor(document);
            if (forward) {
                newCursor.setPosition(0);
            } else {
                newCursor.setPosition(document->characterCount());
            }
            found = document->find(searchText, newCursor, flags);

            if (!found.isNull()) {
                textEdit->setTextCursor(found);
                textEdit->ensureCursorVisible();
                // 显示提示信息
                // ui->statusbar->showMessage(tr("已循环到%1").arg(forward ? tr("开头") : tr("末尾")), 2000);
                label_Query->setText(tr("已循环到%1").arg(forward ? tr("开头") : tr("末尾")));
            } else {
                // ui->statusbar->showMessage(tr("未找到：%1").arg(searchText), 2000);
                label_Query->setText(tr("未找到：%1").arg(searchText));
            }
        } else {
            // ui->statusbar->showMessage(tr("未找到：%1").arg(searchText), 2000);
            label_Query->setText(tr("未找到：%1").arg(searchText));
        }
    } else {
        // 找到匹配项
        textEdit->setTextCursor(found);
        textEdit->ensureCursorVisible();
        mCurrentSearchPosition = found.position();

        label_Query->setText(tr("找到：%1").arg(searchText));
    }
}

void MainWindow::highlightAllMatches(const QString &searchText)
{
    if (searchText.isEmpty()) {
        clearHighlights();
        return;
    }

    QPlainTextEdit *textEdit = ui->plainTextEdit_log;
    QTextDocument *document = textEdit->document();

    // 清除之前的高亮
    clearHighlights();

    // 设置高亮格式
    QTextCharFormat highlightFormat;
    highlightFormat.setBackground(QBrush(QColor(255, 255, 0, 100)));  // 黄色半透明背景
    if (mIsDarkTheme) {
        highlightFormat.setForeground(QBrush(QColor(255, 255, 0)));  // 黄色文字
    }

    // 从文档开头开始查找所有匹配项
    QTextCursor cursor(document);
    cursor.setPosition(0);

    mExtraSelections.clear();
    while (true) {
        cursor = document->find(searchText, cursor, QTextDocument::FindFlags());
        if (cursor.isNull()) {
            break;
        }

        QTextEdit::ExtraSelection extra;
        extra.cursor = cursor;
        extra.format = highlightFormat;
        mExtraSelections.append(extra);
    }

    // 应用高亮
    textEdit->setExtraSelections(mExtraSelections);
}

void MainWindow::clearHighlights()
{
    mExtraSelections.clear();
    ui->plainTextEdit_log->setExtraSelections(mExtraSelections);
}

// 能量刻度
void MainWindow::on_action_energycalibration_triggered()
{
    //模态窗口
    // EnergyCalibration *w = new EnergyCalibration(this);
    EnergyCalibration *w = new EnergyCalibration(nullptr);
    w->setAttribute(Qt::WA_DeleteOnClose, true); // 关闭时自动删除
    w->setWindowFlags(Qt::WindowCloseButtonHint|Qt::Dialog); // 只显示关闭按钮
    w->setWindowModality(Qt::ApplicationModal);//模态属性，NonModal=非模态，ApplicationModal=应用程序模态（阻塞本程序所有窗口），WindowModal=窗口模态（阻塞父窗口）
    w->show();
}


#include "neutronyieldcalibration.h"
void MainWindow::on_action_yieldCalibration_triggered()
{
    //模态窗口
    // NeutronYieldCalibration *w = new NeutronYieldCalibration(this);
    NeutronYieldCalibration *w = new NeutronYieldCalibration(nullptr);
    w->setAttribute(Qt::WA_DeleteOnClose, true); // 关闭时自动删除
    w->setWindowFlags(Qt::WindowCloseButtonHint|Qt::Dialog); // 只显示关闭按钮
    w->setWindowModality(Qt::ApplicationModal);//模态属性，NonModal=非模态，ApplicationModal=应用程序模态（阻塞本程序所有窗口），WindowModal=窗口模态（阻塞父窗口）
    w->show();
}

void MainWindow::startMeasure()
{
    /*设置发次信息*/
    QString shotDir = ui->lineEdit_filePath->text();
    QString shotNumStr = ui->lineEdit_shotNum->text().trimmed(); //去掉 开头 和 结尾 的空白,不会删除中间的空格
    if (shotDir.isEmpty()){
        shotDir = "./cache";
        ui->lineEdit_filePath->setText(shotDir);
    }

    // 保存测量数据
    QString savePath = QString(tr("%1/%2")).arg(shotDir).arg(shotNumStr);
    QDir dir(QString(tr("%1/%2")).arg(shotDir).arg(shotNumStr));
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    {
        GlobalSettings settings(QString("%1/Settings.ini").arg(savePath));
        settings.setValue("mainWindow/TriggerMode", ui->com_triggerModel->currentIndex());
        settings.setValue("mainWindow/ShotNumStr", shotNumStr);
        settings.setValue("mainWindow/MeasureTime", ui->spinBox_measureTime->value());
    }

    {
        GlobalSettings settings(DEVICE_CONFIG_FILE);
        settings.setValue("mainWindow/ShotNumStr", shotNumStr);
        settings.setValue("mainWindow/ShotNumIsAutoIncrease", ui->checkBox_autoIncrease->isChecked());
        settings.setValue("mainWindow/CacheDir", ui->lineEdit_filePath->text());
        settings.setValue("mainWindow/TriggerMode", ui->com_triggerModel->currentIndex());
        settings.setValue("mainWindow/MeasureTime", ui->spinBox_measureTime->value());
    }

    commHelper->setShotInformation(shotDir, shotNumStr);

    // 启动测量计时器
    mMeasureTimer->start();
    // 初始化测量时长显示为 00:00:00
    ui->edit_measureTime->setText("00:00:00");

    // 判断是否连续测量
    if (ui->checkBox_continueMeasure->isChecked())
    {
        misTimedMeasure = false;
    }
    else{
        misTimedMeasure = true;
        // 读取倒计时时间（秒）
        int countdownSeconds = ui->spinBox_measureTime->value();
        if (countdownSeconds > 0) {
            mTotalCountdown = countdownSeconds;
            qInfo().noquote().nospace() << QString("开始测量，倒计时：%1 秒").arg(countdownSeconds);
        }
    }

    CommandAdapter::MeasureMode measureMode = ui->cbb_measureMode->currentIndex() == 0 ? \
        CommandAdapter::MeasureMode::mmManual : CommandAdapter::MeasureMode::mmAuto;
    QDateTime tmStart = QDateTime::currentDateTime();
    if (ui->cbb_measureMode->currentIndex() == 1)
    {
        tmStart = ui->dateTimeEdit_startTime->dateTime();
    }

    // 再发开始测量指令
    if (ui->action_waveformMode->isChecked())
        commHelper->startMeasure(tmStart, measureMode, CommandAdapter::WorkMode::wmWaveform);
    else if (ui->action_spectrumMode->isChecked())
        commHelper->startMeasure(tmStart, measureMode, CommandAdapter::WorkMode::wmSpectrum);

    // 测量中禁用参数配置
    //ui->cb_calibration->setEnabled(false);
}

void MainWindow::stopMeasure()
{
    if(mEnableAutoMeasure){
        qInfo().noquote() << "自动化测量已停止";
    }
    else{
        qInfo().noquote() << "手动测量已停止";
    }

    // 显示最终测量时长
    int elapsedSeconds = mMeasureTimer->elapsed() / 1000;
    ui->edit_measureTime->setText(formatTimeString(elapsedSeconds));
    mMeasureTimer->invalidate();
    misTimedMeasure = false;

    // 自动更新发次号
    if (ui->checkBox_autoIncrease->isChecked()){
        ui->lineEdit_shotNum->setText(increaseShotNumSuffix(ui->lineEdit_shotNum->text().trimmed()));
        GlobalSettings settings(DEVICE_CONFIG_FILE);
        settings.setValue("mainWindow/ShotNumStr", ui->lineEdit_shotNum->text());
    }

    // 停止测量
    commHelper->stopMeasure();
    //清空温度超时报警的探测器ID
    mTemperatureTimeoutDetectors.clear();
    //清空探测器正在测量记录
    mDetectorMeasuring.clear();

    // 非测量中可用参数配置
    //ui->cb_calibration->setEnabled(true);
}

QCustomPlot* MainWindow::getCustomPlot(int detectorId, bool isSpectrum)
{
    if (isSpectrum)
        return this->findChild<QCustomPlot*>(QString("spectroMeter%1_bottom").arg(detectorId <= (DET_NUM/2) ? 1 : 2));
    else
        return this->findChild<QCustomPlot*>(QString("spectroMeter%1_top").arg(detectorId <= (DET_NUM/2) ? 1 : 2));
}

QCPGraph* MainWindow::getGraph(int detectorId, bool isSpectrum)
{
    QCustomPlot *customPlot = getCustomPlot(detectorId, isSpectrum);
    if (!customPlot) nullptr;

    return  customPlot->graph((detectorId-1) % (DET_NUM/2));
}

void MainWindow::on_checkBox_continueMeasure_toggled(bool toggled)
{
    ui->spinBox_measureTime->setEnabled(!toggled);
}

void MainWindow::on_cbb_energyCalibration_toggled(bool checked)
{
    mEnScale = checked;

    updateSpectrumPlotSettings();
    ui->spectroMeter1_bottom->replot();
    ui->spectroMeter2_bottom->replot();
}

void MainWindow::updateSpectrumPlotSettings(int detectorId)
{
    if(detectorId == 0)
    {
        for(int index = 1; index <= DET_NUM; ++index)
        {
            updateSpectrumPlotSettings(index);
            // 更新X轴范围
            ui->spectroMeter1_bottom->xAxis->setRange(m_spectrumPlotSettings[index-1].xMin, m_spectrumPlotSettings[index-1].xMax);
            ui->spectroMeter2_bottom->xAxis->setRange(m_spectrumPlotSettings[index-1].xMin, m_spectrumPlotSettings[index-1].xMax);
        }

        // 取公共最小值/最大值
        // double xMin = m_spectrumPlotSettings[0].xMin;
        // double xMax = m_spectrumPlotSettings[0].xMax;
        // for(int index = 1; index < DET_NUM; ++index)
        // {
        //     xMin = qMin(xMin, m_spectrumPlotSettings[index].xMin);
        //     xMax = qMax(xMax, m_spectrumPlotSettings[index].xMax);
        // }

        return;
    }

    // 读取探测器参数
    // 读取道数
    QMap<quint8, DetParameter>& detParameters = HDF5Settings::instance()->detParameters();

    //更新m_spectrumPlotSettings
    DetParameter& detParameter = detParameters[detectorId];
    int multiCh = detParameter.spectrumLength;
    m_spectrumPlotSettings[detectorId-1].multiChannel = multiCh;

    double xMin = 0.0;
    double xMax = multiCh*1.0;

    if (mEnScale)
    {
        m_spectrumPlotSettings[detectorId-1].xMin = detParameter.spectrumPlotEnMin;
        m_spectrumPlotSettings[detectorId-1].xMax = detParameter.spectrumPlotEnMax;

        // 读取能量刻度系数
        // 判断是否勾选能量刻度
        GlobalSettings settings(DEVICE_CONFIG_FILE);
        settings.beginGroup("EnCalibration");
        int fitType = 1;
        if (settings.contains(QString("Detector%1/pointsX").arg(detectorId)) == true)
        {
            settings.beginGroup(QString("Detector%1").arg(detectorId));
            //拟合参数赋值
            m_spectrumPlotSettings[detectorId-1].fitType = settings.value("type", 1).toInt();
            if (fitType == 1){
                //拟合参数赋值
                double k = settings.value("c0", 0.0).toDouble();
                double b = settings.value("c1", 0.0).toDouble();
                xMin = b;
                xMax = k*multiCh + b;

                //更新m_spectrumPlotSettings
                m_spectrumPlotSettings[detectorId-1].c0 = k;
                m_spectrumPlotSettings[detectorId-1].c1 = b;
            } else if (fitType == 2){
                //拟合参数赋值
                double a = settings.value("c0", 0.0).toDouble();
                double b = settings.value("c1", 0.0).toDouble();
                double c = settings.value("c2", 0.0).toDouble();
                xMin = c;
                xMax = a*multiCh*multiCh + b*multiCh + c;

                //更新m_spectrumPlotSettings
                m_spectrumPlotSettings[detectorId-1].c0 = a;
                m_spectrumPlotSettings[detectorId-1].c1 = b;
                m_spectrumPlotSettings[detectorId-1].c2 = c;
            }

            m_spectrumPlotSettings[detectorId-1].fitType = fitType;
            settings.endGroup();
        }

        settings.endGroup();

        //获取图表数据
        QCPGraph* graph = getGraph(detectorId, true);
        if (graph->dataCount() > 0){
            // 更新图表数据
            QVector<double> x, y;
            for (int i = 0; i < graph->dataCount(); ++i) {
                double key = graph->data()->at(i)->key;
                double value = graph->data()->at(i)->value;
                double newKey= 0.0;
                if (fitType == 1){
                    newKey = m_spectrumPlotSettings[detectorId-1].c0*key + m_spectrumPlotSettings[detectorId-1].c1;
                } else if (fitType == 2){
                    newKey = m_spectrumPlotSettings[detectorId-1].c0*key*key + m_spectrumPlotSettings[detectorId-1].c1*key + m_spectrumPlotSettings[detectorId-1].c2;
                }
                x << newKey;
                y << value;
            }
            graph->setData(x, y);
        }
    }
    else
    {
        m_spectrumPlotSettings[detectorId-1].xMin = xMin;
        m_spectrumPlotSettings[detectorId-1].xMax = xMax;
        //获取图表数据
        QCPGraph* graph = getGraph(detectorId, true);
        if (graph->dataCount() > 0){
            // 更新图表数据
            QVector<double> x, y;
            for (int i = 0; i < graph->dataCount(); ++i) {
                double key = graph->data()->at(i)->key;
                double value = graph->data()->at(i)->value;
                // 反能量刻度转换回道数
                double newKey;
                if (m_spectrumPlotSettings[detectorId-1].fitType == 1){
                    newKey = (key - m_spectrumPlotSettings[detectorId-1].c1) / m_spectrumPlotSettings[detectorId-1].c0;
                } else if (m_spectrumPlotSettings[detectorId-1].fitType == 2){
                    // 二次拟合反算道数，使用求根公式
                    double a = m_spectrumPlotSettings[detectorId-1].c0;
                    double b = m_spectrumPlotSettings[detectorId-1].c1;
                    double c = m_spectrumPlotSettings[detectorId-1].c2 - key;
                    double discriminant = b * b - 4 * a * c;
                    if (discriminant >= 0) {
                        double sqrtDiscriminant = sqrt(discriminant);
                        double root1 = (-b + sqrtDiscriminant) / (2 * a);
                        double root2 = (-b - sqrtDiscriminant) / (2 * a);
                        // 选择合理的根作为道数（通常是正数且在范围内）
                        if (root1 >= 0 && root1 <= m_spectrumPlotSettings[detectorId-1].multiChannel) {
                            newKey = root1;
                        } else if (root2 >= 0 && root2 <= m_spectrumPlotSettings[detectorId-1].multiChannel) {
                            newKey = root2;
                        } else {
                            newKey = key; // 如果没有合理的根，保持原值
                        }
                    } else {
                        newKey = key; // 如果判别式为负，保持原值
                    }
                }
                x << newKey;
                y << value;
            }
            graph->setData(x, y);
        }
    }
}

#include "particalwindow.h"
void MainWindow::on_action_particalMode_triggered()
{
    /*设置发次信息*/
    QString shotDir = ui->lineEdit_filePath->text();
    QString shotNumStr = ui->lineEdit_shotNum->text();
    if (shotDir.isEmpty()){
        shotDir = "./cache";
        ui->lineEdit_filePath->setText(shotDir);
    }

    // 保存测量数据
    QString savePath = QString(tr("%1/%2")).arg(shotDir).arg(shotNumStr);
    QDir dir(QString(tr("%1/%2")).arg(shotDir).arg(shotNumStr));
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    commHelper->setShotInformation(shotDir, shotNumStr);

    ParticalWindow *w = new ParticalWindow(nullptr);
    w->setAttribute(Qt::WA_DeleteOnClose, true); // 关闭时自动删除
    w->setWindowFlags(Qt::WindowCloseButtonHint|Qt::Dialog); // 只显示关闭按钮
    w->setWindowModality(Qt::ApplicationModal);//模态属性，NonModal=非模态，ApplicationModal=应用程序模态（阻塞本程序所有窗口），WindowModal=窗口模态（阻塞父窗口）
    w->show();
}


void MainWindow::on_action_countRateStatistics_triggered()
{
    QString program = QCoreApplication::applicationFilePath();
    QStringList arguments;
    arguments.append("-m");
    arguments.append("countRateStatistics");

    static int num = 0;
    arguments.append("-num");
    arguments.append(QString::number(num));
    num++;
    QProcess::startDetached(program, arguments);

    qInfo().nospace().noquote() << tr("打开离线数据分析程序-计数率统计");
}


void MainWindow::on_action_neutronYieldStatistics_triggered()
{
    QString program = QCoreApplication::applicationFilePath();
    QStringList arguments;
    arguments.append("-m");
    arguments.append("neutronYieldStatistics");

    static int num = 0;
    arguments.append("-num");
    arguments.append(QString::number(num));
    num++;
    QProcess::startDetached(program, arguments);

    qInfo().nospace().noquote() << tr("打开离线数据分析程序-中子产额统计");
}

#include "waveformwindow.h"
void MainWindow::on_action_waveformMode_triggered()
{
    /*设置发次信息*/
    QString shotDir = ui->lineEdit_filePath->text();
    QString shotNumStr = ui->lineEdit_shotNum->text();
    if (shotDir.isEmpty()){
        shotDir = "./cache";
        ui->lineEdit_filePath->setText(shotDir);
    }

    // 保存测量数据
    QString savePath = QString(tr("%1/%2")).arg(shotDir).arg(shotNumStr);
    QDir dir(QString(tr("%1/%2")).arg(shotDir).arg(shotNumStr));
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    commHelper->setShotInformation(shotDir, shotNumStr);

    WaveformWindow *w = new WaveformWindow(nullptr);
    w->setAttribute(Qt::WA_DeleteOnClose, true); // 关闭时自动删除
    w->setWindowFlags(Qt::WindowCloseButtonHint|Qt::Dialog); // 只显示关闭按钮
    w->setWindowModality(Qt::ApplicationModal);//模态属性，NonModal=非模态，ApplicationModal=应用程序模态（阻塞本程序所有窗口），WindowModal=窗口模态（阻塞父窗口）
    w->show();
}


void MainWindow::on_lineEdit_filePath_editingFinished()
{
    QString shotDir = ui->lineEdit_filePath->text();
    if (shotDir.isEmpty()){
        shotDir = "./cache";
        ui->lineEdit_filePath->setText(shotDir);
    }
}


void MainWindow::on_action_OTA_triggered()
{
    mOTAUpgradeWindow->show();
    mOTAUpgradeWindow->raise();
    mOTAUpgradeWindow->activateWindow();
}




