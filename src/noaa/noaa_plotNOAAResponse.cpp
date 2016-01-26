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

int noaa::PlotNOAAResponse(QVector<QString> &javascript)
{
    QVector<QString> NOAAData,Error;
    QString tempJava;
    int i;

    for(i=0;i<CurrentNOAAStation.length();i++)
        CurrentNOAAStation[i].clear();
    CurrentNOAAStation.clear();

    CurrentNOAAStation.resize(NOAAWebData.length());

    NOAAData.resize(NOAAWebData.length());
    Error.resize(NOAAWebData.length());
    javascript.resize(NOAAWebData.length()+1);
    for(i=0;i<NOAAWebData.length();i++)
    {
        NOAAData[i] = this->FormatNOAAResponse(this->NOAAWebData[i],Error[i],i);
        Error[i].remove(QRegExp("[\\n\\t\\r]"));
        tempJava="AddDataSeries("+QString::number(i)+","+NOAAData[i]+",'"+Error[i]+"')";
        javascript[i] = tempJava;
    }
    javascript[NOAAWebData.length()] = "PlotTimeseries()";
    return 0;
}
