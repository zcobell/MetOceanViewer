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
#include <noaa.h>

QString noaa::FormatNOAAResponse(QVector<QByteArray> Input,QString &ErrorString,int index)
{
    int i,j,k;
    int dataCount;
    QString Output,TempData,DateS,YearS,MonthS,DayS,HourMinS,HourS,MinS,WLS;
    QStringList TimeSnap;
    QVector<QString> InputData;
    QVector<QStringList> DataList;
    QVector<QString> Temp;

    InputData.resize(Input.length());
    DataList.resize(Input.length());
    Temp.resize(Input.length());

    for(i=0;i<DataList.length();i++)
    {
        InputData[i] = QString(Input[i]);
        DataList[i] = InputData[i].split(QRegExp("[\r\n]"),QString::SkipEmptyParts);
        Temp[i] = QString(Input[i]);
        ErrorString = Temp[i] + "\n";
    }

    Output = "'";

    dataCount = 0;
    for(i=0;i<DataList.length();i++)
        dataCount = dataCount+DataList[i].length()-1;

    CurrentNOAAStation[index].resize(dataCount);

    k = 0;
    for(j=0;j<DataList.length();j++)
    {
        for(i=1;i<DataList[j].length();i++)
        {
            TempData = DataList[j].value(i);
            TimeSnap = TempData.split(",");
            DateS    = TimeSnap.value(0);
            YearS    = DateS.mid(0,4);
            MonthS   = DateS.mid(5,2);
            DayS     = DateS.mid(8,2);
            HourMinS = DateS.mid(11,5);
            HourS    = HourMinS.mid(0,2);
            MinS     = HourMinS.mid(3,2);
            WLS      = TimeSnap.value(1);
            Output=Output+YearS+":"+MonthS+":"+
                   DayS+":"+HourS+":"+MinS+":"+WLS+";";
            TempData = YearS+"/"+MonthS+"/"+DayS;
            this->CurrentNOAAStation[index][k].Date.setDate(YearS.toInt(),MonthS.toInt(),DayS.toInt());
            this->CurrentNOAAStation[index][k].Time = QTime(HourS.toInt(),MinS.toInt(),0);
            this->CurrentNOAAStation[index][k].value = WLS.toDouble();
            k = k + 1;
        }
    }
    Output = Output+"'";

    return Output;
}
