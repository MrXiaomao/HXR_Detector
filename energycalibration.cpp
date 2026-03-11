/*
 * @Author: MrPan
 * @Date: 2025-12-29 22:08:10
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2026-01-25 18:57:24
 * @Description: 用于能量刻度，可进行线性拟合、二次函数拟合，能量刻度点的添加、删除、拟合等功能
 */
#include "energycalibration.h"
#include "ui_energycalibration.h"
#include "qcustomplot.h"
#include "globalsettings.h"
#include "curveFit.h"
// #include "ap.h"
// #include "interpolation.h"
// #include <math.h>
// using namespace alglib;
#include <QStyledItemDelegate>
#include <QLineEdit>
#include <QDoubleValidator>

// 表格浮点数输入委托：限制单元格仅允许输入浮点数
class DoubleItemDelegate : public QStyledItemDelegate
{
public:
    explicit DoubleItemDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    QWidget *createEditor(QWidget *parent,
                          const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        Q_UNUSED(option);
        Q_UNUSED(index);

        QLineEdit *editor = new QLineEdit(parent);
        auto *validator = new QDoubleValidator(editor);
        validator->setNotation(QDoubleValidator::StandardNotation);
        editor->setValidator(validator);
        return editor;
    }
};

EnergyCalibration::EnergyCalibration(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::EnergyCalibration),
    saveStatus(false),
    datafit(false),
    fitType(0),
    detButtonGroup(nullptr)
{
    ui->setupUi(this);

    initCustomPlot();

    initUI();

}

EnergyCalibration::~EnergyCalibration()
{
    // 保存勾选的通道号
    GlobalSettings settings(DEVICE_CONFIG_FILE);
    settings.beginGroup("EnCalibration");
    int detChannel = 0;
    for (int i=0; i<24; ++i){
        if (detRadioButtons[i]->isChecked())
            detChannel = i+1;
    }
    settings.setValue("CheckedDetChannel", detChannel);
    settings.endGroup();
    
    // 清理按钮组
    if (detButtonGroup)
    {
        delete detButtonGroup;
        detButtonGroup = nullptr;
    }
    delete ui;
}

