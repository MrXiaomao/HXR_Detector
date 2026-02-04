/*
 * @Author: MrPan
 * @Date: 2025-11-13 11:36:00
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2026-01-04 20:25:35
 * @Description: 请填写简介
 */
#ifndef DETSETTINGWINDOW_H
#define DETSETTINGWINDOW_H

#include <QWidget>
#include "commhelper.h"

namespace Ui {
class DetSettingWindow;
}

class DetSettingWindow : public QWidget
{
    Q_OBJECT

public:
    explicit DetSettingWindow(QWidget *parent = nullptr);
    ~DetSettingWindow();

    Q_SIGNAL void connectPeerConnection(QString,quint16);//客户端上线
    Q_SIGNAL void disconnectPeerConnection(QString,quint16);//客户端下线

    void setSaveEnabled(bool enabled);

    Q_SIGNAL void settingfinished();

public:
    virtual bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void on_pushButton_save_clicked();

    void on_pushButton_cancel_clicked();

    void on_pushButton_read_clicked();

private:
    void loadAt(quint8 detId);
    void saveAt(quint8 detId);
    void updateSelectAllState();
    Ui::DetSettingWindow *ui;
    CommHelper* mCommhelper = nullptr;
    bool mUpdatingSelectAll = true;
};

#endif // DETSETTINGWINDOW_H
