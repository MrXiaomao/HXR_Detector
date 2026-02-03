//https://www.sohu.com/a/898129854_121124019
#include "MollweideHeatmapWidget.h"
#include <QPainter>
#include <cmath>
#include <algorithm>
#include <QPainterPath>

MollweideHeatmapWidget::MollweideHeatmapWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumSize(400, 220);
}

void MollweideHeatmapWidget::setSensors(const std::vector<Sensor>& s) {
    sensors_ = s;
    shReady_ = false;
}

void MollweideHeatmapWidget::setPower(double p) {
    idwPower_ = p;
}

double MollweideHeatmapWidget::deg2rad(double d){ return d * M_PI / 180.0; }

QColor MollweideHeatmapWidget::colormap(double t01) {
    t01 = std::clamp(t01, 0.0, 1.0);
    int r = int(255 * t01);
    int b = int(255 * (1.0 - t01));
    int g = int(255 * (0.5 - std::abs(t01 - 0.5)) * 2.0);
    return QColor(r, g, b);
}

double MollweideHeatmapWidget::greatCircleAngle(double lon1, double lat1, double lon2, double lat2) {
    double dlon = lon2 - lon1;
    double dlat = lat2 - lat1;
    double a = std::sin(dlat/2)*std::sin(dlat/2)
               + std::cos(lat1)*std::cos(lat2)*std::sin(dlon/2)*std::sin(dlon/2);
    a = std::clamp(a, 0.0, 1.0);
    return 2.0 * std::asin(std::sqrt(a));
}

bool MollweideHeatmapWidget::inverseMollweide(double x, double y, double &lon, double &lat) {
    const double SQRT2 = std::sqrt(2.0);
    if (std::abs(y) > SQRT2) return false;

    double s = y / SQRT2;
    s = std::clamp(s, -1.0, 1.0);
    double theta = std::asin(s);

    double c = std::cos(theta);
    if (std::abs(c) < 1e-12) return false;

    lon = (M_PI * x) / (2.0 * SQRT2 * c);

    double sinLat = (2.0*theta + std::sin(2.0*theta)) / M_PI;
    sinLat = std::clamp(sinLat, -1.0, 1.0);
    lat = std::asin(sinLat);

    lon = std::clamp(lon, -M_PI, M_PI);
    return true;
}

bool MollweideHeatmapWidget::forwardMollweide(double lon, double lat, double &x, double &y) {
    const double SQRT2 = std::sqrt(2.0);
    double target = M_PI * std::sin(lat);
    double theta = lat;

    for (int i=0; i<12; ++i) {
        double f  = 2.0*theta + std::sin(2.0*theta) - target;
        double fp = 2.0 + 2.0*std::cos(2.0*theta);
        if (std::abs(fp) < 1e-12) break;
        double d = f / fp;
        theta -= d;
        if (std::abs(d) < 1e-12) break;
    }

    x = (2.0 * SQRT2 / M_PI) * lon * std::cos(theta);
    y = SQRT2 * std::sin(theta);
    return true;
}

