#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "qcustomplot.h"
#include "globalsettings.h"
#include "switchbutton.h"
#include <QTimer>
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

    // 连续测量计时器
    mContinueMeasureTimer = new QElapsedTimer();
    // 自动化测量计时器
    mAutoMeasureDelayTimer = new QTimer(this);
    mAutoMeasureDelayTimer->setInterval(1000); // 每秒触发一次
    connect(mAutoMeasureDelayTimer, &QTimer::timeout, this, [=](){
        QDateTime now = QDateTime::currentDateTime();
        quint32 coolTimelength = ui->spinBox_coolTimeLength->value();
        QDateTime delay = ui->dateTimeEdit_autoTrigger->dateTime().addSecs(coolTimelength-30); // 提前30秒做好准备工作
        if (now >= delay)
        {
            mAutoMeasureDelayTimer->stop();

            // 第一步 先启动服务
            if (!commHelper->isOpen())
            {
                emit ui->action_startServer->trigger();
            }

            // 第二步 先连接交换机
            commHelper->connectSwitcher(false);
        }
    });

    // 初始化测量倒计时定时器
    mMeasureCountdownTimer = new QTimer(this);
    mMeasureCountdownTimer->setSingleShot(false);
    mMeasureCountdownTimer->setInterval(1000); // 每秒触发一次
    connect(mMeasureCountdownTimer, &QTimer::timeout, this, &MainWindow::onMeasureCountdownTimeout);
    
    // 初始化连接按钮禁用定时器
    mConnectButtonDisableTimer = new QTimer(this);
    mConnectButtonDisableTimer->setSingleShot(true);
    mConnectButtonDisableTimer->setInterval(10000); // 10秒
    connect(mConnectButtonDisableTimer, &QTimer::timeout, this, [=](){
        // 定时器超时后，根据连接状态决定是否启用按钮
        if (!mSwitcherConnected) {
            qInfo().nospace() << tr("交换机连接失败");
            ui->action_connectSwitch->setEnabled(true);
        }
    });
    
    initUi();
    initNet();

    restoreSettings();
    applyColorTheme();

    connect(this, SIGNAL(sigWriteLog(const QString&,QtMsgType)), this, SLOT(slotWriteLog(const QString&,QtMsgType)));

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

    // 继电器
    connect(commHelper, &CommHelper::switcherConnected, this, [=](QString ip){
        mSwitcherConnected = true;
<<<<<<< HEAD
        qInfo().nospace().noquote() << tr("交换机[%1]已连通").arg(ip);
=======
        qInfo().nospace().noquote() << tr("交换机[ %1 ]已连通").arg(ip);
>>>>>>> 26822851222f1b350478b3dc690d0d41742dfecc
    });

    connect(commHelper, &CommHelper::switcherLogged, this, [=](QString ip){
        // QLabel* label_Connected = this->findChild<QLabel*>("label_Connected");
        // label_Connected->setStyleSheet("color:#00ff00;");
        // label_Connected->setText(tr("交换机[%1]已连通").arg(ip));
        mSwitcherLogged = true;
        updateConnectButtonState(true);
        ui->action_powerOn->setEnabled(true);
        ui->action_powerOff->setEnabled(true);
<<<<<<< HEAD
        qInfo().nospace().noquote() << tr("交换机[%1]已登录").arg(ip);
=======
        qInfo().nospace().noquote() << tr("交换机[ %1 ]已登录").arg(ip);
>>>>>>> 26822851222f1b350478b3dc690d0d41742dfecc

        /// 再给设备上电
        if (mEnableAutoMeasure)
        {
            commHelper->openPower();

            // 第三步 30s之后开始自动测量
            ui->action_stopMeasure->setEnabled(false);            
            QTimer::singleShot(30000, this, [=](){
                ui->action_stopMeasure->setEnabled(true);

                /// 开始测量
                startMeasure();
            });
        }
    });
    connect(commHelper, &CommHelper::switcherDisconnected, this, [=](QString ip){
        mSwitcherConnected = false;
        updateConnectButtonState(false);
        ui->action_powerOn->setEnabled(false);
        ui->action_powerOff->setEnabled(false);
        qCritical().nospace().noquote() << tr("交换机[ %1 ]连接已断开").arg(ip);
    });

    // 探测器
    connect(commHelper, &CommHelper::detectorOnline, this, [=](quint8 index){
        int row = index;
        QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 1));
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::green));
        //记录联网的探测器ID
        mOnlineDetectors.append(index);
        qInfo().nospace() << "谱仪[#" << index << "]在线";
        //如果该探测器在温度超时报警列表中，则删除
        if (mTemperatureTimeoutDetectors.contains(index)){
            mTemperatureTimeoutDetectors.removeOne(index);
            //如果该探测器正在测量，则重新开始测量
            if (mDetectorMeasuring[index]){
                commHelper->startMeasure(CommandAdapter::WorkMode::wmSpectrum, index);
                //打印日志
                qInfo().nospace() << "谱仪[#" << index << "]自动重新开始测量";
            }
        }
    });

    connect(commHelper, &CommHelper::detectorOffline, this, [=](quint8 index){
        int row = index;
        QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 1));
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::red));
        cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 2));
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::red));

        // 探测器离线时，清除测量状态记录
        // mDetectorMeasuring[index] = false;
        // mDetectorMeasuring.remove(index);
        //清除联网的探测器ID
        if (mOnlineDetectors.contains(index)){
            mOnlineDetectors.removeOne(index);
            qInfo().nospace() << "谱仪[#" << index << "]离线";
        }
    });

    //测量开始
    connect(commHelper, &CommHelper::measureStart, this, [=](quint8 index){
        int row = index;
        QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 2));
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::green));

        // 重置该探测器的能谱数据
        resetDetectorSpectrum(index);

        // 记录探测器正在测量
        mDetectorMeasuring[index] = true;

        qInfo().nospace() << "谱仪[#" << index << "]测量已启动，准备接收数据";//开始实验，准备接收数据
    });

    //测量结束
    connect(commHelper, &CommHelper::measureStop, this, [=](quint8 index){
        int row = index;
        QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 2));
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::red));

        qInfo().nospace() << "谱仪[#" << index << "]测量已停止";
    });

    //POE电源状态
    connect(commHelper, &CommHelper::reportPoePowerStatus, this, [=](quint8 index, bool on){
        int row = index;
        SwitchButton* cell =  qobject_cast<SwitchButton*>(ui->tableWidget_detector->cellWidget(row, 0));
        qInfo().nospace() << "谱仪[#" << index << "]POE电源状态: " << (on ? "开" : "关");
        cell->setChecked(on);
    });

    connect(ui->statusbar,&QStatusBar::messageChanged,this,[&](const QString &message){
        if(message.isEmpty()) {
            ui->statusbar->showMessage(tr("准备就绪"));
        } else {
            ui->statusbar->showMessage(message);
        }
    });
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
    GlobalSettings settings(CONFIG_FILENAME);
    // 保存界面设置
    settings.setValue("mainWindow/TriggerMode", ui->com_triggerModel->currentIndex());
    settings.setValue("mainWindow/ShotNumIsAutoIncrease", ui->checkBox_autoIncrease->isChecked());
    settings.setValue("mainWindow/ShotNumStr", ui->lineEdit_shotNum->text());
    settings.setValue("mainWindow/ShotDir", ui->lineEdit_filePath->text());
    settings.setValue("mainWindow/MeasureTime", ui->spinBox_measureTime->value());

    GlobalSettings settingsGlobal;
    QSplitter *splitterH1 = this->findChild<QSplitter*>("splitterH1");// QSplitter(Qt::Horizontal,this);
    if (splitterH1){
        settingsGlobal.setValue("Global/splitterH1/State", splitterH1->saveState());
        settingsGlobal.setValue("Global/splitterH1/Geometry", splitterH1->saveGeometry());
    }

    QSplitter *splitterV2 = this->findChild<QSplitter*>("splitterV2");
    if (splitterV2){
        settingsGlobal.setValue("Global/splitterV2/State", splitterV2->saveState());
        settingsGlobal.setValue("Global/splitterV2/Geometry", splitterV2->saveGeometry());
    }

    settingsGlobal.setValue("Global/MainWindows/State", this->saveState());
    delete ui;
}

