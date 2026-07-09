#include "energycalibration.h"
#include "ui_energycalibration.h"
#include "globalsettings.h"

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleValidator>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QStyledItemDelegate>
#include <QTableWidgetItem>
#include <QTextStream>
#include <cmath>

namespace {

constexpr int kDetectorColumn = 0;
constexpr int kKColumn = 1;
constexpr int kBColumn = 2;

QString energyCalibrationCsvPath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(ENERGY_CALIBRATION_CSV_FILE);
}

class EnergyValueDelegate : public QStyledItemDelegate
{
public:
    explicit EnergyValueDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        Q_UNUSED(option);

        QLineEdit *editor = new QLineEdit(parent);
        if (index.column() == kKColumn || index.column() == kBColumn) {
            auto *validator = new QDoubleValidator(editor);
            validator->setNotation(QDoubleValidator::ScientificNotation);
            editor->setValidator(validator);
        }
        return editor;
    }
};

bool parseCsvLine(const QString &line, int *detector, double *k, double *b)
{
    const QStringList parts = line.split(',');
    if (parts.size() < 3)
        return false;

    bool okDetector = false;
    bool okK = false;
    bool okB = false;
    const int detectorValue = parts.at(0).trimmed().toInt(&okDetector);
    const double kValue = parts.at(1).trimmed().toDouble(&okK);
    const double bValue = parts.at(2).trimmed().toDouble(&okB);
    if (!okDetector || !okK || !okB || detectorValue < 1 || detectorValue > DET_NUM)
        return false;

    *detector = detectorValue;
    *k = kValue;
    *b = bValue;
    return true;
}

void setNumericItem(QTableWidget *table, int row, int column, double value)
{
    auto *item = table->item(row, column);
    if (!item) {
        item = new QTableWidgetItem;
        table->setItem(row, column, item);
    }
    item->setText(QString::number(value, 'g', 15));
    item->setTextAlignment(Qt::AlignCenter);
}

} // namespace

EnergyCalibration::EnergyCalibration(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::EnergyCalibration)
{
    ui->setupUi(this);
    initUI();
    loadCalibration();
}

EnergyCalibration::~EnergyCalibration()
{
    delete ui;
}

void EnergyCalibration::initUI()
{
    ui->tableWidget->setColumnCount(3);
    ui->tableWidget->setRowCount(DET_NUM);
    ui->tableWidget->setHorizontalHeaderLabels(QStringList() << tr("探测器编号") << tr("k值") << tr("b值"));
    ui->tableWidget->verticalHeader()->setVisible(false);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->tableWidget->setEditTriggers(QAbstractItemView::DoubleClicked
                                     | QAbstractItemView::SelectedClicked
                                     | QAbstractItemView::EditKeyPressed);
    ui->tableWidget->setItemDelegate(new EnergyValueDelegate(ui->tableWidget));

    for (int row = 0; row < DET_NUM; ++row) {
        auto *detectorItem = new QTableWidgetItem(QString::number(row + 1));
        detectorItem->setFlags(detectorItem->flags() & ~Qt::ItemIsEditable);
        detectorItem->setTextAlignment(Qt::AlignCenter);
        ui->tableWidget->setItem(row, kDetectorColumn, detectorItem);
        setNumericItem(ui->tableWidget, row, kKColumn, 1.0);
        setNumericItem(ui->tableWidget, row, kBColumn, 0.0);
    }

    ui->plainTextEdit_log->setReadOnly(true);
    ui->plainTextEdit_log->setPlainText(tr("能量刻度参数文件：%1").arg(energyCalibrationCsvPath()));
}

