#include "CrasterParabolicHeatmapWidget.h"
#include <QPainter>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QWidgetAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QResizeEvent>
#include <cmath>
#include <algorithm>

CrasterParabolicHeatmapWidget::CrasterParabolicHeatmapWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumSize(400, 220);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested,
            this, &CrasterParabolicHeatmapWidget::onCustomContextMenuRequested);
    setupZoomBar();
}

void CrasterParabolicHeatmapWidget::setupZoomBar()
{
    zoomBar_ = new QFrame(this);
    zoomBar_->setObjectName(QStringLiteral("zoomBar"));
    zoomBar_->setFrameShape(QFrame::NoFrame);
    zoomBar_->setStyleSheet(
        QStringLiteral(
            "QFrame#zoomBar { background: rgba(245,245,245,230); border: 1px solid #c0c0c0; border-radius: 3px; }"
            "QToolButton { border: none; padding: 0 4px; }"
            "QToolButton:hover { background: #e0e0e0; border-radius: 2px; }"
            "QToolButton#btnZoomPercent { padding: 0 6px; min-width: 52px; }"
            "QToolButton#btnZoomPercent::menu-indicator { width: 10px; }"));

    // 百分比下拉（类似 WPS：固定档位 + 自定义百分比）
    btnZoomPercent_ = new QToolButton(zoomBar_);
    btnZoomPercent_->setObjectName(QStringLiteral("btnZoomPercent"));
    btnZoomPercent_->setText(QStringLiteral("100%"));
    btnZoomPercent_->setToolTip(tr("显示比例"));
    btnZoomPercent_->setPopupMode(QToolButton::InstantPopup);
    btnZoomPercent_->setAutoRaise(true);

    zoomMenu_ = new QMenu(btnZoomPercent_);
    zoomMenu_->setTitle(tr("显示比例"));

    auto *titleAct = zoomMenu_->addSection(tr("显示比例"));
    Q_UNUSED(titleAct);

    zoomPresetGroup_ = new QActionGroup(zoomMenu_);
    zoomPresetGroup_->setExclusive(false); // 自定义百分比时允许全部不勾选

    const int presets[] = {500, 400, 300, 200, 150, 100, 75, 50, 25};
    for (int p : presets) {
        QAction *act = zoomMenu_->addAction(QStringLiteral("%1%").arg(p));
        act->setCheckable(true);
        act->setData(p);
        zoomPresetGroup_->addAction(act);
        if (p == 100)
            act->setChecked(true);
    }

    zoomMenu_->addSeparator();
    QAction *fitAct = zoomMenu_->addAction(tr("适合窗口"));
    fitAct->setData(-1); // 复位视图
    fitAct->setCheckable(false);

    zoomMenu_->addSeparator();

    // 自定义百分比输入区
    auto *percentWidget = new QWidget(zoomMenu_);
    auto *percentLay = new QHBoxLayout(percentWidget);
    percentLay->setContentsMargins(10, 4, 10, 6);
    percentLay->setSpacing(6);
    auto *percentLabel = new QLabel(tr("百分比:"), percentWidget);
    zoomSpin_ = new QSpinBox(percentWidget);
    zoomSpin_->setRange(int(kMinScale_ * 100), int(kMaxScale_ * 100));
    zoomSpin_->setSuffix(QStringLiteral(" %"));
    zoomSpin_->setSingleStep(5);
    zoomSpin_->setValue(100);
    zoomSpin_->setFixedWidth(90);
    percentLay->addWidget(percentLabel);
    percentLay->addWidget(zoomSpin_);
    percentLay->addStretch();

    auto *spinAction = new QWidgetAction(zoomMenu_);
    spinAction->setDefaultWidget(percentWidget);
    zoomMenu_->addAction(spinAction);

    btnZoomPercent_->setMenu(zoomMenu_);

    btnZoomOut_ = new QToolButton(zoomBar_);
    btnZoomOut_->setText(QStringLiteral("−"));
    btnZoomOut_->setToolTip(tr("缩小"));
    btnZoomOut_->setAutoRaise(true);
    btnZoomOut_->setFixedSize(22, 22);

    zoomSlider_ = new QSlider(Qt::Horizontal, zoomBar_);
    zoomSlider_->setRange(int(kMinScale_ * 100), int(kMaxScale_ * 100)); // 10% ~ 500%
    zoomSlider_->setSingleStep(10);
    zoomSlider_->setPageStep(25);
    zoomSlider_->setValue(100);
    zoomSlider_->setFixedWidth(120);
    zoomSlider_->setToolTip(tr("缩放"));

    btnZoomIn_ = new QToolButton(zoomBar_);
    btnZoomIn_->setText(QStringLiteral("+"));
    btnZoomIn_->setToolTip(tr("放大"));
    btnZoomIn_->setAutoRaise(true);
    btnZoomIn_->setFixedSize(22, 22);

    // 布局：百分比下拉 | − | 滑条 | +（贴近 WPS）
    auto *lay = new QHBoxLayout(zoomBar_);
    lay->setContentsMargins(4, 2, 6, 2);
    lay->setSpacing(2);
    lay->addWidget(btnZoomPercent_);
    lay->addWidget(btnZoomOut_);
    lay->addWidget(zoomSlider_);
    lay->addWidget(btnZoomIn_);

    connect(zoomSlider_, &QSlider::valueChanged,
            this, &CrasterParabolicHeatmapWidget::onZoomSliderChanged);
    connect(btnZoomIn_, &QToolButton::clicked,
            this, &CrasterParabolicHeatmapWidget::onZoomInClicked);
    connect(btnZoomOut_, &QToolButton::clicked,
            this, &CrasterParabolicHeatmapWidget::onZoomOutClicked);
    connect(zoomPresetGroup_, &QActionGroup::triggered,
            this, &CrasterParabolicHeatmapWidget::onZoomPresetTriggered);
    connect(fitAct, &QAction::triggered, this, [this]() { resetView(); });
    connect(zoomSpin_, &QSpinBox::editingFinished,
            this, &CrasterParabolicHeatmapWidget::onZoomSpinEditingFinished);
    // 微调百分比时实时生效，菜单保持打开（贴近 WPS）
    connect(zoomSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (syncingZoomUi_)
            return;
        setViewScaleAt(v / 100.0, mapRect().center());
    });
    connect(zoomMenu_, &QMenu::aboutToShow,
            this, &CrasterParabolicHeatmapWidget::onZoomMenuAboutToShow);

    zoomBar_->adjustSize();
    zoomBar_->raise();
}

