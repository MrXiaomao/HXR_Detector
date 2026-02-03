#include "CrasterParabolicHeatmapWidget.h"
#include <QPainter>
#include <algorithm>

CrasterParabolicHeatmapWidget::CrasterParabolicHeatmapWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumSize(400, 220);
}

void CrasterParabolicHeatmapWidget::setSensors(const std::vector<Sensor>& s) {
    sensors_ = s;
    shReady_ = false;
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

    int bottomReserve = margin + barH + gap + textH + margin;
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

    p.restore();
}

void CrasterParabolicHeatmapWidget::drawColorbar(QPainter& p) const {
    const int margin = 10;
    const int barH = 26;
    const int textH = 18;
    const int gap = 6;

    int y0 = height() - margin - textH - gap - barH;
    QRect barRect(margin, y0, width() - 2*margin, barH);

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

    QRect textRect(margin, barRect.bottom() + gap, width() - 2*margin, textH);
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

    // QRectF r = mapRect();
    QRectF mapR  = mapRect();               // 仍然用于“地图区域布局”
    QRectF drawR = heatDrawRect(mapR);      // 实际显示热力图/网格/边框/点的矩形

    // 1) 热力图（直接贴到 mapRect 像素位置）
    // 1) 热力图：按 drawR 显示（允许等比缩放/或不缩放）
    // 且裁剪到 mapR，避免盖到色条区域
    if (!heatMap_.isNull()) {
        p.save();
        p.setClipRect(mapR, Qt::ReplaceClip);
        p.drawImage(drawR, heatMap_);   // ⭐ 关键：用 drawR，而不是 heatMapRect_
        p.restore();
    }

    // 2) 边框（真实轮廓）
    p.setPen(QPen(Qt::black, 2));
    drawBorder(p, drawR);

    // 3) 探测器点
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

    // 4) 色条
    drawColorbar(p);

    // 5) 经纬网格（最后画 = 最上层）
    drawGraticule(p, drawR);
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
