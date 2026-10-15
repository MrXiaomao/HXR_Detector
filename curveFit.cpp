/*
 * @Author: MaoXiaoqing
 * @Date: 2025-04-22 17:21:41
 * @LastEditors: Maoxiaoqing
 * @LastEditTime: 2026-10-15 20:01:08
 * @Description: 借助ALGLIB库（只需添加到项目便可使用）进行函数拟合，给出拟合结果以及拟合残差，拟合曲线
 */

#include "curveFit.h"
#include <algorithm>
#include <cmath>
#include <QDebug>
#include <math.h>

using namespace alglib;
namespace CurveFit {

namespace {

void fillLsFitCov(LsFitCov *covOut, const lsfitreport &rep, int nParam, double resnorm)
{
    if (!covOut)
        return;
    covOut->valid = false;
    covOut->nParam = nParam;
    covOut->resnorm = resnorm;
    covOut->rmsError = rep.rmserror;
    covOut->cov = QVector<double>(nParam * nParam, 0.0);
    if (nParam <= 0)
        return;
    if (rep.covpar.rows() < nParam || rep.covpar.cols() < nParam)
        return;
    for (int i = 0; i < nParam; ++i) {
        for (int j = 0; j < nParam; ++j)
            covOut->cov[i * nParam + j] = rep.covpar[i][j];
    }
    covOut->valid = true;
}

} // namespace

    
    void function_linear(const real_1d_array &c, const real_1d_array &x, double &func, void *ptr)
    {
        func = c[0]*x[0] + c[1];
    }

    void function_poly_2terms(const real_1d_array &c, const real_1d_array &x, double &func, void *ptr)
    {
        func = c[0]*x[0]*x[0] + c[1]*x[0] + c[2];
    }

    void function_gauss_1terms(const real_1d_array &c, const real_1d_array &x, double &func, void *ptr)
    {
        func = c[0]*exp(-0.5*pow((x[0]-c[1])/c[2],2));
    }

    void function_gauss_linear(const real_1d_array &c, const real_1d_array &x, double &func, void *_peak)
    {
        double peak = *static_cast<double*>(_peak);
        func = c[0]*exp(-0.5*pow((x[0]-peak)/c[1],2)) + c[2]*x[0] + c[3];
    }

    void function_gauss_linear2(const real_1d_array &c, const real_1d_array &x, double &func, void *ptr)
    {
        func = c[0]*exp(-0.5*pow((x[0]-c[1])/c[2],2)) + c[3]*x[0] + c[4];
    }

    void function_gauss_poly4(const real_1d_array &c, const real_1d_array &x, double &func, void *ptr)
    {
        func = c[0]*exp(-0.5*pow((x[0]-c[1])/c[2], 2)) + 
            c[3]*pow(x[0], 4) + c[4]*pow(x[0], 3) + c[5]*pow(x[0], 2) + c[6]*x[0] + c[7];
    }

    void function_2gauss_poly4(const alglib::real_1d_array &c, const alglib::real_1d_array &x, double &func, void *ptr)
    {
        func = c[0]*exp(-0.5*pow((x[0]-c[1])/c[2], 2)) + c[3]*exp(-0.5*pow((x[0]-c[4])/c[5], 2)) +
            c[6]*pow(x[0], 4) + c[7]*pow(x[0], 3) + c[8]*pow(x[0], 2) + c[9]*x[0] + c[10];
    }

    void function_log(const real_1d_array &c, const real_1d_array &t, double &func, void *ptr)
    {
        func = c[0] - log(2)/(78.4*60)*t[0];
    }

    void function_exp_decay(const real_1d_array &c, const real_1d_array &t, double &func, void *ptr)
    {
        Q_UNUSED(ptr);
        func = c[0] * exp(-log(2) / (78.4 * 60) * t[0]);
    }

