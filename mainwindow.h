#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "commhelper.h"
#include "clientpeerswindow.h"
#include "detsettingwindow.h"
#include "QGoodWindowHelper"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QCustomPlot;
class QCPGraph;
class QTimer;

// 探测器数据结构
struct DetectorData {
    // double countRate;                 // 当前计数率
    QVector<double> countRateHistory;    // 计数率历史，cps，每秒钟更新一次
    quint32 spectrum[8192];              // 累积能谱 (固定长度8192)
    quint32 lastSpectrumID;              // 上次测量累积时间的能谱序号
    quint32 lastAccumulateCount;         // 上次测量累积时间的计数率,暂时不考虑丢包带来的计数率修复
    // QDateTime lastUpdate;

    DetectorData() : lastSpectrumID(0), lastAccumulateCount(0) {
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
    void clearCustomPlotData();
    void applyColorTheme();

    // 新增的探测器数据管理方法
    void resetDetectorSpectrum(int detectorId);
    DetectorData getDetectorData(int detectorId) const;
    bool isDetectorOnline(int detectorId) const;
    QList<int> getOnlineDetectors() const;

    // 更新能谱显示
    void updateSpectrumDisplay(int detectorId, const quint32 spectrum[]);

    /**
     * @brief 计数率显示，最快更新时间间隔不小于1000ms
     * @param detectorId 探测器编号
     * @param FPGATime FPGA内部时钟，当前的计数率对应的FPGA时钟，单位s，能谱数据中单位是ms，这里采用向下取整
     * @param countRate 计数率，单位cps
     */
    void updateCountRateDisplay(int detectorId, double FPGATime, double countRate);

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
    // 触发模式
    void onTriggerModelChanged(int index);
    // 全通道开始测量
    void on_action_startMeasure_triggered();
    // 全通道停止测量
    void on_action_stopMeasure_triggered();
    // 自定义通道开始测量
    void on_pushButton_startMeasure_clicked();
    // 自定义通道停止测量
    void on_pushButton_stopMeasure_clicked();
    // 打开所有通道POE电源
    void on_action_powerOn_triggered();
    // 关闭所有通道POE电源
    void on_action_powerOff_triggered();
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
    void on_cbb_measureMode_activated(int index);
    void on_cbb_energyCalibration_toggled(bool checked);
    void on_action_particalMode_triggered();
    // 计数率统计
    void on_action_countRateStatistics_triggered();

    void on_action_neutronYieldStatistics_triggered();

    void on_action_waveformMode_triggered();

    void on_lineEdit_filePath_editingFinished();

private:
    QString increaseShotNumSuffix(QString shotNumStr);
    QCustomPlot* getCustomPlot(int detectorId, bool isSpectrum = true);
    QCPGraph* getGraph(int detectorId, bool isSpectrum = true);

private:
    Ui::MainWindow *ui;
    ClientPeersWindow *mClientPeersWindow = nullptr;
    DetSettingWindow *mDetSettingWindow = nullptr;
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
    // 探测器数据管理
    QHash<int, DetectorData> m_detectorData;  // 只存储联网探测器的数据
    CommHelper *commHelper = nullptr;
    
    // 测量倒计时定时器
    QTimer *mMeasureCountdownTimer = nullptr;
    int mRemainingCountdown = 0;  // 剩余倒计时（秒）
    int mTotalCountdown = 0;  // 总倒计时时间（秒），用于计算已测量时长
    //bool mContinueMeasure = false; // 是否继续测量模式

    // 自动化测量延时器
    QTimer *mAutoMeasureDelayTimer = nullptr;
    // 连续测量计时器
    QElapsedTimer *mContinueMeasureTimer = nullptr;

    // 交换机连接状态管理
    bool mSwitcherLogged = false;  // 交换机是否已登录
    bool mSwitcherConnected = false;  // 交换机是否已连接
    QTimer *mConnectButtonDisableTimer = nullptr;  // 连接按钮禁用定时器

    QVector<QColor> mGraphisColor;
    // 更新连接按钮状态（文本、图标、启用/禁用）
    void updateConnectButtonState(bool connected);

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
    QVector<SpectrumPlotSettings> m_spectrumPlotSettings = QVector<SpectrumPlotSettings>(24);

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
    void startMeasure();
    void stopMeasure();
};

#endif // MAINWINDOW_H
