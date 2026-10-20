#ifndef LOCALSETTINGWINDOW_H
#define LOCALSETTINGWINDOW_H

#include <QString>
#include <QWidget>

class CommHelper;

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

private:
    Ui::LocalSettingWindow *ui;
    CommHelper *commHelper = nullptr;

    QString mLoadedSwitcherIp;
    QString mLoadedSwitcherDetectors;
};

#endif // LOCALSETTINGWINDOW_H
