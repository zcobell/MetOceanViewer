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

void noaa::ReadNOAAResponse(QNetworkReply *reply, int index, int index2)
{
    QByteArray Data;

    //Catch some errors during the download
    if(reply->error()!=0)
    {
        QMessageBox::information(0,"ERROR","ERROR: "+reply->errorString());
        reply->deleteLater();
        return;
    }

    //Read the data received from NOAA server
    Data=reply->readAll();

    //Save the data into an array and increment the counter
    this->NOAAWebData[index2][index] = Data;

    //Delete this response
    reply->deleteLater();

    return;
}
