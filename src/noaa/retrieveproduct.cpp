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

int noaa::retrieveProduct(int type, QString &Product, QString &Product2)
{
    Product2 = "null";
    int index = this->ProductIndex;
    if(type==1)
    {
        switch(index)
        {
            case(0):Product = "6 Min Observed Water Level vs. Predicted"; break;
            case(1):Product = "6 Min Observed Water Level"; break;
            case(2):Product = "Hourly Observed Water Level"; break;
            case(3):Product = "Predicted Water Level"; break;
            case(4):Product = "Air Temperature"; break;
            case(5):Product = "Water Temperature"; break;
            case(6):Product = "Wind Speed"; break;
            case(7):Product = "Relative Humidity"; break;
            case(8):Product = "Air Pressure"; break;
        }
    }
    else if(type==2)
    {
        switch(index)
        {
            case(0):Product = "water_level"; Product2 = "predictions"; break;
            case(1):Product = "water_level"; break;
            case(2):Product = "hourly_height"; break;
            case(3):Product = "predictions"; break;
            case(4):Product = "air_temperature"; break;
            case(5):Product = "water_temperature"; break;
            case(6):Product = "wind"; break;
            case(7):Product = "humidity"; break;
            case(8):Product = "air_pressure"; break;
        }
    }
    return 0;
}