void MainWindow::initUi()
{
    // 自动测量
    ui->dateTimeEdit_autoTrigger->setDateTime(QDateTime::currentDateTime().addSecs(180));
    emit ui->cbb_measureMode->activated(0);

    // 谱仪最大化按钮
    auto titleClicked = ([=](){
        QToolButton* btn = qobject_cast<QToolButton*>(sender());
        QString name = btn->objectName();
        int index = name.right(1).toInt();
        QWidget* spectroMeterWidget = this->findChild<QWidget*>(QString("spectroMeterWidget%1").arg(index));
        if (spectroMeterWidget->property("isZoomIn").toBool())
        {
            this->setProperty("isZoomIn", false);
            spectroMeterWidget->setProperty("isZoomIn", false);
            btn->setIcon(QIcon(":/zoom_in.png"));
            for (int i=1; i<=2; ++i)
            {
                spectroMeterWidget = this->findChild<QWidget*>(QString("spectroMeterWidget%1").arg(i));
                spectroMeterWidget->show();
            }
        }
        else
        {
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
        GlobalSettings settings(CONFIG_FILENAME);
        HDF5Settings::instance()->setPluseTime(settings.value("Global/PluseTime", 10).toUInt());
        settings.beginGroup("mainWindow");
        bool flag = settings.value("ShotNumIsAutoIncrease", true).toBool();
        ui->checkBox_autoIncrease->setChecked(settings.value("ShotNumIsAutoIncrease", true).toBool());
        ui->lineEdit_shotNum->setText(settings.value("ShotNumStr", "000").toString());
        ui->lineEdit_filePath->setText(settings.value("ShotDir", "./cache").toString());
        if (!QDir(ui->lineEdit_filePath->text()).exists())
            ui->lineEdit_filePath->setText("./cache");
        ui->spinBox_measureTime->setValue(settings.value("MeasureTime", 10).toInt());
        // 触发模式
        int triggerMode = settings.value("TriggerMode", 0).toInt();
        ui->com_triggerModel->setCurrentIndex(triggerMode); // 或者从配置文件中读取
        onTriggerModelChanged(triggerMode);
        settings.endGroup();
    }

    connect(ui->com_triggerModel, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onTriggerModelChanged);

    // 创建上线客户端页面
    mClientPeersWindow = new ClientPeersWindow();
    // mClientPeersWindow->setAttribute(Qt::WA_TranslucentBackground);
    mClientPeersWindow->setWindowFlags(Qt::Widget | Qt::WindowStaysOnTopHint);
    mClientPeersWindow->hide();
    connect(commHelper, &CommHelper::connectPeerConnection, mClientPeersWindow, &ClientPeersWindow::connectPeerConnection);
    connect(commHelper, &CommHelper::disconnectPeerConnection, mClientPeersWindow, &ClientPeersWindow::disconnectPeerConnection);

    mDetSettingWindow = new DetSettingWindow();
    mDetSettingWindow->setWindowFlags(Qt::Widget | Qt::WindowStaysOnTopHint);
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

        if (mContinueMeasureTimer && mContinueMeasureTimer->isValid())
        {
            ui->edit_measureTime->setText(formatTimeString(mContinueMeasureTimer->elapsed()));
        }
    });
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

    ui->tableWidget_detector->verticalHeader()->setDefaultSectionSize(25); // 设置默认行高
    ui->tableWidget_detector->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed); // 固定高度
    ui->tableWidget_detector->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget_detector->horizontalHeader()->setFixedHeight(25);
    // for (int i=0; i<25; ++i)
    //      ui->tableWidget_detector->setRowHeight(i, 25);
    ui->tableWidget_detector->setRowHeight(0, 50);
    ui->tableWidget_detector->setFixedHeight(677);
    ui->tableWidget_detector->verticalHeaderItem(0)->setText("时间同步\n触发模块");

    for (int row=0; row<ui->tableWidget_detector->rowCount(); ++row){
        for (int column=0; column<=3; ++column){
            if (column == 0){
                SwitchButton* cell = new SwitchButton();
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
                        commHelper->openSwitcherPOEPower(row);
                    }
                    else
                    {
                        if (!cell->getChecked()){
                            if (commHelper->openSwitcherPOEPower(row))
                            {
                                commHelper->manualOpenSwitcherPOEPower(row);
                                cell->setChecked(true);
                                //打印日志
                                qInfo().nospace() << "手动打开探测器" << row << "的POE供电";
                            }
                        }
                        else{
                            if (commHelper->closeSwitcherPOEPower(row))
                            {
                                commHelper->manualCloseSwitcherPOEPower(row);
                                cell->setChecked(false);
                                //打印日志
                                qInfo().nospace() << "手动关闭探测器" << row << "的POE供电";
                            }
                        }
                    }
                });
            }
            else {
                QLabel* cell = new QLabel();
                cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::gray));
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
            GlobalSettings settings(CONFIG_FILENAME);
            settings.setValue("Global/CacheDir", cacheDir);
            ui->lineEdit_filePath->setText(cacheDir);
        }
    });

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

    auto getRandomColor = []() {
        static std::mt19937 engine(std::chrono::system_clock::now().time_since_epoch().count());
        std::uniform_int_distribution<int> distR(0, 255); // 红色通道高值
        std::uniform_int_distribution<int> distG(0, 255);  // 绿色通道中值
        std::uniform_int_distribution<int> distB(0, 255);   // 蓝色通道低值
        return QColor(distR(engine), distG(engine), distB(engine));
    };
    mGraphisColor.push_back(QColor::fromRgb(0,47,167));
    mGraphisColor.push_back(QColor::fromRgb(10,186,181));
    mGraphisColor.push_back(QColor::fromRgb(5,75,58));
    mGraphisColor.push_back(QColor::fromRgb(129,216,208));
    mGraphisColor.push_back(QColor::fromRgb(0,140,141));
    mGraphisColor.push_back(QColor::fromRgb(255,0,77));
    //mGraphisColor.push_back(QColor::fromRgb(0,47,167));
    //mGraphisColor.push_back(QColor::fromRgb(70,40,20));
    mGraphisColor.push_back(QColor::fromRgb(128,0,32));
    mGraphisColor.push_back(QColor::fromRgb(142,76,41));
    mGraphisColor.push_back(QColor::fromRgb(205,0,0));
    mGraphisColor.push_back(QColor::fromRgb(255,204,51));
    mGraphisColor.push_back(QColor::fromRgb(0,102,128));
    mGraphisColor.push_back(QColor::fromRgb(32,88,39));
    // for (int i=0; i<12; ++i)
    //     mGraphisColor.push_back(getRandomColor());

    for (int i=1; i<=2; ++i){
        QCustomPlot *spectroMeter_top = this->findChild<QCustomPlot*>(QString("spectroMeter%1_top").arg(i));
        initCustomPlot(i, spectroMeter_top, tr(""), tr("时间/s 计数率/cps"), tr("计数曲线"), 12);
        QCustomPlot *spectroMeter_bottom = this->findChild<QCustomPlot*>(QString("spectroMeter%1_bottom").arg(i));
        initCustomPlot(i, spectroMeter_bottom, tr(""), tr("道址 计数"), tr("累积能谱"), 12);
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
        QGridLayout* layoutRight = new QGridLayout(ui->widget_left);
        layoutRight->setSpacing(5);
        layoutRight->setContentsMargins(9,3,9,3);
        ui->widget_left->setLayout(layoutLeft);
        ui->widget_right->setLayout(layoutRight);

        for (int index=1; index<=24; ++index)
        {
            QCheckBox* checkBox = new QCheckBox(index==1 ? ui->widget_left : ui->widget_right);
            checkBox->setText(QLatin1String("")+QString::number(index));
            checkBox->setObjectName(QLatin1String("CH ")+QString::number(index));
            checkBox->setIcon(QIcon(roundPixmap(QSize(16, 16), mGraphisColor.at((index-1) % 12))));
            checkBox->setProperty("index", index);
            checkBox->setChecked(true);
            connect(checkBox, &QCheckBox::stateChanged, this, [=](int state){
                int index = checkBox->property("index").toInt();

                // 能谱
                QCPGraph *graph = getGraph(index % 12);
                if (graph){
                    graph->setVisible(Qt::CheckState::Checked == state ? true : false);
                    getCustomPlot(index % 12)->replot();
                }

                // 计数率
                graph = getGraph(index % 12, false);
                if (graph){
                    graph->setVisible(Qt::CheckState::Checked == state ? true : false);
                    getCustomPlot(index % 12, false)->replot();
                }
            });

            if (index<=12)
            {
                layoutLeft->addWidget(checkBox, index<=6 ? 0 : 1, (index-1) % 6);
            }
            else
            {
                layoutRight->addWidget(checkBox, index<=18 ? 0 : 1, (index-1) % 6);
            }
        }

        layoutLeft->addWidget(ui->toolButton_title_1, 0, 7);
        layoutRight->addWidget(ui->toolButton_title_2, 0, 7);
    }

    //更新温度状态
    connect(commHelper, &CommHelper::reportDetectorTemperature, this, [=](quint8 index, float temperature){
        int row = index;
        QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 3));
        cell->setText(QString::number(temperature, 'f', 1) + " ℃");
        
        //将温度数据保存到文件中，实时写入，文件名称为探测器编号-日期.txt，路径为应用程序目录下的temperature文件夹中
        QString temperatureFilePath = QApplication::applicationDirPath() + "/temperature/" + QString::number(index) + "-" + QDateTime::currentDateTime().toString("yyyy-MM-dd") + ".txt";
        QFile temperatureFile(temperatureFilePath);
        if (!temperatureFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
            qWarning() << "Failed to open temperature file:" << temperatureFilePath;
            return;
        }
        QTextStream temperatureStream(&temperatureFile);
        temperatureStream << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "," << QString::number(temperature, 'f', 1) << endl;
        temperatureFile.close();

        if (temperature > 70.0) {
            cell->setStyleSheet("color: red;");
        } else {
            cell->setStyleSheet("color: green;");
        }
    }, Qt::QueuedConnection);
    
    connect(commHelper, &CommHelper::reportTemperatureTimeout, this, [=](quint8 index){
        QLabel* cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(index, 3));
        cell->setStyleSheet("color: gray;");//灰色字体
        mTemperatureTimeoutDetectors.append(index);
    }, Qt::QueuedConnection);

    // 接收完整的能谱数据
    connect(commHelper, &CommHelper::reportFullSpectrum, this, [=](quint8 index, const FullSpectrum& fullSpectrum){ 
        if (index < 1 || index > 24) {
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

        //记录累积计数率
        data.lastAccumulateCount += currentCount;

        // 更新能谱显示
        updateSpectrumDisplay(index, data.spectrum);
        
        //最快每秒更新一次计数率,这里不考虑丢包带来的计数率修复
        quint32 accuTime = (fullSpectrum.sequence - data.lastSpectrumID) * fullSpectrum.measureTime;//单位ms
        double currenTime = 1.0 * fullSpectrum.sequence * fullSpectrum.measureTime / 1000; //单位s，这里最大数对应2^32/24/3600=49days
        if(accuTime >= 1000) {
            double countRate = 1000.0*data.lastAccumulateCount * 1.0 / accuTime; //cps
            data.lastSpectrumID = fullSpectrum.sequence;
            data.lastAccumulateCount = 0;
            data.countRateHistory.append(countRate);
            // 更新计数率显示
            updateCountRateDisplay(index, currenTime, countRate);
        }
    });
    
    // 接收波形曲线数据
    connect(commHelper, &CommHelper::reportWaveformCurveData, this, [=](quint8 index, QVector<quint16>& data){
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
        //customPlot->yAxis->setRange(0, 10000);
        customPlot->replot(QCustomPlot::rpQueuedReplot);
    });

    // 接收时间戳属性据
    connect(commHelper, &CommHelper::reportTimestampe, this, [=](quint8 index, QDateTime& tm){
        ui->dateTimeEdit_autoTrigger->setDateTime(tm);
    });

    // QTimer *timer = new QTimer(this);
    // static QElapsedTimer elapsedTimer;
    // elapsedTimer.start();
    // connect(timer, &QTimer::timeout, this, [=](){
    //     if (elapsedTimer.elapsed() > 10000)
    //         return;

    //     static quint64 base = 0;
    //     quint16 waveformLength = 512;
    //     quint16 period = 128;  //周期
    //     quint16 amplitude = 10000; // 振幅
    //     quint16 grain = 1;    //粒度
    //     QVector<double> x2, y2;
    //     for(quint64 x=base; x < base + waveformLength; x += grain)
    //     {
    //         double angle = (float) x / period * 2 * 3.1415926;
    //         quint16 data = (double)amplitude * sin(angle) + amplitude;

    //         QByteArray waveformBytes;
    //         waveformBytes.push_back(quint8((data >> 8) & 0xFF));
    //         waveformBytes.push_back(quint8(data & 0xFF));

    //         x2 << x - base;
    //         //y2 << data;
    //         bool ok;
    //         y2 << waveformBytes.mid(0, 2).toHex().toUShort(&ok, 16);
    //     }

    //     base++;
    //     ui->spectroMeter1_top->graph(0)->setData(x2, y2);
    //     ui->spectroMeter1_top->xAxis->rescale(true);
    //     ui->spectroMeter1_top->yAxis->rescale(true);
    //     //ui->spectroMeter1_top->yAxis->setRange(0, 4100);
    //     ui->spectroMeter1_top->replot(QCustomPlot::rpQueuedReplot);

    //     qDebug() << "elapsedTimer:" << elapsedTimer.elapsed() << base;
    // });
    // timer->start(10);
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
    //customPlot->installEventFilter(this);
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
        graph->setPen(mGraphisColor.at(i % 12));
        //graph->selectionDecorator()->setPen(QPen(getRandomColor()));
        graph->setLineStyle(QCPGraph::lsLine);
        graph->setSelectable(QCP::SelectionType::stNone);
        //graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, colors[i], 2));//显示散点图
        //graph->setSmooth(true);
    }

    // if (graphCount == 1){
    //     QSharedPointer<QCPAxisTickerLog> logTicker(new QCPAxisTickerLog);
    //     customPlot->yAxis->setTicker(logTicker);
    //     //customPlot->yAxis2->setTicker(logTicker);
    //     customPlot->yAxis->setScaleType(QCPAxis::ScaleType::stLogarithmic);
    //     customPlot->yAxis->setNumberFormat("eb");//使用科学计数法表示刻度
    //     customPlot->yAxis->setNumberPrecision(0);//小数点后面小数位数

    //     customPlot->yAxis2->setTicker(logTicker);
    //     customPlot->yAxis2->setScaleType(QCPAxis::ScaleType::stLogarithmic);
    //     customPlot->yAxis2->setNumberFormat("eb");//使用科学计数法表示刻度
    //     customPlot->yAxis2->setNumberPrecision(0);//小数点后面小数位数
    // }
    // else {
    //     QSharedPointer<QCPAxisTicker> ticker(new QCPAxisTicker);
    //     customPlot->yAxis->setTicker(ticker);
    // }

    customPlot->replot();
    connect(customPlot->xAxis, SIGNAL(rangeChanged(const QCPRange &)), customPlot->xAxis2, SLOT(setRange(const QCPRange &)));
    connect(customPlot->yAxis, SIGNAL(rangeChanged(const QCPRange &)), customPlot->yAxis2, SLOT(setRange(const QCPRange &)));


    // 是否允许X轴自适应缩放
    connect(customPlot, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(slotShowTracer(QMouseEvent*)));
    connect(customPlot, SIGNAL(mouseRelease(QMouseEvent*)), this, SLOT(slotRestorePlot(QMouseEvent*)));
    //connect(customPlot, SIGNAL(mouseWheel(QWheelEvent*)), this, SLOT(slotRestorePlot(QWheelEvent*)));
    //connect(customPlot, SIGNAL(mouseMove(QMouseEvent*)), this,SLOT(slotShowTracer(QMouseEvent*)));
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (mIsMeasuring){
        QMessageBox::information(this, tr("系统退出提示"), tr("测量中禁止退出软件系统！"),
                                             QMessageBox::Ok, QMessageBox::Ok);
        event->ignore();
    }
    else
    {
        // 断开网络
        commHelper->stopMeasure();
        commHelper->stopServer();
        event->accept();
    }
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


