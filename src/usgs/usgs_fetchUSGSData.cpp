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

int usgs::fetchUSGSData()
{
    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QString endDateString1,startDateString1;
    QString endDateString2,startDateString2;
    QString RequestURL;
    QEventLoop loop;
    int ierr;

    //...Get the current marker
    this->setMarkerSelection();

    //...Format the date strings
    endDateString1 = "&endDT="+this->requestEndDate.toString("yyyy-MM-dd");
    startDateString1 = "&startDT="+this->requestStartDate.toString("yyyy-MM-dd");
    endDateString2 = "&end_date="+this->requestEndDate.toString("yyyy-MM-dd");
    startDateString2 = "&begin_date="+this->requestStartDate.toString("yyyy-MM-dd");

    //...Construct the correct request URL
    if(this->USGSdataMethod==0)
        RequestURL = "http://nwis.waterdata.usgs.gov/nwis/uv?format=rdb&site_no="+this->USGSMarkerID+startDateString2+endDateString2;
    else if(this->USGSdataMethod==1)
        RequestURL = "http://waterservices.usgs.gov/nwis/iv/?sites="+this->USGSMarkerID+startDateString1+endDateString1+"&format=rdb";
    else
        RequestURL = "http://waterservices.usgs.gov/nwis/dv/?sites="+this->USGSMarkerID+startDateString1+endDateString1+"&format=rdb";

    //...Make the request to the server
    QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(RequestURL)));
    connect(reply,SIGNAL(finished()),&loop,SLOT(quit()));
    connect(reply,SIGNAL(error(QNetworkReply::NetworkError)),&loop,SLOT(quit()));
    loop.exec();

    //...Read the response
    ierr = this->readUSGSDataFinished(reply);
    if(ierr!=0)
        return ERR_USGS_READDATA;

    return 0;
}
