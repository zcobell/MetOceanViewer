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

int usgs::plotNewUSGSStation()
{
    int i,ierr;

    //...Check the data type
    if(instantButton->isChecked())
        this->USGSdataMethod = 1;
    else if(dailyButton->isChecked())
        this->USGSdataMethod = 2;
    else
        this->USGSdataMethod = 0;

    //...Set status bar
    statusBar->showMessage("Downloading data from USGS...");

    //...Wipe out the combo box
    productBox->clear();

    //...Retrieve info from google maps
    this->setMarkerSelection();

    //...Sanity Check
    if(this->USGSMarkerID=="none")
    {
        this->USGSErrorString = "No station has been selected.";
        return -1;
    }

    //...Get the time period for the data
    this->requestEndDate = endDateEdit->dateTime();
    this->requestStartDate = startDateEdit->dateTime();

    //...Grab the data from the server
    ierr = this->fetchUSGSData();
    if(ierr!=0)
        return -1;

    //...Update the combo box
    for(i=0;i<this->Parameters.length();i++)
        productBox->addItem(this->Parameters[i]);
    productBox->setCurrentIndex(0);
    this->ProductName = productBox->currentText();

    //...Plot the first series
    ierr = this->plotUSGS();
    if(ierr!=0)
    {
        this->USGSErrorString = "No data available for this station";
        return -1;
    }

    //...Restore the status bar
    statusBar->clearMessage();

    return 0;
}

int usgs::replotCurrentUSGSStation(int index)
{
    int ierr;
    if(this->USGSDataReady)
    {
        this->ProductIndex = index;
        this->ProductName = productBox->currentText();
        ierr = this->plotUSGS();
        statusBar->clearMessage();
        if(ierr!=0)
        {
            this->USGSErrorString = "No data available for this selection.";
            return ierr;
        }
        return 0;
    }
    return 0;
}

