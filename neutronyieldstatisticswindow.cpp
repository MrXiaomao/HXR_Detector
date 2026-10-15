/*
 * @Author: MrPan
 * @Date: 2025-04-20 09:21:28
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2026-09-03 20:25:29
 * @Description: 离线数据分析 中子产额分析
 */
#include "neutronyieldstatisticswindow.h"
#include "ui_neutronyieldstatisticswindow.h"
#include "globalsettings.h"
#include "energycalibrationstore.h"

#include <QButtonGroup>
#include <QCoreApplication>
#include <QFileDialog>
#include <QAction>
#include <QToolButton>
#include <QHeaderView>
#include <QProgressBar>
#include <QThread>
#include <QProgressBar>
#include <QThread>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMessageBox>
#include <QSettings>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QFile>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVBoxLayout>
#include <QSplitter>
#include <QPainter>
#include <math.h>
#include <algorithm>
#include <cstring>
#include <cmath>

// #include "H5Cpp.h"
#include "CrasterParabolicHeatmapWidget.h"
#include "MollweideHeatmapWidget.h"

namespace {
constexpr int kSpectrumPlotSlotCount = 10;
constexpr int kDetectorTableSelectAllRow = DET_NUM;

// 自动导入时使用：返回全部谱仪编号。
QSet<int> allEnergyCalibrationDetectors()
{
    QSet<int> detectors;
    for (int detId = 1; detId <= DET_NUM; ++detId)
        detectors.insert(detId);
    return detectors;
}

struct EnergyCalCompareRow {
    int detectorId = 0;
    bool hasOld = false;
    bool hasOldSigma = false;
    double oldK = 0.0;
    double oldB = 0.0;
    double oldSigma[3] = {5.0, 6.0, 7.0};
    bool hasNew = false;
    double newK = 0.0;
    double newB = 0.0;
    double newSigma[3] = {5.0, 6.0, 7.0};
};

QString formatCalValue(bool ok, double value)
{
    return ok ? QString::number(value, 'g', 6) : QStringLiteral("-");
}

// 弹出谱仪对比表，供导出能量刻度时选择目标谱仪（展示原有与最新 k/b/sigma）。
QSet<int> selectEnergyCalibrationDetectors(QWidget *parent,
                                           const QString &title,
                                           const QString &labelText,
                                           const QVector<EnergyCalCompareRow> &rows)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.resize(1100, 560);

    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(labelText, &dialog);
    layout->addWidget(label);

    enum Col {
        ColCheck = 0,
        ColDet,
        ColOldK,
        ColOldB,
        ColOldS511,
        ColOldS846,
        ColOldS909,
        ColNewK,
        ColNewB,
        ColNewS511,
        ColNewS846,
        ColNewS909,
        ColCount
    };

    auto *table = new QTableWidget(rows.size(), ColCount, &dialog);
    table->setHorizontalHeaderLabels({
        QObject::tr("导出"),
        QObject::tr("谱仪"),
        QObject::tr("原K"),
        QObject::tr("原B"),
        QObject::tr("原σ511"),
        QObject::tr("原σ846"),
        QObject::tr("原σ909"),
        QObject::tr("新K"),
        QObject::tr("新B"),
        QObject::tr("新σ511"),
        QObject::tr("新σ846"),
        QObject::tr("新σ909")
    });
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);

    for (int row = 0; row < rows.size(); ++row) {
        const EnergyCalCompareRow &data = rows.at(row);

        auto *checkItem = new QTableWidgetItem;
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        checkItem->setData(Qt::UserRole, data.detectorId);
        checkItem->setCheckState(data.hasNew ? Qt::Checked : Qt::Unchecked);
        if (!data.hasNew)
            checkItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        table->setItem(row, ColCheck, checkItem);

        auto makeTextItem = [](const QString &text) {
            auto *item = new QTableWidgetItem(text);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setTextAlignment(Qt::AlignCenter);
            return item;
        };

        table->setItem(row, ColDet, makeTextItem(QObject::tr("#%1").arg(data.detectorId)));
        table->setItem(row, ColOldK, makeTextItem(formatCalValue(data.hasOld, data.oldK)));
        table->setItem(row, ColOldB, makeTextItem(formatCalValue(data.hasOld, data.oldB)));
        table->setItem(row, ColOldS511, makeTextItem(formatCalValue(data.hasOldSigma, data.oldSigma[0])));
        table->setItem(row, ColOldS846, makeTextItem(formatCalValue(data.hasOldSigma, data.oldSigma[1])));
        table->setItem(row, ColOldS909, makeTextItem(formatCalValue(data.hasOldSigma, data.oldSigma[2])));
        table->setItem(row, ColNewK, makeTextItem(formatCalValue(data.hasNew, data.newK)));
        table->setItem(row, ColNewB, makeTextItem(formatCalValue(data.hasNew, data.newB)));
        table->setItem(row, ColNewS511, makeTextItem(formatCalValue(data.hasNew, data.newSigma[0])));
        table->setItem(row, ColNewS846, makeTextItem(formatCalValue(data.hasNew, data.newSigma[1])));
        table->setItem(row, ColNewS909, makeTextItem(formatCalValue(data.hasNew, data.newSigma[2])));
    }
    layout->addWidget(table);

    auto *selectLayout = new QHBoxLayout;
    auto *selectAllButton = new QPushButton(QObject::tr("全选可导出"), &dialog);
    auto *clearButton = new QPushButton(QObject::tr("清空"), &dialog);
    selectLayout->addWidget(selectAllButton);
    selectLayout->addWidget(clearButton);
    selectLayout->addStretch();
    layout->addLayout(selectLayout);

    auto setCheckableRows = [table, &rows](Qt::CheckState state) {
        for (int row = 0; row < table->rowCount(); ++row) {
            if (row >= rows.size() || !rows.at(row).hasNew)
                continue;
            if (QTableWidgetItem *item = table->item(row, 0))
                item->setCheckState(state);
        }
    };
    QObject::connect(selectAllButton, &QPushButton::clicked, &dialog, [=]() { setCheckableRows(Qt::Checked); });
    QObject::connect(clearButton, &QPushButton::clicked, &dialog, [=]() { setCheckableRows(Qt::Unchecked); });

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    QSet<int> selectedDetectors;
    if (dialog.exec() != QDialog::Accepted)
        return selectedDetectors;

    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem *item = table->item(row, ColCheck);
        if (!item || item->checkState() != Qt::Checked)
            continue;
        selectedDetectors.insert(item->data(Qt::UserRole).toInt());
    }
    return selectedDetectors;
}

QString neutronYieldConfigPath()
{
    return QStringLiteral("./config/NeutronYield.ini");
}

void ensureParseConfigDefaults(QSettings &settings)
{
    settings.beginGroup(QStringLiteral("Parse"));
    if (!settings.contains(QStringLiteral("spectrumFitR2Min")))
        settings.setValue(QStringLiteral("spectrumFitR2Min"), CurveFit::kDefaultSpectrumFitR2Min);
    if (!settings.contains(QStringLiteral("initialPeakFindWidth")))
        settings.setValue(QStringLiteral("initialPeakFindWidth"), ParseData::kDefaultInitialPeakFindWidth);
    if (!settings.contains(QStringLiteral("peakHeightMin")))
        settings.setValue(QStringLiteral("peakHeightMin"), ParseData::kDefaultPeakHeightMin);
    if (!settings.contains(QStringLiteral("peakSigmaRatioMin")))
        settings.setValue(QStringLiteral("peakSigmaRatioMin"), ParseData::kDefaultPeakSigmaRatioMin);
    if (!settings.contains(QStringLiteral("peakSigmaRatioMax")))
        settings.setValue(QStringLiteral("peakSigmaRatioMax"), ParseData::kDefaultPeakSigmaRatioMax);
    settings.endGroup();
}

void ensureTableConfigDefaults(QSettings &settings)
{
    settings.beginGroup(QStringLiteral("Table"));
    for (int detId = 1; detId <= DET_NUM; ++detId) {
        if (!settings.contains(QStringLiteral("yield%1").arg(detId)))
            settings.setValue(QStringLiteral("yield%1").arg(detId), 1.0);
        if (!settings.contains(QStringLiteral("initial909Count%1").arg(detId)))
            settings.setValue(QStringLiteral("initial909Count%1").arg(detId), 1.0);
        if (!settings.contains(QStringLiteral("sensitivity%1").arg(detId)))
            settings.setValue(QStringLiteral("sensitivity%1").arg(detId), 1.0);
    }
    settings.endGroup();
}

void ensureNeutronYieldConfigExists()
{
    QDir().mkpath(QStringLiteral("./config"));

    const QString configPath = neutronYieldConfigPath();
    const bool fileExists = QFileInfo::exists(configPath);

    QSettings settings(configPath, QSettings::IniFormat);
    ensureParseConfigDefaults(settings);
    if (!fileExists)
        ensureTableConfigDefaults(settings);
    settings.sync();
}

// 将 E = kx + b 反算为 511/909 keV 初始道址并同步到内存表。
bool updateInitialCalibrationFromEnergyScale(int detectorId, double k, double b)
{
    if (!std::isfinite(k) || !std::isfinite(b) || qFuzzyIsNull(k))
        return false;

    const double ch511 = (511.0 - b) / k;
    const double ch909 = (909.0 - b) / k;
    return ParseData::setInitialCalibrationChannels(detectorId, ch511, ch909);
}

const QList<QColor> &spectrumSlotColors()
{
    static const QList<QColor> colors = {
        Qt::red, Qt::green, Qt::blue, Qt::cyan, Qt::darkCyan
    };
    return colors;
}

int defaultSpecNoForSlot(int slot, int specCount)
{
    if (specCount <= 0)
        return 1;
    if (specCount > 20) {
        return qBound(1,
                      qRound(1.0 + (slot - 1) * (specCount - 1) / 9.0),
                      specCount);
    }
    return qMin(slot, specCount);
}

QPixmap peakChannelRoundPixmap(const QSize &size, const QColor &color)
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(0, 0, size.width(), size.height());
    return pixmap;
}

QColor peakChannelGraphColor(int detId)
{
    return QColor::fromHsv((detId * 360 / DET_NUM) % 360, 200, 220);
}

enum FileListColumn {
    ColIndex = 0,
    ColFileName,
    ColStartTime,
    ColEndTime,
    ColDuration,
    ColSpectrumCount,
    ColSpecTimeInterval,
    ColValidChannels,
    ColGapTime,
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

    ensureNeutronYieldConfigExists();

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

