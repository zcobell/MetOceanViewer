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
        
#include <hwm.h>

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

int ComputeLinearRegression(bool ForceThroughZero,QVector<hwm_data> HWM, double &M, double &B, double &R2, double &StdDev)
{
    double SumXY, SumX2, SumX, SumY, SumY2, N, NDry, N2, SSE, SSTOT, YBar, SumErr, MeanErr;
    int i;

    //Compute the sums used in the regression
    //  This will be forced through 0,0
    SumXY  = 0;
    SumX2  = 0;
    SumY2  = 0;
    SumY   = 0;
    SumX   = 0;
    M      = 0;
    B      = 0;
    N      = static_cast<double>(HWM.size());
    NDry   = 0;
    SSE    = 0;
    SSTOT  = 0;
    SumErr = 0;

    try
    {
        for(i = 0; i<N; i++)
        {
            //We ditch points that didn't wet since they
            //skew calculation
            if(HWM[i].modeled>-9999)
            {
                SumX   = SumX   + (HWM[i].measured);
                SumY   = SumY   + (HWM[i].modeled);
                SumXY  = SumXY  + (HWM[i].measured*HWM[i].modeled);
                SumX2  = SumX2  + (HWM[i].measured*HWM[i].measured);
                SumY2  = SumY2  + (HWM[i].modeled*HWM[i].modeled);
                SumErr = SumErr +  HWM[i].error;
            }
            else
                NDry = NDry + 1;
        }

        //Number of points that we'll end up using
        N2 = N - NDry;

        //Calculate the slope (M) and Correllation (R2)
        if(ForceThroughZero)
        {
            //Slope
            M   = SumXY / SumX2;

            //Forced through zero
            B   = 0;

            //Average Y
            YBar = SumY / N2;

            //Calculate Total Sum of Squares
            for(i = 0; i<N; i++)
            {
                //We ditch points that didn't wet since they
                //skew calculation
                if(HWM[i].modeled>-9999)
                {
                    SSTOT = SSTOT + qPow((HWM[i].modeled - YBar),2.0);
                }
            }

            //Sum of square errors
            SSE = SumY2 - M*M*SumX2;

            //R2
            R2  = 1 - ( SSE / SSTOT );
        }
        else
        {
            //Slope
            M  = (N2*SumXY - SumX*SumY) / (N2*SumX2-(SumX*SumX));

            //Intercept
            B  = ((SumY*SumX2)-(SumX*SumXY))/(N2*SumX2-(SumX*SumX));

            //R2 calculation
            R2 = qPow(((N2*SumXY - (SumX*SumY)) /
                   sqrt( (N2*SumX2 - (SumX*SumX))*(N2*SumY2-(SumY*SumY)) )),2.0);
        }

        //Calculate Standard Deviation
        MeanErr = SumErr / N2;
        SumErr = 0;
        for( i = 0; i<N; i++)
            if(HWM[i].modeled>-9999)
                SumErr = SumErr + qPow(HWM[i].error - MeanErr,2.0);

        StdDev = qSqrt(SumErr / N2);


    }
    catch(...)
    {
        return 1;
    }
    return 0;

}
