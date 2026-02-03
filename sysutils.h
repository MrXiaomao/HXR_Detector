#ifndef SYSUTILS_H
#define SYSUTILS_H
#include <vector>
class SysUtils
{
public:
    SysUtils();

    /**
     * @brief 滑动平均滤波函数
     * @param data 待滤波数据
     * @param output 滤波输出数据
     * @param data_size 数据点个数
     * @param window_size 窗宽（必须是奇数）
     */
    static void smooth(double *data, double *output, int data_size, int window_size);

    /**
     * @brief Savitzky-Golay滤波器
     * @param x 待滤波数据
     * @param p 多项式阶数
     * @param f 窗宽（奇数）
     * @return 滤波输出数据
     */
    static std::vector<double> sgolayfilt_matlab_like(const std::vector<double>& x,int p,int f);
};

#endif // SYSUTILS_H
