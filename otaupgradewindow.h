#ifndef OTAUPGRADEWINDOW_H
#define OTAUPGRADEWINDOW_H

#include <QWidget>
#include <QtConcurrent>
#include "commhelper.h"

namespace Ui {
class OTAUpgradeWindow;
}

class OTAUpgradeWindow : public QWidget
{
    Q_OBJECT

public:
    explicit OTAUpgradeWindow(QWidget *parent = nullptr);
    ~OTAUpgradeWindow();

    Q_SIGNAL void sigWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);
    Q_SLOT void slotWriteLog(const QString &msg, QtMsgType msgType = QtDebugMsg);

    // 异步发送进度更新槽函数（主线程执行）
    Q_SIGNAL void sigSendProgress(int index, const QString& partition, int current, int total);
    Q_SLOT void slotSendProgress(int index, const QString& partition, int current, int total);

    // 异步发送完成槽函数（主线程执行）
    Q_SIGNAL void sigSendFinished(int index, const QString& partition, bool success);
    Q_SLOT void slotSendFinished(int index, const QString& partition, bool success);

    Q_SIGNAL void sigUpgradeFinished();
    Q_SLOT void slotUpgradeFinished();

private slots:
    void on_pushButton_ok_clicked();

    void on_pushButton_exit_clicked();

    void on_pushButton_switch_clicked();

private:
    Ui::OTAUpgradeWindow *ui;
    bool mIsDarkTheme = false;

    CommHelper *commHelper = nullptr;

    //记录联网的探测器ID
    QVector<quint8> mOnlineDetectors;

protected:
    QPixmap dblroundPixmap(QSize sz, QColor clrIn, QColor clrOut = Qt::gray);

private:
    std::atomic<qint64> m_returnDataSize[25] = {0}; // 共享变量：返回数据大小
    qint64 m_currentFileSize[25] = {0}; // 共享变量：返回升级文件大小
    int m_runningTasks = 0; // 异步任务计数器（用于控制按钮状态）
    QList<QFutureWatcher<void>*> m_watchers; // 任务监控器列表（支持取消操作）

private:
    QList<QPair<int, QString>> m_taskQueue; // 任务队列：<设备索引, 分区名称>
    QMap<int, QString> m_rpdFiles;
    int m_currentTaskIndex = 0; // 当前执行的任务索引
    QEventLoop m_waitLoop; // 等待事件循环，用于等待升级完成

    // 异步发送OTA数据的核心函数（子线程执行）
    void asyncSendOTAData(int index, const QString& filePath, const QString& selectedPartition);
    void startNextTask();

private slots:


};

#endif // OTAUPGRADEWINDOW_H
