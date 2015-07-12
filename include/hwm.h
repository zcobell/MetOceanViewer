//----GPL-----------------------------------------------------------------------
//
// This file is part of MetOceanViewer.
//
//    MetOceanViewer is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    MetOceanViewer is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with MetOceanViewer.  If not, see <http://www.gnu.org/licenses/>.
//------------------------------------------------------------------------------
        
#ifndef HWM_H
#define HWM_H

#include <QMainWindow>
#include <QVector>
#include <QFile>
#include <QMessageBox>
#include <qmath.h>

//-------------------------------------------//
//Data structure for high water marks
//-------------------------------------------//
struct hwm_data
{
    double lat;
    double lon;
    double bathy;
    double measured;
    double modeled;
    double error;
};
//-------------------------------------------//

//-------------------------------------------//
//Global variable used for the high water
//marks
//-------------------------------------------//
extern QVector<double> classes;
//-------------------------------------------//

//-------------------------------------------//
//Function prototypes
//-------------------------------------------//
int ReadHWMData(QString Filename, QVector<hwm_data> &HWM);

int ComputeLinearRegression(bool ForceThroughZero,
                            QVector<hwm_data> HWM,
                            double &M, double &B,
                            double &Y, double &StdDev);
//-------------------------------------------//

#endif // HWM_H
