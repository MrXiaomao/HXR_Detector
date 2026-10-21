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

class CommHelper;

#include <QStyledItemDelegate>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QIntValidator>
#include <QHBoxLayout>
#include <QEventLoop>
class QTableWidgetItem;

class DetParamDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit DetParamDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {}

    QWidget* createEditor(QWidget* parent,
                          const QStyleOptionViewItem&,
                          const QModelIndex& index) const override
    {
        if (index.column() == 2){// 增益
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
        else if (index.column() == 3){// 触发阈值
            QLineEdit* editor = new QLineEdit(parent);
            auto *validator = new QIntValidator(0, 65535, editor);
            editor->setValidator(validator);
            editor->setInputMethodHints(Qt::ImhFormattedNumbersOnly);
            return editor;
        }
        else if (index.column() == 4 || index.column() == 5){// 死时间/高压
            QLineEdit* editor = new QLineEdit(parent);

            QIntValidator* validator = nullptr;
            if (index.column() == 5)
                validator = new QIntValidator(0, 1000, editor);
            else
                validator = new QIntValidator(0, 65535, editor);
            editor->setValidator(validator);
            editor->setInputMethodHints(Qt::ImhFormattedNumbersOnly);

            return editor;
        }

        return QStyledItemDelegate::createEditor(parent, {}, index);
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        if (index.column() == 3 || index.column() == 4 || index.column() == 5){
            return QStyledItemDelegate::setEditorData(editor, index);
        }
        else if (index.column() == 2){
            QComboBox *comboBox = static_cast<QComboBox *>(editor);
            comboBox->setCurrentText(index.data(Qt::EditRole).toString());
        }
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override
    {
        if (index.column() == 3) {
            auto *lineEdit = static_cast<QLineEdit *>(editor);
            bool ok = false;
            int v = lineEdit->text().trimmed().toInt(&ok);
            if (!ok)
                v = 0;
            model->setData(index, QString::number(qBound(0, v, 65535)), Qt::EditRole);
            return;
        }
        if (index.column() == 4 || index.column() == 5){
            return QStyledItemDelegate::setModelData(editor, model, index);
        }
        else if (index.column() == 2){
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
