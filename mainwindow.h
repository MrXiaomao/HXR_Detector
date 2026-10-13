#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QPixmap>
#include <QTextEdit>
#include <QVector>
#include <atomic>
#include <vector>
#include "spectypes.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QCloseEvent;
class QEvent;

class CommHelper;
class ClientPeersWindow;
class DetSettingWindow;
class OTAUpgradeWindow;
class QGoodWindowHelper;
class QCustomPlot;
class QCPGraph;
class QTimer;
class QComboBox;
class QCheckBox;

// 探测器数据结构
struct DetectorData {
    // double countRate;                 // 当前计数率
    QVector<double> countRateHistory;    // 计数率历史，cps，每秒钟更新一次
    quint32 spectrum[8192];              // 累积能谱 (固定长度8192)
    quint32 lastSpectrumID;              // 上次测量累积时间的能谱序号
    quint32 lastAccumulateCount;         // 上次测量累积时间的计数率,暂时不考虑丢包带来的计数率修复
    double lastAccuCountFixed;     // 上次测量累积时间的计数率，考虑死时间修复后的值
    /** 计数率曲线已画到的最右 X（秒）；重测时叠墙钟缺口 deltaT 后再作为新段 offset */
    double lastCountRatePlotTimeSec;
    /** 当前测量段的时间偏置：plotTime = offset + sequence*measureTime/1000 */
    double countRateTimeOffsetSec;
    /** 掉线/停收时的墙钟 ms，用于计算重测缺口 */
    qint64 disconnectWallMs;
    // QDateTime lastUpdate;

    DetectorData()
        : lastSpectrumID(0)
        , lastAccumulateCount(0)
        , lastAccuCountFixed(0.0)
        , lastCountRatePlotTimeSec(0.0)
        , countRateTimeOffsetSec(0.0)
        , disconnectWallMs(0)
    {
        countRateHistory.clear();
        for(int i=0; i<8192; i++) spectrum[i] = 0;// 初始化能谱为全0
    }
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(bool isDarkTheme = true, QWidget *parent = nullptr);
    ~MainWindow();

    /*
    初始化
    */
    void initUi();
    void initNet();
    void restoreSettings();
    void initCustomPlot(int index, QCustomPlot* customPlot, QString axisXLabel, QString axisYLabel, QString title, int graphCount = 1);
    //清除全部图像数据（计数曲线、能谱曲线）
    void clearCustomPlotData(const std::vector<int>& channels = {});
    void applyColorTheme();

    // 新增的探测器数据管理方法
    void resetDetectorSpectrum(int detectorId);
    DetectorData getDetectorData(int detectorId) const;
    bool isDetectorOnline(int detectorId) const;
    QList<int> getOnlineDetectors() const;

    // 更新能谱显示
    void updateSpecPlotData(int detectorId, const quint32 spectrum[]);

    /**
     * @brief 计数率显示，最快更新时间间隔不小于1000ms
     * @param detectorId 探测器编号
     * @param FPGATime FPGA内部时钟，当前的计数率对应的FPGA时钟，单位s，能谱数据中单位是ms，这里采用向下取整
     * @param countRate 计数率，单位cps
     */
    void updateCountRatePlotData(int detectorId, double FPGATime, double countRate);

    // 将秒数转换为 天/时分秒 格式字符串
    QString formatTimeString(int totalSeconds);

private:
    //最近窗口的 min/max
    static inline QPair<double,double> calcRecentYRange(const QVector<double>& history, int windowPoints)
    {
        if (history.isEmpty())
            return {0.0, 1.0};

        int n = history.size();
        int start = qMax(0, n - windowPoints);

        double ymin = history[start];
        double ymax = history[start];
        for (int i = start + 1; i < n; ++i) {
            double v = history[i];
            if (v < ymin) ymin = v;
            if (v > ymax) ymax = v;
        }
        if (qFuzzyCompare(ymin, ymax)) {
            ymax = ymin + 1.0; // 防止上下界相等导致范围为0
        }
        return {ymin, ymax};
    }

public:
    virtual void closeEvent(QCloseEvent *event) override;
    virtual bool eventFilter(QObject *watched, QEvent *event) override;

    bool checkStatusTipEvent(QEvent * event);

public slots:
    void slotWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);//操作日志

signals:
    void sigUpdateBootInfo(const QString &msg);
    void sigWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);
    void detectorDataUpdated(int detectorId, double countRate, const int spectrum[]);

