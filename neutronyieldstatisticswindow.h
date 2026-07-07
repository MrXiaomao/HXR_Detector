/*
 * @Author: MrPan
 * @Date: 2026-01-15 21:24:55
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2026-03-06 09:29:58
 * @Description: 请填写简介
 */
#ifndef NEUTRONYIELDSTATISTICSWINDOW_H
#define NEUTRONYIELDSTATISTICSWINDOW_H

#include <QWidget>
#include <QDateTime>
#include <vector>
#include "QGoodWindowHelper"
#include "qcustomplothelper.h"
#include "parsedata.h"
#include "neutronyieldparseworker.h"

class CrasterParabolicHeatmapWidget;
class QTableWidgetItem;
class QProgressBar;
class QThread;

namespace Ui {
class NeutronYieldStatisticsWindow;
}

class QCustomPlot;
class NeutronYieldStatisticsWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit NeutronYieldStatisticsWindow(bool isDarkTheme = true, QWidget *parent = nullptr);
    ~NeutronYieldStatisticsWindow();

    void initUi();
    // 初始化各时间段段的全谱分布
    void initSpectrumCustomPlot();
    // 初始化909keV伽马计数随时间变化曲线及各时间段的909能谱
    void initCountCustomPlot();
    // 初始化中子产额空间分布
    void initYieldCustomPlot();
    
    void applyColorTheme();
    void restoreSettings();

    void initFileListTable();
    void addFilesToList(const QStringList &paths);
    void sortFileListByStartTime();
    void refreshFileListBasicColumns();
    QStringList filePathsFromTable() const;
    void updateMeasurementInfoFromFirstFile();
    void updateDetectorSpecCountFromAllFiles();

    /**
     * @brief 设置经纬度
     * @param row 行号，从0开始计数
     * @param lon 经度 0-360
     * @param lat 纬度 0-180
     * @return
     */
    bool setLonLat(int row, double lon, double lat);

    Q_SIGNAL void reporWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);
    Q_SIGNAL void sigWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);
    Q_SLOT void replyWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);

signals:
    void sigPlot(FullSpectrum);
    void sigPausePlot(bool); //是否暂停图像刷新
    void sigEnd(bool);
    void sigSuccess(int detID);
    void sigFail();

public slots:
    void slotFail();
    void slotSuccess(int detID);

    /**
     * @brief 绘制多段能谱数据，每段能谱数据都是完整的8192道
     * @param mergeSpecData 各个时间段对应的总能谱
     */
    void slotUpdateMultiSegmentPlotDatas(std::vector<ParseData::mergeSpecData>); //分时能谱

    /**
     * @brief 绘制909keV计数随时间衰减曲线
     * @param time 时刻
     * @param count 909keV峰位下的计数
     * @param fitcount 909计数的拟合曲线
     * @param residual 拟合数据的残差
     */
    void slotUpdate_Count909_time(std::vector<double> time,
                                  std::vector<double> count,
                                  std::vector<double> fitcount,
                                  std::vector<double> residual);

    /**
     * @brief 绘制能谱909keV能段的图像，包含实测数据散点、能谱拟合数据、拟合残差
     * @param channel 能谱的X值，带有能量刻度的X轴
     * @param count 能谱上的Y值，实测计数。
     * @param fitcount 能谱拟合数据Y值
     * @param residual 能谱的拟合残差
     */
    void slotUpdateSpec_909keV(std::vector<double> channel,
                               std::vector<double> count,
                               std::vector<double> fitcount,
                               std::vector<double>residual);

    /**
     * @brief 绘制中子产额空间分布
     * @param sensors 探测器所在经纬度及其对应产额
     */
    void slotUpdate_yieldDis(QMap<int, Sensor> yield);