void MainWindow::on_action_cfgParam_triggered()
{
    mDetSettingWindow->show();
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
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::gray));
        cell =  qobject_cast<QLabel*>(ui->tableWidget_detector->cellWidget(row, 2));
        cell->setPixmap(dblroundPixmap(QSize(20,20), Qt::gray));
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
}

/**
 * @brief MainWindow::onTriggerModelChanged 触发模式选择：软件触发/外触发
 * @param index 下拉框序号，触发类型
 */
void MainWindow::onTriggerModelChanged(int index)
{
    switch(index) {
    case 0: // 软件触发
        ui->label_7->setVisible(false);
        ui->lineEdit_triggerflag->setVisible(false);
        ui->dateTime_shotTime->setEnabled(true);
        break;
    case 1: // 外触发
        ui->label_7->setVisible(true);
        ui->lineEdit_triggerflag->setVisible(true);
        ui->dateTime_shotTime->setEnabled(false);
        break;
    default:
        break;
    }

    // 实时保存触发模式
    GlobalSettings settings;
    settings.setValue("mainWindow/TriggerMode", index);
}

// 开始测量 全部通道测量
void MainWindow::on_action_startMeasure_triggered()
{
    ui->action_startMeasure->setEnabled(false);
    ui->action_stopMeasure->setEnabled(true);
    ui->dateTime_shotTime->setEnabled(false);
    ui->com_triggerModel->setEnabled(false);
    ui->lineEdit_shotNum->setEnabled(false);
    ui->checkBox_autoIncrease->setEnabled(false);
    ui->spinBox_measureTime->setEnabled(false);
    ui->checkBox_continueMeasure->setEnabled(false);
    ui->lineEdit_filePath->setEnabled(false);
    ui->dateTimeEdit_autoTrigger->setEnabled(false);
    ui->cbb_measureMode->setEnabled(false);
    ui->pushButton_startMeasure->setEnabled(false);
<<<<<<< HEAD
    ui->pushButton_stopMeasure->setEnabled(true);
    ui->action_waveformMode->setEnabled(false);
    ui->action_particalMode->setEnabled(false);
=======
    ui->pushButton_stopMeasure->setEnabled(false);
>>>>>>> 26822851222f1b350478b3dc690d0d41742dfecc

    updateSpectrumPlotSettings();

    mEnableAutoMeasure = ui->cbb_measureMode->currentIndex()==1;

    if (mEnableAutoMeasure)
    {
        qInfo().nospace().noquote() << tr("开启自动测量");
    }
    else
    {
        qInfo().nospace().noquote() << tr("开启手动测量");
    }

    QVector<double> keys, values;
    for (int i=1; i<=2; ++i){
        QCustomPlot *spectroMeter_top = this->findChild<QCustomPlot*>(QString("spectroMeter%1_top").arg(i));
        for (int j=0; j<spectroMeter_top->graphCount(); ++j)
            spectroMeter_top->graph(j)->data()->clear();

        QCustomPlot *spectroMeter_bottom = this->findChild<QCustomPlot*>(QString("spectroMeter%1_bottom").arg(i));
        for (int j=0; j<spectroMeter_bottom->graphCount(); ++j)
            spectroMeter_bottom->graph(j)->data()->clear();

        spectroMeter_top->replot();
        spectroMeter_bottom->replot();
    }

    for (auto index : mOnlineDetectors){
        if (index != 0)
            m_selectedChannels.append(index);
    }

    if (mEnableAutoMeasure)
    {
        mAutoMeasureDelayTimer->start();
    }
    else
    {
        // 开始测量
        startMeasure();
    }
}

