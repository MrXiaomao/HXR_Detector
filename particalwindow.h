#ifndef PARTICALWINDOW_H
#define PARTICALWINDOW_H

#include <QWidget>
#include <QTimer>
#include <QDateTime>
#include "commhelper.h"

namespace Ui {
class ParticalWindow;
}

class ParticalWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ParticalWindow(QWidget *parent = nullptr);
    ~ParticalWindow();

private slots:
    void on_pushButton_start_clicked();

    void on_pushButton_save_clicked();

    void on_pushButton_stop_clicked();

private:
    Ui::ParticalWindow *ui;
    QTimer *timer;
    QDateTime timerStart;
    CommHelper *commHelper = nullptr;//网络指令
    bool measuring = false;
    quint64 pkgCount = 0;
};

#endif // PARTICALWINDOW_H
