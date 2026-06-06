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

#include <QStyledItemDelegate>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QDoubleValidator>
#include <QIntValidator>
#include <QHBoxLayout>
#include <QEventLoop>
class QTableWidgetItem;

class LonLatDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit LonLatDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {}

    QWidget* createEditor(QWidget* parent,
                          const QStyleOptionViewItem&,
                          const QModelIndex& index) const override
    {
        // 只处理第 3、4 列（0-based）
        if (index.column() == 2 || index.column() == 3)
        {
            QLineEdit* editor = new QLineEdit(parent);

            QDoubleValidator* validator = nullptr;

            if (index.column() == 2) {
                // 第三列：经度 0~360
                validator = new QDoubleValidator(0.0, 360.0, 6, editor);
            } else {
                // 第四列：纬度 0~180
                validator = new QDoubleValidator(0.0, 180.0, 6, editor);
            }

            validator->setNotation(QDoubleValidator::StandardNotation);
            editor->setValidator(validator);
            editor->setInputMethodHints(Qt::ImhFormattedNumbersOnly);

            return editor;
        }
        else if (index.column() == 4){// 增益
            QComboBox* editor = new QComboBox(parent);
            editor->addItem(QStringLiteral("0.08"));
            editor->addItem(QStringLiteral("0.16"));
            editor->addItem(QStringLiteral("0.32"));
            editor->addItem(QStringLiteral("0.63"));
            editor->addItem(QStringLiteral("1.26"));
            editor->addItem(QStringLiteral("2.52"));
            editor->addItem(QStringLiteral("5.01"));
            editor->addItem(QStringLiteral("10.0"));
            return editor;
        }
        else if (index.column() == 5 || index.column() == 6 || index.column() == 7){// 阈值/死时间/高压
            QLineEdit* editor = new QLineEdit(parent);

            QIntValidator* validator = nullptr;
            if (index.column() == 7)
                validator = new QIntValidator(0, 1000, editor);
            else
                validator = new QIntValidator(0, 65535, editor);
            editor->setValidator(validator);
            editor->setInputMethodHints(Qt::ImhFormattedNumbersOnly);

            return editor;
        }

        // 其他列默认行为
        return QStyledItemDelegate::createEditor(parent, {}, index);
    }

    // 设置编辑器数据
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        // 将参数editor转换为对应创建的控件，再进行数据初始设置就行
        if (index.column() == 2 || index.column() == 3 || index.column() == 5 || index.column() == 6 || index.column() == 7 ){
            return QStyledItemDelegate::setEditorData(editor, index);
        }
        else if (index.column() == 4){
            QComboBox *comboBox = static_cast<QComboBox *>(editor);
            comboBox->setCurrentText(index.data(Qt::EditRole).toString());
        }
    }

    // 设置模型数据
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override
    {
        if (index.column() == 2 || index.column() == 3 || index.column() == 5 || index.column() == 6 || index.column() == 7 ){
            return QStyledItemDelegate::setModelData(editor, model, index);
        }
        else if (index.column() == 4){
            QComboBox *comboBox = static_cast<QComboBox *>(editor);
            model->setData(index, comboBox->currentText(), Qt::EditRole);
        }
    }

};

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
    virtual void showEvent(QShowEvent *event) override;

private slots:
    void on_pushButton_save_clicked();

    void on_pushButton_cancel_clicked();

    void on_pushButton_read_clicked();

    void on_tableWidget_itemChanged(QTableWidgetItem *item);

private:
    //获取指定行的经纬度，这里row从0开始计数
    bool getLonLat(int row, double& lon, double& lat);
    //设置经纬度，这里row从0开始计数
    bool setLonLat(int row, double lon, double lat);

    // 编号从1开始；recordLog 为 true 时记录用户加载表格相关日志
    void loadAt(quint8 detId, bool recordLog = false);
    // 编号从1开始
    void saveAt(quint8 detId);
    void updateSelectAllState();
    void logHardwareParameters(const QString &reason, quint8 detId);
    void logHardwareParametersFromUi(const QString &reason, quint8 detId);
    quint8 currentSelectedDetId() const;
    Ui::DetSettingWindow *ui;
    CommHelper* mCommhelper = nullptr;
    bool mUpdatingSelectAll = true;
    bool mBlockParameterLogging = false;

    QString queryMsg;
    QEventLoop queryWaitEvent;
};

#endif // DETSETTINGWINDOW_H