void EnergyCalibration::initCustomPlot()
{
    // 创建绘图区域
    customPlot = new QCustomPlot(ui->widget_plot);
    customPlot->setObjectName("customPlot");
    customPlot->setLocale(QLocale(QLocale::Chinese, QLocale::China));
    ui->widget_plot->layout()->addWidget(customPlot);

    // 设置选择容忍度，即鼠标点击点到数据点的距离
    //customPlot->setSelectionTolerance(5);
    // 设置全局抗锯齿
    customPlot->setAntialiasedElements(QCP::aeAll);
    //customPlot->setNotAntialiasedElements(QCP::aeAll);
    // 图例名称隐藏
    customPlot->legend->setVisible(false);
    // 图例名称显示位置
    customPlot->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop|Qt::AlignRight);
    // 设置边界
    customPlot->setContentsMargins(0, 0, 0, 0);
    // 设置标签倾斜角度，避免显示不下
    customPlot->xAxis->setTickLabelRotation(-45);
    // 背景色
    customPlot->setBackground(QBrush(Qt::white));
    // 图像画布边界
    customPlot->axisRect()->setMinimumMargins(QMargins(0, 0, 0, 0));
    // 坐标背景色
    customPlot->axisRect()->setBackground(Qt::white);
    // 允许拖拽，缩放
    customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
    // 允许轴自适应大小
    //customPlot->graph(0)->rescaleValueAxis(true);
    //    customPlot->xAxis->rescale(true);
    //    customPlot->yAxis->rescale(true);

        // 设置刻度范围
    //    QSharedPointer<QCPAxisTickerFixed> axisTickerFixed(new QCPAxisTickerFixed);
    //    axisTickerFixed->setTickStep(256);
    //    axisTickerFixed->setScaleStrategy(QCPAxisTickerFixed::ssNone);
    //    customPlot->xAxis->setTicker(axisTickerFixed);
    //    customPlot->xAxis->setRange(0, 2048);
    //    customPlot->yAxis->setRange(0, 100);
    //    customPlot->yAxis->ticker()->setTickCount(5);
    //    customPlot->xAxis->ticker()->setTickCount(8);
    // 设置刻度可见
    customPlot->xAxis->setTicks(true);
    customPlot->xAxis2->setTicks(false);
    customPlot->yAxis->setTicks(true);
    customPlot->yAxis2->setTicks(false);
    // 设置轴线可见
    customPlot->xAxis->setVisible(true);
    customPlot->xAxis2->setVisible(true);
    customPlot->yAxis->setVisible(true);
    customPlot->yAxis2->setVisible(true);
    //customPlot->axisRect()->setupFullAxesBox();//四边安装轴并显示
    // 设置刻度标签可见
    customPlot->xAxis->setTickLabels(true);
    customPlot->xAxis2->setTickLabels(false);
    customPlot->yAxis->setTickLabels(true);
    customPlot->yAxis2->setTickLabels(false);
    // 设置子刻度可见
    customPlot->xAxis->setSubTicks(true);
    customPlot->xAxis2->setSubTicks(false);
    customPlot->yAxis->setSubTicks(true);
    customPlot->yAxis2->setSubTicks(false);
    //设置轴标签名称
    customPlot->xAxis->setLabel(QObject::tr("道址"));
    customPlot->yAxis->setLabel(QObject::tr("能量/keV"));
    // 设置网格线颜色
    customPlot->xAxis->grid()->setPen(QPen(QColor(180, 180, 180, 128), 1, Qt::PenStyle::DashLine));
    customPlot->yAxis->grid()->setPen(QPen(QColor(180, 180, 180, 128), 1, Qt::PenStyle::DashLine));
    customPlot->xAxis->grid()->setSubGridPen(QPen(QColor(50, 50, 50, 128), 1, Qt::DotLine));
    customPlot->yAxis->grid()->setSubGridPen(QPen(QColor(50, 50, 50, 128), 1, Qt::DotLine));
    customPlot->xAxis->grid()->setZeroLinePen(QPen(QColor(50, 50, 50, 100), 1, Qt::SolidLine));
    customPlot->yAxis->grid()->setZeroLinePen(QPen(QColor(50, 50, 50, 100), 1, Qt::SolidLine));
    // 设置网格线是否可见
    customPlot->xAxis->grid()->setVisible(false);
    customPlot->yAxis->grid()->setVisible(false);
    // 设置子网格线是否可见
    customPlot->xAxis->grid()->setSubGridVisible(false);
    customPlot->yAxis->grid()->setSubGridVisible(false);

    // 文本元素随窗口变动而变动
    fixedTextTtem = new QCPItemText(customPlot);
    fixedTextTtem->setColor(Qt::black);
    fixedTextTtem->position->setType(QCPItemPosition::ptAbsolute);
    fixedTextTtem->setPositionAlignment(Qt::AlignTop | Qt::AlignLeft);
    fixedTextTtem->setTextAlignment(Qt::AlignLeft);
    fixedTextTtem->setFont(QFont(font().family(), 12));
    fixedTextTtem->setPadding(QMargins(8, 0, 0, 0));
    fixedTextTtem->position->setCoords(20.0, 20.0);
    fixedTextTtem->setText("");

    // 添加散点图
    QCPGraph *curGraphDot = customPlot->addGraph(customPlot->xAxis, customPlot->yAxis);
    curGraphDot->setPen(QPen(Qt::black));
    curGraphDot->setLineStyle(QCPGraph::lsNone);// 取消线性图，改为散点图
    curGraphDot->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, 5));

    QCPGraph *curGraphCurve = customPlot->addGraph(customPlot->xAxis, customPlot->yAxis);
    curGraphCurve->setPen(QPen(Qt::blue, 2, Qt::PenStyle::SolidLine));
    curGraphCurve->setLineStyle(QCPGraph::lsLine);

    connect(customPlot, &QCustomPlot::afterLayout, this, [=](){
        fixedTextTtem->position->setCoords(customPlot->axisRect()->topLeft().x(), customPlot->axisRect()->topLeft().y());
    });
    // 图形刷新
    customPlot->replot();
}