    // 界面打开后自动从 device_config.ini 导入初始刻度，避免用户每次手动导入。
    QTimer::singleShot(0, this, [this]() {
        int importedCount = 0;
        int skippedCount = 0;
        if (EnergyCalibrationStore::importInitialFromDeviceConfig(allEnergyCalibrationDetectors(),
                                                                  &importedCount,
                                                                  &skippedCount,
                                                                  nullptr)) {
            emit reporWriteLog(tr("已自动导入 device_config.ini 初始能量刻度：%1 个谱仪已更新，%2 个跳过。")
                                   .arg(importedCount)
                                   .arg(skippedCount));
        } else {
            emit reporWriteLog(tr("未自动导入 device_config.ini 初始能量刻度，继续使用程序默认初始道址。"), QtWarningMsg);
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


NeutronYieldStatisticsWindow::~NeutronYieldStatisticsWindow()
{
    {
        GlobalSettings settingsSystem;
        if (QSplitter *splitterH1 = findChild<QSplitter*>(QStringLiteral("splitterH1")))
            settingsSystem.setValue(QStringLiteral("Global/splitterH1/State"), splitterH1->saveState());
        if (QSplitter *splitterFileList = findChild<QSplitter*>(QStringLiteral("splitterFileList")))
            settingsSystem.setValue(QStringLiteral("Global/splitterFileList/State"), splitterFileList->saveState());
    }

    if (mParseWorker)
        mParseWorker->requestCancel();
    if (mParseWorkerThread) {
        mParseWorkerThread->quit();
        mParseWorkerThread->wait(3000);
    }

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

    mProgressBar = new QProgressBar(ui->statusbar);
    mProgressBar->setObjectName(QStringLiteral("progressBar_parse"));
    mProgressBar->setMinimumWidth(220);
    mProgressBar->setMaximumWidth(420);
    mProgressBar->setTextVisible(true);
    mProgressBar->hide();
    ui->statusbar->addPermanentWidget(mProgressBar);

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

        // 右侧：按 .ui 顺序垂直可拖动分割
        // gbFileInfo → gbAnalyticPara → gbSaveAs → gbLog
        ui->verticalLayout_4->removeWidget(ui->gbFileInfo);
        ui->verticalLayout_4->removeWidget(ui->gbAnalyticPara);
        ui->verticalLayout_4->removeWidget(ui->gbSaveAs);
        ui->verticalLayout_4->removeWidget(ui->gbLog);

        QWidget *rightBottomPanel = new QWidget(this);
        rightBottomPanel->setObjectName(QStringLiteral("rightBottomPanel"));
        QVBoxLayout *rightBottomLayout = new QVBoxLayout(rightBottomPanel);
        rightBottomLayout->setContentsMargins(0, 0, 0, 0);
        rightBottomLayout->setSpacing(ui->verticalLayout_4->spacing());
        rightBottomLayout->addWidget(ui->gbAnalyticPara);
        rightBottomLayout->addWidget(ui->gbSaveAs);
        rightBottomLayout->addWidget(ui->gbLog, 1);

        QSplitter *splitterFileList = new QSplitter(Qt::Vertical, this);
        splitterFileList->setObjectName(QStringLiteral("splitterFileList"));
        splitterFileList->setHandleWidth(4);
        splitterFileList->addWidget(ui->gbFileInfo);
        splitterFileList->addWidget(rightBottomPanel);
        splitterFileList->setStretchFactor(0, 1);
        splitterFileList->setStretchFactor(1, 2);
        splitterFileList->setSizes(QList<int>() << 360 << 420);
        splitterFileList->setCollapsible(0, false);
        splitterFileList->setCollapsible(1, false);
        ui->verticalLayout_4->addWidget(splitterFileList);

        // 文件列表在「基本信息」内随高度伸展；时刻/模式控件垂直高度固定不压缩
        ui->widget_fileListToolbar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        ui->widget_4->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        ui->fusionDateTime->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        ui->messureDateTime->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        ui->widget_shotTimeMode->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        ui->radioButton_unifiedShotTime->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        ui->radioButton_perChannelShotTime->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        ui->spinBox_channelId->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        const int fileListIndex = ui->verticalLayout_6->indexOf(ui->tableWidget_fileList);
        if (fileListIndex >= 0)
            ui->verticalLayout_6->setStretch(fileListIndex, 1);
        const int toolbarIndex = ui->verticalLayout_6->indexOf(ui->widget_fileListToolbar);
        if (toolbarIndex >= 0)
            ui->verticalLayout_6->setStretch(toolbarIndex, 0);
        const int formIndex = ui->verticalLayout_6->indexOf(ui->widget_4);
        if (formIndex >= 0)
            ui->verticalLayout_6->setStretch(formIndex, 0);
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
    
    ui->twResults->setEditTriggers(QAbstractItemView::DoubleClicked
                                     | QAbstractItemView::SelectedClicked
                                     | QAbstractItemView::EditKeyPressed);
    ui->twResults->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->twResults->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    {
        QHeaderView *header = ui->twResults->horizontalHeader();
        header->setSectionResizeMode(QHeaderView::Interactive);
        header->setStretchLastSection(false);
        header->setSectionResizeMode(0, QHeaderView::Fixed);
        ui->twResults->setColumnWidth(0, 40);   // #
        ui->twResults->setColumnWidth(1, 90);   // 中子产额
        ui->twResults->setColumnWidth(2, 95);   // 不确定度(%)
        ui->twResults->setColumnWidth(3, 60);   // 经度
        ui->twResults->setColumnWidth(4, 60);   // 纬度
        ui->twResults->setColumnWidth(5, 130);  // 初始计数(909keV)
        ui->twResults->setColumnWidth(6, 70);   // 能谱个数
    }

    ui->twResults->blockSignals(true);
    for (int column = 0; column < ui->twResults->columnCount(); ++column) {
        for (int row = 0; row < DET_NUM; ++row) {
            auto *item = new QTableWidgetItem("");
            applyDetectorTableItemFlags(item, column);
            ui->twResults->setItem(row, column, item);
        }
    }

    for (int row = 0; row < DET_NUM; ++row) {
        ui->twResults->setRowHeight(row, 25);
        auto *indexItem = new QTableWidgetItem(QString("#%1").arg(row + 1));
        applyDetectorTableItemFlags(indexItem, 0);
        ui->twResults->setItem(row, 0, indexItem);
    }
    initDetectorTableSelectAllRow();
    ui->twResults->blockSignals(false);
    // 表头 + 32 通道 + 全选行，并预留横向滚动条高度
    const int horizontalScrollBarSpace = 18;
    ui->twResults->setFixedHeight((DET_NUM + 1 + 1) * 25 + horizontalScrollBarSpace);
    ui->leftStackedWidget->setMinimumWidth(200);

    initFileListTable();

    // 连接表格选择变化信号（图表通道改由 spinBox_pictureDet 控制，不再联动 twResults 行选择）
    // connect(ui->twResults, &QTableWidget::itemSelectionChanged, this, &NeutronYieldStatisticsWindow::on_twResults_selectionChanged);

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
            if (isPerChannelShotTimeMode())
                syncMeasureDateTimeForDetector(detId);
        });
        connect(ui->spinBox_pictureDet, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int detId) {
            updatePictureForDetector(detId);
        });
        updateShotTimeModeUi();
    }

    initSpectrumCustomPlot();
    initCountCustomPlot();
    initPeakChannelCustomPlot();
    initYieldCustomPlot();

    connect(ui->twResults, &QTableWidget::itemChanged, this,
            &NeutronYieldStatisticsWindow::on_twResults_itemChanged);

    // 显示计数/能谱
    QButtonGroup *grp = new QButtonGroup(this);
    grp->addButton(ui->radioButton_spectrum, 0);
    grp->addButton(ui->radioButton_count, 1);
    grp->addButton(ui->rb_peakChannel_909keV, 2);
    grp->addButton(ui->radioButton_neutronYield, 3);
    connect(grp, QOverload<int>::of(&QButtonGroup::idClicked), this, [=](int index){
        ui->stackedWidget->setCurrentIndex(index);
        if (index == 2) {
            ensurePeakChannelCurveColumn();
            updateAllPeakChannelPlot();
        }
    });
    ui->radioButton_spectrum->setChecked(true);
    ui->stackedWidget->setCurrentIndex(0);

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

