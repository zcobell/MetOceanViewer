//-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2015  Zach Cobell
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// The name "MetOcean Viewer" is specific to this project and may not be
// used for projects "forked" or derived from this work.
//
//-----------------------------------------------------------------------//
        
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