void CrasterParabolicHeatmapWidget::layoutZoomBar()
{
    if (!zoomBar_)
        return;
    zoomBar_->adjustSize();
    const int margin = 8;
    const int x = width() - zoomBar_->width() - margin;
    const int y = height() - zoomBar_->height() - margin;
    zoomBar_->move(std::max(margin, x), std::max(margin, y));
    zoomBar_->raise();
}

void CrasterParabolicHeatmapWidget::syncZoomUi()
{
    if (!zoomSlider_ || !btnZoomPercent_)
        return;
    syncingZoomUi_ = true;
    const int percent = qRound(viewScale_ * 100.0);
    zoomSlider_->setValue(percent);
    btnZoomPercent_->setText(QStringLiteral("%1%").arg(percent));
    if (zoomSpin_)
        zoomSpin_->setValue(percent);

    // 同步下拉勾选：仅当恰好命中固定档位时勾选
    if (zoomPresetGroup_) {
        bool matched = false;
        const auto actions = zoomPresetGroup_->actions();
        for (QAction *act : actions) {
            const bool on = (act->data().toInt() == percent);
            act->setChecked(on);
            matched = matched || on;
        }
        if (!matched) {
            for (QAction *act : actions)
                act->setChecked(false);
        }
    }
    syncingZoomUi_ = false;
}

void CrasterParabolicHeatmapWidget::onZoomMenuAboutToShow()
{
    syncZoomUi();
}

void CrasterParabolicHeatmapWidget::onZoomPresetTriggered(QAction *action)
{
    if (!action || syncingZoomUi_)
        return;
    const int percent = action->data().toInt();
    if (percent <= 0)
        return;

    // 强制保持当前档位勾选（避免再次点击取消勾选）
    if (zoomPresetGroup_) {
        for (QAction *act : zoomPresetGroup_->actions())
            act->setChecked(act == action);
    }
    setViewScaleAt(percent / 100.0, mapRect().center());
}

void CrasterParabolicHeatmapWidget::onZoomSpinEditingFinished()
{
    if (syncingZoomUi_ || !zoomSpin_)
        return;
    setViewScaleAt(zoomSpin_->value() / 100.0, mapRect().center());
}

void CrasterParabolicHeatmapWidget::onZoomSliderChanged(int percent)
{
    if (syncingZoomUi_)
        return;
    // 滑条缩放以地图中心为锚点
    setViewScaleAt(percent / 100.0, mapRect().center());
}

void CrasterParabolicHeatmapWidget::onZoomInClicked()
{
    const double step = (viewScale_ < 1.0) ? 0.10 : 0.25;
    setViewScaleAt(viewScale_ + step, mapRect().center());
}

