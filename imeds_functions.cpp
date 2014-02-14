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

//Adds the dual plot data for a time series validation station to the map
void MainWindow::addIMEDSandADCIRCMarker(IMEDS Observation, IMEDS ADCIRC)
{
    QVector<QString> MarkerCalls;
    QString TempString,DateString,RangeString;
    QDateTime TempStart1,TempStart2,TempEnd1,TempEnd2,StartData,EndData;
    int i,j;
    bool hasData;
    double ymin,ymax;

    ui->imeds_map->page()->mainFrame()->evaluateJavaScript("clearMarkers()");
    MarkerCalls.resize(ADCIRC.nstations);

    if(ui->check_imedyauto->isChecked())
        RangeString="'auto'";
    else
    {
        ymin = ui->spin_imedsymin->value();
        ymax = ui->spin_imedsymax->value();
        RangeString="'"+QString::number(ymin)+":"+QString::number(ymax)+"'";
    }

    for(i=0;i<ADCIRC.nstations;i++)
    {

        if(ui->check_imedsalldata->isChecked())
        {
            getStartEndTime(Observation,i,TempStart1,TempEnd1);
            getStartEndTime(ADCIRC,i,TempStart2,TempEnd2);
            if(TempStart1<TempStart2)
                StartData = TempStart1;
            else
                StartData = TempStart2;
            if(TempEnd1>TempEnd2)
                EndData = TempEnd1;
            else
                EndData = TempEnd2;

            DateString = "'"+StartData.toString("yyyy:MM:dd::hh:mm:ss")+"','"+
                    EndData.toString("yyyy:MM:dd::hh:mm:ss")+"'";
        }
        else
        {
            StartData.setDate(ui->date_imedsstart->date());
            EndData.setDate(ui->date_imedsend->date());
            StartData.setTime(QTime(0,0,0));
            EndData.setTime(QTime(23,59,59));
            DateString = "'"+StartData.toString("yyyy:MM:dd::hh:mm:ss")+"','"+
                    EndData.toString("yyyy:MM:dd::hh:mm:ss")+"'";
        }

        MarkerCalls[i]="addValidationStation(1,1,"+QString::number(ADCIRC.station[i].longitude)+","
                +QString::number(ADCIRC.station[i].latitude)+","
                +QString::number(ADCIRC.station[i].StationIndex)+",'"
                +ADCIRC.station[i].StationName+"',"
                +RangeString+",'";
        TempString = QString();
        hasData=false;
        for(j=0;j<ADCIRC.station[i].NumSnaps;j++)
        {
            if(ADCIRC.station[i].date[j].operator >(StartData) &&
                 ADCIRC.station[i].date[j].operator <(EndData))
            {
                if(ADCIRC.station[i].data[j]>-999)
                    hasData=true;
                TempString = ADCIRC.station[i].date[j].toString("yyyy:MM:dd:hh:mm");
                TempString = TempString+":"+QString::number(ADCIRC.station[i].data[j]);
                MarkerCalls[i]=MarkerCalls[i]+TempString+";";
            }
        }
        MarkerCalls[i]=MarkerCalls[i]+"','";
        TempString = QString();
        for(j=0;j<Observation.station[i].NumSnaps;j++)
        {
            if(Observation.station[i].date[j].operator >(StartData) &&
                 Observation.station[i].date[j].operator <(EndData))
            {
                if(Observation.station[i].data[j]>-999)
                    hasData=true;
                TempString = Observation.station[i].date[j].toString("yyyy:MM:dd:hh:mm");
                TempString = TempString+":"+QString::number(Observation.station[i].data[j]);
                MarkerCalls[i]=MarkerCalls[i]+TempString+";";
            }
        }

        MarkerCalls[i]=MarkerCalls[i]+"')";
        if(ui->check_ignoreNodata->isChecked() && hasData==false)
        {
            hasData=false;
        }
        else
        {
            ui->imeds_map->page()->mainFrame()->evaluateJavaScript(MarkerCalls[i]);
        }

        double complete = (static_cast<double>(i) / (static_cast<double>(ADCIRC.nstations)-1))*100.0;
        ui->progress_IMEDS->setValue(static_cast<int>(complete));
    }
    ui->subtab_IMEDS->setCurrentIndex(1);
    delay(1);
    ui->imeds_map->page()->mainFrame()->evaluateJavaScript("fitMarkers()");
    ui->progress_IMEDS->setValue(0);

    return;

}