private slots:
    // 配置参数设置
    void on_action_cfgParam_triggered();
    // 退出程序
    void on_action_exit_triggered();
    // 测量模式选择：手动测量/继续测量
    void onMeasureModeChanged(int index);
    // 触发模式选择：软件触发/外触发
    void onTriggerModelChanged(int index);
    // 全通道开始测量
    void on_action_startAllMeasure_triggered();
    // 全通道停止测量
    void on_action_stopAllMeasure_triggered();
    // 自定义通道开始测量
    void on_btn_startSelectedMeasure_clicked();
    // 自定义通道停止测量
    void on_btn_stopSelectedMeasure_clicked();
    // 打开所有通道POE电源
    void on_action_powerOn_triggered();
    // 关闭所有通道POE电源
    void on_action_powerOff_triggered();
    // 清空图像曲线数据（非测量状态）
    void on_action_picture_reset_triggered();
    // 开启服务，打开本地TCP server
    void on_action_startServer_triggered();
    // 关闭服务，关闭本地TCP server
    void on_action_stopServer_triggered();
    //视图
    void on_actiontoolBar_triggered();
    // 关于栏
    void on_action_about_triggered();
    // 关于Qt
    void on_action_aboutQt_triggered();
    // 明亮主题色
    void on_action_lightTheme_triggered();
    // 暗黑主题色
    void on_action_darkTheme_triggered();
    // 颜色主题设置
    void on_action_colorTheme_triggered();
    // 查看本地网络服务
    void on_action_localService_triggered();
    
    // 交换机连接与断开
    void on_action_connectSwitch_triggered();

    // 测量倒计时结束处理
    void onMeasureCountdownTimeout();
    // 清理日志
    void on_bt_clearLog_clicked();

    // 更新所有能谱图像的属性,detectorId为0时更新所有能谱图像的属性
    void updateSpectrumPlotSettings(int detectorId=0);

    // 自定测量定时检查
    void onAutoMeasureDelayTimeout();

    // 处理数据解析后的能谱数据（解析线程）：累积数据；绘图投递到主线程
    void onFullSpectrum_Thread(int detID, const FullSpectrum& fullSpectrum);
    // 解析线程上报温度，仅写缓存
    void onReportDetectorTemperature(quint8 index, float temperature, const QDateTime& trTime);

    // 系统刷新定时器，暂定于每秒钟刷新1次，刷新内容包括：测量时间、能谱图像、计数率曲线等
    void onSystemTimer();

    // 交换机登录状态更新
    void onSwitcherLogged(const QString& ip);

    // 探测器上线处理
    void onDetectorOnline(quint8 index);

    // 探测器离线处理
    void onDetectorOffline(quint8 index);

    // 交换机POE电源状态报告
    void onPoePowerStatusReport(quint8 index, bool on);

    // 日志内容查找功能
    void on_bt_search_clicked();
    void on_bt_searchPrevious_clicked();
    void on_bt_searchNext_clicked();
    void on_bt_highlightAll_toggled(bool checked);
    void on_lineEdit_search_returnPressed();
    void on_lineEdit_search_textChanged(const QString &text);
    void on_action_energycalibration_triggered();
    void on_action_yieldCalibration_triggered();
    void on_checkBox_continueMeasure_toggled(bool toggled);
    void on_cbb_energyCalibration_toggled(bool checked);
    void on_action_particalMode_triggered();
    // 计数率统计
    void on_action_countRateStatistics_triggered();

    void on_action_neutronYieldStatistics_triggered();

    void on_action_spectrumH5Rebuild_triggered();

    void on_action_waveformMode_triggered();

    void on_lineEdit_filePath_editingFinished();

    void on_action_OTA_triggered();

    void on_actionstatusBar_triggered();

private:
    struct PendingLogEntry {
        QString timeText;   // yyyy-MM-dd hh:mm:ss.zzz
        QString msg;
        QtMsgType msgType = QtDebugMsg;
    };

    QString increaseShotNumSuffix(QString shotNumStr);
    QCustomPlot* getCustomPlot(int detectorId, bool isSpectrum = true);
    QCPGraph* getGraph(int detectorId, bool isSpectrum = true);
    void flushPendingLogs();

