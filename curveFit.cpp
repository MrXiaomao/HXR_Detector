/*
 * @Author: MaoXiaoqing
 * @Date: 2025-04-22 17:21:41
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2026-10-15 20:01:08
 * @Description: 借助ALGLIB库进行能量刻度线性、二次拟合
 */

#include "curveFit.h"
#include <QDebug>

using namespace alglib;
namespace CurveFit {

    void function_linear(const real_1d_array &c, const real_1d_array &x, double &func, void *ptr)
    {
        Q_UNUSED(ptr);
        func = c[0]*x[0] + c[1];
    }

    void function_poly_2terms(const real_1d_array &c, const real_1d_array &x, double &func, void *ptr)
    {
        Q_UNUSED(ptr);
        func = c[0]*x[0]*x[0] + c[1]*x[0] + c[2];
    }

    bool fit_linear(QVector<QPointF> points, double* fit_c, double* R2, lsfitreport* rep)
    {
        int paraNum = 2; //待拟合参数个数
        int num = points.size();
        try
        {
            QVector<double> fit_x, fit_y;
            for(int i=0; i<num; i++)
            {
                fit_x.push_back(points.at(i).x());
                fit_y.push_back(points.at(i).y());
            }

            alglib::real_2d_array x;
            x.setcontent(num, 1, fit_x.constData());

            alglib::real_1d_array y;
            y.setcontent(fit_y.size(), fit_y.constData());
            alglib::real_1d_array c;
            c.setcontent(paraNum, fit_c);

            const double diffstep = 1.4901e-08;
            double epsx = 0.000001;
            ae_int_t maxits = 10000;
            lsfitstate state;
            lsfitreport rep_local;

            lsfitcreatef(x, y, c, diffstep, state);
            alglib::lsfitsetcond(state, epsx, maxits);
            alglib::lsfitfit(state, function_linear);
            lsfitresults(state, c, rep_local);

            for(int i=0; i<paraNum; i++){
                fit_c[i] = c[i];
            }

            *R2 = rep_local.r2;

            if(rep != nullptr)
            {
                *rep = rep_local;
            }

            qDebug()<<"fit_linear c:["<<QString::number(fit_c[0], 'g', 9)<<","
                     <<QString::number(fit_c[1], 'g', 9)
                     <<"], iterationscount="<<rep_local.iterationscount
                     <<", r2="<<rep_local.r2
                     <<", terminationtype="<<rep_local.terminationtype;
        }
        catch(alglib::ap_error alglib_exception)
        {
            printf("ALGLIB exception with message '%s'\n", alglib_exception.msg.c_str());
            return 0;
        }
        return 1;
    }

    bool fit_poly_2terms(QVector<QPointF> points, double* fit_c, double* R2, lsfitreport* rep)
    {
        int paraNum = 3; //待拟合参数个数
        int num = points.size();
        try
        {
            QVector<double> fit_x, fit_y;
            for(int i=0; i<num; i++)
            {
                fit_x.push_back(points.at(i).x());
                fit_y.push_back(points.at(i).y());
            }

            alglib::real_2d_array x;
            x.setcontent(num, 1, fit_x.constData());

            alglib::real_1d_array y;
            y.setcontent(fit_y.size(), fit_y.constData());

            alglib::real_1d_array c;
            c.setcontent(paraNum, fit_c);

            const double diffstep = 1.4901e-08;
            double epsx = 0.000001;
            ae_int_t maxits = 10000;
            lsfitstate state;
            lsfitreport rep_local;

            lsfitcreatef(x, y, c, diffstep, state);
            alglib::lsfitsetcond(state, epsx, maxits);
            alglib::lsfitfit(state, function_poly_2terms);
            lsfitresults(state, c, rep_local);

            for(int i=0; i<paraNum; i++){
                fit_c[i] = c[i];
            }

            *R2 = rep_local.r2;

            if(rep != nullptr)
            {
                *rep = rep_local;
            }
        }
        catch(alglib::ap_error alglib_exception)
        {
            printf("ALGLIB exception with message '%s'\n", alglib_exception.msg.c_str());
            return 0;
        }
        return 1;
    }

}
