/*
 * @Author: MaoXiaoqing
 * @Date: 2025-04-22 17:20:42
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2026-01-25 21:16:36
 * @Description: 能量刻度所用的线性与二次拟合
 */

#include "ap.h"
#include "interpolation.h"
#include <QVector>
#include <QPointF>

#pragma once

namespace CurveFit {

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
     * @brief 线性拟合
     * func = c[0]*x + c[1];
     * @param fit_c 拟合参数一维数组c的初值 拟合成功后，会将拟合结果存放在fit_c中
     * @param R2    拟合决定系数 R²
     * @param rep   拟合报告
     * @return 拟合是否成功
     */
    bool fit_linear(QVector<QPointF> points, double* fit_c, double* R2, alglib::lsfitreport* rep = nullptr);

    /**
     * @brief 二次函数拟合
     * func = c[0]*x^2 + c[1]*x + c[2];
     * @param fit_c 拟合参数一维数组c的初值 拟合成功后，会将拟合结果存放在fit_c中
     * @param rep   拟合报告
     * @return 拟合是否成功
     */
    bool fit_poly_2terms(QVector<QPointF> points, double* fit_c, double* R2, alglib::lsfitreport* rep = nullptr);
}
