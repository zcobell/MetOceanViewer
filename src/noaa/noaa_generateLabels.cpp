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

int noaa::generateLabels()
{
    int ierr;
    QString Product,Product2;

    ierr = this->retrieveProduct(1,Product,Product2);

    if(this->ProductIndex == 0)
    {
        if(this->Units=="metric")
            this->Units="m";
        else
            this->Units="ft";
        this->yLabel = "Water Level ("+this->Units+", "+this->Datum+")";
    }
    else if(this->ProductIndex == 1 || this->ProductIndex == 2 || this->ProductIndex == 3)
    {
        if(this->Units=="metric")
            this->Units="m";
        else
            this->Units="ft";
        this->yLabel = Product+" ("+Units+", "+this->Datum+")";
    }
    else if(this->ProductIndex == 6)
    {
        if(this->Units=="metric")
            this->Units="m/s";
        else
            this->Units="knots";
        this->Datum = "Stnd";
        this->yLabel = Product+" ("+this->Units+")";
    }
    else if(this->ProductIndex == 4 || this->ProductIndex == 5)
    {
        if(this->Units=="metric")
            this->Units="Celcius";
        else
            this->Units="Fahrenheit";
        this->Datum = "Stnd";
        this->yLabel = Product+" ("+this->Units+")";
    }
    else if(this->ProductIndex == 7)
    {
        this->Units = "%";
        this->Datum = "Stnd";
        this->yLabel = Product+" ("+this->Units+")";
    }
    else if(this->ProductIndex == 8)
    {
        this->Units = "mb";
        this->Datum = "Stnd";
        this->yLabel = Product+" ("+this->Units+")";
    }

    this->plotTitle = "Station "+QString::number(this->NOAAMarkerID)+": "+this->CurrentNOAAStationName;

    return 0;
}