        if (settingsSystem.contains(QStringLiteral("Global/splitterFileList/State"))){
            QSplitter *splitterFileList = this->findChild<QSplitter*>(QStringLiteral("splitterFileList"));
            if (splitterFileList)
            {
                splitterFileList->restoreState(
                    settingsSystem.value(QStringLiteral("Global/splitterFileList/State")).toByteArray());
            }
        }
    }

    {
        QSettings nySettings(neutronYieldConfigPath(), QSettings::IniFormat);
        ui->doubleSpinBox_fitR2Min->setValue(
            nySettings.value(QStringLiteral("Parse/spectrumFitR2Min"),
                             CurveFit::kDefaultSpectrumFitR2Min).toDouble());
    }

    setParseBusy(false);
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
    table->horizontalHeader()->setSectionResizeMode(ColFileName, QHeaderView::Interactive);
    table->horizontalHeader()->setSectionResizeMode(ColStartTime, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(ColEndTime, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(ColDuration, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(ColSpectrumCount, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(ColSpecTimeInterval, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(ColValidChannels, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(ColGapTime, QHeaderView::Interactive);
    table->setColumnWidth(ColIndex, 40);
    table->setColumnWidth(ColFileName, 150);
    table->setColumnWidth(ColStartTime, 180);
    table->setColumnWidth(ColEndTime, 180);
    table->setColumnWidth(ColDuration, 70);
    table->setColumnWidth(ColSpectrumCount, 60);
    table->setColumnWidth(ColSpecTimeInterval, 90);
    table->setColumnWidth(ColValidChannels, 70);
    table->setColumnWidth(ColGapTime, 70);
    ui->tableWidget_fileList->setEditTriggers(
        QAbstractItemView::DoubleClicked |
        QAbstractItemView::SelectedClicked |
        QAbstractItemView::EditKeyPressed
    );
    connect(table->model(), &QAbstractItemModel::rowsMoved, this, [this]() {
        updateFileListGapColumns();
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

                // emit reporWriteLog(tr("谱仪%1时间范围：%2 ~ %3")
                //                        .arg(detId)
                //                        .arg(firstDt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")))
                //                        .arg(lastDt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"))));

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
        QTableWidgetItem *gapItem = table->item(row, ColGapTime);
        if (!gapItem) {
            gapItem = new QTableWidgetItem();
            gapItem->setTextAlignment(Qt::AlignCenter);
            table->setItem(row, ColGapTime, gapItem);
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
    // spinBox_pictureDet 始终用于切换查看已解析通道结果，与解析通道号解耦
    ui->spinBox_pictureDet->setEnabled(true);
    if (perChannel)
        syncMeasureDateTimeForDetector(ui->spinBox_channelId->value());
}

int NeutronYieldStatisticsWindow::currentPictureDetId() const
{
    return ui->spinBox_pictureDet->value();
}

void NeutronYieldStatisticsWindow::updateDetectorTableSpecCount(int detId)
{
    if (detId < 1 || detId > DET_NUM)
        return;

    quint32 count = 0;
    if (ParseData *parsed = dealFile[detId - 1]) {
        const QVector<ParseData::mergeSpecData> mergeSpec = parsed->GetMergeSpec();
        for (const ParseData::mergeSpecData &seg : mergeSpec)
            count += seg.rowCount;
    }

    auto *specCountItem = ui->twResults->item(detId - 1, 6);
    if (!specCountItem) {
        specCountItem = new QTableWidgetItem;
        applyDetectorTableItemFlags(specCountItem, 6);
        ui->twResults->setItem(detId - 1, 6, specCountItem);
    }
    specCountItem->setText(count > 0 ? QString::number(count) : QString());
}

void NeutronYieldStatisticsWindow::clearDetectorTableSpecCount(int detId)
{
    if (detId < 1 || detId > DET_NUM)
        return;

    if (QTableWidgetItem *specCountItem = ui->twResults->item(detId - 1, 6))
        specCountItem->setText(QString());
}

void NeutronYieldStatisticsWindow::updatePictureForDetector(int detID)
{
    if (detID < 1 || detID > DET_NUM)
        return;

    auto revertPictureDetSpinBox = [this]() {
        if (ui->spinBox_pictureDet->value() == m_lastPictureDetId)
            return;
        const bool blocked = ui->spinBox_pictureDet->blockSignals(true);
        ui->spinBox_pictureDet->setValue(m_lastPictureDetId);
        ui->spinBox_pictureDet->blockSignals(blocked);
    };

    if (!dealFile[detID - 1]) {
        if (!mDealSucess.value(detID, false)) {
            QMessageBox::information(this, tr("提示"), tr("谱仪%1不存在解析成功的数据！").arg(detID));
        }
        revertPictureDetSpinBox();
        return;
    }

    const QVector<ParseData::mergeSpecData> mergeSpecData = dealFile[detID - 1]->GetMergeSpec();
    const QVector<specStripData> count909_picture = dealFile[detID - 1]->GetCount909Data();
    const bool hasMergeSpec = !mergeSpecData.isEmpty();
    const bool hasFitData = !count909_picture.isEmpty();

    if (!hasMergeSpec && !hasFitData) {
        if (!mDealSucess.value(detID, false)) {
            QMessageBox::information(this, tr("提示"), tr("谱仪%1不存在解析成功的数据！").arg(detID));
        }
        revertPictureDetSpinBox();
        return;
    }

    m_lastPictureDetId = detID;

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
    customPlotHelper->setRangeSelectActionVisible(false); // 禁用右键的选择范围
    customPlot->setProperty("enable-dataTracer", true);
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
    customPlot->setProperty("enable-dataTracer", true);

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
        graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, 8));//显示散点图
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
        graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, 8));//显示散点图
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
        graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, 5));//显示散点图
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
        // graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, 5));//显示散点图
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
        graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, 5));//显示散点图
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

void NeutronYieldStatisticsWindow::initPeakChannelCustomPlot()
{
    QCustomPlot *customPlot = ui->spectorMeter_PeakChannel;
    QCustomPlotHelper *customPlotHelper = new QCustomPlotHelper(customPlot, this);
    customPlotHelper->setResetActionVisible(true);
    customPlotHelper->setClearMarkerActionVisible(false);
    customPlotHelper->setStraightLineActionVisible(true);
    customPlotHelper->setRangeSelectActionVisible(false);
    customPlot->setProperty("enable-dataTracer", true);

    customPlot->setAntialiasedElements(QCP::aeAll);
    customPlot->legend->setVisible(true);
    //customPlot->legend->setFont(QFont(QStringLiteral("Helvetica"), 8));
    customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
    customPlot->xAxis->setLabel(tr("时间(分钟)"));
    customPlot->yAxis->setLabel(tr("峰位(道址)"));
    customPlot->yAxis->ticker()->setTickCount(6);
    customPlot->xAxis->ticker()->setTickCount(8);

    customPlot->xAxis2->setTicks(false);
    customPlot->xAxis2->setSubTicks(false);
    customPlot->yAxis2->setTicks(false);
    customPlot->yAxis2->setSubTicks(false);

    for (int detId = 1; detId <= DET_NUM; ++detId) {
        QCPGraph *graph = customPlot->addGraph();
        graph->setName(QStringLiteral("Ch%1").arg(detId));
        graph->setProperty("peakChannelDetId", detId);
        graph->setAntialiased(true);
        graph->setPen(QPen(peakChannelGraphColor(detId), 2, Qt::SolidLine));
        graph->setLineStyle(QCPGraph::lsLine);
        graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, 6));
        graph->setVisible(false);
    }

    customPlot->replot();
    connect(customPlot->xAxis, SIGNAL(rangeChanged(const QCPRange &)), customPlot->xAxis2, SLOT(setRange(const QCPRange &)));
    connect(customPlot->yAxis, SIGNAL(rangeChanged(const QCPRange &)), customPlot->yAxis2, SLOT(setRange(const QCPRange &)));
    connect(customPlot, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(slotShowTracer(QMouseEvent*)));
    connect(customPlot, SIGNAL(mouseRelease(QMouseEvent*)), this, SLOT(slotRestorePlot(QMouseEvent*)));
}

QCheckBox *NeutronYieldStatisticsWindow::peakChannelCurveCheckBox(int row) const
{
    if (!mPeakChannelCurveColumnReady || mPeakChannelCurveColumn < 0)
        return nullptr;
    if (row < 0 || row >= DET_NUM)
        return nullptr;
    QWidget *cell = ui->twResults->cellWidget(row, mPeakChannelCurveColumn);
    return cell ? cell->findChild<QCheckBox*>() : nullptr;
}

void NeutronYieldStatisticsWindow::initDetectorTableSelectAllRow()
{
    if (ui->twResults->rowCount() <= kDetectorTableSelectAllRow)
        ui->twResults->insertRow(kDetectorTableSelectAllRow);

    ui->twResults->setRowHeight(kDetectorTableSelectAllRow, 25);

    auto *indexItem = new QTableWidgetItem(tr("全选"));
    applyDetectorTableItemFlags(indexItem, 0);
    indexItem->setFlags(indexItem->flags() & ~Qt::ItemIsEditable);
    ui->twResults->setItem(kDetectorTableSelectAllRow, 0, indexItem);

    for (int col = 1; col < ui->twResults->columnCount(); ++col) {
        auto *item = new QTableWidgetItem;
        applyDetectorTableItemFlags(item, col);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        ui->twResults->setItem(kDetectorTableSelectAllRow, col, item);
    }
}

void NeutronYieldStatisticsWindow::updatePeakChannelSelectAllState()
{
    if (!mCheckBoxPeakChannelSelectAll)
        return;

    int checkedCount = 0;
    int enabledCount = 0;
    for (int row = 0; row < DET_NUM; ++row) {
        QCheckBox *cb = peakChannelCurveCheckBox(row);
        if (!cb || !cb->isEnabled())
            continue;
        ++enabledCount;
        if (cb->isChecked())
            ++checkedCount;
    }

    mUpdatingPeakChannelSelectAll = true;
    if (enabledCount == 0 || checkedCount == 0) {
        mCheckBoxPeakChannelSelectAll->setTristate(false);
        mCheckBoxPeakChannelSelectAll->setCheckState(Qt::Unchecked);
    } else if (checkedCount == enabledCount) {
        mCheckBoxPeakChannelSelectAll->setTristate(false);
        mCheckBoxPeakChannelSelectAll->setCheckState(Qt::Checked);
    } else {
        mCheckBoxPeakChannelSelectAll->setTristate(true);
        mCheckBoxPeakChannelSelectAll->setCheckState(Qt::PartiallyChecked);
    }
    mUpdatingPeakChannelSelectAll = false;
}

void NeutronYieldStatisticsWindow::ensurePeakChannelCurveColumn()
{
    if (mPeakChannelCurveColumnReady)
        return;

    const int col = ui->twResults->columnCount();
    ui->twResults->insertColumn(col);
    mPeakChannelCurveColumn = col;

    auto *header = new QTableWidgetItem(tr("曲线显示"));
    ui->twResults->setHorizontalHeaderItem(col, header);
    ui->twResults->setColumnWidth(col, 80);

    for (int row = 0; row < DET_NUM; ++row) {
        const int detId = row + 1;
        auto *checkBox = new QCheckBox();
        checkBox->setText(QString::number(detId));
        checkBox->setProperty("detId", detId);
        checkBox->setIcon(QIcon(peakChannelRoundPixmap(QSize(16, 16), peakChannelGraphColor(detId))));
        checkBox->setChecked(true);

        auto *cellWidget = new QWidget(ui->twResults);
        auto *cellLayout = new QHBoxLayout(cellWidget);
        cellLayout->setContentsMargins(4, 0, 0, 0);
        cellLayout->setSpacing(0);
        cellLayout->addWidget(checkBox, 0, Qt::AlignLeft | Qt::AlignVCenter);
        cellLayout->addStretch();
        ui->twResults->setCellWidget(row, col, cellWidget);

        connect(checkBox, &QCheckBox::stateChanged, this, [this](int) {
            if (!mUpdatingPeakChannelSelectAll)
                updatePeakChannelSelectAllState();
            if (ui->stackedWidget->currentIndex() == 2)
                applyPeakChannelCurveVisibility();
        });
    }

    mCheckBoxPeakChannelSelectAll = new QCheckBox(tr("全选"));
    mCheckBoxPeakChannelSelectAll->setTristate(true);
    mCheckBoxPeakChannelSelectAll->setChecked(true);
    auto *selectAllCell = new QWidget(ui->twResults);
    auto *selectAllLayout = new QHBoxLayout(selectAllCell);
    selectAllLayout->setContentsMargins(4, 0, 0, 0);
    selectAllLayout->setSpacing(0);
    selectAllLayout->addWidget(mCheckBoxPeakChannelSelectAll, 0, Qt::AlignLeft | Qt::AlignVCenter);
    selectAllLayout->addStretch();
    ui->twResults->setCellWidget(kDetectorTableSelectAllRow, col, selectAllCell);
    connect(mCheckBoxPeakChannelSelectAll, &QCheckBox::stateChanged, this, [this](int state) {
        if (mUpdatingPeakChannelSelectAll || state == Qt::PartiallyChecked)
            return;
        const bool checked = (state == Qt::Checked);
        mUpdatingPeakChannelSelectAll = true;
        for (int row = 0; row < DET_NUM; ++row) {
            if (QCheckBox *cb = peakChannelCurveCheckBox(row)) {
                if (cb->isEnabled())
                    cb->setChecked(checked);
            }
        }
        mUpdatingPeakChannelSelectAll = false;
        updatePeakChannelSelectAllState();
        if (ui->stackedWidget->currentIndex() == 2)
            applyPeakChannelCurveVisibility();
    });

    mPeakChannelCurveColumnReady = true;
    updatePeakChannelSelectAllState();
}