void CrasterParabolicHeatmapWidget::onZoomOutClicked()
{
    const double step = (viewScale_ <= 1.0) ? 0.10 : 0.25;
    setViewScaleAt(viewScale_ - step, mapRect().center());
}

void CrasterParabolicHeatmapWidget::resetView()
{
    viewScale_ = 1.0;
    viewOffset_ = QPointF(0, 0);
    syncZoomUi();
    update();
}

void CrasterParabolicHeatmapWidget::setViewScaleAt(double newScale, const QPointF& anchorWidgetPos)
{
    newScale = std::clamp(newScale, kMinScale_, kMaxScale_);
    if (std::abs(newScale - viewScale_) < 1e-9) {
        syncZoomUi(); // 仍刷新 UI（如滑条与下拉勾选）
        return;
    }

    const QRectF mapR = mapRect();
    const QRectF base = heatDrawRect(mapR);
    const QPointF c = base.center();

    // display = c + viewOffset_ + v * viewScale_
    // 锚点下内容向量 v 在缩放前后保持不变
    const QPointF v = (anchorWidgetPos - c - viewOffset_) / viewScale_;
    viewOffset_ = anchorWidgetPos - c - v * newScale;
    viewScale_ = newScale;

    syncZoomUi();
    update();
}

QRectF CrasterParabolicHeatmapWidget::viewDrawRect(const QRectF& baseDrawR) const
{
    const QPointF c = baseDrawR.center();
    const double w = baseDrawR.width() * viewScale_;
    const double h = baseDrawR.height() * viewScale_;
    return QRectF(c.x() - w * 0.5 + viewOffset_.x(),
                  c.y() - h * 0.5 + viewOffset_.y(),
                  w, h);
}

void CrasterParabolicHeatmapWidget::wheelEvent(QWheelEvent* e)
{
    if (!mapRect().contains(e->posF())) {
        QWidget::wheelEvent(e);
        return;
    }

    const QPoint numDegrees = e->angleDelta() / 8;
    if (numDegrees.isNull()) {
        e->ignore();
        return;
    }

    // 向上滚放大，向下滚缩小；步进约 10%~15%
    const double steps = numDegrees.y() / 15.0;
    const double factor = std::pow(1.12, steps);
    setViewScaleAt(viewScale_ * factor, e->posF());
    e->accept();
}

void CrasterParabolicHeatmapWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && mapRect().contains(e->localPos())) {
        panning_ = true;
        lastPanPos_ = e->pos();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void CrasterParabolicHeatmapWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (panning_) {
        const QPoint delta = e->pos() - lastPanPos_;
        lastPanPos_ = e->pos();
        viewOffset_ += delta;
        update();
        e->accept();
        return;
    }

    if (mapRect().contains(e->localPos()))
        setCursor(Qt::OpenHandCursor);
    else
        unsetCursor();

    QWidget::mouseMoveEvent(e);
}

void CrasterParabolicHeatmapWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && panning_) {
        panning_ = false;
        if (mapRect().contains(e->localPos()))
            setCursor(Qt::OpenHandCursor);
        else
            unsetCursor();
        e->accept();
        return;
    }
    QWidget::mouseReleaseEvent(e);
}

void CrasterParabolicHeatmapWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && mapRect().contains(e->localPos())) {
        resetView();
        e->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

void CrasterParabolicHeatmapWidget::onCustomContextMenuRequested(const QPoint &pos)
{
    QMenu menu(this);
    QAction *resetViewAct = menu.addAction(tr("复位视图（100%）"));
    QAction *exportPngAct = menu.addAction(tr("导出为 PNG…"));
    QAction *chosen = menu.exec(mapToGlobal(pos));
    if (chosen == resetViewAct) {
        resetView();
        return;
    }
    if (chosen != exportPngAct)
        return;

    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("导出图片"),
        QStringLiteral("中子产额空间分布.png"),
        tr("PNG 图片 (*.png)"));
    if (path.isEmpty())
        return;

    const QPixmap pix = grab();
    if (pix.isNull() || !exportToPng(path)) {
        QMessageBox::warning(this, tr("提示"), tr("PNG 导出失败。"));
        return;
    }
    QMessageBox::information(this, tr("提示"), tr("已导出：%1").arg(path));
}

void CrasterParabolicHeatmapWidget::setSensors(const std::vector<Sensor>& s) {
    sensors_ = s;
    shReady_ = false;
}

