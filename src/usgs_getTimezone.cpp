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

int usgs::getTimezoneOffset(QString timezone)
{
    if(timezone.isNull()||timezone.isEmpty())
        return 0;
    else if(timezone=="UTC")
        return 0;
    else if(timezone=="GMT")
        return 0;
    else if(timezone=="EST")
        return -5;
    else if(timezone=="EDT")
        return -4;
    else if(timezone=="CST")
        return -6;
    else if(timezone=="CDT")
        return -5;
    else if(timezone=="MST")
        return -7;
    else if(timezone=="MDT")
        return -6;
    else if(timezone=="PST")
        return -8;
    else if(timezone=="PDT")
        return -7;
    else if(timezone=="AKST")
        return -9;
    else if(timezone=="AKDT")
        return -8;
    else if(timezone=="HST")
        return -10;
    else if(timezone=="HDT")
        return -9;
    else if(timezone=="AST")
        return -4;
    else if(timezone=="ADT")
        return -3;
    else if(timezone=="SST")
        return -11;
    else if(timezone=="SDT")
        return -10;
    else
        return -9999;
}