void NeutronYieldStatisticsWindow::applyPeakChannelCurveVisibility()
{
    if (ui->stackedWidget->currentIndex() != 2)
        return;

    QCustomPlot *customPlot = ui->spectorMeter_PeakChannel;
    if (!customPlot)
        return;

    bool hasVisible = false;
    double minY = 0.0;
    double maxY = 0.0;
    bool yInit = false;

    for (int detId = 1; detId <= DET_NUM; ++detId) {
        QCPGraph *graph = customPlot->graph(detId - 1);
        if (!graph)
            continue;

        const bool hasData = graph->dataCount() > 0;
        QCheckBox *checkBox = peakChannelCurveCheckBox(detId - 1);
        if (checkBox)
            checkBox->setEnabled(hasData);

        const bool shouldShow = hasData && checkBox && checkBox->isChecked();
        graph->setVisible(shouldShow);
        if (!shouldShow)
            continue;

        hasVisible = true;
        for (auto it = graph->data()->constBegin(); it != graph->data()->constEnd(); ++it) {
            if (!yInit) {
                minY = maxY = it->value;
                yInit = true;
            } else {
                minY = qMin(minY, it->value);
                maxY = qMax(maxY, it->value);
            }
        }
    }

    if (hasVisible) {
        customPlot->xAxis->rescale(true);
        if (yInit) {
            const double margin = qMax(1.0, (maxY - minY) * 0.05);
            customPlot->yAxis->setRange(minY - margin, maxY + margin);
        } else {
            customPlot->yAxis->rescale(true);
        }
        customPlot->xAxis->scaleRange(1.05, customPlot->xAxis->range().center());
    }

    updatePeakChannelSelectAllState();
    customPlot->replot();
}