bool CrasterParabolicHeatmapWidget::exportToPng(const QString &filePath) const
{
    if (filePath.isEmpty())
        return false;

    auto *self = const_cast<CrasterParabolicHeatmapWidget *>(this);
    const QSize oldSize = size();
    const bool needTempSize = width() < 50 || height() < 50;
    if (needTempSize) {
        self->resize(1200, 650);
        self->rebuild();
    }

    const QPixmap pix = self->grab();
    if (needTempSize) {
        self->resize(oldSize);
        if (!oldSize.isEmpty())
            self->rebuild();
    }

    if (pix.isNull() || pix.size().isEmpty())
        return false;
    return pix.save(filePath, "PNG");
}

void CrasterParabolicHeatmapWidget::setPower(double p) { idwPower_ = p; }

void CrasterParabolicHeatmapWidget::setInterpMode(InterpMode m) {
    mode_ = m;
    shReady_ = false;
}

void CrasterParabolicHeatmapWidget::setSHOrder(int L) {
    shOrder_ = std::max(0, L);
    shReady_ = false;
}

void CrasterParabolicHeatmapWidget::setSHLambda(double lambda) {
    shLambda_ = std::max(0.0, lambda);
    shReady_ = false;
}

double CrasterParabolicHeatmapWidget::deg2rad(double d){ return d * M_PI / 180.0; }

QColor CrasterParabolicHeatmapWidget::colormap(double t01) {
    t01 = std::clamp(t01, 0.0, 1.0);
    int r = int(255 * t01);
    int b = int(255 * (1.0 - t01));
    int g = int(255 * (0.5 - std::abs(t01 - 0.5)) * 2.0);
    return QColor(r, g, b);
}

double CrasterParabolicHeatmapWidget::greatCircleAngle(double lon1, double lat1, double lon2, double lat2) {
    double dlon = lon2 - lon1;
    double dlat = lat2 - lat1;
    double a = std::sin(dlat/2)*std::sin(dlat/2)
               + std::cos(lat1)*std::cos(lat2)*std::sin(dlon/2)*std::sin(dlon/2);
    a = std::clamp(a, 0.0, 1.0);
    return 2.0 * std::asin(std::sqrt(a));
}

// -------------------- PROJ crast (Craster Parabolic / Putnins P4) --------------------
bool CrasterParabolicHeatmapWidget::forwardCraster(double lon, double lat, double &x, double &y) {
    double phi3 = lat / 3.0;
    double c = 2.0 * std::cos(2.0 * phi3) - 1.0;

    x = XM_ * lon * c;
    y = YM_ * std::sin(phi3);
    return true;
}

bool CrasterParabolicHeatmapWidget::inverseCraster(double x, double y, double &lon, double &lat) {
    double t = std::clamp(y * RYM_, -1.0, 1.0);
    double phi3 = std::asin(t);
    lat = 3.0 * phi3;

    double c = 2.0 * std::cos(2.0 * phi3) - 1.0;
    if (std::abs(c) < 1e-12) return false;

    lon = x * RXM_ / c;
    return (lon >= -M_PI && lon <= M_PI && lat >= -M_PI_2 && lat <= M_PI_2);
}

// -------------------- layout mapping --------------------
QRectF CrasterParabolicHeatmapWidget::mapRect() const {
    const int margin = 10;
    const int barH = 26;
    const int textH = 18;
    const int gap = 6;

    // 底部：色条 + 文字 + 缩放条
    int bottomReserve = margin + barH + gap + textH + gap + kZoomBarH_ + margin;
    QRectF r(margin, margin, width() - 2*margin, height() - bottomReserve - margin);
    if (r.height() < 50) r.setHeight(50);
    if (r.width()  < 50) r.setWidth(50);
    return r;
}

QPointF CrasterParabolicHeatmapWidget::projToPixelInRect(double x, double y, const QRectF& r) const {
    // ⭐ 用 PROJ 的投影范围：x∈[-XMAX_,XMAX_], y∈[-YMAX_,YMAX_]
    double xn = x / XMAX_; // [-1,1]
    double yn = y / YMAX_; // [-1,1]

    double px = r.left() + (xn + 1.0) * 0.5 * r.width();
    double py = r.top()  + (1.0 - (yn + 1.0) * 0.5) * r.height();
    return QPointF(px, py);
}

void CrasterParabolicHeatmapWidget::pixelToProjInRect(const QPointF& pt,
                                                      const QRectF& r,
                                                      double& x, double& y) const
{
    double u = (pt.x() - r.left()) / r.width();   // 0..1
    double v = (pt.y() - r.top())  / r.height();  // 0..1
    double xn = 2.0*u - 1.0;      // -1..1
    double yn = 1.0 - 2.0*v;      // +1..-1 (y翻转)

    x = xn * XMAX_;
    y = yn * YMAX_;
}