QImage MollweideHeatmapWidget::renderHeatmapMollweide(const std::vector<Sensor>& sensors, int W, int H, double p) {
    QImage img(W, H, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    double vmin = 1e100, vmax = -1e100;
    struct S2 { double lon, lat, val; };
    std::vector<S2> s2; s2.reserve(sensors.size());
    for (auto &s: sensors) {
        vmin = std::min(vmin, s.value);
        vmax = std::max(vmax, s.value);
        s2.push_back({deg2rad(s.lonDeg), deg2rad(s.latDeg), s.value});
    }
    double denomV = std::max(1e-12, vmax - vmin);

    const double SQRT2 = std::sqrt(2.0);
    const double XMAX = 2.0 * SQRT2;
    const double YMAX = SQRT2;
    const double eps = 1e-6;

    for (int py=0; py<H; ++py) {
        QRgb* scan = reinterpret_cast<QRgb*>(img.scanLine(py));
        double yn = 1.0 - 2.0 * (double(py) / (H - 1));
        double y = yn * YMAX;

        for (int px=0; px<W; ++px) {
            double xn = 2.0 * (double(px) / (W - 1)) - 1.0;
            double x = xn * XMAX;

            double e = (x/XMAX)*(x/XMAX) + (y/YMAX)*(y/YMAX);
            if (e > 1.0) { scan[px] = qRgba(0,0,0,0); continue; }

            double lon, lat;
            if (!inverseMollweide(x, y, lon, lat)) { scan[px] = qRgba(0,0,0,0); continue; }

            double wsum = 0.0, vsum = 0.0;
            double v = 0.0;
            if (mode_ == InterpMode::SphericalHarmonics && shReady_) {
                v = evalSphericalHarmonics(lon, lat);
            } else {
                double wsum = 0.0, vsum = 0.0;
                for (auto &s: s2) {
                    double ang = greatCircleAngle(lon, lat, s.lon, s.lat);
                    double w = 1.0 / (std::pow(ang, p) + eps);
                    wsum += w;
                    vsum += w * s.val;
                }
                v = vsum / std::max(1e-12, wsum);
            }

            double t01 = (v - vmin) / denomV;
            QColor c = colormap(t01);
            scan[px] = qRgba(c.red(), c.green(), c.blue(), 255);
        }
    }
    return img;
}

void MollweideHeatmapWidget::rebuild() {
    if (sensors_.empty()) {
        heat_ = QImage();
        update();
        return;
    }

    vmin_ = 1e100;
    vmax_ = -1e100;
    for (auto &s: sensors_) {
        vmin_ = std::min(vmin_, s.value);
        vmax_ = std::max(vmax_, s.value);
    }
    if (std::abs(vmax_ - vmin_) < 1e-12) {
        vmax_ = vmin_ + 1.0; // 避免全相等时除0
    }

    if (mode_ == InterpMode::SphericalHarmonics) {
        fitSphericalHarmonics();
    }

    heat_ = renderHeatmapMollweide(sensors_, 1200, 650, idwPower_);
    update();
}

void MollweideHeatmapWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QRectF r = mapRect();

    // 1) 热力图绘制到地图区域 r
    if (!heat_.isNull())
        p.drawImage(r, heat_);


    // 2) 椭圆边框
    p.setPen(QPen(Qt::black, 2));
    drawEllipseBorder(p, r);

    // 3) 探测器点（也用 r 映射）
    p.setPen(QPen(Qt::black, 2));
    p.setBrush(Qt::white);
    for (auto &s: sensors_) {
        double lon = deg2rad(s.lonDeg);
        double lat = deg2rad(s.latDeg);
        double x, y;
        forwardMollweide(lon, lat, x, y);
        QPointF pt = projToPixelInRect(x, y, r);
        p.drawEllipse(pt, 5, 5);
    }

    // 4) 底部水平色条
    drawColorbar(p);

    // 5) 经纬网格（clip到椭圆内）
    drawGraticule(p, r);
}

QPointF MollweideHeatmapWidget::projToPixel(double x, double y) const {
    const double SQRT2 = std::sqrt(2.0);
    const double XMAX = 2.0 * SQRT2;
    const double YMAX = SQRT2;

    double xn = x / XMAX; // [-1,1]
    double yn = y / YMAX; // [-1,1]

    double px = (xn + 1.0) * 0.5 * (width() - 1);
    double py = (1.0 - (yn + 1.0) * 0.5) * (height() - 1); // y翻转
    return QPointF(px, py);
}

void MollweideHeatmapWidget::drawEllipseBorder(QPainter& p, const QRectF& r) const {
    p.drawEllipse(r);
}

void MollweideHeatmapWidget::drawGraticule(QPainter& p, const QRectF& r) const {
    const int lonStep = 30;
    const int latStep = 30;
    const double dDeg = 2.0;

    QPen pen(QColor(0, 0, 0, 80));
    pen.setWidthF(1.0);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    // 可选：clip 到椭圆内，避免画到外面透明区域
    QPainterPath clip;
    clip.addEllipse(r);
    p.save();
    p.setClipPath(clip);

    // 纬线
    for (int latDeg = -60; latDeg <= 60; latDeg += latStep) {
        QPainterPath path; bool started=false;
        for (double lonDeg = -180.0; lonDeg <= 180.0; lonDeg += dDeg) {
            double lon = deg2rad(lonDeg), lat = deg2rad((double)latDeg);
            double x,y; forwardMollweide(lon, lat, x, y);
            QPointF pt = projToPixelInRect(x, y, r);
            if (!started){ path.moveTo(pt); started=true; } else path.lineTo(pt);
        }
        p.drawPath(path);
    }

    // 经线
    for (int lonDeg = -150; lonDeg <= 150; lonDeg += lonStep) {
        QPainterPath path; bool started=false;
        for (double latDeg = -89.0; latDeg <= 89.0; latDeg += dDeg) {
            double lon = deg2rad((double)lonDeg), lat = deg2rad(latDeg);
            double x,y; forwardMollweide(lon, lat, x, y);
            QPointF pt = projToPixelInRect(x, y, r);
            if (!started){ path.moveTo(pt); started=true; } else path.lineTo(pt);
        }
        p.drawPath(path);
    }

    p.restore();
}