private slots:
    // 添加待处理文件（可多选），目前只支持处理H5文件
    void on_action_open_triggered();

    void on_bt_addFiles_clicked();
    void on_bt_removeFile_clicked();
    void on_bt_clearFiles_clicked();
    void on_btn_sort_clicked();

    // 开始解析文件
    void on_action_startCalculate_triggered();

    // 停止解析，在解析时，逐个通道解析，当解析函数返回到主界面的函数中时，对停止解析动作做判断，停止后续解析
    void on_action_stopCalculate_triggered();

    void on_action_lightTheme_triggered();
    void on_action_darkTheme_triggered();
    void on_action_colorTheme_triggered();

    void on_action_exit_triggered();

    // 选中行发生改变
    void on_tableWidget_selectionChanged();

    void on_tableWidget_itemChanged(QTableWidgetItem *item);

    void on_bt_saveResult_clicked();

    void onParseProgress(int value, int maximum, const QString &message);
    void onParseLogMessage(const QString &msg, QtMsgType type);
    void onParseChannelFinished(int detId, bool mergeOk, const QStringList &fitErrors);
    void onParseFinished(bool cancelled, const QString &summaryMessage);
    void onParseWorkerThreadFinished();

    void setParseBusy(bool busy);
    void startParseWorker(const NeutronYieldParseJob &job);

private:
    struct H5FileChannelMeta {
        QMap<int, QDateTime> firstDateTime;
        QMap<int, QDateTime> lastDateTime;
        QMap<int, quint32> specCount;
        QDateTime configStartTime;
        QDateTime configShotTime;
        QDateTime commonStart;
        QDateTime commonEnd;
        quint32 measureTimeMs = 0;
        bool hasMeasureTime = false;
        bool valid = false;
    };

    bool readH5FileChannelMeta(const QString &filePath, H5FileChannelMeta &out,
                               bool applyDetectorLonLat = false, bool showErrorDialog = false);
    void applyMeasurementMetaToFileListRow(int row, const H5FileChannelMeta &meta);
    void updateFileListGapColumns();
    static void computeCommonTimeInterval(const H5FileChannelMeta &meta,
                                          QDateTime &commonStart,
                                          QDateTime &commonEnd);

    bool isPerChannelShotTimeMode() const;
    void updateShotTimeModeUi();
    void syncMeasureDateTimeForDetector(int detId);
    qint64 beginTimeForDetector(int detId, const QDateTime &fusionTime) const;
    int currentPictureDetId() const;
    void updatePictureForDetector(int detID);
    void updateYieldSpatialDistributionForRow(int row);
    void refreshSpectrumPlotSlot(int slotIndex);
    void refreshAllSpectrumPlotSlots();

    Ui::NeutronYieldStatisticsWindow *ui;
    bool mIsDarkTheme = true;
    bool mThemeColorEnable = true;
    QColor mThemeColor = QColor(255,255,255);
    class QGoodWindowHelper *mainWindow = nullptr;
    CrasterParabolicHeatmapWidget *w = nullptr;

    // 中断解析
    std::atomic<bool> mInterrupted = false;

    QMap<quint8, QVector<double>> mMapSpectrum;
    std::vector<ParseData::mergeSpecData> m_currentMergeSpectrum;

    quint32 stepT = 1;
    unsigned int startTime_FPGA; //记录保存数据的FPGA起始时刻。单位s，FPGA内部时钟
    bool reAnalyzer = false; // 是否重新开始解析
    bool analyzerFinished = true;// 解析是否完成
    quint64 detectNum = 0; //探测到的粒子数

    bool firstPopup = true;//控制弹窗是否首次出现
    FullSpectrum totalSingleSpectrum;

    unsigned int startTimeUI = 0;
    unsigned int endTimeUI = 0;

    QMap<int, quint32> mDetectorSpecCount; //存放各个通道中能谱的数据,探测器编号：1~DET_NUM
    QMap<int, bool> mDealSucess; //存放各个通道中子产额解析是否成功的标志,探测器编号：1~DET_NUM
    QMap<int, QDateTime> mDetectorFirstDateTime; // 各通道首条能谱 UTC 时间，键 1..DET_NUM
    QMap<int, QDateTime> mDetectorLastDateTime;  // 各通道末条能谱 UTC 时间，键 1..DET_NUM
    QMap<int, Sensor> myield; //存放各个通道中子产额。通道编号：1-DET_NUM，通道经纬度，通道的中子产额
    ParseData* dealFile[DET_NUM];
    QStringList mFilePaths; // 待解析的文件列表（按表格顺序）

    QProgressBar *mProgressBar = nullptr;
    NeutronYieldParseWorker *mParseWorker = nullptr;
    QThread *mParseWorkerThread = nullptr;
};

#endif // NEUTRONYIELDSTATISTICSWINDOW_H