void EnergyCalibration::loadCalibration()
{
    QFile file(energyCalibrationCsvPath());
    bool loadedFromCsv = false;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        in.setCodec("UTF-8");
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty())
                continue;

            int detector = 0;
            double k = 0.0;
            double b = 0.0;
            if (!parseCsvLine(line, &detector, &k, &b))
                continue;

            setNumericItem(ui->tableWidget, detector - 1, kKColumn, k);
            setNumericItem(ui->tableWidget, detector - 1, kBColumn, b);
            loadedFromCsv = true;
        }
        file.close();
    }

    if (loadedFromCsv) {
        ui->plainTextEdit_log->appendPlainText(tr("已读取能量刻度 CSV 文件。"));
        return;
    }

    GlobalSettings settings(DEVICE_CONFIG_FILE);
    settings.beginGroup("EnCalibration");
    bool loadedFromOldConfig = false;
    for (int detector = 1; detector <= DET_NUM; ++detector) {
        const QString detectorGroup = QString("Detector%1").arg(detector);
        if (!settings.contains(QString("%1/c0").arg(detectorGroup))
            || !settings.contains(QString("%1/c1").arg(detectorGroup))) {
            continue;
        }

        settings.beginGroup(detectorGroup);
        const int type = settings.value("type", 1).toInt();
        if (type == 1) {
            setNumericItem(ui->tableWidget, detector - 1, kKColumn, settings.value("c0", 1.0).toDouble());
            setNumericItem(ui->tableWidget, detector - 1, kBColumn, settings.value("c1", 0.0).toDouble());
            loadedFromOldConfig = true;
        }
        settings.endGroup();
    }
    settings.endGroup();

    if (loadedFromOldConfig) {
        ui->plainTextEdit_log->appendPlainText(tr("未找到 CSV 文件，已从旧 device_config.ini 读取已有线性参数。"));
    } else {
        ui->plainTextEdit_log->appendPlainText(tr("未找到已有能量刻度参数，默认使用 k=1、b=0。"));
    }
}

bool EnergyCalibration::readRowValue(int row, int column, double *value, QString *errorText) const
{
    const QTableWidgetItem *item = ui->tableWidget->item(row, column);
    const QString columnName = column == kKColumn ? tr("k值") : tr("b值");
    if (!item || item->text().trimmed().isEmpty()) {
        *errorText = tr("第 %1 行 %2 不能为空。").arg(row + 1).arg(columnName);
        return false;
    }

    bool ok = false;
    const double parsedValue = item->text().trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(parsedValue)) {
        *errorText = tr("第 %1 行 %2 不是合法数字。").arg(row + 1).arg(columnName);
        return false;
    }

    if (column == kKColumn && qFuzzyIsNull(parsedValue)) {
        *errorText = tr("第 %1 行 k值不能为 0。").arg(row + 1);
        return false;
    }

    *value = parsedValue;
    return true;
}

bool EnergyCalibration::saveCalibration()
{
    QDir dir;
    const QFileInfo csvInfo(energyCalibrationCsvPath());
    if (!dir.mkpath(csvInfo.absolutePath())) {
        QMessageBox::warning(this, tr("提示"), tr("创建 CSV 目录失败：%1").arg(csvInfo.absolutePath()));
        return false;
    }

    QVector<QPair<double, double>> values;
    values.reserve(DET_NUM);
    for (int row = 0; row < DET_NUM; ++row) {
        double k = 0.0;
        double b = 0.0;
        QString errorText;
        if (!readRowValue(row, kKColumn, &k, &errorText) || !readRowValue(row, kBColumn, &b, &errorText)) {
            QMessageBox::warning(this, tr("输入错误"), errorText);
            ui->tableWidget->setCurrentCell(row, errorText.contains("k") ? kKColumn : kBColumn);
            return false;
        }
        values.push_back(qMakePair(k, b));
    }

    QFile file(energyCalibrationCsvPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("提示"), tr("保存 CSV 文件失败：%1").arg(file.errorString()));
        return false;
    }

    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << "Detector,K,B\n";
    for (int row = 0; row < values.size(); ++row) {
        out << row + 1 << ','
            << QString::number(values[row].first, 'g', 15) << ','
            << QString::number(values[row].second, 'g', 15) << '\n';
    }
    file.close();
    return true;
}

void EnergyCalibration::on_bt_SaveFit_clicked()
{
    if (!saveCalibration())
        return;

    ui->plainTextEdit_log->appendPlainText(tr("能量刻度参数已保存：%1").arg(energyCalibrationCsvPath()));
    QMessageBox::information(this, tr("提示"), tr("能量刻度参数保存成功。"));
}