//单通道测量，单独测量某个通道
void MainWindow::on_pushButton_startMeasure_clicked()
{
    /*设置发次信息*/
    QString shotDir = ui->lineEdit_filePath->text();
    QString shotNumStr = ui->lineEdit_shotNum->text();

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

    {
        GlobalSettings settings(QString("%1/Settings.ini").arg(savePath));
        settings.setValue("mainWindow/TriggerMode", ui->com_triggerModel->currentText());
        settings.setValue("mainWindow/ShotNumStr", shotNumStr);
        settings.setValue("mainWindow/MeasureTimeSeconds(s)", ui->spinBox_measureTime->value());
    }

    {
        GlobalSettings settings(CONFIG_FILENAME);
        settings.setValue("mainWindow/ShotNumStr", shotNumStr);
        settings.setValue("mainWindow/CacheDir", ui->lineEdit_filePath->text());
    }

    commHelper->setShotInformation(shotDir, shotNumStr);
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
        qInfo()<<"自定义通道测量，波形测量模式";
        QString str = QString("开始测量，通道号：");
        for (const auto &mi : validIndexes) {
            int row = mi.row();
            if (0 == row)
                continue;

            quint8 index = row;

            if (m_selectedChannels.contains(index))
                continue;

            // 先清空曲线内容
            {
                QCustomPlot* spectroMeter_bottom = getCustomPlot(index, true);
                QCPGraph* graph = getGraph(index, true);
                graph->data()->clear();
                spectroMeter_bottom->replot();
            }
            {
                QCustomPlot* spectroMeter_top = getCustomPlot(index, false);
                QCPGraph* graph = getGraph(index, false);
                graph->data()->clear();
                spectroMeter_top->replot();
            }

            // 再发送开始测量指令
            if (mOnlineDetectors.contains(index))
            {
                // 启动连续测量计时器
                if (!mContinueMeasureTimer->isValid())
                    mContinueMeasureTimer->start();

                commHelper->startMeasure(CommandAdapter::WorkMode::wmWaveform, index);
                //记录下所选的通道号，保存到成员变量中，停止测量的时候使用并清空
                m_selectedChannels.append(index);
            }
        }
    }
    else if (ui->action_spectrumMode->isChecked())
    {
        qInfo()<<"自定义通道测量，能谱测量模式";
        QString str = QString("开始测量，通道号：");
        for (const auto &mi : validIndexes) {
            int row = mi.row();
            if (0 == row)
                continue;

            quint8 index = row;

            if (m_selectedChannels.contains(index))
                continue;

            // 先清空曲线内容
            {
                QCustomPlot* spectroMeter_bottom = getCustomPlot(index, true);
                QCPGraph* graph = getGraph(index, true);
                graph->data()->clear();
                spectroMeter_bottom->replot();
            }
            {
                QCustomPlot* spectroMeter_top = getCustomPlot(index, false);
                QCPGraph* graph = getGraph(index, false);
                graph->data()->clear();
                spectroMeter_top->replot();
            }

            // 再发送开始测量指令
            if (mOnlineDetectors.contains(index))
            {
                commHelper->startMeasure(CommandAdapter::WorkMode::wmSpectrum, index);
                //记录下所选的通道号，保存到成员变量中，停止测量的时候使用并清空
                m_selectedChannels.append(index);
            }
        }
    }

    // 测量中禁用参数配置
    mDetSettingWindow->setEnabled(false);