void MollweideHeatmapWidget::drawColorbar(QPainter& p) const {
    const int margin = 10;
    const int barH = 26;
    const int textH = 18;
    const int gap = 6;

    int y0 = height() - margin - textH - gap - barH;
    QRect barRect(margin, y0, width() - 2*margin, barH);

    // 横向渐变：左 vmin -> 右 vmax
    QImage grad(barRect.width(), 1, QImage::Format_ARGB32);
    for (int x=0; x<barRect.width(); ++x) {
        double t = double(x) / std::max(1, barRect.width()-1); // 0..1
        QColor c = colormap(t);
        grad.setPixel(x, 0, qRgba(c.red(), c.green(), c.blue(), 255));
    }
    p.drawImage(barRect, grad.scaled(barRect.size()));

    // 边框
    p.setPen(QPen(Qt::black, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(barRect);

    // 文本：min / mid / max
    auto fmt = [](double v){ return QString::number(v, 'g', 4); };
    double mid = 0.5*(vmin_ + vmax_);

    QRect textRect(margin, barRect.bottom() + gap, width() - 2*margin, textH);
    p.setPen(Qt::black);

    // 三段对齐
    p.drawText(QRect(textRect.left(), textRect.top(), textRect.width()/3, textRect.height()),
               Qt::AlignLeft|Qt::AlignVCenter, fmt(vmin_));
    p.drawText(QRect(textRect.left()+textRect.width()/3, textRect.top(), textRect.width()/3, textRect.height()),
               Qt::AlignHCenter|Qt::AlignVCenter, fmt(mid));
    p.drawText(QRect(textRect.left()+2*textRect.width()/3, textRect.top(), textRect.width()/3, textRect.height()),
               Qt::AlignRight|Qt::AlignVCenter, fmt(vmax_));

    // 刻度线（min/mid/max）
    int yTick0 = barRect.top();
    int yTick1 = barRect.bottom();
    int xMin = barRect.left();
    int xMid = barRect.left() + barRect.width()/2;
    int xMax = barRect.right();

    p.drawLine(xMin, yTick1, xMin, yTick1+5);
    p.drawLine(xMid, yTick1, xMid, yTick1+5);
    p.drawLine(xMax, yTick1, xMax, yTick1+5);
}

QRectF MollweideHeatmapWidget::mapRect() const {
    // 底部留出色条高度
    const int margin = 10;
    const int barH = 26;     // 色条本体高度（不含文字）
    const int textH = 18;    // 文字高度
    const int gap = 6;

    int bottomReserve = margin + barH + gap + textH + margin;
    QRectF r(margin, margin, width() - 2*margin, height() - bottomReserve - margin);

    // 避免太小
    if (r.height() < 50) r.setHeight(50);
    if (r.width()  < 50) r.setWidth(50);
    return r;
}

QPointF MollweideHeatmapWidget::projToPixelInRect(double x, double y, const QRectF& r) const {
    const double SQRT2 = std::sqrt(2.0);
    const double XMAX = 2.0 * SQRT2; // [-XMAX, XMAX]
    const double YMAX = SQRT2;       // [-YMAX, YMAX]

    double xn = x / XMAX; // [-1,1]
    double yn = y / YMAX; // [-1,1]

    double px = r.left() + (xn + 1.0) * 0.5 * r.width();
    double py = r.top()  + (1.0 - (yn + 1.0) * 0.5) * r.height(); // y翻转
    return QPointF(px, py);
}

void MollweideHeatmapWidget::setInterpMode(InterpMode m) {
    mode_ = m;
    shReady_ = false; // 模式切换时让它重新拟合
}

void MollweideHeatmapWidget::setSHOrder(int L) {
    shOrder_ = std::max(0, L);
    shReady_ = false;
}

double MollweideHeatmapWidget::assocLegendreP(int l, int m, double x) {
    // m >= 0
    // 计算 P_l^m(x)
    double pmm = 1.0;
    if (m > 0) {
        double somx2 = std::sqrt(std::max(0.0, 1.0 - x*x));
        double fact = 1.0;
        for (int i = 1; i <= m; ++i) {
            pmm *= (-fact) * somx2; // Condon-Shortley: (-1)^m
            fact += 2.0;
        }
    }
    if (l == m) return pmm;

    double pmmp1 = x * (2.0*m + 1.0) * pmm;
    if (l == m + 1) return pmmp1;

    double pll = 0.0;
    double p1 = pmmp1;
    double p2 = pmm;
    for (int ll = m + 2; ll <= l; ++ll) {
        pll = ((2.0*ll - 1.0)*x*p1 - (ll + m - 1.0)*p2) / (ll - m);
        p2 = p1;
        p1 = pll;
    }
    return pll;
}

double MollweideHeatmapWidget::factorialRatio(int a, int b) {
    // a!/b!，假设 a<=b 或 a>=b 都可处理
    // 这里用于 (l-m)!/(l+m)!
    if (a == b) return 1.0;
    double r = 1.0;
    if (a < b) {
        for (int i = a + 1; i <= b; ++i) r /= double(i);
    } else {
        for (int i = b + 1; i <= a; ++i) r *= double(i);
    }
    return r;
}

double MollweideHeatmapWidget::normNlm(int l, int m) {
    // N_lm = sqrt((2l+1)/(4pi) * (l-m)!/(l+m)!)
    const double PI4 = 4.0 * M_PI;
    double a = (2.0*l + 1.0) / PI4;
    double fr = factorialRatio(l - m, l + m);
    return std::sqrt(a * fr);
}

void MollweideHeatmapWidget::setSHLambda(double lambda) {
    shLambda_ = std::max(0.0, lambda);
    shReady_ = false;
}

int MollweideHeatmapWidget::shIndex(int l, int m) {
    // k 从 0 开始
    // l=0: m=0 -> 0
    // l=1: m=-1,0,1 -> 1,2,3
    // l=2: m=-2..2 -> 4..8
    return l*l + (m + l);
}

void MollweideHeatmapWidget::shBasisReal(int L, double lon, double lat, std::vector<double>& out) {
    out.assign((L+1)*(L+1), 0.0);

    // theta = colatitude = pi/2 - lat
    double theta = M_PI_2 - lat;
    double x = std::cos(theta);  // = sin(lat)

    // 预计算 cos(m*lon), sin(m*lon)
    std::vector<double> cosml(L+1), sinml(L+1);
    cosml[0] = 1.0; sinml[0] = 0.0;
    for (int m=1; m<=L; ++m) {
        cosml[m] = std::cos(m * lon);
        sinml[m] = std::sin(m * lon);
    }

    // m=0
    for (int l=0; l<=L; ++l) {
        double Pl0 = assocLegendreP(l, 0, x);
        double Y = normNlm(l,0) * Pl0;
        out[shIndex(l,0)] = Y;
    }

    // m>0：用 cos/sin 形成实球谐
    for (int l=1; l<=L; ++l) {
        for (int m=1; m<=l; ++m) {
            double Plm = assocLegendreP(l, m, x);
            double N = normNlm(l,m);
            double K = std::sqrt(2.0) * N * Plm;
            out[shIndex(l, +m)] = K * cosml[m]; // cos项
            out[shIndex(l, -m)] = K * sinml[m]; // sin项
        }
    }
}

void MollweideHeatmapWidget::fitSphericalHarmonics() {
    int L = shOrder_;
    int K = shBasisCount(L);
    if ((int)sensors_.size() < K) {
        // 点太少不够解（仍可解岭回归，但会很“凭空”）
        // 这里仍允许，只是提示性地降低阶数也行：L=3
        // 你也可以改成自动降阶
    }

    std::vector<double> M(K*K, 0.0);
    std::vector<double> rhs(K, 0.0);

    std::vector<double> y; y.reserve(K);

    for (auto &s : sensors_) {
        double lon = deg2rad(s.lonDeg);
        double lat = deg2rad(s.latDeg);

        shBasisReal(L, lon, lat, y);

        // 累积 A^T A 与 A^T b
        for (int i=0; i<K; ++i) {
            rhs[i] += y[i] * s.value;
            for (int j=0; j<K; ++j) {
                M[i*K + j] += y[i] * y[j];
            }
        }
    }

    // 岭回归：+ lambda I
    for (int i=0; i<K; ++i) {
        M[i*K + i] += shLambda_;
    }

    // 解线性方程 M * c = rhs
    bool ok = solveSymmetricSystem(M, rhs, K);
    if (!ok) {
        shReady_ = false;
        return;
    }

    shCoeff_ = rhs; // rhs 被解向量覆盖
    shReady_ = true;
}

double MollweideHeatmapWidget::evalSphericalHarmonics(double lon, double lat) const {
    int L = shOrder_;
    int K = shBasisCount(L);
    if (!shReady_ || (int)shCoeff_.size() != K) return 0.0;

    std::vector<double> y;
    shBasisReal(L, lon, lat, y);

    double v = 0.0;
    for (int k=0; k<K; ++k) v += shCoeff_[k] * y[k];
    return v;
}

bool MollweideHeatmapWidget::solveSymmetricSystem(std::vector<double>& M,
                                                  std::vector<double>& rhs,
                                                  int n) {
    // 高斯消元（带部分主元）
    for (int i=0; i<n; ++i) {
        // 选主元
        int piv = i;
        double best = std::abs(M[i*n + i]);
        for (int r=i+1; r<n; ++r) {
            double v = std::abs(M[r*n + i]);
            if (v > best) { best = v; piv = r; }
        }
        if (best < 1e-14) return false;

        // 交换行
        if (piv != i) {
            for (int c=i; c<n; ++c) std::swap(M[i*n + c], M[piv*n + c]);
            std::swap(rhs[i], rhs[piv]);
        }

        double diag = M[i*n + i];
        // 归一化
        for (int c=i; c<n; ++c) M[i*n + c] /= diag;
        rhs[i] /= diag;

        // 消元
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
