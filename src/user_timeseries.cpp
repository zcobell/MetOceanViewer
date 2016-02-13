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
#include <user_timeseries.h>
#include <general_functions.h>
#include <mov_flags.h>

user_timeseries::user_timeseries(QTableWidget *inTable, QCheckBox *inXAxisCheck,
                                 QCheckBox *inYAxisCheck, QDateEdit *inStartDate,
                                 QDateEdit *inEndDate, QDoubleSpinBox *inYMinEdit,
                                 QDoubleSpinBox *inYMaxEdit, QLineEdit *inPlotTitle,
                                 QLineEdit *inXLabelEdit, QLineEdit *inYLabelEdit,
                                 QWebEngineView *inMap, QChartView *inChart,
                                 QObject *parent) : QObject(parent)
{
    table      = inTable;
    xAxisCheck = inXAxisCheck;
    yAxisCheck = inYAxisCheck;
    startDate  = inStartDate;
    endDate    = inEndDate;
    yMaxEdit   = inYMaxEdit;
    yMinEdit   = inYMinEdit;
    plotTitle  = inPlotTitle;
    xLabelEdit = inXLabelEdit;
    yLabelEdit = inYLabelEdit;
    map        = inMap;
    chart      = inChart;
}

int user_timeseries::getDataBounds(double &ymin, double &ymax, QDateTime &minDate, QDateTime &maxDate)
{
    int i,j;
    double unitConversion,addY;

    ymin = DBL_MAX;
    ymax = DBL_MIN;
    minDate = QDateTime(QDate(3000,1,1),QTime(0,0,0));
    maxDate = QDateTime(QDate(1500,1,1),QTime(0,0,0));

    for(i=0;i<fileDataUnique.length();i++)
    {
        unitConversion = table->item(i,3)->text().toDouble();
        addY = table->item(i,5)->text().toDouble();
        for(j=0;j<fileDataUnique[i].station[markerID].NumSnaps;j++)
        {
            if(fileDataUnique[i].station[markerID].data[j]*unitConversion+addY<ymin && fileDataUnique[i].station[markerID].data[j]>MOV_NULL_TS)
                ymin = fileDataUnique[i].station[markerID].data[j]*unitConversion+addY;
            if(fileDataUnique[i].station[markerID].data[j]*unitConversion+addY>ymax && fileDataUnique[i].station[markerID].data[j]>MOV_NULL_TS)
                ymax = fileDataUnique[i].station[markerID].data[j]*unitConversion+addY;
            if(fileDataUnique[i].station[markerID].date[j]<minDate)
                minDate = fileDataUnique[i].station[markerID].date[j];
            if(fileDataUnique[i].station[markerID].date[j]>maxDate)
                maxDate = fileDataUnique[i].station[markerID].date[j];
        }
    }
    return 0;
}