// 909峰位演变图
void NeutronYieldStatisticsWindow::updateAllPeakChannelPlot()
{
    QCustomPlot *customPlot = ui->spectorMeter_PeakChannel;
    if (!customPlot)
        return;

    bool hasAny = false;
    double minY = 0.0;
    double maxY = 0.0;
    bool yInit = false;

    for (int detId = 1; detId <= DET_NUM; ++detId) {
        QCPGraph *graph = customPlot->graph(detId - 1);
        if (!graph)
            continue;

        ParseData *parsed = dealFile[detId - 1];
        const QVector<double> peakPos = parsed ? parsed->count909PeakPos() : QVector<double>();
        const QVector<specStripData> countData = parsed ? parsed->GetCount909Data() : QVector<specStripData>();

        if (!parsed || peakPos.isEmpty() || countData.isEmpty()) {
            graph->setVisible(false);
            graph->data()->clear();
            continue;
        }

        QVector<double> keys;
        QVector<double> values;
        const int n = qMin(peakPos.size(), countData.size());
        keys.reserve(n);
        values.reserve(n);
        for (int i = 0; i < n; ++i) {
            keys << countData.at(i).x;
            values << peakPos.at(i);
            if (!yInit) {
                minY = maxY = peakPos.at(i);
                yInit = true;
            } else {
                minY = qMin(minY, peakPos.at(i));
                maxY = qMax(maxY, peakPos.at(i));
            }
        }

        graph->setData(keys, values);
        hasAny = true;
    }

    if (ui->stackedWidget->currentIndex() == 2)
        applyPeakChannelCurveVisibility();
    else if (hasAny) {
        for (int detId = 1; detId <= DET_NUM; ++detId) {
            QCPGraph *graph = customPlot->graph(detId - 1);
            if (graph && graph->dataCount() > 0)
                graph->setVisible(true);
        }
        customPlot->xAxis->rescale(true);
        if (yInit) {
            const double margin = qMax(1.0, (maxY - minY) * 0.05);
            customPlot->yAxis->setRange(minY - margin, maxY + margin);
        } else {
            customPlot->yAxis->rescale(true);
        }
        customPlot->xAxis->scaleRange(1.05, customPlot->xAxis->range().center());
        customPlot->replot();
    } else {
        customPlot->replot();
    }
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
            {-180, 45, 76}, {-90, 45, 82}, {0, 45, 73}, {90, 45, 60}, {150, 45, 75},
            {-180, -45, 86}, {-90, -45, 83}, {0, -45, 90}, {90, -45, 76}, {150, -45, 76},
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
    updateFileListGapColumns();
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
    if (mParseWorkerThread) {
        QMessageBox::information(this, tr("提示"), tr("解析正在进行中，请先停止当前任务。"));
        return;
    }

    mInterrupted = false;

    const int startTime = ui->spinBox_timeStart->value() * 60;
    const int endTime = ui->spinBox_timeEnd->value() * 60;
    const int timeStep = ui->spinBox_step->value() * 60;
    const double spectrumFitR2Min = ui->doubleSpinBox_fitR2Min->value();

    if (startTime >= endTime) {
        QMessageBox::information(this, tr("提示"), tr("退出解析！输入的解析时间起点>=解析时间终点。"));
        return;
    }

    if (spectrumFitR2Min <= 0.0 || spectrumFitR2Min > 1.0) {
        QMessageBox::information(this, tr("提示"), tr("拟合优度下限 R² 须在 (0, 1] 范围内。"));
        return;
    }

    {
        QSettings nySettings(neutronYieldConfigPath(), QSettings::IniFormat);
        nySettings.setValue(QStringLiteral("Parse/spectrumFitR2Min"), spectrumFitR2Min);
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
        ui->spinBox_pictureDet->setValue(detID);
    } else {
        detIds = mDetectorSpecCount.keys();
        std::sort(detIds.begin(), detIds.end());
    }

    const QDateTime windowLeft = fusionTime.addSecs(startTime);
    const QDateTime windowRight = fusionTime.addSecs(endTime);

    for (int detId : detIds)
        clearDetectorTableSpecCount(detId);

    const QString tmStart = windowLeft.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    const QString tmEnd = windowRight.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    const QString step = QString::number(timeStep / 60);
    const QString f_segment = QString::number((endTime - startTime) * 1.0 / timeStep, 'f', 1);
    const QString i_segment = QString::number((endTime - startTime) / timeStep);
    const QString fitR2Text = QString::number(spectrumFitR2Min, 'f', 2);

    QString msgTip;
    if (perChannelMode) {
        msgTip = QString(tr("模式：分通道打靶时刻"
                            "\n通道：谱仪 %1"
                            "\n打靶时刻：%2"
                            "\n起点时间：%3"
                            "\n结束时间：%4"
                            "\n时间步长：%5分钟"
                            "\n拟合优度下限 R²：%8"
                            "\n计算结果：%6条曲线，根据整除原则，将只显示%7条曲线"
                            "\n\n点击[Yes]开始解析"
                            "\n点击[No]返回重新修改"))
                   .arg(detIds.first())
                   .arg(fusionTime.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")))
                   .arg(tmStart, tmEnd, step, f_segment, i_segment, fitR2Text);
    } else {
        msgTip = QString(tr("模式：统一打靶时刻"
                            "\n打靶时刻：%1"
                            "\n起点时间：%2"
                            "\n结束时间：%3"
                            "\n时间步长：%4分钟"
                            "\n拟合优度下限 R²：%7"
                            "\n计算结果：%5条曲线，根据整除原则，将只显示%6条曲线"
                            "\n\n点击[Yes]开始解析"
                            "\n点击[No]返回重新修改"))
                   .arg(fusionTime.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")))
                   .arg(tmStart, tmEnd, step, f_segment, i_segment, fitR2Text);
    }
    if (QMessageBox::question(this, tr("提示"), msgTip, QMessageBox::Yes, QMessageBox::No) != QMessageBox::Yes)
        return;

    emit reporWriteLog(tr("开始解析..."));
    emit reporWriteLog(tr("解析窗口：%1 ~ %2，步长 %3 s，全局 %4 段，拟合优度下限 R²=%5")
                           .arg(tmStart, tmEnd)
                           .arg(timeStep)
                           .arg((endTime - startTime) / timeStep)
                           .arg(fitR2Text));
    if (perChannelMode) {
        emit reporWriteLog(tr("分通道模式：谱仪 %1，打靶时刻 %2")
                               .arg(detIds.first())
                               .arg(fusionTime.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))));
    } else {
        emit reporWriteLog(tr("统一打靶时刻：%1")
                               .arg(fusionTime.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"))));
    }

    NeutronYieldParseJob job;
    job.h5FilePaths = h5FilePaths;
    job.detIds = detIds;
    job.fusionTime = fusionTime;
    job.windowLeft = windowLeft;
    job.windowRight = windowRight;
    job.timeStepSec = timeStep;
    job.perChannelMode = perChannelMode;
    job.spectrumFitR2Min = spectrumFitR2Min;
    {
        QSettings nySettings(neutronYieldConfigPath(), QSettings::IniFormat);
        job.initialPeakFindWidth = nySettings.value(
            QStringLiteral("Parse/initialPeakFindWidth"),
            ParseData::kDefaultInitialPeakFindWidth).toDouble();
        job.peakHeightMin = nySettings.value(
            QStringLiteral("Parse/peakHeightMin"),
            ParseData::kDefaultPeakHeightMin).toDouble();
        job.peakSigmaRatioMin = nySettings.value(
            QStringLiteral("Parse/peakSigmaRatioMin"),
            ParseData::kDefaultPeakSigmaRatioMin).toDouble();
        job.peakSigmaRatioMax = nySettings.value(
            QStringLiteral("Parse/peakSigmaRatioMax"),
            ParseData::kDefaultPeakSigmaRatioMax).toDouble();
    }
    for (int detID : detIds) {
        if (detID >= 1 && detID <= DET_NUM)
            job.beginTimeByDet.insert(detID, static_cast<int>(beginTimeForDetector(detID, fusionTime)));
    }

    startParseWorker(job);
}


void NeutronYieldStatisticsWindow::startParseWorker(const NeutronYieldParseJob &job)
{
    if (mParseWorkerThread)
        return;

    mInterrupted = false;

    if (mProgressBar) {
        mProgressBar->setRange(0, 1);
        mProgressBar->setValue(0);
        mProgressBar->show();
    }

    setParseBusy(true);

    mParseWorker = new NeutronYieldParseWorker();
    mParseWorkerThread = new QThread(this);
    mParseWorker->moveToThread(mParseWorkerThread);

    connect(mParseWorkerThread, &QThread::started, mParseWorker, [this, job]() {
        mParseWorker->run(job);
    });
    connect(mParseWorker, &NeutronYieldParseWorker::progress,
            this, &NeutronYieldStatisticsWindow::onParseProgress, Qt::QueuedConnection);
    connect(mParseWorker, &NeutronYieldParseWorker::logMessage,
            this, &NeutronYieldStatisticsWindow::onParseLogMessage, Qt::QueuedConnection);
    connect(mParseWorker, &NeutronYieldParseWorker::channelFinished,
            this, &NeutronYieldStatisticsWindow::onParseChannelFinished, Qt::QueuedConnection);
    connect(mParseWorker, &NeutronYieldParseWorker::finished,
            this, &NeutronYieldStatisticsWindow::onParseFinished, Qt::QueuedConnection);
    connect(mParseWorkerThread, &QThread::finished,
            this, &NeutronYieldStatisticsWindow::onParseWorkerThreadFinished);

    mParseWorkerThread->start();
}

void NeutronYieldStatisticsWindow::setParseBusy(bool busy)
{
    ui->action_startCalculate->setEnabled(!busy);
    ui->action_stopCalculate->setEnabled(busy);
    ui->pushButton_start->setEnabled(!busy);
    ui->updateEnCalibration_bt->setEnabled(!busy);
    ui->action_open->setEnabled(!busy);
    ui->bt_addFiles->setEnabled(!busy);
    ui->bt_removeFile->setEnabled(!busy);
    ui->bt_clearFiles->setEnabled(!busy);
    ui->btn_sort->setEnabled(!busy);
    ui->doubleSpinBox_fitR2Min->setEnabled(!busy);
    updateClearActionEnabled();
}

void NeutronYieldStatisticsWindow::updateClearActionEnabled()
{
    ui->action_clear->setEnabled(mParseWorkerThread == nullptr);
}

void NeutronYieldStatisticsWindow::clearDetectorTableResults()
{
    ui->twResults->blockSignals(true);
    for (int row = 0; row < DET_NUM; ++row) {
        for (int col : {1, 2, 5, 6}) {
            if (QTableWidgetItem *item = ui->twResults->item(row, col))
                item->setText(QString());
        }
        if (mPeakChannelCurveColumnReady) {
            if (QCheckBox *cb = peakChannelCurveCheckBox(row)) {
                cb->setChecked(true);
                cb->setEnabled(false);
            }
        }
    }
    ui->twResults->blockSignals(false);
    if (mPeakChannelCurveColumnReady)
        updatePeakChannelSelectAllState();
}

void NeutronYieldStatisticsWindow::clearAllPlotViews()
{
    slotUpdateMultiSegmentPlotDatas({});

    if (QCustomPlot *countPlot = ui->spectorMeter_Count) {
        const QStringList graphNames = {
            QStringLiteral("timeCountGraph"),
            QStringLiteral("timeCountFitGraph"),
            QStringLiteral("timeCountRemainGraph"),
            QStringLiteral("Spec909_Graph"),
            QStringLiteral("spec909_FitGraph"),
            QStringLiteral("Spec909_residualGraph"),
        };
        for (const QString &name : graphNames) {
            if (QCPGraph *graph = countPlot->graph(name))
                graph->data()->clear();
        }
        countPlot->replot();
    }

    if (QCustomPlot *peakPlot = ui->spectorMeter_PeakChannel) {
        for (int i = 0; i < peakPlot->graphCount(); ++i) {
            if (QCPGraph *graph = peakPlot->graph(i)) {
                graph->data()->clear();
                graph->setVisible(false);
            }
        }
        peakPlot->replot();
    }

    slotUpdate_yieldDis({});
}

void NeutronYieldStatisticsWindow::clearAllParseResults()
{
    for (int i = 0; i < DET_NUM; ++i) {
        delete dealFile[i];
        dealFile[i] = nullptr;
    }

    mDealSucess.clear();
    myield.clear();
    m_currentMergeSpectrum.clear();

    clearDetectorTableResults();
    clearAllPlotViews();
}

void NeutronYieldStatisticsWindow::on_action_clear_triggered()
{
    if (mParseWorkerThread)
        return;

    if (QMessageBox::question(this, tr("确认"),
                              tr("确定清除所有处理结果吗？文件列表和经纬度将保留。")) != QMessageBox::Yes) {
        return;
    }

    clearAllParseResults();
    emit reporWriteLog(tr("已清除全部处理结果。"));
}

void NeutronYieldStatisticsWindow::onParseProgress(int value, int maximum, const QString &message)
{
    if (mProgressBar) {
        mProgressBar->setMaximum(qMax(1, maximum));
        mProgressBar->setValue(qBound(0, value, mProgressBar->maximum()));
    }
    ui->statusbar->showMessage(message);
}

void NeutronYieldStatisticsWindow::onParseLogMessage(const QString &msg, QtMsgType type)
{
    emit reporWriteLog(msg, type);
}

// 909峰位演变图
void NeutronYieldStatisticsWindow::onParseChannelFinished(int detId, bool mergeOk, const QStringList &fitErrors)
{
    for (const QString &msg : fitErrors)
        emit reporWriteLog(msg, QtWarningMsg);

    mDealSucess[detId] = false;

    if (!mergeOk || !mParseWorker) {
        clearDetectorTableSpecCount(detId);
        emit reporWriteLog(tr("谱仪%1解析失败").arg(detId));
        emit reporWriteLog(QString("谱仪%1解析完成").arg(detId));
        return;
    }

    ParseData *parsed = mParseWorker->takeParseData(detId);
    if (!parsed) {
        clearDetectorTableSpecCount(detId);
        emit reporWriteLog(tr("谱仪%1解析失败").arg(detId));
        emit reporWriteLog(QString("谱仪%1解析完成").arg(detId));
        return;
    }

    delete dealFile[detId - 1];
    dealFile[detId - 1] = parsed;

    updateDetectorTableSpecCount(detId);

    const bool fitOk = dealFile[detId - 1]->successfulFitSegmentCount() > 0;
    mDealSucess[detId] = fitOk;
    if (fitOk) {
        double energyK = 0.0;
        double energyB = 0.0;
        // 解析成功后只缓存最新 K/B；是否写入 device_config.ini 由“导出能量刻度”按钮决定。
        if (dealFile[detId - 1]->latestEnergyCalibration(&energyK, &energyB)) {
            double sigma[3] = {5.0, 6.0, 7.0};
            const bool hasSigma = dealFile[detId - 1]->latestSigmas(sigma);
            emit reporWriteLog(tr("谱仪%1解析成功，已生成最新能量刻度：K=%2，B=%3，可点击“导出能量刻度”写入 device_config.ini。")
                                   .arg(detId)
                                   .arg(energyK, 0, 'g', 12)
                                   .arg(energyB, 0, 'g', 12));
            if (hasSigma) {
                emit reporWriteLog(tr("谱仪%1已生成最新 sigma：511=%2，846=%3，909=%4")
                                       .arg(detId)
                                       .arg(sigma[0], 0, 'g', 8)
                                       .arg(sigma[1], 0, 'g', 8)
                                       .arg(sigma[2], 0, 'g', 8));
            }
        } else {
            emit reporWriteLog(tr("谱仪%1解析成功，但未获得可写入的最新能量刻度。").arg(detId), QtWarningMsg);
        }
        emit sigSuccess(detId);
    }

    emit reporWriteLog(QString("谱仪%1解析完成").arg(detId));
}

void NeutronYieldStatisticsWindow::onParseFinished(bool cancelled, const QString &summaryMessage)
{
    if (cancelled)
        emit reporWriteLog(tr("解析被中断"));

    if (mProgressBar) {
        mProgressBar->setValue(mProgressBar->maximum());
        mProgressBar->hide();
    }

    emit reporWriteLog(summaryMessage);

    if (!cancelled) {
        ui->radioButton_count->setChecked(true);
        ui->stackedWidget->setCurrentIndex(1);
        QMessageBox::information(this, tr("提示"), summaryMessage);
    }

    if (mParseWorkerThread)
        mParseWorkerThread->quit();
}

void NeutronYieldStatisticsWindow::onParseWorkerThreadFinished()
{
    if (mParseWorker) {
        mParseWorker->deleteLater();
        mParseWorker = nullptr;
    }
    if (mParseWorkerThread) {
        mParseWorkerThread->deleteLater();
        mParseWorkerThread = nullptr;
    }

    setParseBusy(false);
    ui->statusbar->showMessage(tr("就绪"));
}


void NeutronYieldStatisticsWindow::on_action_stopCalculate_triggered()
{
    emit reporWriteLog(tr("中断解析"));
    mInterrupted = true;
    if (mParseWorker)
        mParseWorker->requestCancel();
}

void NeutronYieldStatisticsWindow::updateYieldSpatialDistributionForRow(int row)
{
    if (row < 0 || row >= DET_NUM)
        return;

    const int detID = row + 1;
    QTableWidgetItem *yieldItem = ui->twResults->item(row, 1);
    QTableWidgetItem *longitudeItem = ui->twResults->item(row, 3);
    QTableWidgetItem *latitudeItem = ui->twResults->item(row, 4);

    const double yield = yieldItem ? yieldItem->text().toDouble() : 0.0;
    const double longitude = longitudeItem ? longitudeItem->text().toDouble() : 0.0;
    const double latitude = latitudeItem ? latitudeItem->text().toDouble() : 0.0;

    myield[detID] = Sensor{longitude, latitude, yield};
    slotUpdate_yieldDis(myield);
}

void NeutronYieldStatisticsWindow::on_twResults_itemChanged(QTableWidgetItem *item)
{
    if (!item)
        return;

    if (item->row() == kDetectorTableSelectAllRow)
        return;

    const int col = item->column();
    if (col != 3 && col != 4)
        return;

    bool ok = false;
    const double v = item->text().trimmed().toDouble(&ok);
    if (!ok) {
        ui->twResults->blockSignals(true);
        item->setText(QStringLiteral("0"));
        ui->twResults->blockSignals(false);
    } else if (col == 3 && (v < -180.0 || v > 180.0)) {
        QMessageBox::warning(this, tr("错误"), tr("经度范围必须是 -180~180"));
        ui->twResults->blockSignals(true);
        item->setText(QStringLiteral("0"));
        ui->twResults->blockSignals(false);
    } else if (col == 4 && (v < -90.0 || v > 90.0)) {
        QMessageBox::warning(this, tr("错误"), tr("纬度范围必须是 -90~90"));
        ui->twResults->blockSignals(true);
        item->setText(QStringLiteral("0"));
        ui->twResults->blockSignals(false);
    }

    updateYieldSpatialDistributionForRow(item->row());
}

void NeutronYieldStatisticsWindow::on_twResults_selectionChanged()
{
    QList<QTableWidgetItem*> selectedItems = ui->twResults->selectedItems();
    if (selectedItems.isEmpty())
        return;

    const int row = selectedItems[0]->row();
    if (row < 0 || row >= DET_NUM)
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
    if (detID > 0 && detID <= DET_NUM) {
        auto *countItem = new QTableWidgetItem(QString::number(count909_N0));
        applyDetectorTableItemFlags(countItem, 5);
        ui->twResults->setItem(detID-1, 5, countItem);

        //读取灵敏度标定数据，给出中子产额
        QSettings mSettings(neutronYieldConfigPath(), QSettings::IniFormat);
        double yield_c = mSettings.value(QString("Table/yield%1").arg(detID), 1.0).toDouble();
        double initN_c = mSettings.value(QString("Table/initial909Count%1").arg(detID), 1.0).toDouble();
        double yield = 0.0;
        if(initN_c>0.0){
            yield = count909_N0 / initN_c * yield_c;
        }

        auto *yieldItem = new QTableWidgetItem(QString::number(yield));
        applyDetectorTableItemFlags(yieldItem, 1);
        ui->twResults->setItem(detID-1, 1, yieldItem);

        // Uncertainty 列：衰减拟合参数 a 的相对不确定度 σ_a/|a|×100（%），与产额相对不确定度相同
        auto *uncItem = new QTableWidgetItem;
        applyDetectorTableItemFlags(uncItem, 2);
        double relSigmaAPct = 0.0;
        if (dealFile[detID - 1]->count909DecayFit(nullptr, nullptr, nullptr, &relSigmaAPct))
            uncItem->setText(QString::number(relSigmaAPct, 'f', 2));
        ui->twResults->setItem(detID - 1, 2, uncItem);

        //从界面左侧表格读取探测器经纬度(这意味着用户可以离线分析中手动修改各个探测器的坐标位置）
        updateYieldSpatialDistributionForRow(detID - 1);
    }

    if (detID == currentPictureDetId())
        updatePictureForDetector(detID);

    if (ui->stackedWidget->currentIndex() == 2) {
        ensurePeakChannelCurveColumn();
        updateAllPeakChannelPlot();
    }

    // QTimer::singleShot(1, this, [=](){
    //     SplashWidget::instance()->hide();
    // });
}

//更新多段能谱数据
void NeutronYieldStatisticsWindow::refreshSpectrumPlotSlot(int slotIndex)
{
    if (slotIndex < 1 || slotIndex > kSpectrumPlotSlotCount)
        return;

    QCustomPlot *customPlot = ui->spectorMeter_Spectrum;
    QWidget *flowLayoutContainer = ui->widget_flowLayoutContainer;
    QCheckBox *checkBox = flowLayoutContainer->findChild<QCheckBox *>(
        QStringLiteral("spectrumSlotCheck#%1").arg(slotIndex));
    QComboBox *comboBox = flowLayoutContainer->findChild<QComboBox *>(
        QStringLiteral("spectrumSlotCombo#%1").arg(slotIndex));
    QCPGraph *graph = customPlot->graph(QStringLiteral("energyGraph#%1").arg(slotIndex));
    if (!checkBox || !comboBox || !graph)
        return;

    const bool shouldShow = checkBox->isChecked() && !m_currentMergeSpectrum.empty();
    if (!shouldShow || comboBox->count() == 0) {
        graph->setVisible(false);
        return;
    }

    const int specIndex = comboBox->currentData().toInt() - 1;
    if (specIndex < 0 || specIndex >= static_cast<int>(m_currentMergeSpectrum.size())) {
        graph->setVisible(false);
        return;
    }

    const QColor slotColor = spectrumSlotColors()[(slotIndex - 1) % spectrumSlotColors().size()];
    QVector<double> keys;
    QVector<double> values;
    QVector<QColor> colors;
    keys.reserve(2048);
    values.reserve(2048);
    colors.reserve(2048);
    for (int j = 0; j < 2048; ++j) {
        keys << j + 1;
        values << m_currentMergeSpectrum[static_cast<size_t>(specIndex)].spectrum[j] * 1.0;
        colors << slotColor;
    }
    graph->setData(keys, values, colors);
    graph->setVisible(true);
}

void NeutronYieldStatisticsWindow::refreshAllSpectrumPlotSlots()
{
    for (int slot = 1; slot <= kSpectrumPlotSlotCount; ++slot)
        refreshSpectrumPlotSlot(slot);

    QCustomPlot *customPlot = ui->spectorMeter_Spectrum;
    customPlot->rescaleAxes(true);
    customPlot->replot();
}

// 分时能谱图
void NeutronYieldStatisticsWindow::slotUpdateMultiSegmentPlotDatas(std::vector<ParseData::mergeSpecData> allSpectrum)
{
    QCustomPlot *customPlot = ui->spectorMeter_Spectrum;
    customPlot->clearGraphs();
    m_currentMergeSpectrum = std::move(allSpectrum);

    QFlowLayout *flowLayout = this->findChild<QFlowLayout *>("flowLayout");
    QWidget *flowLayoutContainer = ui->widget_flowLayoutContainer;
    while (QLayoutItem *item = flowLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        if (QLayout *childLayout = item->layout())
            delete childLayout;
        delete item;
    }

    const int specCount = static_cast<int>(m_currentMergeSpectrum.size());
    const QList<QColor> &colors = spectrumSlotColors();

    QCheckBox *checkBoxAll = new QCheckBox(flowLayoutContainer);
    checkBoxAll->setObjectName(QStringLiteral("checkBoxAll"));
    checkBoxAll->setText(tr("显示所有曲线"));
    checkBoxAll->setMinimumWidth(100);
    checkBoxAll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    checkBoxAll->setChecked(true);
    flowLayout->addWidget(checkBoxAll);

    for (int slot = 1; slot <= kSpectrumPlotSlotCount; ++slot) {
        QWidget *rowWidget = new QWidget(flowLayoutContainer);
        rowWidget->setObjectName(QStringLiteral("spectrumSlotWidget#%1").arg(slot));
        rowWidget->setContentsMargins(0, 0, 0, 0);
        rowWidget->setFixedWidth(130);

        QHBoxLayout *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setMargin(0);
        rowLayout->setSpacing(4);

        QCheckBox *checkBox = new QCheckBox(rowWidget);
        checkBox->setObjectName(QStringLiteral("spectrumSlotCheck#%1").arg(slot));
        checkBox->setChecked(true);

        QLabel *labelColor = new QLabel(rowWidget);
        labelColor->setFixedSize(20, 20);
        const QColor slotColor = colors[(slot - 1) % colors.size()];
        labelColor->setStyleSheet(
            QStringLiteral("background-color:rgb(%1,%2,%3)")
                .arg(slotColor.red())
                .arg(slotColor.green())
                .arg(slotColor.blue()));

        QComboBox *comboBox = new QComboBox(rowWidget);
        comboBox->setObjectName(QStringLiteral("spectrumSlotCombo#%1").arg(slot));
        comboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        for (int specNo = 1; specNo <= specCount; ++specNo)
            comboBox->addItem(QStringLiteral("#%1").arg(specNo), specNo);

        if (specCount > 0) {
            const int defaultSpecNo = defaultSpecNoForSlot(slot, specCount);
            comboBox->setCurrentIndex(comboBox->findData(defaultSpecNo));
        } else {
            comboBox->setEnabled(false);
            checkBox->setEnabled(false);
        }

        rowLayout->addWidget(checkBox);
        rowLayout->addWidget(labelColor);
        rowLayout->addWidget(comboBox);
        flowLayout->addWidget(rowWidget);

        connect(checkBox, &QCheckBox::stateChanged, this, [this, slot](int) {
            refreshSpectrumPlotSlot(slot);
            ui->spectorMeter_Spectrum->replot(QCustomPlot::rpQueuedReplot);
        });
        connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, slot](int) {
            refreshSpectrumPlotSlot(slot);
            ui->spectorMeter_Spectrum->rescaleAxes(true);
            ui->spectorMeter_Spectrum->replot(QCustomPlot::rpQueuedReplot);
        });
    }

    connect(checkBoxAll, &QCheckBox::stateChanged, this, [this, flowLayoutContainer](int state) {
        const bool checked = (state == Qt::Checked);
        for (int slot = 1; slot <= kSpectrumPlotSlotCount; ++slot) {
            QCheckBox *slotCheckBox = flowLayoutContainer->findChild<QCheckBox *>(
                QStringLiteral("spectrumSlotCheck#%1").arg(slot));
            if (!slotCheckBox)
                continue;
            slotCheckBox->blockSignals(true);
            slotCheckBox->setChecked(checked);
            slotCheckBox->blockSignals(false);
        }
        refreshAllSpectrumPlotSlots();
    });

    for (int slot = 1; slot <= kSpectrumPlotSlotCount; ++slot) {
        QCPGraph *graph = customPlot->addGraph();
        graph->setName(QStringLiteral("energyGraph#%1").arg(slot));
        graph->setAntialiased(true);
        graph->setPen(QPen(QBrush(colors[(slot - 1) % colors.size()]), 2, Qt::SolidLine));
        graph->setLineStyle(QCPGraph::lsNone);
        graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCross, 5));
        graph->setVisible(false);
    }

    flowLayoutContainer->show();
    refreshAllSpectrumPlotSlots();
}