void EnergyCalibration::initUI()
{
    // 清空单选按钮列表
    detRadioButtons.clear();
    
    // 创建按钮组（如果不存在）
    if (!detButtonGroup)
    {
        detButtonGroup = new QButtonGroup(this);
        detButtonGroup->setExclusive(true); // 确保互斥
    }

    // 设置表格列数为1（用于放置单选按钮）
    ui->tableWidget_det->setColumnCount(1);
    // 设置列标题（可选）
    ui->tableWidget_det->setHorizontalHeaderLabels(QStringList() << "选择");
    // 设置列宽自适应
    ui->tableWidget_det->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    
    // 确保表格有24行（UI文件中已定义，但为了安全起见检查一下）
    if (ui->tableWidget_det->rowCount() < 24)
    {
        ui->tableWidget_det->setRowCount(24);
    }

    // 初始化tableWidget_det表格，24行，每行设置一个单选按钮，用于选择是否使用该探测器进行能量刻度
    for (int i=0; i<24; ++i){
        //单选按钮
        QRadioButton *radioButton = new QRadioButton(ui->tableWidget_det);
        radioButton->setChecked(false);
        radioButton->setText(tr("谱仪#%0").arg(i+1));
        // 将单选按钮添加到第i行，第0列
        ui->tableWidget_det->setCellWidget(i, 0, radioButton);
        
        // 将单选按钮添加到按钮组，ID为i+1（通道号）
        detButtonGroup->addButton(radioButton, i+1);
        
        // 存储单选按钮指针并连接信号槽
        detRadioButtons.append(radioButton);
        connect(radioButton, &QRadioButton::toggled, this, &EnergyCalibration::on_detRadioButton_toggled);
    }

    // 读取上一次勾选的通道号，并勾选
    GlobalSettings settings(DEVICE_CONFIG_FILE);
    settings.beginGroup("EnCalibration");

    //读取当前勾选的通道号，并获取刻度数据
    int detChannel = settings.value("CheckedDetChannel", 0).toInt();
    if (detChannel > 0 && detChannel <= detRadioButtons.size()){
        detRadioButtons[detChannel-1]->setChecked(true);
        // 选中表格行（可选，用于高亮显示）
        ui->tableWidget_det->selectRow(detChannel-1);
    }

    // 读取当前通道的拟合数据
    if (detChannel > 0){
        if (settings.contains(QString("EnCalibration/Detector%1/pointsX").arg(detChannel)) == true)
        {
            settings.beginGroup(QString("EnCalibration/Detector%1").arg(detChannel));
            QVector<double> points_X = settings.GetDoubleVector("pointsX");
            QVector<double> points_Y = settings.GetDoubleVector("pointsY");

            // 拟合数据点表格赋值
            int count = points_X.size();
            for (int i=0; i<count; ++i){
                int row = ui->tableWidget->rowCount();
                ui->tableWidget->insertRow(row);
                ui->tableWidget->setItem(row, 0, new QTableWidgetItem(QString("%1").arg(points_X[i])));
                ui->tableWidget->setItem(row, 1, new QTableWidgetItem(QString("%1").arg(points_Y[i])));
            }
            
            //拟合参数赋值
            fitType = settings.value("type", 0).toInt();
            if (fitType == 1){
                //控件赋值
                ui->btnFit1->setChecked(true);
                ui->btnFit2->setChecked(false);
                //拟合参数赋值
                C[0] = settings.value("c0", 0.0).toDouble();
                C[1] = settings.value("c1", 0.0).toDouble();

                saveStatus = true;
                datafit = true;
                //绘制散点与拟合曲线
                drawScatterAndFitCurve(points_X, points_Y, fitType);
            } else if (fitType == 2){
                //控件赋值
                ui->btnFit1->setChecked(false);
                ui->btnFit2->setChecked(true);
                //拟合参数赋值
                C[0] = settings.value("c0", 0.0).toDouble();
                C[1] = settings.value("c1", 0.0).toDouble();
                C[2] = settings.value("c2", 0.0).toDouble();

                saveStatus = true;
                datafit = true;
                //绘制散点与拟合曲线
                drawScatterAndFitCurve(points_X, points_Y, fitType);
            }
            settings.endGroup();
            settings.endGroup();
        }
    }

    // 将右侧拟合点表格的单元格输入限制为浮点数
    ui->tableWidget->setItemDelegate(new DoubleItemDelegate(ui->tableWidget));
    
}

void EnergyCalibration::on_pushButton_add_clicked()
{
    // 添加
    int row = ui->tableWidget->rowCount();
    ui->tableWidget->insertRow(row);
    ui->tableWidget->setItem(row, 0, new QTableWidgetItem(QString("%1").arg(ui->doubleSpinBox_x->value())));
    ui->tableWidget->setItem(row, 1, new QTableWidgetItem(QString("%1").arg(ui->doubleSpinBox_y->value())));
}

