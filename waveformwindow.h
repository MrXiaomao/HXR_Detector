#ifndef WAVEFORMWINDOW_H
#define WAVEFORMWINDOW_H

#include <QWidget>
#include <QTimer>
#include <QDateTime>
#include "commhelper.h"

namespace Ui {
class WaveformWindow;
}

class WaveformWindow : public QWidget
{
    Q_OBJECT

public:
    explicit WaveformWindow(QWidget *parent = nullptr);
    ~WaveformWindow();

<<<<<<< HEAD
    virtual void closeEvent(QCloseEvent *event) override;

=======
>>>>>>> 26822851222f1b350478b3dc690d0d41742dfecc
private slots:
    void on_pushButton_start_clicked();

    void on_pushButton_save_clicked();

    void on_pushButton_stop_clicked();

private:
    Ui::WaveformWindow *ui;
    QTimer *timer;
    QDateTime timerStart;
    CommHelper *commHelper = nullptr;//网络指令
    bool measuring = false;
    quint64 pkgCount = 0;
};

#endif // WAVEFORMWINDOW_H