// 909全能峰计数图
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
            graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, 8));//显示散点图
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
            //graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, 5));//显示散点图
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
            //graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, 5));//显示散点图
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
            graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, 4));//显示散点图
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

// 中子产额空间分布图
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
    if (row < 0 || row >= DET_NUM)
        return false;

    // 范围校验（与 MollweideHeatmapWidget / Sensor 一致）
    if (lon < -180.0 || lon > 180.0)
        return false;
    if (lat < -90.0 || lat > 90.0)
        return false;

    ui->twResults->blockSignals(true); // 防止触发 itemChanged

    // 经度（第四列）
    QTableWidgetItem* lonItem = ui->twResults->item(row, 3);
    if (!lonItem) {
        lonItem = new QTableWidgetItem;
        applyDetectorTableItemFlags(lonItem, 3);
        ui->twResults->setItem(row, 3, lonItem);
    }
    lonItem->setText(QString::number(lon, 'f', 2));

    // 纬度（第五列）
    QTableWidgetItem* latItem = ui->twResults->item(row, 4);
    if (!latItem) {
        latItem = new QTableWidgetItem;
        applyDetectorTableItemFlags(latItem, 4);
        ui->twResults->setItem(row, 4, latItem);
    }
    latItem->setText(QString::number(lat, 'f', 2));

    ui->twResults->blockSignals(false);
    return true;
}