<<<<<<< HEAD
    //ui->action_startMeasure->setEnabled(false);
    ui->action_stopMeasure->setEnabled(true);
    ui->action_waveformMode->setEnabled(false);
    ui->action_particalMode->setEnabled(false);
=======
    ui->action_startMeasure->setEnabled(false);
    ui->action_stopMeasure->setEnabled(false);
>>>>>>> 26822851222f1b350478b3dc690d0d41742dfecc
}


//（自定义通道）停止测量，停止测量已选中的通道
void MainWindow::on_pushButton_stopMeasure_clicked()
{
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
    qInfo()<<"点击(自定义通道)停止测量";

    for (const auto &mi : validIndexes) {
        int row = mi.row();
        if (0 == row)
            continue;

<<<<<<< HEAD
        quint8 index = row;
        commHelper->stopMeasure(index);
        m_selectedChannels.removeOne(index);
    }

    if (m_selectedChannels.empty()){
        // 非测量中可用参数配置
        mDetSettingWindow->setEnabled(true);

        ui->action_startMeasure->setEnabled(true);
        ui->action_stopMeasure->setEnabled(false);

        ui->pushButton_stopMeasure->setEnabled(false);
        ui->action_waveformMode->setEnabled(true);
        ui->action_particalMode->setEnabled(true);
    }

    ui->pushButton_startMeasure->setEnabled(true);
=======
    // 非测量中可用参数配置
    mDetSettingWindow->setEnabled(true);

    ui->action_startMeasure->setEnabled(true);
    ui->action_stopMeasure->setEnabled(true);
>>>>>>> 26822851222f1b350478b3dc690d0d41742dfecc
}


