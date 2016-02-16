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

int usgs::readUSGSDataFinished(QNetworkReply *reply)
{
    int ierr;

    if(reply->error()!=0)
    {
        this->USGSErrorString = reply->errorString();
        return ERR_USGS_SERVERREADERROR;
    }

    //Read the data that was received
    QByteArray RawUSGSData = reply->readAll();

    //Put the data into an array with all variables
    if(this->USGSdataMethod==0||this->USGSdataMethod==1)
        ierr = this->formatUSGSInstantResponse(RawUSGSData);
    else
        ierr = this->formatUSGSDailyResponse(RawUSGSData);
    if(ierr!=0)
        return ERR_USGS_FORMATTING;

    this->USGSDataReady = true;

    //Delete the QNetworkReply object off the heap
    reply->deleteLater();

    return 0;
}
