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
int noaa::getDataBounds(double &ymin, double &ymax)
{
    int i,j;

    ymin =  999999999.0;
    ymax = -999999999.0;

    for(i=0;i<this->CurrentNOAAStation.length();i++)
    {
        for(j=0;j<this->CurrentNOAAStation[i].length();j++)
        {
            if(this->CurrentNOAAStation[i][j].value<ymin)
                ymin = this->CurrentNOAAStation[i][j].value;
            if(this->CurrentNOAAStation[i][j].value>ymax)
                ymax = this->CurrentNOAAStation[i][j].value;
        }
    }
    return 0;
}
