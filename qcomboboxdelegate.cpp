#include "qcomboboxdelegate.h"
#include <QComboBox>

QComboBoxDelegate::QComboBoxDelegate(QObject *parent)
    : QItemDelegate(parent) {
}

QComboBoxDelegate::QComboBoxDelegate(QStringList list, QObject *parent)
    :QItemDelegate(parent) {
    this->mComboBoxItems = list;
}

QComboBoxDelegate::~QComboBoxDelegate() {

}

// 创建编辑器
#include <QLineEdit>
#include <QMessageBox>
QWidget *QComboBoxDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    // 创建自己需要的控件进行返回
    QComboBox *editor = new QComboBox(parent);
    editor->setEditable(true);

    QLineEdit *ip_port_edit = editor->lineEdit();

    // 设置 IP:Port 正则验证器
    QRegExp ip_port_regex(R"(^((25[0-5]|2[0-4]\d|1\d{2}|[1-9]\d|\d)\.){3}(25[0-5]|2[0-4]\d|1\d{2}|[1-9]\d|\d)(:\d{1,5})?$)");
    ip_port_regex.setPatternSyntax(QRegExp::RegExp2); // 启用Perl兼容语法（支持\d等元字符.exactMatch(str);）
    QRegExpValidator *ip_port_validator = new QRegExpValidator(ip_port_regex, editor);
    ip_port_edit->setValidator(ip_port_validator);

    return editor;
}

// 设置编辑器数据
void QComboBoxDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const {
    // 将参数editor转换为对应创建的控件，再进行数据初始设置就行
    QComboBox *comboBox = static_cast<QComboBox *>(editor);
    // 对下拉列表项进行排序（升序）
    QStringList sortedItems = mComboBoxItems;
    sortedItems.sort();  // 核心排序代码
    comboBox->addItems(sortedItems);
    comboBox->setCurrentText(index.data(Qt::EditRole).toString());
}

// 更新编辑器集合属性
void QComboBoxDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    // 将编辑器设置为矩形属性
    editor->setGeometry(option.rect);
}

// 设置模型数据
void QComboBoxDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const {
    QComboBox *comboBox = static_cast<QComboBox *>(editor);	// 类型转换

    // 模型（单元格）显示的数据
    QString currentText = comboBox->currentText();//index.model()->data(index, Qt::EditRole).toString();
    QRegExp ip_port_regex(R"(^((25[0-5]|2[0-4]\d|1\d{2}|[1-9]\d|\d)\.){3}(25[0-5]|2[0-4]\d|1\d{2}|[1-9]\d|\d)(:\d{1,5})?$)");

    if (!ip_port_regex.exactMatch(currentText)) {
        QMessageBox::warning(editor, "错误", "请输入合法的 IP:Port 格式！\r\n如 192.168.1.1 或 192.168.1.1:8080");
        comboBox->setCurrentText("0.0.0.0");
        model->setData(index, comboBox->currentText(), Qt::EditRole);
        return;
    }

    model->setData(index, comboBox->currentText(), Qt::EditRole);
}

// 插入数据
void QComboBoxDelegate::insertItem(QString str) {
    if (!this->mComboBoxItems.contains(str))
        this->mComboBoxItems.append(str);
}

// 移除数据
void QComboBoxDelegate::removeItem(QString str) {
    for (int i = 0; i < this->mComboBoxItems.size(); i++) {
        if (str == this->mComboBoxItems[i]) {
            this->mComboBoxItems.removeAt(i);
            return;
        }
    }
}
