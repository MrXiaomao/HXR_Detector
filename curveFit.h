/*
 * @Author: MaoXiaoqing
 * @Date: 2025-04-22 17:20:42
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2026-01-25 21:16:36
 * @Description: 请填写简介
 */

#include "ap.h"
#include "interpolation.h"
#include <QVector>
#include <QPointF>

#pragma once

namespace CurveFit {
    const int gauss_arr_count_min = 10; //高斯拟合数据点数最小值
    constexpr double kDefaultSpectrumFitR2Min = 0.8; // 能谱拟合优度 R² 默认下限
    /** 寻峰 fit_gauss_linear2 不做 R² 门槛时使用 */
    constexpr double kNoSpectrumFitR2Check = -1.0;

    /** 非线性最小二乘参数协方差（行主序 nParam×nParam） */
    struct LsFitCov {
        int nParam = 0;
        double resnorm = 0.0;
        double rmsError = 0.0;
        QVector<double> cov;
        bool valid = false;

        double at(int row, int col) const
        {
            if (!valid || row < 0 || col < 0 || row >= nParam || col >= nParam)
                return 0.0;
            const int idx = row * nParam + col;
            if (idx < 0 || idx >= cov.size())
                return 0.0;
            return cov.at(idx);
        }
    };

    /**
     * @brief 定义线性函数 f=kx+b
     * @param c 拟合参数c
     * @param x 自变量x
     * @param func 函数值
     * @param ptr 用于自定义传参
     */
    void function_linear(const alglib::real_1d_array &c, const alglib::real_1d_array &x, double &func, void *ptr);

    /**
     * @brief 定义待拟合函数 (二阶)多项式 f=ax^2+bx+c
     * @param c 拟合参数c
     * @param x 自变量x
     * @param func 函数值
     * @param ptr 用于自定义传参
     */
    void function_poly_2terms(const alglib::real_1d_array &c, const alglib::real_1d_array &x, double &func, void *ptr);

    /**
     * @brief 定义待拟合函数 (一阶)高斯拟合 func = c[0]*exp(-0.5*pow((x[0]-c[1])/c[2],2));
     * @param c 拟合参数c
     * @param x 自变量x
     * @param func 函数值
     * @param ptr 用于自定义传参
     */    
    void function_gauss_1terms(const alglib::real_1d_array &c, const alglib::real_1d_array &x, double &func, void *ptr);

    /**
     * @brief 定义待拟合函数 高斯+线性拟合 func = c[0]*exp(-0.5*pow((x[0]-peak)/c[1],2)) + c[2]*x[0] + c[3];
     * @param c 拟合参数c
     * @param x 自变量x
     * @param func 函数值
     * @param _peak 高斯拟合的中心
     */
    void function_gauss_linear(const alglib::real_1d_array &c, const alglib::real_1d_array &x, double &func, void *_peak);

    /**
     * @brief 定义待拟合函数 高斯+线性拟合 func = c[0]*exp(-0.5*pow((x[0]-c[1])/c[2],2)) + c[3]*x[0] + c[4];
     * @param c 拟合参数c
     * @param x 自变量x
     * @param func 函数值
     * @param ptr 用于自定义传参
     */
    void function_gauss_linear2(const alglib::real_1d_array &c, const alglib::real_1d_array &x, double &func, void *ptr);

    /**
     * @brief 定义待拟合函数 单高斯+4阶多项式拟合
     * @param c 拟合参数c
     * @param x 自变量x
     * @param func 函数值
     * @param ptr 用于自定义传参
     */
    void function_gauss_poly4(const alglib::real_1d_array &c, const alglib::real_1d_array &x, double &func, void *ptr);

    /**
     * @brief 定义待拟合函数 双高斯+4阶多项式拟合
     * @param c 拟合参数c
     * @param x 自变量x
     * @param func 函数值
     * @param ptr 用于自定义传参
     */
    void function_2gauss_poly4(const alglib::real_1d_array &c, const alglib::real_1d_array &x, double &func, void *ptr);

    /**
     * @brief 定义待拟合函数 对数拟合
     * @param c 拟合参数c
     * @param t 自变量，时间，单位min
     * @param func 函数值
     * @param ptr 用于自定义传参
     */
    void function_log(const alglib::real_1d_array &c, const alglib::real_1d_array &t, double &func, void *ptr);

    /** @brief a*exp(-ln2/(78.4*60)*t)，t 单位 min */
    void function_exp_decay(const alglib::real_1d_array &c, const alglib::real_1d_array &t, double &func, void *ptr);

    /**
     * @brief 线性拟合
     * func = c[0]*x + c[1];
     * @param fit_x 待拟合一维数组x
     * @param fit_y 待拟合一维数组y
     * @param fit_c 拟合参数一维数组c的初值 拟合成功后，会将拟合结果存放在fit_c中
     * @param R2    拟合决定系数 R²
     * @param rep   拟合报告
     * @return 拟合是否成功
     */
    bool fit_linear(QVector<QPointF> points, double* fit_c, double* R2, alglib::lsfitreport* rep = nullptr);

