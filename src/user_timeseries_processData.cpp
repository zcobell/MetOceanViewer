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
#include <user_timeseries.h>
#include <general_functions.h>
#include <mov_flags.h>

int user_timeseries::processData()
{
    int ierr,i,j,nRow;
    double x,y;
    QStringList TempList;
    QString javascript,StationName,TempFile,TempStationFile,InputFileType;
    ADCNC TempAdcircNC;
    ADCASCII TempAdcircAscii;
    QDateTime ColdStart;

    nRow = table->rowCount();
    map->reload();

    j = 0;

    for(i=0;i<nRow;i++)
    {
        TempFile = table->item(i,6)->text();
        TempList = TempFile.split(".");
        InputFileType = TempList.value(TempList.length()-1).toUpper();
        this->fileData.resize(j+1);
        if(InputFileType=="IMEDS")
        {
            ierr = this->readIMEDS(TempFile,this->fileData[j]);
            if(ierr!=0)
            {
                this->errorString = "Error reading file: "+TempFile;
                return -1;
            }
        }
        else if(InputFileType=="NC")
        {
            ColdStart = QDateTime::fromString(table->item(i,7)->text(),"yyyy-MM-dd hh:mm:ss");
            ierr = this->readADCIRCnetCDF(TempFile,TempAdcircNC);
            if(ierr!=0)
            {
                this->errorString = "Error reading file: "+TempFile;
                return -1;
            }
            ierr = this->NetCDF_to_IMEDS(TempAdcircNC,ColdStart,this->fileData[j]);
            if(ierr!=0)
            {
                this->errorString = "Error reading file: "+TempFile;
                return -1;
            }
        }
        else if(InputFileType=="61"||InputFileType=="62"||InputFileType=="71"||InputFileType=="72")
        {
            ColdStart = QDateTime::fromString(table->item(i,7)->text(),"yyyy-MM-dd hh:mm:ss");
            TempStationFile = table->item(i,9)->text();
            ierr = this->readADCIRCascii(TempFile,TempStationFile,TempAdcircAscii);
            if(ierr!=0)
            {
                this->errorString = "Error reading file: "+TempFile;
                return -1;
            }
            ierr = this->ADCIRC_to_IMEDS(TempAdcircAscii,ColdStart,this->fileData[j]);
            if(ierr!=0)
            {
                this->errorString = "Error reading file: "+TempFile;
                return -1;
            }
        }
        else
        {
            this->errorString = "Invalid file format";
            return -1;
        }

        if(fileData[j].success)
            j = j + 1;
        else
            return -1;

    }

    //...Build a unique set of timeseries data
    ierr = this->getUniqueStationList(fileData,StationXLocs,StationYLocs);
    if(ierr!=0)
    {
        this->errorString = "Error building the station list";
        return -1;
    }
    ierr = this->buildRevisedIMEDS(fileData,StationXLocs,StationYLocs,fileDataUnique);
    if(ierr!=0)
    {
        this->errorString = "Error building the unique dataset";
        return -1;
    }

    //...Check that the page is finished loading
    QEventLoop loop;
    connect(map->page(),SIGNAL(loadFinished(bool)),&loop,SLOT(quit()));
    loop.exec();

    //...Add the markers to the map   
    map->page()->runJavaScript("allocateData("+QString::number(fileDataUnique.length())+")");
    for(i=0;i<fileDataUnique[0].nstations;i++)
    {
        x=-1.0;
        y=-1.0;
        StationName = "NONAME";

        //Check that we aren't sending a null location to
        //the backend
        for(j=0;j<fileDataUnique.length();j++)
        {
            if(!fileDataUnique[j].station[i].isNull)
            {
                StationName = fileDataUnique[j].station[i].StationName;
                x = fileDataUnique[j].station[i].longitude;
                y = fileDataUnique[j].station[i].latitude;
                break;
            }
        }

        javascript = "SetMarkerLocations("+QString::number(i)+
                ","+QString::number(x)+","+
                QString::number(y)+",'"+
                StationName+"')";
        map->page()->runJavaScript(javascript);
    }
    map->page()->runJavaScript("AddToMap()");

    return 0;
}