void EnergyCalibration::on_pushButton_del_clicked()
{
    // 删除
    QList<QTableWidgetItem*> selItems = ui->tableWidget->selectedItems();
    if (selItems.count() <= 0)
        return;

    QTableWidgetItem* selItem = selItems[0];
    int row = selItem->row();
    ui->tableWidget->removeRow(row);
}

void EnergyCalibration::on_pushButton_fit_clicked()
{
    points.clear();
    datafit = false;

    for (int i= 0; i<ui->tableWidget->rowCount(); ++i){
        double x, y;
        x = ui->tableWidget->item(i, 0)->text().toDouble();
        y = ui->tableWidget->item(i, 1)->text().toDouble();
        points.push_back(QPointF(x, y));
    }

    // 检查数据点数量
    if(points.size() < 2)
    {
        ui->textEdit_log->append("拟合失败：数据点不足，至少需要2个数据点才能进行拟合\n\n");
        return;
    }

    if(ui->btnFit1->isChecked())
    {
        //第一个拟合函数
        calculate(1);
    }
    else if(ui->btnFit2->isChecked())
    {
        //第二个拟合函数
        if(points.size() < 3)
        {
            ui->textEdit_log->append("拟合失败：二次函数拟合至少需要3个数据点\n\n");
            return;
        }
        calculate(2);
    }

    /*
    // 保存界面参数
    QSettings mSettings("./config/Calibration.ini", QSettings::IniFormat);
    mSettings.setValue("Table/count", ui->tableWidget->rowCount());
    for (int i= 0; i<ui->tableWidget->rowCount(); ++i){
        double x, y;
        mSettings.setValue(QString("Table/channel%1").arg(i+1), ui->tableWidget->item(i, 0)->text());
        mSettings.setValue(QString("Table/energy%1").arg(i+1), ui->tableWidget->item(i, 1)->text());
    }*/
}

void EnergyCalibration::calculate(int no)
{
    //线性拟合 y=ax+b
    if(no == 1)
    {
        double R2 = 0.0;
        double fit_c[2] = {1.0, 1.0};
        alglib::lsfitreport rep;
        if(CurveFit::fit_linear(points, fit_c, &R2, &rep))
        {
            C[0] = fit_c[0];
            C[1] = fit_c[1];
            datafit = true;
            saveStatus = false;
            fitType = 1;

            if (C[1] > 0)
                fixedTextTtem->setText(QString("y = %1 * x + %2 \nR² = %3").arg(C[0]).arg(C[1]).arg(R2));
            else
                fixedTextTtem->setText(QString("y = %1 * x - %2 \nR² = %3").arg(C[0]).arg(qAbs(C[1])).arg(R2));
            
            // 更新textEdit_log
            QString logText = QString("========== 拟合信息 ==========\n");
            logText += QString("拟合类型: 线性拟合 (y = ax + b)\n");
            logText += QString("拟合参数:\n");
            logText += QString("  a = %1\n").arg(C[0], 0, 'g', 10);
            logText += QString("  b = %1\n").arg(C[1], 0, 'g', 10);
            logText += QString("决定系数 R² = %1\n").arg(R2, 0, 'g', 10);
            logText += QString("迭代次数: %1\n").arg(rep.iterationscount);
            // logText += QString("终止类型: %1\n").arg(rep.terminationtype);
            logText += QString("数据点数: %1\n").arg(points.size());
            logText += QString("=============================\n\n");
            ui->textEdit_log->setText(logText);
        }
        else
        {
            ui->textEdit_log->setText("拟合失败：线性拟合计算错误\n\n");
        }
    }
    else if(no == 2) //二次函数拟合 y=ax^2+bx+c
    {
        double R2 = 0.0;
        double fit_c[3] = {1.0, 1.0, 1.0};
        alglib::lsfitreport rep;
        if(CurveFit::fit_poly_2terms(points, fit_c, &R2, &rep))
        {
            C[0] = fit_c[0];
            C[1] = fit_c[1];
            C[2] = fit_c[2];
            datafit = true;
            saveStatus = false;
            fitType = 2;

            fixedTextTtem->setText(QString("y = %1 * x² + %2 * x + %3\nR² = %4").arg(C[0]).arg(C[1]).arg(C[2]).arg(R2));
            
            // 更新textEdit_log
            QString logText = QString("========== 拟合信息 ==========\n");
            logText += QString("拟合类型: 二次函数拟合 (y = ax² + bx + c)\n");
            logText += QString("拟合参数:\n");
            logText += QString("  a = %1\n").arg(C[0], 0, 'g', 10);
            logText += QString("  b = %1\n").arg(C[1], 0, 'g', 10);
            logText += QString("  c = %1\n").arg(C[2], 0, 'g', 10);
            logText += QString("决定系数 R² = %1\n").arg(R2, 0, 'g', 10);
            logText += QString("迭代次数: %1\n").arg(rep.iterationscount);
            // logText += QString("终止类型: %1\n").arg(rep.terminationtype);
            logText += QString("数据点数: %1\n").arg(points.size());
            logText += QString("=============================\n\n");
            ui->textEdit_log->setText(logText);
        }
        else
        {
            ui->textEdit_log->setText("拟合失败：二次函数拟合计算错误\n\n");
        }
    }

    //绘制待拟合点
    {
        QVector<double> keys, values;        
        for (int i = 0; i < points.size(); i++)
        {
            double x = points[i].x(), y = points[i].y();
            keys << x;
            values << y;
        }

        customPlot->graph(0)->setData(keys, values);
    }

    //绘制拟合曲线
    double maxx = 8192.0;
    if(datafit)
    {
        QVector<double> keys, values;
        
        double nx, ny;
        for (nx = 0.0; nx <= maxx; nx += 0.1)
        {
            if(ui->btnFit1->isChecked())
            {
                alglib::real_1d_array c;
                c.setcontent(2, C);
                alglib::real_1d_array xData;
                xData.setlength(1);
                xData[0] = nx;
                CurveFit::function_linear(c, xData, ny, NULL);
            }
            else
            {
                //计算y值
                alglib::real_1d_array c;
                c.setcontent(3, C);
                alglib::real_1d_array xData;
                xData.setlength(1);
                xData[0] = nx;
                CurveFit::function_poly_2terms(c, xData, ny, NULL);
            }

            keys << nx;
            values << ny;
        }

        customPlot->graph(1)->setData(keys, values);
    }

    customPlot->xAxis->rescale(true);
    customPlot->yAxis->rescale(true);
    
    //X轴和Y轴两端各保留5%的空间
    double SPANX = (customPlot->xAxis->range().upper - customPlot->xAxis->range().lower) * 0.05;
    double SPANY = (customPlot->yAxis->range().upper - customPlot->yAxis->range().lower) * 0.05;
    customPlot->xAxis->setRange(customPlot->xAxis->range().lower - SPANX, customPlot->xAxis->range().upper + SPANX);
    customPlot->yAxis->setRange(customPlot->yAxis->range().lower - SPANY, customPlot->yAxis->range().upper + SPANY);
    customPlot->replot();
}

