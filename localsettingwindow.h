#ifndef LOCALSETTINGWINDOW_H
#define LOCALSETTINGWINDOW_H

#include <QWidget>

namespace Ui {
class LocalSettingWindow;
}

class LocalSettingWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LocalSettingWindow(QWidget *parent = nullptr);
    ~LocalSettingWindow();

    virtual bool eventFilter(QObject *watched, QEvent *event) override;

    Q_SLOT void updateData();

private slots:
    void on_pushButton_ok_clicked();

    void on_pushButton_cancel_clicked();

    void on_spinBox_valueChanged(int arg1);

private:
    Ui::LocalSettingWindow *ui;

    bool mSwitcher2Enabled = false;
    bool mSwitcher3Enabled = false;
};

#endif // LOCALSETTINGWINDOW_H
