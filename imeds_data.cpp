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
//  File: imeds_data.cpp
//
//------------------------------------------------------------------------------

#include "imeds.h"

QString ADCIMEDSFile=QString(), OBSIMEDSFile=QString();
IMEDS ADCIMEDS,OBSIMEDS;

IMEDS readIMEDS(QString filename)
{

    //Variables
    IMEDS Output;
    QString TempString;
    QString year;
    QString month;
    QString day;
    QString hour;
    QString minute;
    QString second;
    QString DateString;
    QStringList TempList;
    QVector<QString> FileData;
    QDate TempDate;
    QTime TempTime;
    int nLine;
    int nStation;
    int i;
    int j;
    int k;
    double value;
    QFile MyFile(filename);

    //Default to an unsuccessful read
    Output.success = false;

    //Check if we can open the file
    if(!MyFile.open(QIODevice::ReadOnly|QIODevice::Text))
    {
        QMessageBox::information(NULL,"ERROR","ERROR:"+MyFile.errorString());
        return Output;
    }

    //Read the header to output variable
    try
    {
        Output.header1 = MyFile.readLine().simplified();
        Output.header2 = MyFile.readLine().simplified();
        Output.header3 = MyFile.readLine().simplified();

        //Read in the data portion of the file
        nLine = 0;
        while(!MyFile.atEnd())
        {
           nLine = nLine + 1;
           FileData.resize(nLine);
           FileData[nLine-1] = MyFile.readLine().simplified();
        }
        MyFile.close();

        //Count the number of stations in the file
        nStation = 0;
        for(i=0;i<nLine;i++)
        {
            TempList = FileData[i].split(" ");
            if(TempList.length()==3)
            {
                nStation = nStation + 1;
            }
        }

        //Size the station vector and read the datasets
        Output.station.resize(nStation);
        Output.nstations = nStation;
        j=-1;
        for(i=0;i<nLine;i++)
        {
            TempList = FileData[i].split(" ");
            if(TempList.length()==3)
            {
                j=j+1;
                Output.station[j].longitude = TempList[2].toDouble();
                Output.station[j].latitude = TempList[1].toDouble();
                Output.station[j].StationName = TempList[0];
                Output.station[j].StationIndex = j;
            }
            else
            {
                Output.station[j].NumSnaps = Output.station[j].NumSnaps + 1;
            }

        }
        //Preallocate arrays for data and dates

        for(i=0;i<nStation;i++)
        {
            Output.station[i].data.resize(Output.station[i].NumSnaps);
            Output.station[i].date.resize(Output.station[i].NumSnaps);
        }

        //Now, loop over the data section and save to vectors
        j=-1;k=-1;
        for(i=0;i<nLine;i++)
        {
            TempList = FileData[i].split(" ");
            if(TempList.length()==3)
            {
                j=j+1;
                k=-1;
            }
            else if(TempList.length()==6)
            {
                k=k+1;
                year = TempList.value(0);
                month = TempList.value(1);
                day = TempList.value(2);
                hour = TempList.value(3);
                minute = TempList.value(4);
                second = "0";
                value = TempList.value(5).toDouble();
                Output.station[j].date[k] =
                        QDateTime(QDate(year.toInt(),month.toInt(),day.toInt()),
                                  QTime(hour.toInt(),minute.toInt(),second.toInt()));
                Output.station[j].data[k] = value;
            }
            else if(TempList.length()==7)
            {
                k=k+1;
                year = TempList.value(0);
                month = TempList.value(1);
                day = TempList.value(2);
                hour = TempList.value(3);
                minute = TempList.value(4);
                second = TempList.value(5);
                value = TempList.value(6).toDouble();
                Output.station[j].date[k] =
                        QDateTime(QDate(year.toInt(),month.toInt(),day.toInt()),
                                  QTime(hour.toInt(),minute.toInt(),second.toInt()));
                Output.station[j].data[k] = value;
            }
            else
            {
                QMessageBox::information(NULL,"ERROR","Invalid dataset");
                Output.success = false;
                return Output;
            }

        }
        Output.success = true;
        return Output;
    }
    catch(...)
    {
        Output.success = false;
        return Output;
    }

}
