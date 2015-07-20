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

#include <MetOceanViewer.h>
#include <ui_MetOceanViewer_main.h>
#include <timeseries.h>

//-------------------------------------------//
//Determines the start and end time in an
//IMEDS variable
//-------------------------------------------//
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
//-------------------------------------------//


//-------------------------------------------//
//Determines the start and end time in a IMEDS variable
//-------------------------------------------//
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
//-------------------------------------------//


//-------------------------------------------//
//Reset the data range shown the selection boxes
//-------------------------------------------//
void MainWindow::UpdateTimeseriesDateRange(IMEDS MyIMEDS)
{
    QDateTime MyMin,MyMax;

    getGlobalStartEndTime(MyIMEDS, MyMin, MyMax);

    if(MyMin<IMEDSMinDate)
        IMEDSMinDate = MyMin;

    if(MyMax>IMEDSMaxDate)
        IMEDSMaxDate = MyMax;

    if(ui->check_TimeseriesAllData->isChecked() && ui->date_TimeseriesStartDate->dateTime() < IMEDSMinDate )
        ui->date_TimeseriesStartDate->setDateTime(IMEDSMinDate);

    if(ui->check_TimeseriesAllData->isChecked() && ui->date_TimeseriesEndDate->dateTime() > IMEDSMaxDate )
        ui->date_TimeseriesEndDate->setDateTime(IMEDSMaxDate);

    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Format the IMEDS data string into something
//we can use in Javascript
//-------------------------------------------//
QString MainWindow::FormatTimeseriesString(IMEDS MyStation,int index, double unitConvert)
{
    QString Response,TempString;
    QDateTime StartData,EndData;
    double value;

    StartData = ui->date_TimeseriesStartDate->dateTime();
    EndData = ui->date_TimeseriesEndDate->dateTime();

    Response = "";

    for(int j=0;j<MyStation.station[index].NumSnaps;j++)
    {
        if(ui->check_TimeseriesAllData->isChecked())
        {
            TempString = "";
            TempString = MyStation.station[index].date[j].toString("yyyy:MM:dd:hh:mm");
            value = MyStation.station[index].data[j]*unitConvert;
            TempString = TempString+":"+QString::number(value);
            Response=Response+TempString+";";
        }
        else
        {
            if(MyStation.station[index].date[j] > StartData &&
                 MyStation.station[index].date[j] < EndData)
            {
                TempString = "";
                TempString = MyStation.station[index].date[j].toString("yyyy:MM:dd:hh:mm");
                value = MyStation.station[index].data[j]*unitConvert;
                TempString = TempString+":"+QString::number(value);
                Response=Response+TempString+";";
            }
        }
    }
    return Response;
}


//-------------------------------------------//
//Check if two files have the same station
//locations inside
//-------------------------------------------//
int MainWindow::CheckStationLocationsTimeseries(IMEDS Control, IMEDS Test)
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
//-------------------------------------------//
