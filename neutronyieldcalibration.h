#ifndef NEUTRONYIELDCALIBRATION_H
#define NEUTRONYIELDCALIBRATION_H

#include <QWidget>
#include <QItemDelegate>
#include <QLineEdit>
#include <QMessageBox>
#include <QStyledItemDelegate>
#include <QDoubleValidator>

namespace Ui {
class NeutronYieldCalibration;
}

class PositiveNumberDelegate  : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit PositiveNumberDelegate (QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {}

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        /*QLineEdit *editor = new QLineEdit(parent);
        //QRegExp regExp("^[-+]?[0-9]*\\.?[0-9]+([eE][-+]?[0-9]+)?$");//只允许输入整数或浮点数（支持科学计数）
        //QRegExp regExp("^?[0-9]*\\.?[0-9]+([eE][-+]?[0-9]+)?$");//限制只能输入正数
        QRegExp regExp("^(?=.*[1-9])[0-9]{0,7}(\\.[0-9]{1,7})?([eE][+]?[0-9]{1,7})?$");//正数输入限制（有效数≤7位）
        editor->setValidator(new QRegExpValidator(regExp, editor));
        return editor;*/
        QLineEdit* editor = new QLineEdit(parent);

        auto* validator = new QDoubleValidator(editor);
        validator->setNotation(QDoubleValidator::ScientificNotation);
        validator->setDecimals(12);

        validator->setBottom(0.0);     // 只允许 ≥ 0
        validator->setTop(1e300);

        editor->setValidator(validator);
        editor->setInputMethodHints(Qt::ImhFormattedNumbersOnly);

        return editor;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        QString value = index.model()->data(index, Qt::EditRole).toString();
        static_cast<QLineEdit*>(editor)->setText(value);
    }

    //当单元格编辑结束时：
    // 1)从 QLineEdit 取用户输入
    // 2)做“必须 > 0”的业务校验
    // 3)规范化输入格式（去掉 +）
    // 4)与旧值对比
    // 5)如果改了 → 标红
    // 6)如果没改 → 还原样式
    // 7)最终写回 model
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override {
        QLineEdit *lineEdit = static_cast<QLineEdit*>(editor);

        QString newValue = lineEdit->text().trimmed();
        bool ok = false;
        double v = newValue.toDouble(&ok);
        if (!ok || v <= 0.0) {
            QMessageBox::warning(nullptr, "错误", "输入必须大于0");
            return;
        }

        if (newValue.left(1) == "+")
            newValue = newValue.remove(0, 1);

        QString oldValue = index.model()->data(index, Qt::EditRole).toString();

        if (newValue != oldValue) {
            QFont font = model->data(index, Qt::FontRole).value<QFont>();
            font.setBold(true);

            QColor clr = model->data(index, Qt::ForegroundRole).value<QColor>();
            model->setData(index, QColor(Qt::blue), Qt::ForegroundRole);
            model->setData(index, clr, Qt::UserRole);
        } else {
            QFont font = model->data(index, Qt::FontRole).value<QFont>();
            font.setBold(false);
            model->setData(index, font, Qt::FontRole);

            QColor clr = model->data(index, Qt::UserRole).value<QColor>();
            model->setData(index, clr, Qt::ForegroundRole);
        }

        model->setData(index, newValue, Qt::EditRole);
    }
};


class NeutronYieldCalibration : public QWidget
{
    Q_OBJECT

public:
    explicit NeutronYieldCalibration(QWidget *parent = nullptr);
    ~NeutronYieldCalibration();

    /**
     * 获取中子产额-初始活度数据对
     */
    static QVector<QPair<double, double>> neutronYield();

private slots:
    void on_pushButton_import_clicked();

    void on_pushButton_export_clicked();

    void on_pushButton_save_clicked();

    void on_pushButton_cancel_clicked();

private:
    Ui::NeutronYieldCalibration *ui;
};

#endif // NEUTRONYIELDCALIBRATION_H
