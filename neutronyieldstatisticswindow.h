/*
 * @Author: MrPan
 * @Date: 2026-01-15 21:24:55
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2026-02-03 14:05:52
 * @Description: 请填写简介
 */
#ifndef NEUTRONYIELDSTATISTICSWINDOW_H
#define NEUTRONYIELDSTATISTICSWINDOW_H

#include <QWidget>
#include "QGoodWindowHelper"
#include "qcustomplothelper.h"
#include "parsedata.h"

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

    Q_SIGNAL void reporWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);
    Q_SLOT void replyWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);

signals:
    // void itemActived(double key, double value);//节点被鼠标点击激活
    void sigPlot(FullSpectrum);
    void sigPausePlot(bool); //是否暂停图像刷新
    void sigEnd(bool);
    void sigSuccess();
    void sigFail();

public slots:
    // void slotCountPlotClick(double key, double value);
    void slotFail();
    void slotSuccess();

    //更新多段能谱数据
    void slotUpdateMultiSegmentPlotDatas(std::vector<ParseData::mergeSpecData>); //分时能谱

    void slotUpdate_Count909_time(std::vector<double> time/*时刻*/, std::vector<double> count/*散点*/,
                                  std::vector<double> fitcount/*拟合曲线*/, std::vector<double> residual/*残差*/);//计数衰减曲线
    void slotUpdateSpec_909keV(std::vector<double> channel, std::vector<double> count, std::vector<double> fitcount, std::vector<double>residual);//909峰位段能谱曲线

private slots:
    void on_action_lightTheme_triggered();
    void on_action_darkTheme_triggered();
    void on_action_colorTheme_triggered();

    void on_action_open_triggered();

    void on_action_exit_triggered();

    void on_action_startMeasure_triggered();

    void on_action_stopMeasure_triggered();

    void on_tableWidget_cellClicked(int row, int column);

private:
    Ui::NeutronYieldStatisticsWindow *ui;
    bool mIsDarkTheme = true;
    bool mThemeColorEnable = true;
    QColor mThemeColor = QColor(255,255,255);
    class QGoodWindowHelper *mainWindow = nullptr;

    // 中断解析
    std::atomic<bool> mInterrupted = false;

    QMap<quint8, QVector<double>> mMapSpectrum;

    quint32 stepT = 1;
    unsigned int startTime_FPGA; //记录保存数据的FPGA起始时刻。单位s，FPGA内部时钟
    bool reAnalyzer = false; // 是否重新开始解析
    bool analyzerFinished = true;// 解析是否完成
    quint64 detectNum = 0; //探测到的粒子数

    bool firstPopup = true;//控制弹窗是否首次出现
    FullSpectrum totalSingleSpectrum;

    unsigned int startTimeUI = 0;
    unsigned int endTimeUI = 0;

    ParseData* dealFile = nullptr;
};

#endif // NEUTRONYIELDSTATISTICSWINDOW_H