void EnergyCalibration::on_btn_clearTable_clicked()
{
    ui->tableWidget->clearContents();
    ui->tableWidget->setRowCount(0);
}

//绘制散点和拟合曲线
void EnergyCalibration::drawScatterAndFitCurve(QVector<double> points_X, QVector<double> points_Y, int fitType)
{
    // Implementation of drawing scatter and fit curve
    points.clear();
    int count = points_X.size();
    for (int i = 0; i < count; ++i) {
        points.push_back(QPointF(points_X[i], points_Y[i]));
    }
    calculate(fitType);
}

//保存参数
void EnergyCalibration::on_bt_SaveFit_clicked()
{
    if(saveStatus)
    {
        QMessageBox::information(this, "提示", "已保存过拟合结果，请勿重复保存");
        return;
    }
    
    // 查找当前勾选的探测器通道号
    int detChannel = 0;
    for (int i = 0; i < detRadioButtons.size(); ++i)
    {
        if (detRadioButtons[i]->isChecked())
        {
            detChannel = i + 1;
            break;
        }
    }
    
    if (detChannel == 0)
    {
        QMessageBox::warning(this, "提示", "请先选择一个探测器通道");
        return;
    }
    
    // 检查是否已经拟合
    if (!datafit)
    {
        QMessageBox::warning(this, "提示", "请先进行拟合操作");
        return;
    }
    
    saveStatus = true;
    GlobalSettings settings(DEVICE_CONFIG_FILE);

    // 先清空该通道对应的子组
    settings.beginGroup("EnCalibration");
    QString channelGroupName = QString("Detector%1").arg(detChannel);
    settings.beginGroup(channelGroupName);
    settings.remove("");  // 删除该子组内的所有键
    settings.endGroup();  // 退出子组，回到EnCalibration组
    
    // 重新进入该通道的子组并保存数据
    settings.beginGroup(channelGroupName);
    
    //保存刻度的数据点
    QVector<double> points_X, points_Y;
    for (int i = 0; i < points.size(); i++)
    {
        double x = points[i].x(), y = points[i].y();
        points_X.push_back(x);
        points_Y.push_back(y);
    }
    
    settings.setDoubleVector("pointsX", points_X);
    settings.setDoubleVector("pointsY", points_Y);
    
    //保存拟合参数
    if(fitType == 1)
    {
        settings.setValue("type", 1);
        settings.setValue("c0", C[0]);
        settings.setValue("c1", C[1]);
    }
    else
    {
        settings.setValue("type", 2);
        settings.setValue("c0", C[0]);
        settings.setValue("c1", C[1]);
        settings.setValue("c2", C[2]);
    }
    
    settings.endGroup();  // 退出子组，回到EnCalibration组
    
    // 更新当前选中的通道号
    settings.setValue("CheckedDetChannel", detChannel);
    
    settings.endGroup();  // 退出EnCalibration组

    // 提示保存成功
    QMessageBox::information(this, "提示", QString("能量刻度参数已保存到谱仪#%1").arg(detChannel));
}