    /**
     * @brief 二次函数拟合
     * func = c[0]*x^2 + c[1]*x + c[2];
     * @param fit_x 待拟合一维数组x
     * @param fit_y 待拟合一维数组y
     * @param fit_c 拟合参数一维数组c的初值 拟合成功后，会将拟合结果存放在fit_c中
     * @param rep   拟合报告
     * @return 拟合是否成功
     */    
    bool fit_poly_2terms(QVector<QPointF> points, double* fit_c, double* R2, alglib::lsfitreport* rep = nullptr);
    
    /**
     * @brief 高斯函数（单高斯）拟合
     * func = A*exp(-0.5*pow((x[0]-mean)/sigma,2))
     * @param fit_x 待拟合一维数组x
     * @param fit_y 待拟合一维数组y
     * @param fit_c 拟合参数一维数组c的初值 拟合成功后，会将拟合结果存放在fit_c中
     * @param rep   拟合报告
     * @return 拟合是否成功
     */     
    bool fit_gauss_1terms(QVector<QPointF> points, double* fit_c, double* R2, alglib::lsfitreport* rep = nullptr);

    /**
     * @brief 候选峰拟合，假定peak为高斯部分的峰位，以此为条件进行拟合
     * func = c[0]*exp(-0.5*pow((x-peak)/c[1],2)) + c[2]*x + c[3];
     * @param points 待拟合数据对
     * @param fit_c 待拟合参数一维数组c的初值 拟合成功后，会将拟合结果存放在fit_c中
     * @param peak 高斯峰位
     * @param chi_square 拟合方差
     * @return 拟合是否成功
     */
    bool fit_gauss_linear(QVector<QPointF> points, double* fit_c, double peak, double* chi_square);

    /**
     * @brief 高斯+线性拟合 func = c[0]*exp(-0.5*pow((x[0]-c[1])/c[2],2)) + c[3]*x[0] + c[4];
     * @param points 待拟合数据对
     * @param fit_c 待拟合参数一维数组c的初值 拟合成功后，会将拟合结果存放在fit_c中
     * @return 拟合是否成功
     */
    bool fit_gauss_linear2(QVector<QPointF> points, double* fit_c,
                           double r2Min = kDefaultSpectrumFitR2Min);

    /**
     * @brief 高斯+4阶多项式拟合
     * func = c0*exp(-0.5*pow((x[0]-c1)/c2, 2)) + c3*pow(x[0], 4) + c4*pow(x[0], 3) + c5*pow(x[0], 2) + c6*x[0] + c7;
     * @param points 待拟合数据对
     * @param fit_c 待拟合参数一维数组c的初值 拟合成功后，会将拟合结果存放在fit_c中
     * @param residual_rate 每个数据点的拟合相对残差
     * @return 拟合是否成功
     */
    bool fit_gauss_ploy4(QVector<QPointF> points, double* fit_c, QVector<double> &residual_rate,
                         double r2Min = kDefaultSpectrumFitR2Min, double *r2Out = nullptr,
                         LsFitCov *covOut = nullptr);

    /**
     * @brief 双高斯+4阶多项式拟合
     * fit_type2 = @(p, x) p(1).*exp(-1/2*((x-p(2))./p(3)).^2) + p(4).*exp(-1/2*((x-p(5))./p(6)).^2)
     *             + p(7).*x.^4 + p(8).*x.^3 + p(9).*x.^2 + p(10).*x + p(11);
     * @param points 待拟合数据对
     * @param fit_c 待拟合参数一维数组c的初值 拟合成功后，会将拟合结果存放在fit_c中
     * @param residual_rate 每个数据点的拟合相对残差
     * @return 拟合是否成功
     */
    bool fit_2gauss_ploy4(QVector<QPointF> points, double* fit_c, QVector<double> &residual_rate,
                          double r2Min = kDefaultSpectrumFitR2Min, double *r2Out = nullptr,
                          LsFitCov *covOut = nullptr);

    /**
     * @brief 对909全能峰计数随时间变化曲线进行拟合
     * func = c0 - log(2)/(78.4*60)*t;
     * @param points 待拟合数据对
     * @param fit_c 待拟合参数一维数组c的初值 拟合成功后，会将拟合结果存放在fit_c中
     * @param residual_rate 每个数据点的拟合相对残差
     * @return 拟合是否成功
     */
    bool fit_log(QVector<QPointF> points, double* fit_c, QVector<double> &residual_rate);

    /**
     * @brief 909 全能峰计数随时间指数衰减拟合 func = a*exp(-ln2/(78.4*60)*t)
     * @param r2Out 可选，输出拟合优度 R²
     * @param weights 可选，逆方差权重 w_i=1/σ_i²；为空则等权
     * @param rmseOut 可选，输出加权/等权 RMSE（供 σ_a 计算）
     */
    bool fit_exp_decay(QVector<QPointF> points, double *fit_a, QVector<double> &residual_rate,
                       double *r2Out = nullptr,
                       const QVector<double> *weights = nullptr,
                       double *rmseOut = nullptr);
}