// -------------------- true outline (NOT ellipse) --------------------
QPainterPath CrasterParabolicHeatmapWidget::crasterOutlinePath(const QRectF& r) const {
    QPainterPath path;
    bool first = true;

    // 左边界：lon=-pi
    for (double latDeg = -90.0; latDeg <= 90.0; latDeg += 0.25) {
        double lon = -M_PI;
        double lat = deg2rad(latDeg);
        double x, y;
        forwardCraster(lon, lat, x, y);
        QPointF pt = projToPixelInRect(x, y, r);
        if (first) { path.moveTo(pt); first = false; }
        else       { path.lineTo(pt); }
    }

    // 右边界：lon=+pi（反向回去闭合）
    for (double latDeg = 90.0; latDeg >= -90.0; latDeg -= 0.25) {
        double lon = +M_PI;
        double lat = deg2rad(latDeg);
        double x, y;
        forwardCraster(lon, lat, x, y);
        path.lineTo(projToPixelInRect(x, y, r));
    }

    path.closeSubpath();
    return path;
}

// -------------------- field & color --------------------
double CrasterParabolicHeatmapWidget::evaluateField(double lon, double lat) const {
    if (mode_ == InterpMode::SphericalHarmonics && shReady_) {
        return evalSphericalHarmonics(lon, lat);
    }

    const double eps = 1e-6;
    double wsum = 0.0, vsum = 0.0;

    for (auto &s: sensors_) {
        double slon = deg2rad(s.lonDeg);
        double slat = deg2rad(s.latDeg);
        double ang  = greatCircleAngle(lon, lat, slon, slat);
        double w    = 1.0 / (std::pow(ang, idwPower_) + eps);
        wsum += w;
        vsum += w * s.value;
    }
    return vsum / std::max(1e-12, wsum);
}

QColor CrasterParabolicHeatmapWidget::colorFromValue(double v) const {
    double denom = std::max(1e-12, vmax_ - vmin_);
    double t01 = (v - vmin_) / denom;
    return colormap(t01);
}

// -------------------- heatmap render (mapRect pixel area) --------------------
QImage CrasterParabolicHeatmapWidget::renderHeatmapCraster(const QSize& sz) const {
    QImage img(sz, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    // 注意：这里的 r 是“热力图像素坐标系的矩形”
    QRectF rImg(0, 0, sz.width(), sz.height());

    for (int j = 0; j < sz.height(); ++j) {
        QRgb* scan = reinterpret_cast<QRgb*>(img.scanLine(j));
        for (int i = 0; i < sz.width(); ++i) {
            double x, y;
            // 用 pixelToProjInRect：输入的是“图像像素坐标”，r 用 rImg
            pixelToProjInRect(QPointF(i + 0.5, j + 0.5), rImg, x, y);

            double lon, lat;
            if (!inverseCraster(x, y, lon, lat)) { scan[i] = qRgba(0,0,0,0); continue; }

            double v = evaluateField(lon, lat);
            QColor c = colorFromValue(v);
            scan[i] = qRgba(c.red(), c.green(), c.blue(), 255);
        }
    }
    return img;
}

// -------------------- rebuild & paint --------------------
void CrasterParabolicHeatmapWidget::rebuild() {
    if (sensors_.empty()) { heatMap_ = QImage(); update(); return; }

    vmin_ = 1e100; vmax_ = -1e100;
    for (auto &s: sensors_) { vmin_ = std::min(vmin_, s.value); vmax_ = std::max(vmax_, s.value); }
    if (std::abs(vmax_ - vmin_) < 1e-12) vmax_ = vmin_ + 1.0;

    if (mode_ == InterpMode::SphericalHarmonics) fitSphericalHarmonics();

    heatMap_ = renderHeatmapCraster(QSize(HEAT_W, HEAT_H));
    heatMapRect_ = QRect();  // 可选：防止误用
    update();
}

void CrasterParabolicHeatmapWidget::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    layoutZoomBar();
    rebuild();
}

