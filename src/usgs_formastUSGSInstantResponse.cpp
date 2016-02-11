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

int usgs::formatUSGSInstantResponse(QByteArray Input)
{
    bool doubleok;
    int i,j,ParamStart,ParamStop,OffsetHours;
    int HeaderEnd;
    double TempData;
    QStringList TempList;
    QString TempLine,TempDateString,TempTimeZoneString;
    QDateTime CurrentDate;

    QString InputData(Input);
    QStringList SplitByLine = InputData.split(QRegExp("[\n]"),QString::SkipEmptyParts);

    ParamStart = -1;
    ParamStop = -1;
    HeaderEnd = -1;

    if(InputData.isEmpty()||InputData.isNull())
    {
        this->USGSErrorString = QString("This data is not available except from the USGS archive server.");
        return ERR_USGS_ARCHIVEONLY;
    }

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

    //...Find out where the header ends
    for(i=0;i<SplitByLine.length();i++)
    {
        if(SplitByLine.value(i).left(1) != "#")
        {
            HeaderEnd = i + 2;
            break;
        }
    }

    //...Initialize the array
    this->CurrentUSGSStation.resize(this->Parameters.length());

    //...Zero counters
    for(i=0;i<this->CurrentUSGSStation.length();i++)
        this->CurrentUSGSStation[i].NumDataPoints = 0;

    //...Read the data into the array
    for(i=HeaderEnd;i<SplitByLine.length();i++)
    {
        TempLine = SplitByLine.value(i);
        TempList = TempLine.split(QRegExp("[\t]"));
        TempDateString = TempList.value(2);
        TempTimeZoneString = TempList.value(3);
        CurrentDate = QDateTime::fromString(TempDateString,"yyyy-MM-dd hh:mm");
        CurrentDate.setTimeSpec(Qt::UTC);
        OffsetHours = getTimezoneOffset(TempTimeZoneString);
        CurrentDate = CurrentDate.addSecs(3600*OffsetHours);
        for(j=0;j<this->Parameters.length();j++)
        {
            TempData = TempList.value(2*j+4).toDouble(&doubleok);
            if(!TempList.value(2*j+4).isNull() && doubleok)
            {
                this->CurrentUSGSStation[j].NumDataPoints = this->CurrentUSGSStation[j].NumDataPoints + 1;
                this->CurrentUSGSStation[j].Data.resize(this->CurrentUSGSStation[j].Data.length()+1);
                this->CurrentUSGSStation[j].Date.resize(this->CurrentUSGSStation[j].Date.length()+1);
                this->CurrentUSGSStation[j].Data[this->CurrentUSGSStation[j].Data.length()-1] = TempData;
                this->CurrentUSGSStation[j].Date[this->CurrentUSGSStation[j].Date.length()-1] = CurrentDate;
            }
        }
    }

    return 0;
}