void NeutronYieldStatisticsWindow::writeCommonAnalysisParams(QXlsx::Document &xlsx, int &row, int channel) const
{
    const QStringList allFilePaths = filePathsFromTable();
    xlsx.write(row, 1, QStringLiteral("文件名"));
    xlsx.write(row++, 2, allFilePaths.join(';'));

    xlsx.write(row, 1, QStringLiteral("打靶时刻"));
    xlsx.write(row++, 2, ui->fusionDateTime->dateTime().toString("yyyy-MM-dd HH:mm:ss"));
    xlsx.write(row, 1, QStringLiteral("开测时刻"));
    xlsx.write(row++, 2, ui->messureDateTime->dateTime().toString("yyyy-MM-dd HH:mm:ss"));
    xlsx.write(row, 1, QStringLiteral("通道号"));
    if (channel <= 0)
        xlsx.write(row++, 2, QStringLiteral("全部通道"));
    else
        xlsx.write(row++, 2, channel);
    xlsx.write(row, 1, QStringLiteral("解析时间起点/min"));
    xlsx.write(row++, 2, ui->spinBox_timeStart->value());
    xlsx.write(row, 1, QStringLiteral("解析时间终点/min"));
    xlsx.write(row++, 2, ui->spinBox_timeEnd->value());
    xlsx.write(row, 1, QStringLiteral("时间步长/min"));
    xlsx.write(row++, 2, ui->spinBox_step->value());
    xlsx.write(row, 1, QStringLiteral("拟合优度下限R²"));
    xlsx.write(row++, 2, ui->doubleSpinBox_fitR2Min->value());
}

bool NeutronYieldStatisticsWindow::exportResultTableXlsx(const QString &filePath) const
{
    QXlsx::Document xlsx;
    const QStringList headers = {
        QStringLiteral("DetectorID"),
        QStringLiteral("Yield"),
        QStringLiteral("Uncertainty"),
        QStringLiteral("Longitude"),
        QStringLiteral("Latitude"),
        QStringLiteral("Initial909Count"),
        QStringLiteral("SpecCount")
    };
    for (int c = 0; c < headers.size(); ++c)
        xlsx.write(1, c + 1, headers.at(c));

    int outRow = 2;
    for (int row = 0; row < DET_NUM; ++row) {
        QTableWidgetItem *specCountItem = ui->twResults->item(row, 6);
        if (!specCountItem || specCountItem->text().isEmpty())
            continue;
        for (int col = 0; col < 7; ++col) {
            QTableWidgetItem *item = ui->twResults->item(row, col);
            xlsx.write(outRow, col + 1, item ? item->text() : QString());
        }
        ++outRow;
    }
    return xlsx.saveAs(filePath);
}

bool NeutronYieldStatisticsWindow::exportPeakEvolutionXlsx(const QString &filePath) const
{
    QXlsx::Document xlsx;
    const QString sheetName = QStringLiteral("909峰位演变");
    if (!xlsx.sheetNames().isEmpty())
        xlsx.renameSheet(xlsx.sheetNames().first(), sheetName);
    else
        xlsx.addSheet(sheetName);
    xlsx.selectSheet(sheetName);

    int row = 1;
    writeCommonAnalysisParams(xlsx, row, 0);

    bool hasData = false;
    row++;
    for (int detId = 1; detId <= DET_NUM; ++detId) {
        ParseData *parsed = dealFile[detId - 1];
        if (!parsed)
            continue;

        const QVector<double> peakPos = parsed->count909PeakPos();
        const QVector<specStripData> countData = parsed->GetCount909Data();
        if (peakPos.isEmpty() || countData.isEmpty())
            continue;

        const int n = qMin(peakPos.size(), countData.size());
        if (n <= 0)
            continue;

        hasData = true;
        int col = 1;
        xlsx.write(row, col, QStringLiteral("通道#%1_时间(分钟)").arg(detId));
        xlsx.write(row + 1, col, QStringLiteral("通道#%1_峰位(道址)").arg(detId));
        col++;
        for (int i = 0; i < n; ++i) {
            xlsx.write(row, col + i, countData.at(i).x);
            xlsx.write(row + 1, col + i, peakPos.at(i));
        }
        row += 3;
    }

    if (!hasData)
        xlsx.write(row, 1, QStringLiteral("无可用的909峰位演变数据"));

    return xlsx.saveAs(filePath);
}

bool NeutronYieldStatisticsWindow::exportMultiSegmentXlsx(const QString &filePath) const
{
    QXlsx::Document xlsx;
    bool hasSheet = false;

    for (int detId = 1; detId <= DET_NUM; ++detId) {
        ParseData *parsed = dealFile[detId - 1];
        if (!parsed)
            continue;

        const QVector<ParseData::mergeSpecData> mergeSpec = parsed->GetMergeSpec();
        if (mergeSpec.isEmpty())
            continue;

        const QString sheetName = QStringLiteral("通道#") + QString::number(detId);
        if (xlsx.sheetNames().contains(sheetName))
            xlsx.deleteSheet(sheetName);
        if (!hasSheet && !xlsx.sheetNames().isEmpty()) {
            xlsx.renameSheet(xlsx.sheetNames().first(), sheetName);
        } else {
            xlsx.insertSheet(xlsx.sheetNames().size(), sheetName);
        }
        xlsx.selectSheet(sheetName);
        hasSheet = true;

        int row = 1;
        int col = 1;
        writeCommonAnalysisParams(xlsx, row, detId);

        col = 1;
        row++;
        xlsx.write(row, col++, QStringLiteral("分时段"));
        xlsx.write(row, col++, QStringLiteral("能谱测量结束时刻"));
        xlsx.write(row, col++, QStringLiteral("时间步长，单位ms"));
        xlsx.write(row, col++, QStringLiteral("死时间，单位ns"));
        xlsx.write(row, col++, QStringLiteral("合并子能谱个数"));
        xlsx.write(row, col++, QStringLiteral("各子能谱测量时长累加，单位 ms"));
        xlsx.write(row, col++, QStringLiteral("道址"));
        for (int i = 0; i < 2048; ++i)
            xlsx.write(row, col + i, i + 1);

        row++;
        for (int i = 0; i < mergeSpec.size(); ++i) {
            col = 1;
            xlsx.write(row, col++, i + 1);
            xlsx.write(row, col++, mergeSpec.at(i).currentTime);
            xlsx.write(row, col++, static_cast<qulonglong>(mergeSpec.at(i).specTime));
            xlsx.write(row, col++, static_cast<qulonglong>(mergeSpec.at(i).deathTime));
            xlsx.write(row, col++, mergeSpec.at(i).rowCount);
            xlsx.write(row, col++, static_cast<qulonglong>(mergeSpec.at(i).sumMeasureTimeMs));
            xlsx.write(row, col++, QStringLiteral("值"));
            for (int j = 0; j < 2048; ++j)
                xlsx.write(row, col + j, mergeSpec.at(i).spectrum[j]);
            row++;
        }
    }

    if (!hasSheet) {
        xlsx.write(1, 1, QStringLiteral("无可用的分时能谱数据"));
    }
    return xlsx.saveAs(filePath);
}

bool NeutronYieldStatisticsWindow::exportCount909Xlsx(const QString &filePath) const
{
    QXlsx::Document xlsx;
    bool hasSheet = false;

    for (int detId = 1; detId <= DET_NUM; ++detId) {
        ParseData *parsed = dealFile[detId - 1];
        if (!parsed)
            continue;

        const QVector<specStripData> countData = parsed->GetCount909Data();
        if (countData.isEmpty())
            continue;

        const QString sheetName = QStringLiteral("通道#") + QString::number(detId);
        if (xlsx.sheetNames().contains(sheetName))
            xlsx.deleteSheet(sheetName);
        if (!hasSheet && !xlsx.sheetNames().isEmpty()) {
            xlsx.renameSheet(xlsx.sheetNames().first(), sheetName);
        } else {
            xlsx.insertSheet(xlsx.sheetNames().size(), sheetName);
        }
        xlsx.selectSheet(sheetName);
        hasSheet = true;

        int row = 1;
        int col = 1;
        writeCommonAnalysisParams(xlsx, row, detId);

        col = 1;
        row++;
        xlsx.write(row, col++, QStringLiteral("时间，单位 min"));
        for (int i = 0; i < countData.size(); ++i)
            xlsx.write(row, col + i, countData.at(i).x);

        row++;
        col = 1;
        xlsx.write(row, col++, QStringLiteral("计数率/cps"));
        for (int i = 0; i < countData.size(); ++i)
            xlsx.write(row, col + i, countData.at(i).y);

        row++;
        col = 1;
        xlsx.write(row, col++, QStringLiteral("计数率拟合曲线"));
        for (int i = 0; i < countData.size(); ++i)
            xlsx.write(row, col + i, countData.at(i).fit_y);

        row++;
        col = 1;
        xlsx.write(row, col++, QStringLiteral("残差(%)"));
        for (int i = 0; i < countData.size(); ++i)
            xlsx.write(row, col + i, countData.at(i).residual_rate);

        {
            double fitA = 0.0;
            double fitR2 = 0.0;
            double sigmaA = 0.0;
            double relSigmaAPct = 0.0;
            if (parsed->count909DecayFit(&fitA, &fitR2, &sigmaA, &relSigmaAPct)) {
                row++;
                col = 1;
                xlsx.write(row, col++, QStringLiteral("计数率拟合公式"));
                xlsx.write(row, col, QStringLiteral("y = a*exp(-ln(2)/(78.4*60)*t)，t 单位 min（逆方差加权）"));
                row++;
                col = 1;
                xlsx.write(row, col++, QStringLiteral("a"));
                xlsx.write(row, col, fitA);
                row++;
                col = 1;
                xlsx.write(row, col++, QStringLiteral("σ_a"));
                xlsx.write(row, col, sigmaA);
                row++;
                col = 1;
                xlsx.write(row, col++, QStringLiteral("相对不确定度(%)"));
                xlsx.write(row, col, relSigmaAPct);
                row++;
                col = 1;
                xlsx.write(row, col++, QStringLiteral("R²"));
                xlsx.write(row, col, fitR2);
            }
        }

        // 各时段 909 段能谱（与 PeakChannel/计数图对应的剥谱结果）
        const QVector<SpecStripFitInfo> &stripFits = parsed->specStripFitInfos();
        row += 2;
        for (int i = 0; i < countData.size(); ++i) {
            col = 1;
            xlsx.write(row, col, QStringLiteral("时间(分钟)"));
            xlsx.write(row + 1, col, QStringLiteral("能量(keV)"));
            xlsx.write(row + 2, col, QStringLiteral("能谱计数"));
            xlsx.write(row + 3, col, QStringLiteral("拟合曲线"));
            xlsx.write(row + 4, col, QStringLiteral("残差(%)"));

            col++;
            xlsx.write(row, col, countData.at(i).x);
            const QVector<specStripData> allSpec = parsed->GetStripData(i);
            for (const auto &data : allSpec) {
                xlsx.write(row + 1, col, data.x);
                xlsx.write(row + 2, col, data.y);
                xlsx.write(row + 3, col, data.fit_y);
                xlsx.write(row + 4, col++, data.residual_rate);
            }
            row += 6;

            if (i < stripFits.size() && stripFits.at(i).model != SpecStripFitModel::None) {
                const SpecStripFitInfo &info = stripFits.at(i);
                col = 1;
                xlsx.write(row, col++, QStringLiteral("能谱拟合公式"));
                xlsx.write(row, col, info.formulaText());
                row++;
                col = 1;
                xlsx.write(row, col++, QStringLiteral("R²"));
                xlsx.write(row, col, info.r2);
                row += 2;
            }
        }
    }

    if (!hasSheet) {
        xlsx.write(1, 1, QStringLiteral("无可用的909全能峰计数数据"));
    }
    return xlsx.saveAs(filePath);
}

