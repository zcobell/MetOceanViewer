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
#include <hwm.h>

int hwm::computeLinearRegression()
{
    double SumXY, SumX2, SumX, SumY, SumY2, N, NDry, N2, SSE, SSTOT, YBar, SumErr, MeanErr;
    double M,B,R2,StdDev;
    int i;

    bool ForceThroughZero = this->forceThroughZeroCheckbox->isChecked();

    SumXY  = 0;
    SumX2  = 0;
    SumY2  = 0;
    SumY   = 0;
    SumX   = 0;
    M      = 0;
    B      = 0;
    N      = static_cast<double>(this->highWaterMarks.size());
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
            if(this->highWaterMarks[i].modeled>-9999)
            {
                SumX   = SumX   + (this->highWaterMarks[i].measured);
                SumY   = SumY   + (this->highWaterMarks[i].modeled);
                SumXY  = SumXY  + (this->highWaterMarks[i].measured*this->highWaterMarks[i].modeled);
                SumX2  = SumX2  + (this->highWaterMarks[i].measured*this->highWaterMarks[i].measured);
                SumY2  = SumY2  + (this->highWaterMarks[i].modeled*this->highWaterMarks[i].modeled);
                SumErr = SumErr +  this->highWaterMarks[i].error;
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
                if(this->highWaterMarks[i].modeled>-9999)
                {
                    SSTOT = SSTOT + qPow((this->highWaterMarks[i].modeled - YBar),2.0);
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
            if(this->highWaterMarks[i].modeled>-9999)
                SumErr = SumErr + qPow(this->highWaterMarks[i].error - MeanErr,2.0);

        StdDev = qSqrt(SumErr / N2);


    }
    catch(...)
    {
        return 1;
    }

    this->regLineSlope = M;
    this->regLineIntercept = B;
    this->regCorrelation = R2;
    this->regStdDev = StdDev;

    return 0;

}
