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
#include <hwm.h>
#include <general_functions.h>

int hwm::processHWMData()
{
    QString unitString;
    double c0,c1,c2,c3,c4,c5,c6;
    int unit, ierr;
    bool ThroughZero;

    ThroughZero = this->forceThroughZeroCheckbox->isChecked();

    ierr = this->readHWMData();
    if(ierr!=0)
    {
        this->hwmErrorString = "Could not read the high water mark file.";
        return -1;
    }

    ierr = this->computeLinearRegression();
    if(ierr!=0)
    {
        this->hwmErrorString = "Could not calculate the regression function.";
        return -1;
    }

    unit = this->unitComboBox->currentIndex();
    if(unit==0)
        unitString = "'ft'";
    else
        unitString = "'m'";

    this->map->page()->runJavaScript("clearMarkers()");

    //Sanity check on classes
    if(this->manualClassificationCheckbox->isChecked())
    {
        c0 = this->classes[0];
        c1 = this->classes[1];
        c2 = this->classes[2];
        c3 = this->classes[3];
        c4 = this->classes[4];
        c5 = this->classes[5];
        c6 = this->classes[6];

        if(c1<=c0)
        {
            this->hwmErrorString = "Your classifications are invalid.";
            return -1;
        }
        else if(c2<=c1)
        {
            this->hwmErrorString = "Your classifications are invalid.";
            return -1;
        }
        else if(c3<=c2)
        {
            this->hwmErrorString = "Your classifications are invalid.";
            return -1;
        }
        else if(c4<=c3)
        {
            this->hwmErrorString = "Your classifications are invalid.";
            return -1;
        }
        else if(c5<=c4)
        {
            this->hwmErrorString = "Your classifications are invalid.";
            return -1;
        }
        else if(c6<=c5)
        {
            this->hwmErrorString = "Your classifications are invalid.";
            return -1;
        }
    }

    ierr = this->plotHWMMap();

    ierr = this->plotRegression();

    return 0;
}