//Adds a single ADCIRC measurement to the plot
void MainWindow::addADCIRCMarker(IMEDS ADCIRC)
{
    QVector<QString> MarkerCalls;
    QString TempString,DateString,RangeString;
    int i,j;
    double ymin,ymax;
    bool hasData;
    QDateTime StartData,EndData;

    if(ui->check_imedyauto->isChecked())
        RangeString="'auto'";
    else
    {
        ymin = ui->spin_imedsymin->value();
        ymax = ui->spin_imedsymax->value();
        RangeString="'"+QString::number(ymin)+":"+QString::number(ymax)+"'";
    }

    ui->imeds_map->page()->mainFrame()->evaluateJavaScript("clearMarkers()");
    MarkerCalls.resize(ADCIRC.nstations);
    for(i=0;i<ADCIRC.nstations;i++)
    {
        if(ui->check_imedsalldata->isChecked())
        {
            getStartEndTime(ADCIRC,i,StartData,EndData);
            DateString = "'"+StartData.toString("yyyy:MM:dd::hh:mm:ss")+"','"+
                    EndData.toString("yyyy:MM:dd::hh:mm:ss")+"'";
        }
        else
        {
            StartData.setDate(ui->date_imedsstart->date());
            EndData.setDate(ui->date_imedsend->date());
            StartData.setTime(QTime(0,0,0));
            EndData.setTime(QTime(23,59,59));
            DateString = "'"+StartData.toString("yyyy:MM:dd::hh:mm:ss")+"','"+
                    EndData.toString("yyyy:MM:dd::hh:mm:ss")+"'";
        }

        MarkerCalls[i]="addValidationStation(1,0,"+QString::number(ADCIRC.station[i].longitude)+","
                +QString::number(ADCIRC.station[i].latitude)+","
                +QString::number(ADCIRC.station[i].StationIndex)+",'"
                +ADCIRC.station[i].StationName+"',"+RangeString+",'";
        TempString = QString();
        hasData=false;
        for(j=0;j<ADCIRC.station[i].NumSnaps;j++)
        {
            if(ADCIRC.station[i].date[j].operator >(StartData) &&
                 ADCIRC.station[i].date[j].operator <(EndData))
            {
                if(ADCIRC.station[i].data[j]>-999)
                    hasData=true;
                TempString = ADCIRC.station[i].date[j].toString("yyyy:MM:dd:hh:mm");
                TempString = TempString+":"+QString::number(ADCIRC.station[i].data[j]);
                MarkerCalls[i]=MarkerCalls[i]+TempString+";";
            }
        }
        MarkerCalls[i]=MarkerCalls[i]+"','DmyData')";

        if(ui->check_ignoreNodata->isChecked() && hasData==false)
        {
            hasData=false;
        }
        else
        {
            ui->imeds_map->page()->mainFrame()->evaluateJavaScript(MarkerCalls[i]);
        }

        double complete = (static_cast<double>(i) / (static_cast<double>(ADCIRC.nstations)-1))*100.0;
        ui->progress_IMEDS->setValue(static_cast<int>(complete));
    }
    ui->subtab_IMEDS->setCurrentIndex(1);
    delay(1);
    ui->imeds_map->page()->mainFrame()->evaluateJavaScript("fitMarkers()");
    ui->progress_IMEDS->setValue(0);

    return;
}

//Adds a single IMEDS station to the plot
void MainWindow::addIMEDSMarker(IMEDS Observation)
{
    QVector<QString> MarkerCalls;
    QString TempString,DateString,RangeString;
    QDateTime StartData,EndData;
    int i,j;
    double ymin,ymax;
    bool hasData;

    if(ui->check_imedyauto->isChecked())
        RangeString="'auto'";
    else
    {
        ymin = ui->spin_imedsymin->value();
        ymax = ui->spin_imedsymax->value();
        RangeString="'"+QString::number(ymin)+":"+QString::number(ymax)+"'";
    }

    ui->imeds_map->page()->mainFrame()->evaluateJavaScript("clearMarkers()");
    MarkerCalls.resize(Observation.nstations);
    for(i=0;i<Observation.nstations;i++)
    {
        if(ui->check_imedsalldata->isChecked())
        {
            getStartEndTime(Observation,i,StartData,EndData);
            DateString = "'"+StartData.toString("yyyy:MM:dd::hh:mm:ss")+"','"+
                    EndData.toString("yyyy:MM:dd::hh:mm:ss")+"'";
        }
        else
        {
            StartData.setDate(ui->date_imedsstart->date());
            EndData.setDate(ui->date_imedsend->date());
            StartData.setTime(QTime(0,0,0));
            EndData.setTime(QTime(23,59,59));
            DateString = "'"+StartData.toString("yyyy:MM:dd::hh:mm:ss")+"','"+
                    EndData.toString("yyyy:MM:dd::hh:mm:ss")+"'";
        }
        MarkerCalls[i]="addValidationStation(0,1,"+QString::number(Observation.station[i].longitude)+","
                +QString::number(Observation.station[i].latitude)+","
                +QString::number(Observation.station[i].StationIndex)+",'"
                +Observation.station[i].StationName+"',"+RangeString+",'DmyData','";
        TempString = QString();
        hasData = false;
        for(j=0;j<Observation.station[i].NumSnaps;j++)
        {
            if(Observation.station[i].date[j].operator >(StartData) &&
                 Observation.station[i].date[j].operator <(EndData))
            {
                if(Observation.station[i].data[j]>-999)
                    hasData=true;
                TempString = Observation.station[i].date[j].toString("yyyy:MM:dd:hh:mm");
                TempString = TempString+":"+QString::number(Observation.station[i].data[j]);
                MarkerCalls[i]=MarkerCalls[i]+TempString+";";
            }
        }
        MarkerCalls[i]=MarkerCalls[i]+"')";

        if(ui->check_ignoreNodata->isChecked() && hasData==false)
        {
            hasData=false;
        }
        else
        {
            ui->imeds_map->page()->mainFrame()->evaluateJavaScript(MarkerCalls[i]);
        }

        double complete = (static_cast<double>(i) / (static_cast<double>(Observation.nstations)-1))*100.0;
        ui->progress_IMEDS->setValue(static_cast<int>(complete));

    }
    ui->subtab_IMEDS->setCurrentIndex(1);
    delay(1);
    ui->imeds_map->page()->mainFrame()->evaluateJavaScript("fitMarkers()");
    ui->progress_IMEDS->setValue(0);

    return;
}

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