void EnergyCalibration::on_detRadioButton_toggled(bool checked)
{
    // 如果当前单选按钮被选中
    if (checked)
    {
        // 获取发送信号的单选按钮
        QRadioButton *senderRadioButton = qobject_cast<QRadioButton*>(sender());
        if (senderRadioButton)
        {
            // 获取选中的通道号
            int detChannel = detButtonGroup->id(senderRadioButton);
            if (detChannel > 0)
            {
                // 选中表格行（可选，用于高亮显示）
                ui->tableWidget_det->selectRow(detChannel - 1);
                
                // 加载该通道的拟合数据（如果存在）
                GlobalSettings settings(DEVICE_CONFIG_FILE);
                settings.beginGroup("EnCalibration");
                
                if (settings.contains(QString("Detector%1/pointsX").arg(detChannel)))
                {
                    settings.beginGroup(QString("Detector%1").arg(detChannel));
                    QVector<double> points_X = settings.GetDoubleVector("pointsX");
                    QVector<double> points_Y = settings.GetDoubleVector("pointsY");
                    
                    // 清空当前表格
                    ui->tableWidget->clearContents();
                    ui->tableWidget->setRowCount(0);
                    
                    // 填充数据点
                    int count = points_X.size();
                    for (int i=0; i<count; ++i){
                        int row = ui->tableWidget->rowCount();
                        ui->tableWidget->insertRow(row);
                        ui->tableWidget->setItem(row, 0, new QTableWidgetItem(QString("%1").arg(points_X[i])));
                        ui->tableWidget->setItem(row, 1, new QTableWidgetItem(QString("%1").arg(points_Y[i])));
                    }
                    
                    // 读取拟合参数并绘制
                    fitType = settings.value("type", 0).toInt();
                    if (fitType == 1){
                        ui->btnFit1->setChecked(true);
                        ui->btnFit2->setChecked(false);
                        C[0] = settings.value("c0", 0.0).toDouble();
                        C[1] = settings.value("c1", 0.0).toDouble();
                        saveStatus = true;
                        datafit = true;
                        drawScatterAndFitCurve(points_X, points_Y, fitType);
                    } else if (fitType == 2){
                        ui->btnFit1->setChecked(false);
                        ui->btnFit2->setChecked(true);
                        C[0] = settings.value("c0", 0.0).toDouble();
                        C[1] = settings.value("c1", 0.0).toDouble();
                        C[2] = settings.value("c2", 0.0).toDouble();
                        saveStatus = true;
                        datafit = true;
                        drawScatterAndFitCurve(points_X, points_Y, fitType);
                    }
                    
                    settings.endGroup();
                }
                else
                {
                    // 如果没有保存的数据，清空表格和图形
                    ui->tableWidget->clearContents();
                    ui->tableWidget->setRowCount(0);
                    saveStatus = false;
                    datafit = false;

                    // 清空图形数据
                    customPlot->graph(0)->data()->clear();
                    customPlot->graph(1)->data()->clear();
                    // 清空图例
                    fixedTextTtem->setText("");
                    customPlot->replot();

                    // 清空日志
                    ui->textEdit_log->clear();
                }
                
                settings.endGroup();
            }
        }
    }
}

