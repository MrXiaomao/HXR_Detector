/*
 * @Author: MrPan
 * @Date: 2026-03-15 13:35:25
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2026-04-19 21:41:50
 * @Description: 请填写简介
 */
#ifndef COUNTRATESTATISTICSWINDOW_H
#define COUNTRATESTATISTICSWINDOW_H

#include <QDateTime>
#include <QWidget>
#include "QGoodWindowHelper"
#include "qcustomplothelper.h"

namespace Ui {
class CountRateStatisticsWindow;
}

class QCustomPlot;
class QCheckBox;
class CountRateStatisticsWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit CountRateStatisticsWindow(bool isDarkTheme = true, QWidget *parent = nullptr);
    ~CountRateStatisticsWindow();

    void initUi();
    void initCustomPlot(QCustomPlot* customPlot, QString axisXLabel, QString axisYLabel, int startDetector, int endDetector,
                        const QString &plotTitle = QString(), double xAxisMax = 1000.0);
    void applyColorTheme();
    void restoreSettings();

private slots:
    void on_action_lightTheme_triggered();
    void on_action_darkTheme_triggered();
    void on_action_colorTheme_triggered();

    void on_action_open_triggered();

    void on_action_exit_triggered();

    void on_action_startParse_triggered();

    void on_action_stopParse_triggered();

    void on_ConfirmTime_clicked();

    void on_tableWidget_cellClicked(int row, int column);

    void replyWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);

signals:
    void sigWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);
    void reporWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);

private:
    QPixmap roundPixmap(QSize sz, QColor clrOut);
    /** 与 QCPGraph 笔一致的小线段图标（图例复选框用） */
    QPixmap lineLegendPixmap(const QColor &color, Qt::PenStyle style, int penWidth);
    void updateTableRowCount();
    void clearAllPlots();
    void plotAllDetectors();
    void updateAllPlotVisibility();
    void updatePlotVisibility(QCustomPlot* customPlot, int startDetector, int endDetector);
    void createDetectorLegendCheckboxes();
    void syncDetectorCheckboxes();
    /** 原始/修正 全局复选框：全选或全不选对应列通道 */
    void applyOriginalGlobalState(bool enableAll);
    void applyCorrectGlobalState(bool enableAll);
    bool updateMeasureStartTimeFromShotTime(const QDateTime &shotTime);
    void updateMeasureStartSecond(double startSecond);

    Ui::CountRateStatisticsWindow *ui;
    bool mIsDarkTheme = true;
    bool mThemeColorEnable = true;
    QColor mThemeColor = QColor(255,255,255);
    class QGoodWindowHelper *mainWindow = nullptr;
    QDateTime mMeasureStartTime;
    double mMeasureStartSecond = 0.0;
    double mMeasureDurationSecond = 0.0;

    // 中断解析
    std::atomic<bool> mInterrupted = false;

    /** 每通道：时间(s) -> 原始/修正计数率(cps)，用于曲线绘制 */
    QMap<quint8, QVector<QPair<double, double>>> mMapCount;
    QMap<quint8, QVector<QPair<double, double>>> mMapCountCorrect;
    /** 每通道累积能谱（原始 / 死时间修正），8192 道 */
    QMap<quint8, QVector<quint64>> mMapSpectrum;
    QMap<quint8, QVector<quint64>> mMapSpectrumCorrect;

    // 图例/表格单通道的开关状态（顶部两个复选框仅用于全选/全不选，不参与与运算）
    QVector<bool> mOriginalEnabled;
    QVector<bool> mCorrectEnabled;
    QVector<QCheckBox*> mOriginalLegendChecks;
    QVector<QCheckBox*> mCorrectLegendChecks;
};

#endif // COUNTRATESTATISTICSWINDOW_H
