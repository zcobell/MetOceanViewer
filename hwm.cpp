//---- LGPL --------------------------------------------------------------------
//
// Copyright (C)  ARCADIS, 2011-2013.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation version 3.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// contact: Zachary Cobell, zachary.cobell@arcadis-us.com
// ARCADIS
// 4999 Pearl East Circle, Suite 200
// Boulder, CO 80301
//
// All indications and logos of, and references to, "ARCADIS" 
// are registered trademarks of ARCADIS, and remain the property of
// ARCADIS. All rights reserved.
//
//------------------------------------------------------------------------------
// $Author$
// $Date$
// $Id$
// $HeadURL$
//------------------------------------------------------------------------------
//  File: hwm.cpp
//
//------------------------------------------------------------------------------
        
#include "hwm.h"

int ReadHWMData(QString Filename, QVector<hwm_data> &HWM)
{
    QFile MyFile(Filename);
    QString Line,Temp;
    QStringList List;
    int nLines;

    //Check if we can open the file
    if(!MyFile.open(QIODevice::ReadOnly|QIODevice::Text))
    {
        QMessageBox::information(NULL,"ERROR","ERROR:"+MyFile.errorString());
    }

    try
    {
        nLines = 0;
        while(!MyFile.atEnd())
        {
            Line = MyFile.readLine();
            nLines = nLines + 1;
        }
        MyFile.close();
        MyFile.open(QIODevice::ReadOnly|QIODevice::Text);
        HWM.resize(nLines);
        int i=0;
        while(!MyFile.atEnd())
        {
            Line = MyFile.readLine().simplified();
            List = Line.split(",");
            HWM[i].lon = List.value(0).toDouble();
            HWM[i].lat = List.value(1).toDouble();
            HWM[i].bathy = List.value(2).toDouble();
            HWM[i].measured = List.value(3).toDouble();
            HWM[i].modeled = List.value(4).toDouble();
            HWM[i].error = List.value(5).toDouble();
            i = i + 1;
        }
        return 0;
    }
    catch(...)
    {
        return 1;
    }

}

int ComputeLinearRegression(QVector<hwm_data> HWM, double &M, double &R2)
{
    double SumXY, SumX2, SumX, SumY, SumY2, N;

    //Compute the sums used in the regression
    //  This will be forced through 0,0
    SumXY = 0;
    SumX2 = 0;
    SumY2 = 0;
    SumY  = 0;
    SumX  = 0;
    N     = static_cast<double>(HWM.size());

    try
    {
        for(int i = 0; i<N; i++)
        {
            SumX  = SumX  + (HWM[i].measured);
            SumY  = SumY  + (HWM[i].modeled);
            SumXY = SumXY + (HWM[i].measured*HWM[i].modeled);
            SumX2 = SumX2 + (HWM[i].measured*HWM[i].measured);
            SumY2 = SumY2 + (HWM[i].modeled*HWM[i].modeled);
        }

        //Calculate the slope (M) and Correllation (R2)
        M  = SumXY / SumX2;
        R2 = qPow(((N*SumXY - (SumX*SumY)) /
                sqrt( (N*SumX2 - (SumX*SumX))*(N*SumY2-(SumY*SumY)) )),2.0);
    }
    catch(...)
    {
        return 1;
    }
    return 0;

}