    bool fit_linear(QVector<QPointF> points, double* fit_c, double* R2, lsfitreport* rep)
    {
        int paraNum = 2; //待拟合参数个数
        // 补齐复数的虚数部分。直接在数组的尾部补齐虚数
        int num = points.size();
        try
        {      
            //提取数据点
            QVector<double> fit_x, fit_y;
            for(int i=0; i<num; i++)
            {
                fit_x.push_back(points.at(i).x());
                fit_y.push_back(points.at(i).y());
            }

            // vector容器转real_2d_array
            alglib::real_2d_array x;
            x.setcontent(num, 1, fit_x.constData());

            // vector容器转real_1d_array
            alglib::real_1d_array y;
            y.setcontent(fit_y.size(), fit_y.constData());
            alglib::real_1d_array c;
            c.setcontent(paraNum, fit_c);

            const double diffstep = 1.4901e-08;
            double epsx = 0.000001;
            ae_int_t maxits = 10000;
            lsfitstate state; //拟合的所有信息，每调用一次函数，相关的参数值变更新到state中存放。
            lsfitreport rep_local;

            //
            // Fitting without weights
            //
            lsfitcreatef(x, y, c, diffstep, state);
            alglib::lsfitsetcond(state, epsx, maxits);
            alglib::lsfitfit(state, function_linear); //peak对应function_cx_1_func中void *ptr。
            lsfitresults(state, c, rep_local); //参数存储到state中

            //取出拟合参数c
            for(int i=0; i<paraNum; i++){
                fit_c[i] = c[i];
            }

            //计算拟合y值        
            for(int i=0; i<num; i++)
            {
                double y;
                double x_temp = fit_x.at(i);
                alglib::real_1d_array xData;
                xData.setlength(1);
                xData[0] = x_temp;
                function_linear(c, xData, y, NULL);
            }
            *R2 = rep_local.r2;
            
            // 如果提供了rep指针，则复制报告信息
            if(rep != nullptr)
            {
                *rep = rep_local;
            }

            // printf("fit_linear c:%s, chi_square=%.1f\n", c.tostring(1).c_str(), *chi_square);
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
        // 补齐复数的虚数部分。直接在数组的尾部补齐虚数
        int num = points.size();
        try
        {
            //提取数据点
            QVector<double> fit_x, fit_y;
            for(int i=0; i<num; i++)
            {
                fit_x.push_back(points.at(i).x());
                fit_y.push_back(points.at(i).y());
            }
            
            //QVector容器转real_2d_array
            alglib::real_2d_array x;
            x.setcontent(num, 1, fit_x.constData());

            // QVector容器转real_1d_array
            alglib::real_1d_array y;
            y.setcontent(fit_y.size(), fit_y.constData());

            alglib::real_1d_array c;
            c.setcontent(paraNum, fit_c);

            const double diffstep = 1.4901e-08;
            double epsx = 0.000001;
            ae_int_t maxits = 10000;
            lsfitstate state; //拟合的所有信息，每调用一次函数，相关的参数值变更新到state中存放。
            lsfitreport rep_local;

            //
            // Fitting without weights
            //
            lsfitcreatef(x, y, c, diffstep, state);
            alglib::lsfitsetcond(state, epsx, maxits);
            alglib::lsfitfit(state, function_poly_2terms); 
            lsfitresults(state, c, rep_local); //参数存储到state中

            //取出拟合参数c
            for(int i=0; i<paraNum; i++){
                fit_c[i] = c[i];
            }

            *R2 = rep_local.r2;
            
            // 如果提供了rep指针，则复制报告信息
            if(rep != nullptr)
            {
                *rep = rep_local;
            }
            
            // printf("fit_poly_2terms c:%s, chi_square=%.1f\n", c.tostring(1).c_str(), *chi_square);
            // qDebug()<<"fit_poly_2terms c:"<<c.tostring(1).c_str()<<", iterationscount="<<rep.iterationscount
            //          <<", r2="<<rep.r2
            //          <<", terminationtype="<<rep.terminationtype
            //          <<"chi_square="<<*chi_square;
        }
        catch(alglib::ap_error alglib_exception)
        {
            printf("ALGLIB exception with message '%s'\n", alglib_exception.msg.c_str());
            return 0;
        }
        return 1;
    }

    bool fit_gauss_1terms(QVector<QPointF> points, double* fit_c, double* R2, lsfitreport* rep)
    {
        int paraNum = 3; //待拟合参数个数
        // 补齐复数的虚数部分。直接在数组的尾部补齐虚数
        int num = points.size();
        //拟合点数判断
        if (num < gauss_arr_count_min){
            qDebug().noquote() << "高斯拟合失败，拟合点数过少";
            return 0;
        }

        //提取数据点
        QVector<double> fit_x, fit_y;
        for(int i=0; i<num; i++)
        {
            fit_x.push_back(points.at(i).x());
            fit_y.push_back(points.at(i).y());
        }
        
        //先大致计算出高斯函数待拟合参数[A, mu, sigma]
        //A
        auto maxPosition = std::max_element(fit_y.begin(), fit_y.end());
        float peak = *maxPosition; 

        //mu
        // 获取最大值的索引位置
        size_t max_pos = std::distance(fit_y.begin(), maxPosition);
        double mean = fit_x.at(max_pos);

        //sigma
        //这里0.04是与511keV能量分辨率相关的经验值，不能适用于其他能量的sigma估计
        double sigma = mean * 0.04; 
        
        //若存在上次的拟合结果，直接用上次的拟合值作为本次拟合的初值
        if(fit_c[2] > 0.0000001) sigma = fit_c[2];

        fit_c[0] = peak; //A
        fit_c[1] = mean; //mu
        fit_c[2] = sigma; //sigma

        try
        {
            //QVector容器转real_2d_array
            alglib::real_2d_array x;
            x.setcontent(num, 1, fit_x.data());

            // QVector容器转real_1d_array
            alglib::real_1d_array y;
            y.setcontent(fit_y.size(), fit_y.data());

            alglib::real_1d_array c;
            c.setcontent(paraNum, fit_c);

            double epsx = 0.00001;
            ae_int_t maxits = 10000;
            lsfitstate state; //所有的参数数据都存储到state中的。
            lsfitreport rep;

            double diffstep = 0.0001;

            lsfitcreatef(x, y, c, diffstep, state); //参数存储到state中
            alglib::lsfitsetcond(state, epsx, maxits); //参数存储到state中
            alglib::lsfitfit(state, function_gauss_1terms); //参数存储到state中
            lsfitresults(state, c, rep); //参数存储到state中

            //判断拟合是否成功
            if (rep.terminationtype < 1) 
            {
                qDebug().noquote() << "高斯拟合失败，拟合未收敛";
                return 0;
            }

            //判断拟合优度R2是否合格
            if (rep.r2 < 0.8)
            {
                qDebug().noquote() << "高斯拟合失败，拟合优度R2过低";
                return 0;
            }

            //取出拟合参数c
            for(int i=0; i<paraNum; i++){
                fit_c[i] = c[i];
                
            }
            //高斯拟合的sigma为正数和负数都是可以的(曲线均相同，当sigma较小时容易出现负数)。但是物理上应该为正数  
            if(fit_c[2]<0) fit_c[2] = -fit_c[2];

            *R2 = rep.r2;
            // 打印拟合结果
            // qDebug()<<"Gauss Fit c:"<<c.tostring(1).c_str()<<", iterationscount="<<rep.iterationscount
            //          <<", r2="<<rep.r2
            //          <<", terminationtype="<<rep.terminationtype;
        }
        catch(alglib::ap_error alglib_exception)
        {
            printf("ALGLIB exception with message '%s'\n", alglib_exception.msg.c_str());
            return 0;
        }
        return 1;  
    }

    bool fit_gauss_linear(QVector<QPointF> points, double* fit_c, double peak, double* chi_square)
    {
        int paraNum = 4; //待拟合参数个数
        int num = points.size();

        //提取数据点
        QVector<double> fit_x, fit_y;
        for(int i=0; i<num; i++)
        {
            fit_x.push_back(points.at(i).x());
            fit_y.push_back(points.at(i).y());
        }

        try
        {
            //QVector容器转real_2d_array
            alglib::real_2d_array x;
            x.setcontent(num, 1, fit_x.constData());

            // QVector容器转real_1d_array
            alglib::real_1d_array y;
            y.setcontent(fit_y.size(), fit_y.constData());

            alglib::real_1d_array c;
            c.setcontent(paraNum, fit_c);

            double epsx = 0.000001;
            ae_int_t maxits = 10000;
            lsfitstate state; //拟合的所有信息，每调用一次函数，相关的参数值变更新到state中存放。
            lsfitreport rep;

            //
            // Fitting without weights
            //
            double diffstep = 1.4901e-08;
            lsfitcreatef(x, y, c, diffstep, state);
            alglib::lsfitsetcond(state, epsx, maxits);
            alglib::lsfitfit(state, function_gauss_linear, NULL, &peak);
            lsfitresults(state, c, rep); //参数存储到state中

            //取出拟合参数c
            for(int i=0;i<paraNum;i++){
                fit_c[i] = c[i];
            }

            //计算卡方统计量
            for(int i=0; i<num; i++)
            {
                double x_temp = fit_x.at(i);
                alglib::real_1d_array xData;
                xData.setlength(1);
                xData[0] = x_temp;
                double y;
                function_gauss_linear(c, xData, y, &peak);

                double residual = fit_y.at(i) - y; //残差
                (*chi_square) += pow(residual, 2)/y;
            }
        }
        catch(alglib::ap_error alglib_exception)
        {
            printf("ALGLIB exception with message '%s'\n", alglib_exception.msg.c_str());
            return 0;
        }
        return 1;
    }

    bool fit_gauss_linear2(QVector<QPointF> points, double* fit_c, double r2Min)
    {
        int paraNum = 5; //待拟合参数个数
        int num = points.size();

        //提取数据点
        QVector<double> fit_x, fit_y;
        for(int i=0; i<num; i++)
        {
            fit_x.push_back(points.at(i).x());
            fit_y.push_back(points.at(i).y());
        }

        try
        {
            //QVector容器转real_2d_array
            alglib::real_2d_array x;
            x.setcontent(num, 1, fit_x.constData());

            // QVector容器转real_1d_array
            alglib::real_1d_array y;
            y.setcontent(fit_y.size(), fit_y.constData());

            alglib::real_1d_array c;
            c.setcontent(paraNum, fit_c);

            double epsx = 0.000001;
            const double diffstep = 1.4901e-08;
            ae_int_t maxits = 10000;
            lsfitstate state; //所有的参数数据都存储到state中的。
            lsfitreport rep;

            lsfitcreatef(x, y, c, diffstep, state); //参数存储到state中
            alglib::lsfitsetcond(state, epsx, maxits); //参数存储到state中
            alglib::lsfitfit(state, function_gauss_linear2); //参数存储到state中
            lsfitresults(state, c, rep); //参数存储到state中

            if (rep.terminationtype < 1) {
                qDebug().noquote() << "fit_gauss_linear2 failed: not converged";
                return false;
            }
            if (r2Min >= 0.0 && rep.r2 < r2Min) {
                qDebug().noquote() << "fit_gauss_linear2 failed: R2=" << rep.r2 << "<" << r2Min;
                return false;
            }

            //取出拟合参数c
            for(int i=0; i<paraNum; i++){
                fit_c[i] = c[i];
            }

            //计算卡方统计量
            double chi_square_sum = 0.0;
            for(int i=0; i<num; i++)
            {
                double x_temp = fit_x.at(i);
                alglib::real_1d_array xData;
                xData.setlength(1);
                xData[0] = x_temp;

                //计算拟合得到的y值
                double y;
                function_gauss_linear2(c, xData, y, NULL);

                double residual_tmp = fit_y.at(i) - y; //残差
                double chi_square = pow(residual_tmp, 2)/y;
                chi_square_sum += chi_square;
            }
            qDebug()<<"fit_gauss_linear2 c:"<<c.tostring(1).c_str()<<", iterationscount="<<rep.iterationscount
                    <<", r2="<<rep.r2
                    <<", terminationtype="<<rep.terminationtype
                    <<", chi_square="<<chi_square_sum;
        }
        catch(alglib::ap_error alglib_exception)
        {
            printf("ALGLIB exception with message '%s'\n", alglib_exception.msg.c_str());
            return 0;
        }
        return 1;
    }

    bool fit_gauss_ploy4(QVector<QPointF> points, double* fit_c, QVector<double> &residual_rate,
                         double r2Min, double *r2Out, LsFitCov *covOut)
    {
        int paraNum = 8; //待拟合参数个数
        int num = points.size();

        //提取数据点
        QVector<double> fit_x, fit_y;
        for(int i=0; i<num; i++)
        {
            fit_x.push_back(points.at(i).x());
            fit_y.push_back(points.at(i).y());
        }

        try
        {
            //QVector容器转real_2d_array
            alglib::real_2d_array x;
            x.setcontent(num, 1, fit_x.constData());

            // QVector容器转real_1d_array
            alglib::real_1d_array y;
            y.setcontent(fit_y.size(), fit_y.constData());

            alglib::real_1d_array c;
            c.setcontent(paraNum, fit_c);

            double epsx = 0.00000001;
            ae_int_t maxits = 10000;
            lsfitstate state; //拟合的所有信息，每调用一次函数，相关的参数值变更新到state中存放。
            lsfitreport rep;

            double diffstep_tmp = 1e-9;

            //
            // Fitting without weights
            //
            lsfitcreatef(x, y, c, diffstep_tmp, state);
            alglib::lsfitsetcond(state, epsx, maxits);
            alglib::lsfitfit(state, function_gauss_poly4);
            lsfitresults(state, c, rep); //参数存储到state中

            if (rep.terminationtype < 1) {
                qDebug().noquote() << "fit_gauss_ploy4 failed: not converged";
                return false;
            }
            if (r2Min >= 0.0 && rep.r2 < r2Min) {
                qDebug().noquote() << "fit_gauss_ploy4 failed: R2=" << rep.r2 << "<" << r2Min;
                return false;
            }

            //取出拟合参数c
            for(int i=0;i<paraNum;i++){
                fit_c[i] = c[i];
            }
            if (r2Out)
                *r2Out = rep.r2;

            //计算残差率
            double chi_square_sum = 0.0;
            double resnorm = 0.0;//残差平方和
            residual_rate.clear();
            for(int i=0; i<num; i++)
            {
                double x_temp = fit_x.at(i);
                alglib::real_1d_array xData;
                xData.setlength(1);
                xData[0] = x_temp;

                //计算拟合得到的y值
                double yFit;
                function_gauss_poly4(c, xData, yFit, NULL);

                double residual = fit_y.at(i) - yFit; //残差
                resnorm += residual * residual;
                double resRate = residual / yFit*100.0;
                residual_rate.push_back(resRate);
            }
            fillLsFitCov(covOut, rep, paraNum, resnorm);

            qDebug().noquote()<<"fit_gauss_ploy4 c:["<<fit_c[0]<<","<<fit_c[1]<<","<<fit_c[2]<<","<<QString::number(fit_c[3], 'g', 9)
                    <<","<<QString::number(fit_c[4], 'g', 9)<<","<<QString::number(fit_c[5], 'g', 9)<<","<<QString::number(fit_c[6], 'g', 9)<<","<<fit_c[7]
                    <<"], iterationscount="<<rep.iterationscount
                    <<", r2="<<rep.r2
                    <<", terminationtype="<<rep.terminationtype
                    <<", chi_square="<<chi_square_sum;
        }
        catch(alglib::ap_error alglib_exception)
        {
            printf("ALGLIB exception with message '%s'\n", alglib_exception.msg.c_str());
            return 0;
        }
        return 1;
    }

    bool fit_2gauss_ploy4(QVector<QPointF> points, double* fit_c, QVector<double> &residual_rate,
                          double r2Min, double *r2Out, LsFitCov *covOut)
    {
        int paraNum = 11; //待拟合参数个数
        int num = points.size();

        //提取数据点
        QVector<double> fit_x, fit_y;
        for(int i=0; i<num; i++)
        {
            fit_x.push_back(points.at(i).x());
            fit_y.push_back(points.at(i).y());
        }

        try
        {
            //QVector容器转real_2d_array
            alglib::real_2d_array x;
            x.setcontent(num, 1, fit_x.constData());

            // QVector容器转real_1d_array
            alglib::real_1d_array y;
            y.setcontent(num, fit_y.constData());

            alglib::real_1d_array c;
            c.setcontent(paraNum, fit_c);

            double epsx = 0.00000001;
            ae_int_t maxits = 1000;
            lsfitstate state; //拟合的所有信息，每调用一次函数，相关的参数值变更新到state中存放。
            lsfitreport rep;

            double diffstep_tmp = 1e-9;

            //
            // Fitting without weights
            //
            lsfitcreatef(x, y, c, diffstep_tmp, state);
            alglib::lsfitsetcond(state, epsx, maxits);
            alglib::lsfitfit(state, function_2gauss_poly4);
            lsfitresults(state, c, rep); //参数存储到state中

            if (rep.terminationtype < 1) {
                qDebug().noquote() << "fit_2gauss_ploy4 failed: not converged";
                return false;
            }
            if (r2Min >= 0.0 && rep.r2 < r2Min) {
                qDebug().noquote() << "fit_2gauss_ploy4 failed: R2=" << rep.r2 << "<" << r2Min;
                return false;
            }

            //取出拟合参数c
            for(int i=0;i<paraNum;i++){
                fit_c[i] = c[i];
            }
            if (r2Out)
                *r2Out = rep.r2;

            //计算残差率
            double chi_square_sum = 0.0;
            double resnorm = 0.0;
            residual_rate.clear();
            for(int i=0; i<num; i++)
            {
                double x_temp = fit_x.at(i);
                alglib::real_1d_array xData;
                xData.setlength(1);
                xData[0] = x_temp;

                //计算拟合得到的y值
                double yFit;
                function_2gauss_poly4(c, xData, yFit, NULL);

                double residual = fit_y.at(i) - yFit; //残差
                resnorm += residual * residual;
                double resRate = residual / yFit*100.0;
                residual_rate.push_back(resRate);
            }
            fillLsFitCov(covOut, rep, paraNum, resnorm);

            qDebug().noquote()<<"fit_2gauss_ploy4 c:["<<fit_c[0]<<","<<fit_c[1]<<","<<fit_c[2]<<","
                    <<QString::number(fit_c[3], 'g', 9)<<","<<QString::number(fit_c[4], 'g', 9)<<","
                    <<QString::number(fit_c[5], 'g', 9)<<","<<QString::number(fit_c[6], 'g', 9)<<","
                    <<QString::number(fit_c[7], 'g', 9)<<","<<QString::number(fit_c[8], 'g', 9)<<","
                    <<QString::number(fit_c[9], 'g', 9)<<","<<QString::number(fit_c[10], 'g', 9)
                    <<"], iterationscount="<<rep.iterationscount
                    <<", r2="<<rep.r2
                    <<", terminationtype="<<rep.terminationtype
                    <<", chi_square="<<chi_square_sum;
        }
        catch(alglib::ap_error alglib_exception)
        {
            printf("ALGLIB exception with message '%s'\n", alglib_exception.msg.c_str());
            return 0;
        }
        return 1;
    }

    bool fit_log(QVector<QPointF> points, double* fit_c, QVector<double> &residual_rate)
    {
        int paraNum = 1; //待拟合参数个数
        int num = points.size();

        //提取数据点
        QVector<double> fit_x, fit_y;
        for(int i=0; i<num; i++)
        {
            fit_x.push_back(points.at(i).x());
            fit_y.push_back(points.at(i).y());
        }

        try
        {
            //QVector容器转real_2d_array
            alglib::real_2d_array x;
            x.setcontent(num, 1, fit_x.constData());

            // QVector容器转real_1d_array
            alglib::real_1d_array y;
            y.setcontent(num, fit_y.constData());

            alglib::real_1d_array c;
            c.setcontent(paraNum, fit_c);

            // double epsx = 0.000001;
            double epsx = 0.00000001;
            ae_int_t maxits = 10000;
            lsfitstate state; //拟合的所有信息，每调用一次函数，相关的参数值变更新到state中存放。
            lsfitreport rep;

            double diffstep_tmp = 1e-9;

            //
            // Fitting without weights
            //
            lsfitcreatef(x, y, c, diffstep_tmp, state);
            alglib::lsfitsetcond(state, epsx, maxits);
            alglib::lsfitfit(state, function_log);
            lsfitresults(state, c, rep); //参数存储到state中

            //取出拟合参数c
            for(int i=0; i<paraNum; i++){
                fit_c[i] = c[i];
            }

            //计算残差率
            for(int i=0; i<num; i++)
            {
                double x_temp = fit_x.at(i);
                alglib::real_1d_array xData;
                xData.setlength(1);
                xData[0] = x_temp;

                //计算拟合得到的y值
                double y;
                function_log(c, xData, y, NULL);

                double residual = fit_y.at(i) - y; //残差
                double resRate = residual / y*100.0;
                residual_rate.push_back(resRate);
            }

            qDebug().noquote()<<"CurveFit::fit_log c:["<<fit_c[0]<<","
                    <<"], iterationscount="<<rep.iterationscount
                    <<", r2="<<rep.r2
                    <<", terminationtype="<<rep.terminationtype;
        }
        catch(alglib::ap_error alglib_exception)
        {
            printf("ALGLIB exception with message '%s'\n", alglib_exception.msg.c_str());
            return 0;
        }
        return 1;
    }

    bool fit_exp_decay(QVector<QPointF> points, double *fit_a, QVector<double> &residual_rate,
                       double *r2Out, const QVector<double> *weights, double *rmseOut)
    {
        const int paraNum = 1;
        const int num = points.size();
        if (num < 2 || !fit_a)
            return false;

        const bool useWeights = weights && weights->size() == num;
        QVector<double> fit_x;
        QVector<double> fit_y;
        fit_x.reserve(num);
        fit_y.reserve(num);
        for (int i = 0; i < num; ++i) {
            fit_x.push_back(points.at(i).x());
            fit_y.push_back(points.at(i).y());
        }

        try {
            alglib::real_2d_array x;
            x.setcontent(num, 1, fit_x.constData());

            alglib::real_1d_array y;
            y.setcontent(num, fit_y.constData());

            alglib::real_1d_array c;
            c.setcontent(paraNum, fit_a);

            const double epsx = 1e-9;
            const ae_int_t maxits = 10000;
            lsfitstate state;
            lsfitreport rep;

            if (useWeights) {
                alglib::real_1d_array w;
                w.setcontent(num, weights->constData());
                lsfitcreatewf(x, y, w, c, 1e-9, state);
            } else {
                lsfitcreatef(x, y, c, 1e-9, state);
            }
            alglib::lsfitsetcond(state, epsx, maxits);
            alglib::lsfitfit(state, function_exp_decay);
            lsfitresults(state, c, rep);

            if (rep.terminationtype < 1)
                return false;

            fit_a[0] = c[0];
            if (r2Out)
                *r2Out = rep.r2;
            residual_rate.clear();
            double sseW = 0.0;
            for (int i = 0; i < num; ++i) {
                alglib::real_1d_array xData;
                xData.setlength(1);
                xData[0] = fit_x.at(i);
                double yFit = 0.0;
                function_exp_decay(c, xData, yFit, nullptr);
                const double measured = fit_y.at(i);
                const double residual = measured - yFit;
                const double wi = useWeights ? weights->at(i) : 1.0;
                sseW += wi * residual * residual;
                residual_rate.push_back((measured - yFit) / yFit * 100.0);
            }
            // 与 MATLAB gof.rmse / 加权最小二乘一致：sqrt(Σ w r² / (n-p))
            if (rmseOut) {
                const int dof = std::max(num - paraNum, 1);
                *rmseOut = std::sqrt(sseW / static_cast<double>(dof));
            }

            qDebug().nospace() << "CurveFit::fit_exp_decay a=" << fit_a[0]
                               << " r2=" << rep.r2
                               << " weighted=" << useWeights
                               << " rmse=" << (rmseOut ? *rmseOut : rep.rmserror);
        } catch (alglib::ap_error &alglib_exception) {
            printf("ALGLIB exception with message '%s'\n", alglib_exception.msg.c_str());
            return false;
        }
        return true;
    }
}