QRectF CrasterParabolicHeatmapWidget::heatDrawRect(const QRectF& mapR) const
{
    const double sx = mapR.width()  / double(HEAT_W);
    const double sy = mapR.height() / double(HEAT_H);

    const double epsScale = 1e-3;     // 判断是否“变化”
    const double epsEqual = 0.02;     // 允许 2% 以内认为等比（你可调 0.01~0.03）

    const bool xChanged = std::abs(sx - 1.0) > epsScale;
    const bool yChanged = std::abs(sy - 1.0) > epsScale;

    // 只有 x/y 都变化，并且比例接近，才允许缩放（避免拉伸）
    const bool allowUniformScale = (xChanged && yChanged && (std::abs(sx - sy) < epsEqual));

    double scale = 1.0;
    if (allowUniformScale) {
        // sx≈sy，取平均或取 min 都行；取 min 更保险避免越界
        scale = std::min(sx, sy);
    }

    const double drawW = HEAT_W * scale;
    const double drawH = HEAT_H * scale;

    // 居中放在 mapRect 内
    const double left = mapR.center().x() - drawW * 0.5;
    const double top  = mapR.center().y() - drawH * 0.5;
    return QRectF(left, top, drawW, drawH);
}

void CrasterParabolicHeatmapWidget::drawBorder(QPainter& p, const QRectF& r) const {
    p.drawPath(crasterOutlinePath(r));
}

void CrasterParabolicHeatmapWidget::drawGraticule(QPainter& p, const QRectF& r) const {
    const int lonStep = 30;
    const int latStep = 15;
    const double dDeg = 1.0;

    p.save();
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    QPen pen(QColor(0, 0, 0, 220));
    pen.setWidthF(2.0);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    // ⭐ 用真实边界裁剪
    QPainterPath clip = crasterOutlinePath(r);
    p.setClipPath(clip, Qt::ReplaceClip);

    // 纬线
    for (int latDeg = -90; latDeg <= 90; latDeg += latStep) {
        QPainterPath path;
        bool started = false;

        for (double lonDeg = -180.0; lonDeg <= 180.0; lonDeg += dDeg) {
            double lon = deg2rad(lonDeg), lat = deg2rad((double)latDeg);
            double x,y; forwardCraster(lon, lat, x, y);
            QPointF pt = projToPixelInRect(x, y, r);
            if (!started) { path.moveTo(pt); started = true; }
            else          { path.lineTo(pt); }
        }
        p.drawPath(path);
    }

    // 经线（注意：一定要 moveTo 开始）
    for (int lonDeg = -180; lonDeg <= 180; lonDeg += lonStep) {
        QPainterPath path;
        bool started = false;

        for (double latDeg = -90.0; latDeg <= 90.0; latDeg += dDeg) {
            double lon = deg2rad((double)lonDeg), lat = deg2rad(latDeg);
            double x,y; forwardCraster(lon, lat, x, y);
            QPointF pt = projToPixelInRect(x, y, r);
            if (!started) { path.moveTo(pt); started = true; }
            else          { path.lineTo(pt); }
        }
        p.drawPath(path);
    }

    // 十字交叉标注：经度标在赤道(lat=0)，纬度标在本初子午(lon=0)
    p.setClipping(false);
    QFont labelFont = p.font();
    labelFont.setPointSizeF(std::max(8.0, labelFont.pointSizeF()));
    p.setFont(labelFont);
    const QFontMetrics fm(labelFont);

    auto drawLabelAt = [&](double lonDeg, double latDeg, const QString &text, int alignFlags) {
        double x = 0.0, y = 0.0;
        forwardCraster(deg2rad(lonDeg), deg2rad(latDeg), x, y);
        const QPointF pt = projToPixelInRect(x, y, r);
        const QSize textSize = fm.size(0, text);
        QRectF textRect(0, 0, textSize.width() + 4, textSize.height() + 2);
        if (alignFlags & Qt::AlignLeft)
            textRect.moveLeft(pt.x() + 3);
        else if (alignFlags & Qt::AlignRight)
            textRect.moveRight(pt.x() - 3);
        else
            textRect.moveCenter(QPointF(pt.x(), textRect.center().y()));
        if (alignFlags & Qt::AlignTop)
            textRect.moveTop(pt.y() + 2);
        else if (alignFlags & Qt::AlignBottom)
            textRect.moveBottom(pt.y() - 2);
        else
            textRect.moveCenter(QPointF(textRect.center().x(), pt.y()));

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 180));
        p.drawRoundedRect(textRect, 2, 2);
        p.setPen(QColor(0, 0, 0, 230));
        p.drawText(textRect, Qt::AlignCenter, text);
    };

    // 经度：各经线与赤道交点（±180° 只标一侧）
    for (int lonDeg = -180; lonDeg < 180; lonDeg += lonStep) {
        drawLabelAt(lonDeg, 0.0,
                    QString::number(lonDeg) + QChar(0x00B0),
                    Qt::AlignHCenter | Qt::AlignBottom);
    }

    // 纬度：各纬线与 0° 经线交点（跳过 0°，已由经度标注覆盖）
    for (int latDeg = -90; latDeg <= 90; latDeg += latStep) {
        if (latDeg == 0)
            continue;
        drawLabelAt(0.0, latDeg,
                    QString::number(latDeg) + QChar(0x00B0),
                    Qt::AlignLeft | Qt::AlignVCenter);
    }

    p.restore();
}