void MainWindow::on_action_stopMeasure_triggered()
{
    ui->action_startMeasure->setEnabled(true);
    ui->action_stopMeasure->setEnabled(false);
    ui->dateTime_shotTime->setEnabled(true);
    ui->com_triggerModel->setEnabled(true);
    ui->lineEdit_shotNum->setEnabled(true);
    ui->checkBox_autoIncrease->setEnabled(true);
    ui->spinBox_measureTime->setEnabled(true);
    ui->checkBox_continueMeasure->setEnabled(true);
    ui->lineEdit_filePath->setEnabled(true);
    ui->dateTimeEdit_autoTrigger->setEnabled(true);
    ui->cbb_measureMode->setEnabled(true);
    ui->pushButton_startMeasure->setEnabled(true);
<<<<<<< HEAD
    ui->pushButton_stopMeasure->setEnabled(false);
    ui->action_waveformMode->setEnabled(true);
    ui->action_particalMode->setEnabled(true);
=======
    ui->pushButton_stopMeasure->setEnabled(true);
>>>>>>> 26822851222f1b350478b3dc690d0d41742dfecc

    if (mEnableAutoMeasure)
    {
        qInfo().nospace().noquote() << tr("人工停止自动测量");
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
}

void MainWindow::onMeasureCountdownTimeout()
{
    mRemainingCountdown--;
    
    // 计算已测量时长（总时长 - 剩余倒计时）
    int elapsedSeconds = mTotalCountdown - mRemainingCountdown;
    // 更新测量时长显示
    ui->edit_measureTime->setText(formatTimeString(elapsedSeconds));
    
    if (mRemainingCountdown > 0) {
        // 倒计时进行中，可以在这里更新UI显示剩余时间
        // qInfo().nospace() << QString("测量倒计时剩余：%1 秒").arg(mRemainingCountdown);
    } else {
        // 倒计时结束
        mMeasureCountdownTimer->stop();
        // 显示最终测量时长
        ui->edit_measureTime->setText(formatTimeString(mTotalCountdown));

        // 自动更新发次号
        if (ui->checkBox_autoIncrease->isChecked()){
            ui->lineEdit_shotNum->setText(increaseShotNumSuffix(ui->lineEdit_shotNum->text().trimmed()));
            GlobalSettings settings(CONFIG_FILENAME);
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

        //清空温度超时报警的探测器ID
        mTemperatureTimeoutDetectors.clear();
        //清空探测器正在测量记录
        mDetectorMeasuring.clear();
        
        // ui->action_startServer->setEnabled(true);
        // ui->action_stopServer->setEnabled(false);

        if (mEnableAutoMeasure)
        {
            ui->action_startMeasure->setEnabled(false);
            ui->action_stopMeasure->setEnabled(false);
            ui->pushButton_startMeasure->setEnabled(false);
            ui->pushButton_stopMeasure->setEnabled(false);
            ui->dateTime_shotTime->setEnabled(true);
            ui->com_triggerModel->setEnabled(true);
            ui->lineEdit_shotNum->setEnabled(true);
            ui->checkBox_autoIncrease->setEnabled(true);
            ui->spinBox_measureTime->setEnabled(true);
            ui->checkBox_continueMeasure->setEnabled(true);
            ui->lineEdit_filePath->setEnabled(true);
            ui->dateTimeEdit_autoTrigger->setEnabled(true);
            ui->cbb_measureMode->setEnabled(true);
            ui->action_connectSwitch->setEnabled(false);
        }
        else
        {
            ui->action_startMeasure->setEnabled(true);
            ui->action_stopMeasure->setEnabled(false);
            ui->pushButton_startMeasure->setEnabled(true);
            ui->pushButton_stopMeasure->setEnabled(false);
            ui->dateTime_shotTime->setEnabled(true);
            ui->com_triggerModel->setEnabled(true);
            ui->lineEdit_shotNum->setEnabled(true);
            ui->checkBox_autoIncrease->setEnabled(true);
            ui->spinBox_measureTime->setEnabled(true);
            ui->checkBox_continueMeasure->setEnabled(true);
            ui->lineEdit_filePath->setEnabled(true);
            ui->dateTimeEdit_autoTrigger->setEnabled(true);
            ui->cbb_measureMode->setEnabled(true);
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
    GlobalSettings settingsGlobal;
    settingsGlobal.setValue("Global/Startup/darkTheme","false");
    applyColorTheme();
}


void MainWindow::on_action_darkTheme_triggered()
{
    if(mIsDarkTheme) return;
    mIsDarkTheme = true;
    qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    if(mThemeColorEnable) QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
    GlobalSettings settingsGlobal;
    settingsGlobal.setValue("Global/Startup/darkTheme","true");
    applyColorTheme();
}


void MainWindow::on_action_colorTheme_triggered()
{
    GlobalSettings settingsGlobal;
    QColor color = QColorDialog::getColor(mThemeColor, this, tr("选择颜色"));
    if (color.isValid()) {
        mThemeColor = color;
        mThemeColorEnable = true;
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
        QGoodWindow::setAppCustomTheme(mIsDarkTheme,mThemeColor);
        settingsGlobal.setValue("Global/Startup/themeColor",mThemeColor);
    } else {
        mThemeColorEnable = false;
        qGoodStateHolder->setCurrentThemeDark(mIsDarkTheme);
    }
    settingsGlobal.setValue("Global/Startup/themeColorEnable",mThemeColorEnable);
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
    GlobalSettings settingsGlobal;
    if(mainWindow) {
        mainWindow->restoreGeometry(settingsGlobal.value("MainWindow/Geometry").toByteArray());
        mainWindow->restoreState(settingsGlobal.value("MainWindow/State").toByteArray());
    } else {
        restoreGeometry(settingsGlobal.value("MainWindow/Geometry").toByteArray());
        restoreState(settingsGlobal.value("MainWindow/State").toByteArray());
    }
    mThemeColor = settingsGlobal.value("Global/Startup/themeColor",QColor(30,30,30)).value<QColor>();
    mThemeColorEnable = settingsGlobal.value("Global/Startup/themeColorEnable",true).toBool();
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
    for (int i = 0; i < 8192; ++i)
        it->spectrum[i] = 0;

    // 清空上次测量累积时间的能谱序号
    it->lastSpectrumID = 0;
    // 清空上次测量累积时间的计数率
    it->lastAccumulateCount = 0;
    
    // 清空计数率历史并释放内存
    QVector<double>().swap(it->countRateHistory);
    // 如果是 QList<double>，同样写法：QList<double>().swap(it->countRateHistory);

    qInfo() << "Detector" << detectorId << "spectrum and countRateHistory reset";
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

// 更新能谱、计数率显示
void MainWindow::updateSpectrumDisplay(int detectorId, const quint32 spectrum[]) {
    // 计算在页面中的索引
    QCustomPlot *customPlot = getCustomPlot(detectorId, true);
    if (!customPlot)
        return;

    QVector<double> x, y;

    // 转换数据格式
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

    // 更新图表
    getGraph(detectorId, true)->setData(x, y);

    customPlot->yAxis->rescale(false);
    double y_min = customPlot->yAxis->range().lower;
    double y_max = customPlot->yAxis->range().upper;
    y_max = y_min + (y_max - y_min) * 1.1;
    customPlot->yAxis->setRange(y_min - 1, y_max);

    customPlot->replot(QCustomPlot::rpQueuedReplot);
}

// 添加计数率显示更新函数
void MainWindow::updateCountRateDisplay(int detectorId, double fpgaTime, double countRate) {
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
    // double currentTime = timer.elapsed() / 1000.0; // 转换为秒

    // int currentTime = data.countRateHistory.size();
    // 添加新的数据点
    // int elapsedSeconds = mTotalCountdown - mRemainingCountdown;
    // qDebug().nospace()<<"detID="<<detectorId<<", elapsedTime = "<<elapsedSeconds<<", fpgaTime = "<<fpgaTime;
    getGraph(detectorId, false)->addData(fpgaTime, countRate);

<<<<<<< HEAD
    if (customPlot->property("enableAutoScale").toBool())
        customPlot->rescaleAxes(true);
    else if (customPlot->property("enableFixedScale").toBool()){
        // 显示最近300秒
        const int WINDOW = customPlot->property("xAxisFixedRange").toUInt();
=======
    // 显示最近300秒
    const int WINDOW = 300;
    // 统计总曲线X轴最大值范围
    double x_max = WINDOW;//getGraph((detectorId-1)/12*12+1, false)->data()->at(0)->key;
    for (int detectorIndex = (detectorId-1)/12*12+1; detectorIndex <= (detectorId-1)/12*12+12; ++detectorIndex){
        QCPGraph* tempGraph = getGraph((detectorIndex-1)/12*12+1, false);
        if (tempGraph->dataCount() <= 0)
            continue;

        x_max =  qMax(x_max, tempGraph->data()->at(tempGraph->dataCount() - 1)->key);
    }
    x_max = qMax(x_max, (double)WINDOW);
    double x_min = qMax(0.0, x_max - WINDOW);
    //customPlot->xAxis->setRange(qMax(0.0, fpgaTime*1.0 - WINDOW), fpgaTime + 1);
    customPlot->xAxis->setRange(x_min, x_max);
    
    // y轴范围只由最近300秒的y决定
    auto range = calcRecentYRange(data.countRateHistory, WINDOW);
    double y_min = range.first;
    double y_max = range.second;
    // 统计总曲线Y轴最大、最小值范围
    for (int detectorIndex = (detectorId-1)/12*12+1; detectorIndex <= (detectorId-1)/12*12+12; ++detectorIndex){
        DetectorData &data = m_detectorData[detectorIndex];
        auto range = calcRecentYRange(data.countRateHistory, WINDOW);
        y_min = qMin(y_min, range.first);
        y_max = qMax(y_max, range.second);
    }
>>>>>>> 26822851222f1b350478b3dc690d0d41742dfecc

        // 统计总曲线X轴最大值范围
        double x_max = getGraph((detectorId-1)/12*12+1, false)->data()->at(0)->key;
        for (int detectorIndex = (detectorId-1)/12*12+1; detectorIndex <= (detectorId-1)/12*12+12; ++detectorIndex){
            QCPGraph* tempGraph = getGraph(detectorIndex, false);
            if (tempGraph->dataCount() <= 0)
                continue;

            x_max =  qMax(x_max, tempGraph->data()->at(tempGraph->dataCount() - 1)->key);
        }
        x_max = qMax(x_max, (double)WINDOW);
        double x_min = qMax(0.0, x_max - WINDOW);
        //customPlot->xAxis->setRange(qMax(0.0, fpgaTime*1.0 - WINDOW), fpgaTime + 1);
        customPlot->xAxis->setRange(x_min, x_max);

        // y轴范围只由最近300秒的y决定
        auto range = calcRecentYRange(data.countRateHistory, WINDOW);
        double y_min = range.first;
        double y_max = range.second;
        // 统计总曲线Y轴最大、最小值范围
        for (int detectorIndex = (detectorId-1)/12*12+1; detectorIndex <= (detectorId-1)/12*12+12; ++detectorIndex){
            DetectorData &data = m_detectorData[detectorIndex];
            auto range = calcRecentYRange(data.countRateHistory, WINDOW);
            y_min = qMin(y_min, range.first);
            y_max = qMax(y_max, range.second);
        }

        // 稍微留点头部空间
        double pad = (y_max - y_min) * 0.1;
        customPlot->yAxis->setRange(y_min - pad, y_max + pad);
    }

    customPlot->replot(QCustomPlot::rpQueuedReplot);
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
    ui->textEdit_log->clear();
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

    QTextEdit *textEdit = ui->textEdit_log;
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

    QTextEdit *textEdit = ui->textEdit_log;
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
    ui->textEdit_log->setExtraSelections(mExtraSelections);
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
        GlobalSettings settings(CONFIG_FILENAME);
        settings.setValue("mainWindow/ShotNumStr", shotNumStr);
        settings.setValue("mainWindow/ShotNumIsAutoIncrease", ui->checkBox_autoIncrease->isChecked());
        settings.setValue("mainWindow/CacheDir", ui->lineEdit_filePath->text());
        settings.setValue("mainWindow/TriggerMode", ui->com_triggerModel->currentIndex());
        settings.setValue("mainWindow/MeasureTime", ui->spinBox_measureTime->value());
    }

    commHelper->setShotInformation(shotDir, shotNumStr);

    // 判断是否连续测量
    if (ui->checkBox_continueMeasure->isChecked())
    {
        // 初始化测量时长显示为 00:00:00
        ui->edit_measureTime->setText("00:00:00");
        // 启动连续测量计时器
        mContinueMeasureTimer->start();
    }
    else
    {
        // 读取倒计时时间（秒）
        int countdownSeconds = ui->spinBox_measureTime->value();
        if (countdownSeconds > 0) {
            mRemainingCountdown = countdownSeconds;
            mTotalCountdown = countdownSeconds;
            mMeasureCountdownTimer->start();
            // 初始化测量时长显示为 00:00:00
            ui->edit_measureTime->setText("00:00:00");
            qInfo().nospace() << QString("开始测量，倒计时：%1 秒").arg(countdownSeconds);
        }
    }

    // 再发开始测量指令
    if (ui->action_waveformMode->isChecked())
        commHelper->startMeasure(CommandAdapter::WorkMode::wmWaveform);
    else if (ui->action_spectrumMode->isChecked())
        commHelper->startMeasure(CommandAdapter::WorkMode::wmSpectrum);

    // 测量中禁用参数配置
    mDetSettingWindow->setEnabled(false);
    //ui->cb_calibration->setEnabled(false);
}

void MainWindow::stopMeasure()
{
    // 停止倒计时定时器
    if (mMeasureCountdownTimer && mMeasureCountdownTimer->isActive()) {
        mMeasureCountdownTimer->stop();
        // 显示最终测量时长
        int elapsedSeconds = mTotalCountdown - mRemainingCountdown;
        ui->edit_measureTime->setText(formatTimeString(elapsedSeconds));
        qInfo() << "手动测量倒计时已停止";
    } else if (mContinueMeasureTimer && mContinueMeasureTimer->isValid()){
        mContinueMeasureTimer->invalidate();
        qInfo() << "自动测量倒计时已停止";
    } else {
        // 如果没有倒计时，清空显示
        ui->edit_measureTime->setText("00:00:00");
    }

    if (ui->checkBox_autoIncrease->isChecked()){
        // lineEdit_shotNum尾缀加1
        ui->lineEdit_shotNum->setText(increaseShotNumSuffix(ui->lineEdit_shotNum->text().trimmed()));
        GlobalSettings settings(CONFIG_FILENAME);
        settings.setValue("mainWindow/ShotNumStr", ui->lineEdit_shotNum->text());
    }

    // 停止波形测量
    commHelper->stopMeasure();
    //清空温度超时报警的探测器ID
    mTemperatureTimeoutDetectors.clear();
    //清空探测器正在测量记录
    mDetectorMeasuring.clear();

    // 非测量中可用参数配置
    mDetSettingWindow->setEnabled(true);
    //ui->cb_calibration->setEnabled(true);
}

QCustomPlot* MainWindow::getCustomPlot(int detectorId, bool isSpectrum)
{
    if (isSpectrum)
        return this->findChild<QCustomPlot*>(QString("spectroMeter%1_bottom").arg(detectorId <= 12 ? 1 : 2));
    else
        return this->findChild<QCustomPlot*>(QString("spectroMeter%1_top").arg(detectorId <= 12 ? 1 : 2));
}

QCPGraph* MainWindow::getGraph(int detectorId, bool isSpectrum)
{
    QCustomPlot *customPlot = getCustomPlot(detectorId, isSpectrum);
    if (!customPlot) nullptr;

    return  customPlot->graph((detectorId-1) % 12);
}

void MainWindow::on_checkBox_continueMeasure_toggled(bool toggled)
{
    ui->spinBox_measureTime->setEnabled(!toggled);
}


void MainWindow::on_cbb_measureMode_activated(int index)
{
    if (index == 0)
    {
        ui->label_18->hide();
        ui->dateTimeEdit_autoTrigger->hide();
    }
    else
    {
        ui->label_18->show();
        ui->dateTimeEdit_autoTrigger->show();
    }
}

void MainWindow::on_cbb_energyCalibration_toggled(bool checked)
{
    mEnScale = checked;

    updateSpectrumPlotSettings();
}

void MainWindow::updateSpectrumPlotSettings(int detectorId)
{
    if(detectorId == 0)
    {
        for(int index = 1; index <= DET_NUM; ++index)
        {
            updateSpectrumPlotSettings(index);
        }

        double xMin = m_spectrumPlotSettings[0].xMin;
        double xMax = m_spectrumPlotSettings[0].xMax;
        for(int index = 1; index < DET_NUM; ++index)
        {
            xMin = qMin(xMin, m_spectrumPlotSettings[index].xMin);
            xMax = qMax(xMax, m_spectrumPlotSettings[index].xMax);
        }

        ui->spectroMeter1_bottom->xAxis->setRange(xMin, xMax);
        ui->spectroMeter2_bottom->xAxis->setRange(xMin, xMax);
        ui->spectroMeter1_bottom->replot();
        ui->spectroMeter2_bottom->replot();
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
        // 读取能量刻度系数
        // 判断是否勾选能量刻度
        GlobalSettings settings(CONFIG_FILENAME);
        settings.beginGroup("EnCalibration");
        if (settings.contains(QString("Detector%1/pointsX").arg(detectorId)) == true)
        {
            settings.beginGroup(QString("Detector%1").arg(detectorId));
            //拟合参数赋值
            int fitType = settings.value("type", 0).toInt();
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

            m_spectrumPlotSettings[detectorId-1].xMin = xMin;
            m_spectrumPlotSettings[detectorId-1].xMax = xMax;
            m_spectrumPlotSettings[detectorId-1].fitType = fitType;
            settings.endGroup();
        }

        settings.endGroup();
    }
    else
    {
        m_spectrumPlotSettings[detectorId-1].xMin = xMin;
        m_spectrumPlotSettings[detectorId-1].xMax = xMax;
    }
}

#include "particalwindow.h"
void MainWindow::on_action_particalMode_triggered()
{
    /*设置发次信息*/
    QString shotDir = ui->lineEdit_filePath->text();
    QString shotNumStr = ui->lineEdit_shotNum->text();

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

