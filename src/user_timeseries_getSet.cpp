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

int user_timeseries::getCurrentMarkerID()
{
    return this->markerID;
}

int user_timeseries::setMarkerID()
{
    this->markerID = this->getMarkerIDFromMap();
    return 0;
}

int user_timeseries::getClickedMarkerID()
{
    return this->getMarkerIDFromMap();
}

int user_timeseries::getMarkerIDFromMap()
{
    QVariant eval = QVariant();
    this->map->page()->runJavaScript("getMarker()",[&eval](const QVariant &v){eval = v;});
    while(eval.isNull())
        delayM(5);
    return eval.toInt();
}

QString user_timeseries::getErrorString()
{
    return this->errorString;
}
