#include "sysutils.h"

#include <stdio.h>

#include <vector>
#include <stdexcept>
#include <cstddef>
#include <cmath>
#include <algorithm>
#include <Eigen/Dense>

SysUtils::SysUtils() {}

// 滑动平均滤波函数
void SysUtils::smooth(double *data, double *output, int data_size, int window_size) {
    // 如果窗口大小大于数据长度，则不进行滤波
    if (window_size > data_size) {
        printf("窗口大小不能大于数据大小。\n");
        return;
    }

    // 窗口宽度必须是奇数
    if ( window_size%2 == 0) {
        printf("smooth,窗口宽度只能是奇数，当前窗口宽度%d。\n", window_size);
        return;
    }

    int halfWindow = (window_size - 1) / 2;

    // 计算每个点的滑动平均值
    for (int i = 0; i < data_size; i++) {
        double sum = 0.0;
        int count = 0;
        // 计算窗口内的平均值
        for (int j = i - (window_size - 1) / 2; j <= i + (window_size - 1) / 2; j++) {
            // 确保索引在有效范围内
            if (j >= 0 && j < data_size) {
                sum += data[j];
                count++;
            }
        }
        // 计算并存储当前位置的滑动平均值
        output[i] = sum / count;
    }

    // 重新处理左边界情况
    for (int i = 0; i < halfWindow; i++) {
        double sum = 0.0;
        int count = 0;
        //给出左侧端点个数
        int leftPoint = i;
        for (int j = 0; j <= i + i; j++) {
            // printf("Left side, i = %d, data[%d]=%f\n", i, j, data[j]);
            sum += data[j];
            count++;
        }
        output[i] = sum / count;
    }

    // 重新处理右边界情况
    for (int i = data_size - halfWindow; i < data_size; i++) {
        double sum = 0.0;
        int count = 0;
        //给出右侧端点个数
        int leftPoint = data_size - i- 1;
        for (int j = i - leftPoint; j < data_size; j++) {
            // printf("Right side, i = %d, data[%d]=%f\n", i, j, data[j]);
            sum += data[j];
            count++;
        }
        output[i] = sum / count;
    }
}


// MATLAB-like sgolayfilt for 1D data, smoothing only (derivative order d = 0)
std::vector<double> SysUtils::sgolayfilt_matlab_like(
    const std::vector<double>& x, int p, int f)
{
    if (f <= 0 || (f % 2) == 0) {
        throw std::invalid_argument("frame_length f must be a positive odd integer.");
    }
    if (p < 0) {
        throw std::invalid_argument("polynomial order p must be >= 0.");
    }
    if (p >= f) {
        throw std::invalid_argument("polynomial order p must be < frame_length f.");
    }
    const std::size_t N = x.size();
    if (N < static_cast<std::size_t>(f)) {
        // MATLAB 也要求 frame_length <= length(x)（不足会报错）
        throw std::invalid_argument("Input length must be >= frame_length f.");
    }

    const int m = (f - 1) / 2; // half window

    // 1) Build Vandermonde matrix A (f x (p+1)) with sample positions k = -m..m
    // A(r, j) = k^j
    Eigen::MatrixXd A(f, p + 1);
    for (int r = 0; r < f; ++r) {
        const double k = static_cast<double>(r - m);
        double kj = 1.0;
        for (int j = 0; j <= p; ++j) {
            A(r, j) = kj;
            kj *= k;
        }
    }

    // 2) QR decomposition to obtain an orthonormal basis Q (f x (p+1))
    // Projection matrix P = Q * Q^T (f x f)
    Eigen::HouseholderQR<Eigen::MatrixXd> qr(A);
    Eigen::MatrixXd Q = qr.householderQ() * Eigen::MatrixXd::Identity(f, p + 1);
    Eigen::MatrixXd P = Q * Q.transpose(); // smoothing (d=0) => fitted values = P * y_window

    // 3) Apply with MATLAB-like boundary handling:
    //    - left edge: window is x[0..f-1], row = i
    //    - middle:    window centered at i, start = i-m, row = m
    //    - right edge:window is x[N-f..N-1], row = i - (N-f)
    std::vector<double> y(N);

    for (std::size_t i = 0; i < N; ++i) {
        int start = 0;
        int row = 0;

        if (static_cast<int>(i) < m) {
            start = 0;
            row = static_cast<int>(i);
        } else if (static_cast<int>(i) > static_cast<int>(N) - 1 - m) {
            start = static_cast<int>(N) - f;
            row = static_cast<int>(i) - start; // in [f-m .. f-1]
        } else {
            start = static_cast<int>(i) - m;
            row = m;
        }

        // y[i] = sum_k P(row,k) * x[start+k]
        double acc = 0.0;
        for (int k = 0; k < f; ++k) {
            acc += P(row, k) * x[static_cast<std::size_t>(start + k)];
        }
        y[i] = acc;
    }

    return y;
}

std::vector<double> SysUtils::lowpass(const std::vector<double> &x, double normalizedCutoff)
{
    if (x.empty())
        return x;
    normalizedCutoff = std::max(0.01, std::min(normalizedCutoff, 0.99));
    int window = static_cast<int>(std::round(1.0 / normalizedCutoff));
    window = std::max(3, window);
    if (window % 2 == 0)
        ++window;

    std::vector<double> output(x.size(), 0.0);
    const int half = window / 2;
    for (int i = 0; i < static_cast<int>(x.size()); ++i) {
        int start = std::max(0, i - half);
        int end = std::min(static_cast<int>(x.size()) - 1, i + half);
        double sum = 0.0;
        for (int j = start; j <= end; ++j)
            sum += x[static_cast<std::size_t>(j)];
        output[static_cast<std::size_t>(i)] = sum / static_cast<double>(end - start + 1);
    }
    return output;
}
