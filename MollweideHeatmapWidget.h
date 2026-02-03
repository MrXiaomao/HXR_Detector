/**
 * 中子产额绘制类
 * 用途：绘制物理场空间分布，将球体表面的分布展开为二维图像，并采用色彩来实现物理场的数值强弱
 * 使用说明：setSensors进行数据填充，数据填充后需要rebuild。
 */

#ifndef MOLLWEIDEHEATMAP_H
#define MOLLWEIDEHEATMAP_H

#include <QWidget>
#include <QImage>
#include <vector>
#include "globalsettings.h"

// struct Sensor {
//     double lonDeg;  // 经度[-180,180]
//     double latDeg;  // 纬度[-90, 90]
//     double value;   // 温度
// };

class MollweideHeatmapWidget : public QWidget {
    Q_OBJECT
public:
    enum class InterpMode { IDW, SphericalHarmonics };

    explicit MollweideHeatmapWidget(QWidget* parent=nullptr);

    void setSensors(const std::vector<Sensor>& s);
    void rebuild();                 // 生成热力图缓存
    void setPower(double p);        // IDW 参数，可选

    void setInterpMode(InterpMode m);
    void setSHOrder(int L);          // 建议 3 或 4
    void setSHLambda(double lambda); // 岭回归系数，建议 1e-6 ~ 1e-3

protected:
    void paintEvent(QPaintEvent*) override;

private:
    std::vector<Sensor> sensors_;
    QImage heat_;
    double idwPower_ = 2.0;

    // 内部工具函数
    static double deg2rad(double d);
    static double greatCircleAngle(double lon1, double lat1, double lon2, double lat2);
    static bool inverseMollweide(double x, double y, double &lon, double &lat);
    static bool forwardMollweide(double lon, double lat, double &x, double &y);
    static QColor colormap(double t01);

    QImage renderHeatmapMollweide(const std::vector<Sensor>& sensors, int W, int H, double p);

    double vmin_ = 0.0;
    double vmax_ = 1.0;

    QPointF projToPixel(double x, double y) const; // Mollweide平面(x,y)->控件像素
    void drawGraticule(QPainter& p) const;         // 经纬网格
    void drawEllipseBorder(QPainter& p) const;     // 椭圆边界

    QRectF mapRect() const;                    // 上方地图区域（给椭圆用）
    QPointF projToPixelInRect(double x, double y, const QRectF& r) const;

    void drawColorbar(QPainter& p) const;      // 底部水平色条
    void drawGraticule(QPainter& p, const QRectF& r) const;
    void drawEllipseBorder(QPainter& p, const QRectF& r) const;

    InterpMode mode_ = InterpMode::IDW;

    // --- Spherical Harmonics ---
    int shOrder_ = 4;           // L
    double shLambda_ = 1e-4;    // 岭回归
    bool shReady_ = false;

    std::vector<double> shCoeff_; // 系数向量，长度 (L+1)^2

    void fitSphericalHarmonics();                 // sensors_ 变化时调用一次
    double evalSphericalHarmonics(double lon, double lat) const; // 每像素调用

    static int shBasisCount(int L) { return (L+1)*(L+1); }
    static int shIndex(int l, int m); // 映射(l,m)->k(实球谐基)

    // 计算实球谐基向量 yk，返回 size=(L+1)^2
    static void shBasisReal(int L, double lon, double lat, std::vector<double>& out);

    // 线性代数：解 (A^T A + λI)c = A^T b
    static bool solveSymmetricSystem(std::vector<double>& M, std::vector<double>& rhs, int n);

    static double assocLegendreP(int l, int m, double x);

    static double factorialRatio(int a, int b);

    static double normNlm(int l, int m);
};

#endif // MOLLWEIDEHEATMAP_H
