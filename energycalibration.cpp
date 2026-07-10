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
constexpr int kSigma511Column = 3;
constexpr int kSigma846Column = 4;
constexpr int kSigma909Column = 5;

struct CalibrationCsvRow {
    int detector = 0;
    double k = 1.0;
    double b = 0.0;
    double sigma511 = 5.0;
    double sigma846 = 6.0;
    double sigma909 = 7.0;
};

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
        if (index.column() != kDetectorColumn) {
            auto *validator = new QDoubleValidator(editor);
            validator->setNotation(QDoubleValidator::ScientificNotation);
            editor->setValidator(validator);
        }
        return editor;
    }
};

bool parseCsvLine(const QString &line, CalibrationCsvRow *row)
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

    CalibrationCsvRow parsed;
    parsed.detector = detectorValue;
    parsed.k = kValue;
    parsed.b = bValue;

    if (parts.size() >= 6) {
        bool okSigma511 = false;
        bool okSigma846 = false;
        bool okSigma909 = false;
        const double sigma511 = parts.at(3).trimmed().toDouble(&okSigma511);
        const double sigma846 = parts.at(4).trimmed().toDouble(&okSigma846);
        const double sigma909 = parts.at(5).trimmed().toDouble(&okSigma909);
        if (okSigma511 && okSigma846 && okSigma909
            && sigma511 > 0.0 && sigma846 > 0.0 && sigma909 > 0.0) {
            parsed.sigma511 = sigma511;
            parsed.sigma846 = sigma846;
            parsed.sigma909 = sigma909;
        }
    }

    *row = parsed;
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
    ui->tableWidget->setColumnCount(6);
    ui->tableWidget->setRowCount(DET_NUM);
    ui->tableWidget->setHorizontalHeaderLabels(QStringList() << tr("探测器编号") << tr("k值") << tr("b值") << tr("sigma511") << tr("sigma846") << tr("sigma909"));
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
        setNumericItem(ui->tableWidget, row, kSigma511Column, 5.0);
        setNumericItem(ui->tableWidget, row, kSigma846Column, 6.0);
        setNumericItem(ui->tableWidget, row, kSigma909Column, 7.0);
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

            CalibrationCsvRow row;
            if (!parseCsvLine(line, &row))
                continue;

            setNumericItem(ui->tableWidget, row.detector - 1, kKColumn, row.k);
            setNumericItem(ui->tableWidget, row.detector - 1, kBColumn, row.b);
            setNumericItem(ui->tableWidget, row.detector - 1, kSigma511Column, row.sigma511);
            setNumericItem(ui->tableWidget, row.detector - 1, kSigma846Column, row.sigma846);
            setNumericItem(ui->tableWidget, row.detector - 1, kSigma909Column, row.sigma909);
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
    QString columnName;
    if (column == kKColumn)
        columnName = tr("k值");
    else if (column == kBColumn)
        columnName = tr("b值");
    else if (column == kSigma511Column)
        columnName = tr("sigma511");
    else if (column == kSigma846Column)
        columnName = tr("sigma846");
    else if (column == kSigma909Column)
        columnName = tr("sigma909");
    else
        columnName = tr("数值");
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

    if ((column == kSigma511Column || column == kSigma846Column || column == kSigma909Column)
        && parsedValue <= 0.0) {
        *errorText = tr("第 %1 行 %2 必须大于 0。").arg(row + 1).arg(columnName);
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

    QVector<CalibrationCsvRow> values;
    values.reserve(DET_NUM);
    for (int row = 0; row < DET_NUM; ++row) {
        CalibrationCsvRow value;
        value.detector = row + 1;
        QString errorText;
        if (!readRowValue(row, kKColumn, &value.k, &errorText)
            || !readRowValue(row, kBColumn, &value.b, &errorText)
            || !readRowValue(row, kSigma511Column, &value.sigma511, &errorText)
            || !readRowValue(row, kSigma846Column, &value.sigma846, &errorText)
            || !readRowValue(row, kSigma909Column, &value.sigma909, &errorText)) {
            QMessageBox::warning(this, tr("输入错误"), errorText);
            int errorColumn = kKColumn;
            if (errorText.contains(QStringLiteral("b")))
                errorColumn = kBColumn;
            else if (errorText.contains(QStringLiteral("sigma511")))
                errorColumn = kSigma511Column;
            else if (errorText.contains(QStringLiteral("sigma846")))
                errorColumn = kSigma846Column;
            else if (errorText.contains(QStringLiteral("sigma909")))
                errorColumn = kSigma909Column;
            ui->tableWidget->setCurrentCell(row, errorColumn);
            return false;
        }
        values.push_back(value);
    }

    QFile file(energyCalibrationCsvPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("提示"), tr("保存 CSV 文件失败：%1").arg(file.errorString()));
        return false;
    }

    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << "Detector,K,B,Sigma511,Sigma846,Sigma909\n";
    for (int row = 0; row < values.size(); ++row) {
        out << values[row].detector << ','
            << QString::number(values[row].k, 'g', 15) << ','
            << QString::number(values[row].b, 'g', 15) << ','
            << QString::number(values[row].sigma511, 'g', 15) << ','
            << QString::number(values[row].sigma846, 'g', 15) << ','
            << QString::number(values[row].sigma909, 'g', 15) << '\n';
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
