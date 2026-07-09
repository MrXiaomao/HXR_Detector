#ifndef ENERGYCALIBRATION_H
#define ENERGYCALIBRATION_H

#include <QWidget>

namespace Ui {
class EnergyCalibration;
}

class EnergyCalibration : public QWidget
{
    Q_OBJECT

public:
    explicit EnergyCalibration(QWidget *parent = nullptr);
    ~EnergyCalibration();

private slots:
    void on_bt_SaveFit_clicked();

private:
    void initUI();
    void loadCalibration();
    bool saveCalibration();
    bool readRowValue(int row, int column, double *value, QString *errorText) const;

    Ui::EnergyCalibration *ui;
};

#endif // ENERGYCALIBRATION_H
