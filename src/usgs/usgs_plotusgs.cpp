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

int usgs::plotUSGS(QString &javascript)
{
    QString PlotData;

    PlotData="'";

    //Put the data into a plotting object
    this->USGSPlot.resize(this->CurrentUSGSStation[this->ProductIndex].Date.length());
    for(int i=0;i<this->CurrentUSGSStation[this->ProductIndex].Date.length();i++)
    {
        this->USGSPlot[i].Date = this->CurrentUSGSStation[this->ProductIndex].Date[i].date();
        this->USGSPlot[i].Time = this->CurrentUSGSStation[this->ProductIndex].Date[i].time();
        this->USGSPlot[i].value = this->CurrentUSGSStation[this->ProductIndex].Data[i];
        PlotData=PlotData+this->USGSPlot[i].Date.toString("yyyy:MM:dd")+":"+
                this->USGSPlot[i].Time.toString("hh:mm")+":"+QString::number(this->USGSPlot[i].value)+";";
    }
    PlotData = PlotData+"'";
    this->USGSErrorString = "none";
    javascript="drawUSGSData("+PlotData+",'"+this->ProductName+"','"+this->ProductName.split(",").value(0)+"',"+
            QString::number(CurrentUSGSStation[this->ProductIndex].NumDataPoints)+",'"+this->USGSErrorString+"')";

    this->USGSBeenPlotted = true;

    return 0;
}