void CrasterParabolicHeatmapWidget::drawColorbar(QPainter& p) const {
    const int margin = 10;
    const int barH = 26;
    const int textH = 18;
    const int gap = 6;

    // 色条在缩放条上方；右侧预留缩放条宽度，避免重叠
    const int zoomReserve = zoomBar_ ? (zoomBar_->width() + 12) : 160;
    int y0 = height() - margin - kZoomBarH_ - gap - textH - gap - barH;
    QRect barRect(margin, y0, std::max(50, width() - 2*margin - zoomReserve), barH);

    QImage grad(barRect.width(), 1, QImage::Format_ARGB32);
    for (int x=0; x<barRect.width(); ++x) {
        double t = double(x) / std::max(1, barRect.width()-1);
        QColor c = colormap(t);
        grad.setPixel(x, 0, qRgba(c.red(), c.green(), c.blue(), 255));
    }
    p.drawImage(barRect, grad.scaled(barRect.size()));

    p.setPen(QPen(Qt::black, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(barRect);

    auto fmt = [](double v){ return QString::number(v, 'g', 4); };
    double mid = 0.5*(vmin_ + vmax_);

    QRect textRect(barRect.left(), barRect.bottom() + gap, barRect.width(), textH);
    p.setPen(Qt::black);

    p.drawText(QRect(textRect.left(), textRect.top(), textRect.width()/3, textRect.height()),
               Qt::AlignLeft|Qt::AlignVCenter, fmt(vmin_));
    p.drawText(QRect(textRect.left()+textRect.width()/3, textRect.top(), textRect.width()/3, textRect.height()),
               Qt::AlignHCenter|Qt::AlignVCenter, fmt(mid));
    p.drawText(QRect(textRect.left()+2*textRect.width()/3, textRect.top(), textRect.width()/3, textRect.height()),
               Qt::AlignRight|Qt::AlignVCenter, fmt(vmax_));

    int yTick1 = barRect.bottom();
    int xMin = barRect.left();
    int xMid = barRect.left() + barRect.width()/2;
    int xMax = barRect.right();
    p.drawLine(xMin, yTick1, xMin, yTick1+5);
    p.drawLine(xMid, yTick1, xMid, yTick1+5);
    p.drawLine(xMax, yTick1, xMax, yTick1+5);
}

void CrasterParabolicHeatmapWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QRectF mapR  = mapRect();               // 可用于裁剪的地图区域
    QRectF baseR = heatDrawRect(mapR);      // 100% 时的基准矩形
    QRectF drawR = viewDrawRect(baseR);     // 应用缩放/平移后的显示矩形

    // 1) 热力图：按 drawR 显示，裁剪到 mapR，避免盖到色条/缩放条
    if (!heatMap_.isNull()) {
        p.save();
        p.setClipRect(mapR, Qt::ReplaceClip);
        p.drawImage(drawR, heatMap_);
        p.restore();
    }

    // 2) 边框 / 探头点 / 经纬网同样裁剪到 mapR
    p.save();
    p.setClipRect(mapR, Qt::ReplaceClip);

    p.setPen(QPen(Qt::black, 2));
    drawBorder(p, drawR);

    p.setPen(QPen(Qt::black, 2));
    p.setBrush(Qt::white);
    for (auto &s: sensors_) {
        double lon = deg2rad(s.lonDeg);
        double lat = deg2rad(s.latDeg);
        double x, y;
        forwardCraster(lon, lat, x, y);
        QPointF pt = projToPixelInRect(x, y, drawR);
        p.drawEllipse(pt, 5, 5);
    }

    drawGraticule(p, drawR);
    p.restore();

    // 3) 色条（不受视图变换影响）
    drawColorbar(p);
}

// -------------------- Spherical Harmonics (unchanged) --------------------
double CrasterParabolicHeatmapWidget::assocLegendreP(int l, int m, double x) {
    double pmm = 1.0;
    if (m > 0) {
        double somx2 = std::sqrt(std::max(0.0, 1.0 - x*x));
        double fact = 1.0;
        for (int i=1; i<=m; ++i) {
            pmm *= (-fact) * somx2;
            fact += 2.0;
        }
    }
    if (l == m) return pmm;

    double pmmp1 = x * (2.0*m + 1.0) * pmm;
    if (l == m + 1) return pmmp1;

    double pll = 0.0;
    double p1 = pmmp1;
    double p2 = pmm;
    for (int ll=m+2; ll<=l; ++ll) {
        pll = ((2.0*ll - 1.0)*x*p1 - (ll + m - 1.0)*p2) / (ll - m);
        p2 = p1;
        p1 = pll;
    }
    return pll;
}

double CrasterParabolicHeatmapWidget::factorialRatio(int a, int b) {
    if (a == b) return 1.0;
    double r = 1.0;
    if (a < b) { for (int i=a+1; i<=b; ++i) r /= double(i); }
    else       { for (int i=b+1; i<=a; ++i) r *= double(i); }
    return r;
}

double CrasterParabolicHeatmapWidget::normNlm(int l, int m) {
    const double PI4 = 4.0 * M_PI;
    double a = (2.0*l + 1.0) / PI4;
    double fr = factorialRatio(l - m, l + m);
    return std::sqrt(a * fr);
}

int CrasterParabolicHeatmapWidget::shIndex(int l, int m) {
    return l*l + (m + l);
}

void CrasterParabolicHeatmapWidget::shBasisReal(int L, double lon, double lat, std::vector<double>& out) {
    out.assign((L+1)*(L+1), 0.0);

    double theta = M_PI_2 - lat;
    double x = std::cos(theta); // = sin(lat)

    std::vector<double> cosml(L+1), sinml(L+1);
    cosml[0]=1.0; sinml[0]=0.0;
    for (int m=1; m<=L; ++m) {
        cosml[m] = std::cos(m*lon);
        sinml[m] = std::sin(m*lon);
    }

    for (int l=0; l<=L; ++l) {
        double Pl0 = assocLegendreP(l, 0, x);
        out[shIndex(l,0)] = normNlm(l,0) * Pl0;
    }

    for (int l=1; l<=L; ++l) {
        for (int m=1; m<=l; ++m) {
            double Plm = assocLegendreP(l, m, x);
            double K = std::sqrt(2.0) * normNlm(l,m) * Plm;
            out[shIndex(l, +m)] = K * cosml[m];
            out[shIndex(l, -m)] = K * sinml[m];
        }
    }
}

bool CrasterParabolicHeatmapWidget::solveSymmetricSystem(std::vector<double>& M,
                                                         std::vector<double>& rhs,
                                                         int n) {
    for (int i=0; i<n; ++i) {
        int piv = i;
        double best = std::abs(M[i*n + i]);
        for (int r=i+1; r<n; ++r) {
            double v = std::abs(M[r*n + i]);
            if (v > best) { best = v; piv = r; }
        }
        if (best < 1e-14) return false;

        if (piv != i) {
            for (int c=i; c<n; ++c) std::swap(M[i*n + c], M[piv*n + c]);
            std::swap(rhs[i], rhs[piv]);
        }

        double diag = M[i*n + i];
        for (int c=i; c<n; ++c) M[i*n + c] /= diag;
        rhs[i] /= diag;

        for (int r=0; r<n; ++r) {
            if (r == i) continue;
            double f = M[r*n + i];
            if (std::abs(f) < 1e-16) continue;
            for (int c=i; c<n; ++c) M[r*n + c] -= f * M[i*n + c];
            rhs[r] -= f * rhs[i];
        }
    }
    return true;
}

void CrasterParabolicHeatmapWidget::fitSphericalHarmonics() {
    int L = shOrder_;
    int K = shBasisCount(L);

    std::vector<double> M(K*K, 0.0);
    std::vector<double> rhs(K, 0.0);
    std::vector<double> y; y.reserve(K);

    for (auto &s : sensors_) {
        double lon = deg2rad(s.lonDeg);
        double lat = deg2rad(s.latDeg);
        shBasisReal(L, lon, lat, y);

        for (int i=0; i<K; ++i) {
            rhs[i] += y[i] * s.value;
            for (int j=0; j<K; ++j) {
                M[i*K + j] += y[i] * y[j];
            }
        }
    }

    for (int i=0; i<K; ++i) M[i*K + i] += shLambda_;

    bool ok = solveSymmetricSystem(M, rhs, K);
    if (!ok) { shReady_ = false; return; }

    shCoeff_ = rhs;
    shReady_ = true;
}

double CrasterParabolicHeatmapWidget::evalSphericalHarmonics(double lon, double lat) const {
    int L = shOrder_;
    int K = shBasisCount(L);
    if (!shReady_ || (int)shCoeff_.size() != K) return 0.0;

    std::vector<double> y;
    shBasisReal(L, lon, lat, y);

    double v = 0.0;
    for (int k=0; k<K; ++k) v += shCoeff_[k] * y[k];
    return v;
}
