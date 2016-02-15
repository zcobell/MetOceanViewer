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

int usgs::setUSGSBeenPlotted(bool input)
{
    this->USGSBeenPlotted = input;
    return 0;
}

QString usgs::getClickedUSGSStation()
{
    QString JunkString;
    double JunkDouble1,JunkDouble2;
    return this->getMarkerSelection(JunkString,JunkDouble1,JunkDouble2);
}

QString usgs::getLoadedUSGSStation()
{
    return this->USGSMarkerID;
}

QString usgs::getMarkerSelection(QString &name, double &longitude, double &latitude)
{
    QVariant eval = QVariant();
    map->page()->runJavaScript("returnStationID()",[&eval](const QVariant &v){eval = v;});
    while(eval.isNull())
        delayM(5);
    QStringList evalList = eval.toString().split(";");

    //...Station information
    name = evalList.value(1).simplified();
    longitude = evalList.value(2).toDouble();
    latitude = evalList.value(3).toDouble();

    return evalList.value(0).mid(4);
}

int usgs::setMarkerSelection()
{
    this->USGSMarkerID = getMarkerSelection(this->CurrentUSGSStationName,
                                            this->CurrentUSGSLon,this->CurrentUSGSLat);
    return 0;
}

bool usgs::getUSGSBeenPlotted()
{
    return this->USGSBeenPlotted;
}

QString usgs::getUSGSErrorString()
{
    return this->USGSErrorString;
}
