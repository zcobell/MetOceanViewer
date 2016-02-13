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

int noaa::plotNOAAStation()
{

    //...get the latest station
    int ierr = this->setNOAAStation();

    //...Grab the options from the UI
    this->StartDate = this->startDateEdit->dateTime();
    this->StartDate.setTime(QTime(0,0,0));
    this->EndDate = this->endDateEdit->dateTime();
    this->EndDate = this->EndDate.addDays(1);
    this->EndDate.setTime(QTime(0,0,0));
    this->Units = this->noaaUnits->currentText();
    this->Datum = this->noaaDatum->currentText();
    this->ProductIndex = this->noaaProduct->currentIndex();

    //Update status
    statusBar->showMessage("Downloading data from NOAA...",0);

    //...Generate the javascript calls in this array
    ierr = this->fetchNOAAData();

    //...Update the status bar
    statusBar->showMessage("Plotting the data from NOAA...");

    //...Generate prep the data for plotting
    ierr = this->prepNOAAResponse();

    //...Check for valid data
    if(this->CurrentNOAAStation[0].length()<5)
    {
        this->NOAAErrorString = this->ErrorString[0];
        return -1;
    }

    //...Plot the chart
    ierr = this->plotChart();

    statusBar->clearMessage();

    return 0;

}
