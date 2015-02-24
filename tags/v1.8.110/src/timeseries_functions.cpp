//---- LGPL --------------------------------------------------------------------
//
// Copyright (C)  ARCADIS, 2011-2015.
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
//  File: timeseries_functions.cpp
//
//------------------------------------------------------------------------------

#include <ADCvalidator.h>
#include <ui_ADCvalidator_main.h>
#include <timeseries.h>

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
    QDateTime MyMin,MyMax;

    getGlobalStartEndTime(MyIMEDS, MyMin, MyMax);

    if(MyMin<IMEDSMinDate)
        IMEDSMinDate = MyMin;

    if(MyMax>IMEDSMaxDate)
        IMEDSMaxDate = MyMax;

    if(ui->check_imedsalldata->isChecked() || ui->date_imedsstart->dateTime() < IMEDSMinDate )
        ui->date_imedsstart->setDateTime(IMEDSMinDate);

    if(ui->check_imedsalldata->isChecked() || ui->date_imedsend->dateTime() > IMEDSMaxDate )
        ui->date_imedsend->setDateTime(IMEDSMaxDate);

    return;
}

//Format the IMEDS data string into something we can use in Javascript
QString MainWindow::FormatIMEDSString(IMEDS MyStation,int index, double unitConvert)
{
    QString Response,TempString;
    QDateTime StartData,EndData;
    int DecPlaces;
    double value,value2,multiplier;

    StartData = ui->date_imedsstart->dateTime();
    EndData = ui->date_imedsend->dateTime();
    DecPlaces = ui->spin_imedsdecimalplaces->value();
    multiplier = qPow(10.0,static_cast<double>(DecPlaces));

    Response = "";
    for(int j=0;j<MyStation.station[index].NumSnaps;j++)
    {
        if(MyStation.station[index].date[j] > StartData &&
             MyStation.station[index].date[j] < EndData)
        {
            TempString = "";
            TempString = MyStation.station[index].date[j].toString("yyyy:MM:dd:hh:mm");
            value = MyStation.station[index].data[j]*unitConvert;
            value2 = qRound(value*multiplier)/multiplier;
            TempString = TempString+":"+QString::number(value2);
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
