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
#include <usgs.h>

int usgs::formatUSGSDailyResponse(QByteArray Input)
{
    int i,j,ParamStart,ParamStop;
    int HeaderEnd;
    double TempData;
    QStringList TempList;
    QString TempLine,TempDateString;
    QString InputData(Input);
    QStringList SplitByLine = InputData.split(QRegExp("[\n]"),QString::SkipEmptyParts);
    QDateTime CurrentDate;
    bool doubleok;

    ParamStart = -1;
    ParamStop = -1;
    HeaderEnd = -1;

    //...Save the potential error string
    this->USGSErrorString = InputData.remove(QRegExp("[\n\t\r]"));

    //...Start by finding the header and reading the parameters from it
    for(i=0;i<SplitByLine.length();i++)
    {
        if(SplitByLine.value(i).left(15)=="# Data provided")
        {
            ParamStart = i + 2;
            break;
        }
    }

    for(i=ParamStart;i<SplitByLine.length();i++)
    {
        TempLine = SplitByLine.value(i);
        if(TempLine == "#")
        {
            ParamStop = i - 1;
            break;
        }
    }

    this->Parameters.resize(ParamStop-ParamStart+1);

    for(i=ParamStart;i<=ParamStop;i++)
    {
        TempLine = SplitByLine.value(i);
        TempList = TempLine.split(" ",QString::SkipEmptyParts);
        this->Parameters[i-ParamStart] = QString();
        for(j=3;j<TempList.length();j++)
        {
            if(j==3)
                this->Parameters[i-ParamStart] = TempList.value(j);
            else
                this->Parameters[i-ParamStart] = this->Parameters[i-ParamStart] + " " + TempList.value(j);
        }

    }

    //...Remove the leading number
    for(i=0;i<this->Parameters.length();i++)
        Parameters[i] = Parameters[i].mid(6).simplified();

    //...Find out where the header ends
    for(i=0;i<SplitByLine.length();i++)
    {
        if(SplitByLine.value(i).left(1) != "#")
        {
            HeaderEnd = i + 2;
            break;
        }
    }

    //...Delete the old data
    for(i=0;i<this->CurrentUSGSStation.length();i++)
    {
        this->CurrentUSGSStation[i].Data.clear();
        this->CurrentUSGSStation[i].Date.clear();
        this->CurrentUSGSStation[i].NumDataPoints = 0;
    }
    this->CurrentUSGSStation.clear();

    //...Initialize the array
    this->CurrentUSGSStation.resize(Parameters.length());

    //...Zero counters
    for(i=0;i<this->CurrentUSGSStation.length();i++)
        this->CurrentUSGSStation[i].NumDataPoints = 0;

    //...Read the data into the array
    for(i=HeaderEnd;i<SplitByLine.length();i++)
    {
        TempLine = SplitByLine.value(i);
        TempList = TempLine.split(QRegExp("[\t]"));
        TempDateString = TempList.value(2);
        CurrentDate = QDateTime::fromString(TempDateString,"yyyy-MM-dd");
        CurrentDate.setTimeSpec(Qt::UTC);
        for(j=0;j<Parameters.length();j++)
        {
            TempData = TempList.value(2*j+3).toDouble(&doubleok);
            if(!TempList.value(2*j+3).isNull() && doubleok)
            {
                this->CurrentUSGSStation[j].NumDataPoints = this->CurrentUSGSStation[j].NumDataPoints + 1;
                this->CurrentUSGSStation[j].Data.resize(this->CurrentUSGSStation[j].Data.length()+1);
                this->CurrentUSGSStation[j].Date.resize(this->CurrentUSGSStation[j].Date.length()+1);
                this->CurrentUSGSStation[j].Data[this->CurrentUSGSStation[j].Data.length()-1] = TempData;
                this->CurrentUSGSStation[j].Date[this->CurrentUSGSStation[j].Date.length()-1] = CurrentDate;
            }
        }
    }

    this->USGSDataReady = true;

    return 0;
}