private:
    Ui::MainWindow *ui;
    ClientPeersWindow *mClientPeersWindow = nullptr;
    DetSettingWindow *mDetSettingWindow = nullptr;
    OTAUpgradeWindow *mOTAUpgradeWindow = nullptr;
    bool mIsMeasuring = false;

    QVector<quint8> m_selectedChannels; // 记录所选的通道号，停止测量时使用(自定义通道测量)
    bool mIsDarkTheme = true;
    bool mThemeColorEnable = true;
    QColor mThemeColor = QColor(255,255,255);
    class QGoodWindowHelper *mainWindow = nullptr;

    //记录联网的探测器ID
    QVector<quint8> mOnlineDetectors;
    
    //记录温度超时报警的探测器ID
    QVector<quint8> mTemperatureTimeoutDetectors;
    //记录探测器是否正在测量
    QMap<quint8, bool> mDetectorMeasuring;
    // 温度超时后重上线是否自动续测：能谱为 true，波形/粒子为 false
    bool mAutoResumeOnReconnect = true;
    // 探测器数据管理
    QHash<int, DetectorData> m_detectorData;  // 只存储联网探测器的数据
    mutable QMutex mDetectorDataMutex;       // 保护 m_detectorData（解析线程写、主线程读/绘图）
    CommHelper *commHelper = nullptr;
    
    // 测量倒计时定时器
    int mTotalCountdown = 0;  // 总倒计时时间（秒），用于计算已测量时长
    bool misTimedMeasure = false; // 是否继续测量模式

    // 自动化测量定时器
    QTimer *mStartMeasureDelayTimer = nullptr;// 开始测量倒计时（多用于自动化测量）
    QTimer *mAutoMeasureDelayTimer = nullptr;// 自动测量触发倒计时
    bool mAutoMeasureTimerActive = false; // 自动测量定时器是否正在倒计时
    
    // 连续测量计时器
    QElapsedTimer *mMeasureTimer = nullptr;

    // 交换机连接状态管理
    bool mSwitcherLogged = false;  // 交换机是否已登录
    bool mSwitcherConnected = false;  // 交换机是否已连接
    QTimer *mConnectButtonDisableTimer = nullptr;  // 连接按钮禁用定时器
    bool mUserActionLogEnabled = false;
    int m_lastMeasureModeIndex = -1;
    int m_lastTriggerModeIndex = -1;

    QVector<QColor> mGraphisColor;
    // 更新连接按钮状态（文本、图标、启用/禁用）
    void updateConnectButtonState(bool connected);
    void setupUserActionLogging();
    bool shouldLogComboUserChange(const QComboBox *combo, int index, int lastIndex) const;

    // 固定的24个通道能谱图像属性设置：是否刻度，X/Y坐标轴范围自动与否，坐标轴范围
    struct SpectrumPlotSettings {
        //多道道数
        int multiChannel = 8192;
        double xMin = 0.0;
        double xMax = 8192.0;
        double yMin = 0.0;
        double yMax = 100.0;

        int fitType = 1;
        double c0 = 1.0;
        double c1 = 0.0;
        double c2 = 0.0;
    };
    
    // 能谱图像属性
    std::atomic<bool> mEnScale = false;// 是否勾选能量刻度
    QVector<SpectrumPlotSettings> m_spectrumPlotSettings = QVector<SpectrumPlotSettings>(DET_NUM);
    QMap<quint8,QPair<qint64,float>> m_mapTemperature;// 探测器温度表

    // 温度离线/定时写盘缓存（满足：temperature/<yyyy-MM-dd>/<index>.txt；跨午夜按点归档）
    struct TempPoint {
        QString dtStr;      // "yyyy-MM-dd HH:mm:ss"（精确到秒）
        QString dtStrDetector;      // "yyyy-MM-dd HH:mm:ss"（精确到秒）探测器时间戳
        float temperature;
    };
    QHash<quint8, QHash<QString, QVector<TempPoint>>> m_tempPointCache; // index -> day(yyyy-MM-dd) -> points
    // QMutex mMutexTemperature; //温度数据缓存锁
    void flushTemperatureCache();

    quint32 m_specPlotRefreshTime = 1; // 能谱刷新时长(s)
    std::atomic<quint32> m_plotRefreshCount = 1;// 刷新次数
    std::atomic<quint32> m_plotDataUpdateCount = 1;// 数据更新次数
    std::atomic<quint32> m_plotDataUpdateCount2 = 1;// 数据更新次数

    // 日志内容查找功能相关
    QString mLastSearchText;  // 上次查找的文本
    int mCurrentSearchPosition = 0;  // 当前查找位置
    QList<QTextEdit::ExtraSelection> mExtraSelections;  // 高亮选择列表
    void performSearch(bool forward = true, bool wrap = true);  // 执行查找
    void highlightAllMatches(const QString &searchText);  // 高亮所有匹配项
    void clearHighlights();  // 清除高亮

    QPixmap roundPixmap(QSize sz, QColor clrOut = Qt::gray);//单圆
    QPixmap dblroundPixmap(QSize sz, QColor clrIn, QColor clrOut = Qt::gray);//双圆

    bool mEnableAutoMeasure = false; // 开始自动测量模式
    bool mEnableExtendTrigger = false; // 启用外触发
    QVector<PendingLogEntry> mPendingLogs; // 待批量输出的日志缓存
    qint64 mLastLogFlushSecond = -1; // 上次刷日志的秒级时间戳
    void startMeasure();
    void stopMeasure();
    void logMeasureStartInfo();
    std::atomic<bool> mCountRatePlotReady = false; // 计数率曲线待刷新
    std::atomic<bool> mSpectrumPlotReady = false;  // 能谱曲线待刷新
    void replotSpectrumBottom(QCustomPlot *plot, bool allowYRescale);
    void updateSpectrumBottomXAxisLabels(bool energyScale);
    void setCurveVisible(int detectorId, bool visible);
    void updateCurveSelectAllState();

    QMutex mPlotLocker;
    bool mUpdatingCurveSelectAll = false;
    QCheckBox* mCheckBoxCurveSelectAll = nullptr;
};

#endif // MAINWINDOW_H