bool NeutronYieldStatisticsWindow::exportYieldDisXlsx(const QString &filePath) const
{
    QXlsx::Document xlsx;
    const int channel = currentPictureDetId();
    const QString sheetName = QStringLiteral("通道#") + QString::number(channel);
    if (!xlsx.sheetNames().isEmpty())
        xlsx.renameSheet(xlsx.sheetNames().first(), sheetName);
    else
        xlsx.addSheet(sheetName);
    xlsx.selectSheet(sheetName);

    int row = 1;
    int col = 1;
    writeCommonAnalysisParams(xlsx, row, channel);

    col = 1;
    row++;
    xlsx.write(row, col, QStringLiteral("经度"));
    xlsx.write(row + 1, col, QStringLiteral("纬度"));
    xlsx.write(row + 2, col, QStringLiteral("中子产额"));
    col++;
    for (auto it = myield.constBegin(); it != myield.constEnd(); ++it) {
        xlsx.write(row, col, it.value().lonDeg);
        xlsx.write(row + 1, col, it.value().latDeg);
        xlsx.write(row + 2, col++, it.value().value);
    }

    return xlsx.saveAs(filePath);
}

void NeutronYieldStatisticsWindow::on_bt_saveResult_clicked()
{
    const QString filePath = ui->lineEdit_savepath->text().trimmed();
    if (!QFileInfo::exists(filePath)) {
        QMessageBox::information(this, tr("提示"), tr("文件路径不存在，请重新选择！"));
        return;
    }

    QString prefix = ui->lineEdit_fileName->text().trimmed();
    if (prefix.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive))
        prefix.chop(4);
    if (prefix.endsWith(QStringLiteral(".xlsx"), Qt::CaseInsensitive))
        prefix.chop(5);
    prefix = prefix.trimmed();
    if (prefix.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("请输入文件名前缀！"));
        return;
    }

    const QStringList suffixes = {
        QStringLiteral("计算结果.xlsx"),
        QStringLiteral("909峰位演变数据.xlsx"),
        QStringLiteral("分时能谱.xlsx"),
        QStringLiteral("909全能峰计数.xlsx"),
        QStringLiteral("中子产额空间分布.xlsx"),
        QStringLiteral("中子产额空间分布.png")
    };

    QStringList outputPaths;
    outputPaths.reserve(suffixes.size());
    for (const QString &suffix : suffixes)
        outputPaths << QDir(filePath).filePath(prefix + QLatin1Char('_') + suffix);

    bool anyExists = false;
    for (const QString &path : outputPaths) {
        if (QFileInfo::exists(path)) {
            anyExists = true;
            break;
        }
    }
    if (anyExists) {
        if (QMessageBox::question(this, tr("提示"),
                                  tr("部分结果文件已存在，是否覆盖？"),
                                  QMessageBox::Yes, QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
    }

    struct SaveItem {
        QString path;
        bool ok;
    };
    QVector<SaveItem> results;
    results.append({outputPaths.at(0), exportResultTableXlsx(outputPaths.at(0))});
    results.append({outputPaths.at(1), exportPeakEvolutionXlsx(outputPaths.at(1))});
    results.append({outputPaths.at(2), exportMultiSegmentXlsx(outputPaths.at(2))});
    results.append({outputPaths.at(3), exportCount909Xlsx(outputPaths.at(3))});
    results.append({outputPaths.at(4), exportYieldDisXlsx(outputPaths.at(4))});
    results.append({outputPaths.at(5), w && w->exportToPng(outputPaths.at(5))});

    int okCount = 0;
    QStringList failed;
    for (const SaveItem &item : results) {
        if (item.ok) {
            ++okCount;
            emit reporWriteLog(tr("文件保存成功，路径：%1").arg(item.path));
        } else {
            failed << item.path;
            emit reporWriteLog(tr("文件保存失败，路径：%1").arg(item.path), QtWarningMsg);
        }
    }

    if (failed.isEmpty()) {
        QMessageBox::information(this, tr("提示"),
                                 tr("已成功保存 %1 个结果文件到：\n%2").arg(okCount).arg(filePath));
    } else {
        QMessageBox::warning(this, tr("提示"),
                             tr("成功 %1 个，失败 %2 个：\n%3")
                                 .arg(okCount)
                                 .arg(failed.size())
                                 .arg(failed.join('\n')));
    }
}


// updateEnCalibration_bt 当前作为“导出能量刻度”按钮使用，保留旧对象名以匹配 Qt 自动槽连接。
void NeutronYieldStatisticsWindow::on_updateEnCalibration_bt_clicked()
{
    if (mParseWorkerThread) {
        QMessageBox::information(this, tr("提示"), tr("正在解析中，不能导出能量刻度。"));
        return;
    }

    QVector<EnergyCalCompareRow> compareRows;
    compareRows.reserve(DET_NUM);
    for (int detectorId = 1; detectorId <= DET_NUM; ++detectorId) {
        EnergyCalCompareRow row;
        row.detectorId = detectorId;

        EnergyCalibrationStore::DetectorRecord oldRec;
        if (EnergyCalibrationStore::loadDetector(detectorId, &oldRec) && oldRec.type == 1
            && std::isfinite(oldRec.c0) && std::isfinite(oldRec.c1) && !qFuzzyIsNull(oldRec.c0)) {
            row.hasOld = true;
            row.oldK = oldRec.c0;
            row.oldB = oldRec.c1;
            if (oldRec.hasSigma) {
                row.hasOldSigma = true;
                row.oldSigma[0] = oldRec.sigma511;
                row.oldSigma[1] = oldRec.sigma846;
                row.oldSigma[2] = oldRec.sigma909;
            }
        }

        if (mDealSucess.value(detectorId, false) && dealFile[detectorId - 1]
            && dealFile[detectorId - 1]->latestEnergyCalibration(&row.newK, &row.newB)
            && dealFile[detectorId - 1]->latestSigmas(row.newSigma)) {
            row.hasNew = true;
        }

        compareRows.push_back(row);
    }

    const QSet<int> selectedDetectors = selectEnergyCalibrationDetectors(
        this,
        tr("选择导出能量刻度的谱仪"),
        tr("请对比原有与最新能量刻度后勾选需要写入 device_config.ini 的谱仪："),
        compareRows);
    if (selectedDetectors.isEmpty()) {
        emit reporWriteLog(tr("未选择需要导出能量刻度的谱仪，已取消导出。"), QtInfoMsg);
        return;
    }

    int exportedCount = 0;
    int skippedCount = 0;
    for (int detectorId : selectedDetectors) {
        if (detectorId < 1 || detectorId > DET_NUM
            || !mDealSucess.value(detectorId, false)
            || !dealFile[detectorId - 1]) {
            ++skippedCount;
            emit reporWriteLog(tr("谱仪%1尚未解析成功，已跳过能量刻度导出。").arg(detectorId), QtWarningMsg);
            continue;
        }

        double energyK = 0.0;
        double energyB = 0.0;
        if (!dealFile[detectorId - 1]->latestEnergyCalibration(&energyK, &energyB)) {
            ++skippedCount;
            emit reporWriteLog(tr("谱仪%1没有可导出的最新能量刻度，已跳过。").arg(detectorId), QtWarningMsg);
            continue;
        }

        QVector<double> pointsX;
        QVector<double> pointsY;
        if (!dealFile[detectorId - 1]->latestEnergyCalibrationPoints(&pointsX, &pointsY)) {
            ++skippedCount;
            emit reporWriteLog(tr("谱仪%1没有可导出的能量刻度拟合点，已跳过。").arg(detectorId), QtWarningMsg);
            continue;
        }

        double sigma[3] = {5.0, 6.0, 7.0};
        if (!dealFile[detectorId - 1]->latestSigmas(sigma)) {
            ++skippedCount;
            emit reporWriteLog(tr("谱仪%1没有可导出的最新 sigma，已跳过。").arg(detectorId), QtWarningMsg);
            continue;
        }

        QString writeError;
        if (!EnergyCalibrationStore::saveLinearDetector(detectorId, pointsX, pointsY, energyK, energyB, sigma, &writeError)) {
            ++skippedCount;
            emit reporWriteLog(tr("谱仪%1导出能量刻度失败：%2").arg(detectorId).arg(writeError), QtWarningMsg);
            continue;
        }

        if (!updateInitialCalibrationFromEnergyScale(detectorId, energyK, energyB)) {
            ++skippedCount;
            emit reporWriteLog(tr("谱仪%1已写入 device_config.ini，但同步当前初始能量刻度失败。").arg(detectorId), QtWarningMsg);
            continue;
        }

        if (!ParseData::setInitialSigmas(detectorId, sigma[0], sigma[1], sigma[2])) {
            ++skippedCount;
            emit reporWriteLog(tr("谱仪%1已写入 device_config.ini，但同步当前初始 sigma 失败。").arg(detectorId), QtWarningMsg);
            continue;
        }

        ++exportedCount;
        emit reporWriteLog(tr("谱仪%1已导出能量刻度到 device_config.ini：K=%2，B=%3，sigma=[%4,%5,%6]")
                               .arg(detectorId)
                               .arg(energyK, 0, 'g', 12)
                               .arg(energyB, 0, 'g', 12)
                               .arg(sigma[0], 0, 'g', 8)
                               .arg(sigma[1], 0, 'g', 8)
                               .arg(sigma[2], 0, 'g', 8));
    }

    QMessageBox::information(this, tr("提示"),
                             tr("能量刻度导出完成：%1 个谱仪已写入 device_config.ini，%2 个谱仪跳过。")
                                 .arg(exportedCount)
                                 .arg(skippedCount));
    emit reporWriteLog(tr("能量刻度导出完成：%1 个谱仪已写入 device_config.ini，%2 个谱仪跳过。")
                           .arg(exportedCount)
                           .arg(skippedCount));
}
