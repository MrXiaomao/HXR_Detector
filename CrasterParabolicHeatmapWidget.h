#ifndef CRASTERPARABOLICHEATMAPWIDGET_H
#define CRASTERPARABOLICHEATMAPWIDGET_H

#include <QWidget>
#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <vector>
#include <cmath>
#include "spectypes.h"

class QLabel;
class QSlider;
class QToolButton;
class QFrame;
class QMenu;
class QSpinBox;
class QAction;
class QActionGroup;

class CrasterParabolicHeatmapWidget : public QWidget {
    Q_OBJECT
public:
    enum class InterpMode { IDW, SphericalHarmonics };

    explicit CrasterParabolicHeatmapWidget(QWidget* parent=nullptr);

    void setSensors(const std::vector<Sensor>& s);
    void rebuild();

    /** 将当前热力图控件内容导出为 PNG */
    bool exportToPng(const QString &filePath) const;

    // IDW（“平方插值”默认 power=2）
    void setPower(double p);

    // 模式切换
    void setInterpMode(InterpMode m);
    void setSHOrder(int L);           // 建议 3 或 4
    void setSHLambda(double lambda);  // 建议 1e-6 ~ 1e-3

    /** 复位视图（100% 缩放、无平移） */
    void resetView();

    /** 当前缩放倍率（1.0 = 100%） */
    double viewScale() const { return viewScale_; }

private slots:
    void onCustomContextMenuRequested(const QPoint &pos);
    void onZoomSliderChanged(int percent);
    void onZoomInClicked();
    void onZoomOutClicked();
    void onZoomPresetTriggered(QAction *action);
    void onZoomSpinEditingFinished();
    void onZoomMenuAboutToShow();

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    std::vector<Sensor> sensors_;

    // 热力图缓存（只缓存 mapRect 的像素区域，避免色条区被覆盖）
    QImage heatMap_;      // 尺寸 = mapRect 的像素大小
    QRect  heatMapRect_;  // heatMap_ 对应的控件像素矩形（mapRect().toAlignedRect()）

    // ----- Interp -----
    double idwPower_ = 2.0;
    InterpMode mode_ = InterpMode::IDW;

    // ----- Value range for colorbar -----
    double vmin_ = 0.0;
    double vmax_ = 1.0;

    // ----- View transform (zoom / pan) -----
    double viewScale_ = 1.0;          // 1.0 = 100%
    QPointF viewOffset_;              // 平移（像素）
    bool panning_ = false;
    QPoint lastPanPos_;
    static constexpr double kMinScale_ = 0.10;  // 10%
    static constexpr double kMaxScale_ = 5.00;  // 500%

    // Word 风格缩放条
    QFrame *zoomBar_ = nullptr;
    QToolButton *btnZoomPercent_ = nullptr;
    QToolButton *btnZoomOut_ = nullptr;
    QToolButton *btnZoomIn_ = nullptr;
    QSlider *zoomSlider_ = nullptr;
    QMenu *zoomMenu_ = nullptr;
    QSpinBox *zoomSpin_ = nullptr;
    QActionGroup *zoomPresetGroup_ = nullptr;
    bool syncingZoomUi_ = false;

    void setupZoomBar();
    void layoutZoomBar();
    void syncZoomUi();
    /** 以锚点（控件坐标）为中心设置缩放 */
    void setViewScaleAt(double newScale, const QPointF& anchorWidgetPos);
    QRectF viewDrawRect(const QRectF& baseDrawR) const;

    // ----- Projection: PROJ crast constants -----
    static constexpr double XM_  = 0.97720502380583984317;
    static constexpr double RXM_ = 1.02332670794648848847;   // 1/XM
    static constexpr double YM_  = 3.06998012383946546542;
    static constexpr double RYM_ = 0.32573500793527994772;   // 1/YM

    static constexpr double XMAX_ = XM_ * M_PI; // |x|max at equator lon=±pi
    static constexpr double YMAX_ = YM_;        // |y|max

    //图像尺寸
    static constexpr int HEAT_W = 1200;
    static constexpr int HEAT_H = 1000;

    // 底部色条 + 缩放条预留高度
    static constexpr int kZoomBarH_ = 28;

    // ----- math helpers -----
    static double deg2rad(double d);
    static double greatCircleAngle(double lon1, double lat1, double lon2, double lat2);
    static QColor colormap(double t01);

    // projection
    static bool forwardCraster(double lon, double lat, double &x, double &y);
    static bool inverseCraster(double x, double y, double &lon, double &lat);

    // field & color
    double evaluateField(double lon, double lat) const; // 球谐 or IDW
    QColor colorFromValue(double v) const;

    // render heatmap (ONLY for mapRect pixel area)
    QImage renderHeatmapCraster(const QSize& sz) const;

    // layout & drawing helpers
    QRectF mapRect() const;
    QPointF projToPixelInRect(double x, double y, const QRectF& r) const;
    void pixelToProjInRect(const QPointF& pt, const QRectF& r, double& x, double& y) const;

    // boundary path (true outline, not ellipse)
    QPainterPath crasterOutlinePath(const QRectF& r) const;

    // drawing
    void drawBorder(QPainter& p, const QRectF& r) const;
    void drawGraticule(QPainter& p, const QRectF& r) const;
    void drawColorbar(QPainter& p) const;

    // ----- Spherical Harmonics -----
    int shOrder_ = 4;
    double shLambda_ = 1e-4;
    bool shReady_ = false;
    std::vector<double> shCoeff_;

    void fitSphericalHarmonics();
    double evalSphericalHarmonics(double lon, double lat) const;

    static int shBasisCount(int L) { return (L+1)*(L+1); }
    static int shIndex(int l, int m);
    static void shBasisReal(int L, double lon, double lat, std::vector<double>& out);

    static bool solveSymmetricSystem(std::vector<double>& M, std::vector<double>& rhs, int n);
    static double assocLegendreP(int l, int m, double x);
    static double factorialRatio(int a, int b);
    static double normNlm(int l, int m);

    QRectF heatDrawRect(const QRectF& mapR) const;
};

#endif // CRASTERPARABOLICHEATMAPWIDGET_H
