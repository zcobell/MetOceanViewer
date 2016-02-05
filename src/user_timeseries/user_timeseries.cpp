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
#include "user_timeseries.h"

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


int user_timeseries::processData()
{

    return 0;
}


int user_timeseries::plotData()
{

    return 0;
}
