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
#include "hwm.h"

hwm::hwm(QLineEdit *inFilebox, QCheckBox *inManualCheck, QComboBox *inUnitCombobox,
         QCheckBox *inForceThroughZero, QCheckBox *inUpperLowerLines, QCheckBox *inColorHWMDots,
         QPushButton *inHWMColor, QPushButton *inButton121LineColor,
         QPushButton *inButtonBoundingLineColor, QPushButton *inButtonRegLineColor,
         QLineEdit *inModeledAxisLabel, QLineEdit *inMeasuredAxisLabel,
         QLineEdit *inPlotTitle, QSpinBox *inBoundingLinesValue, QWebEngineView *inMap, QChartView *inChartView,
         QVector<double> &inClassValues, QObject *parent) : QObject(parent)
{

    this->fileBox = inFilebox;
    this->manualClassificationCheckbox = inManualCheck;
    this->unitComboBox = inUnitCombobox;
    this->forceThroughZeroCheckbox = inForceThroughZero;
    this->buttonHWMColor = inHWMColor;
    this->button121LineColor = inButton121LineColor;
    this->buttonBoundingLinecolor = inButtonBoundingLineColor;
    this->buttonRegLineColor = inButtonRegLineColor;
    this->modeledAxisLabelBox = inModeledAxisLabel;
    this->measuredAxisLabelBox = inMeasuredAxisLabel;
    this->upperLowerLinesCheckbox = inUpperLowerLines;
    this->boundingLinesValue = inBoundingLinesValue;
    this->colorHWMDotsCheckbox = inColorHWMDots;
    this->plotTitleBox = inPlotTitle;
    this->chartView = inChartView;
    this->map = inMap;

    this->classes.resize(inClassValues.length());
    for(int i=0;i<this->classes.length();i++)
        classes[i] = inClassValues[i];

}

int hwm::readHWMData()
{
    QString Line;
    QStringList List;
    int nLines;

    QFile MyFile(this->fileBox->text());

    //Check if we can open the file
    if(!MyFile.open(QIODevice::ReadOnly|QIODevice::Text))
    {
        this->hwmErrorString = MyFile.errorString();
        return -1;
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
        this->highWaterMarks.resize(nLines);
        int i=0;
        while(!MyFile.atEnd())
        {
            Line = MyFile.readLine().simplified();
            List = Line.split(",");
            this->highWaterMarks[i].lon = List.value(0).toDouble();
            this->highWaterMarks[i].lat = List.value(1).toDouble();
            this->highWaterMarks[i].bathy = List.value(2).toDouble();
            this->highWaterMarks[i].measured = List.value(3).toDouble();
            this->highWaterMarks[i].modeled = List.value(4).toDouble();
            this->highWaterMarks[i].error = List.value(5).toDouble();
            i = i + 1;
        }
        return 0;
    }
    catch(...)
    {
        this->hwmErrorString = "Unexpected error reading file.";
        return 1;
    }

}

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

int hwm::classifyHWM(double diff)
{
    int color;
    if(diff<this->classes[0])
        color=1;
    else if(diff<this->classes[1])
        color=2;
    else if(diff<this->classes[2])
        color=3;
    else if(diff<this->classes[3])
        color=4;
    else if(diff<this->classes[4])
        color=5;
    else if(diff<this->classes[5])
        color=6;
    else if(diff<this->classes[6])
        color=7;
    else
        color=8;
    return color;
}
