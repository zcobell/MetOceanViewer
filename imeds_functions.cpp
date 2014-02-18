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
//  File: imeds_functions.cpp
//
//------------------------------------------------------------------------------

#include "ADCvalidator.h"
#include "ui_ADCvalidator_main.h"
#include "imeds.h"

//Determines the start and end time in an IMEDS variable
void MainWindow::getStartEndTime(IMEDS Input, int index, QDateTime &Start, QDateTime &End)
{
    Start.setDate(QDate(2200,1,1));
    End.setDate(QDate(1820,1,1));
    Start.setTime(QTime(0,0,0));
    End.setTime(QTime(0,0,0));
    for(int i=0;i<Input.station[index].NumSnaps;i++)
    {
        if(Start.operator >(Input.station[index].date[i]))
        {
            Start = Input.station[index].date[i];
        }
        if(End.operator <(Input.station[index].date[i]))
        {
            End = Input.station[index].date[i];
        }
    }
    return;
}

//Determines the start and end time in a IMEDS vector
void MainWindow::getGlobalStartEndTime(IMEDS Input, QDateTime &Start, QDateTime &End)
{
    Start.setDate(QDate(2200,1,1));
    End.setDate(QDate(1820,1,1));
    Start.setTime(QTime(0,0,0));
    End.setTime(QTime(0,0,0));
    for(int index=0;index<Input.nstations;index++)
    {
        for(int i=0;i<Input.station[index].NumSnaps;i++)
        {
            if(Start.operator >(Input.station[index].date[i]))
            {
                Start = Input.station[index].date[i];
            }
            if(End.operator <(Input.station[index].date[i]))
            {
                End = Input.station[index].date[i];
            }
        }
    }
    return;
}

//Reset the IMEDS data range shown the selection boxes
void MainWindow::UpdateIMEDSDateRange(IMEDS MyIMEDS)
{
    QDateTime CurrentMin,CurrentMax;
    QDateTime MyMin,MyMax;

    CurrentMin = IMEDSMinDate;
    CurrentMax = IMEDSMaxDate;

    getGlobalStartEndTime(MyIMEDS, MyMin, MyMax);

    if(MyMin.operator <(CurrentMin))
        CurrentMin = MyMin;

    if(MyMax.operator >(CurrentMax))
        CurrentMax = MyMax;

    ui->date_imedsstart->setDateTime(CurrentMin);
    ui->date_imedsend->setDateTime(CurrentMax);
    ui->date_imedsstart->setMinimumDateTime(CurrentMin);
    ui->date_imedsstart->setMaximumDateTime(CurrentMax);
    ui->date_imedsend->setMinimumDateTime(CurrentMin);
    ui->date_imedsend->setMaximumDateTime(CurrentMax);

    return;
}

//Format the IMEDS data string into something we can use in Javascript
QString MainWindow::FormatIMEDSString(IMEDS MyStation,int index)
{
    QString Response,TempString;
    QDateTime StartData,EndData;

    StartData = ui->date_imedsstart->dateTime();
    EndData = ui->date_imedsend->dateTime();

    Response = "";
    for(int j=0;j<MyStation.station[index].NumSnaps;j++)
    {
        if(MyStation.station[index].date[j].operator >(StartData) &&
             MyStation.station[index].date[j].operator <(EndData))
        {
            TempString = "";
            TempString = MyStation.station[index].date[j].toString("yyyy:MM:dd:hh:mm");
            TempString = TempString+":"+QString::number(MyStation.station[index].data[j]);
            Response=Response+TempString+";";
        }
    }
    return Response;
}

//Check if two IMEDS files have the same station locations inside
int MainWindow::CheckStationLocationsIMEDS(IMEDS Control, IMEDS Test)
{
    if(Control.nstations!=Test.nstations)
    {
        return 1;
    }
    for(int i=0;i<Control.nstations;i++)
    {
        if(Control.station[i].latitude!=Test.station[i].latitude)
            return 1;
        if(Control.station[i].longitude!=Test.station[i].longitude)
            return 1;
    }
    return 0;
}
