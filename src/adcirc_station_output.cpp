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
#include "adcirc_station_output.h"
#include <netcdf.h>
#include <QtMath>

adcirc_station_output::adcirc_station_output(QObject *parent) : QObject(parent)
{

}

int adcirc_station_output::read(QString AdcircFile, QString AdcircStationFile, QDateTime coldStart)
{
    this->coldStartTime = coldStart;
    int ierr = this->readAscii(AdcircFile,AdcircStationFile);
    return ierr;
}

int adcirc_station_output::read(QString AdcircFile, QDateTime coldStart)
{
    this->coldStartTime = coldStart;
    int ierr = this->readNetCDF(AdcircFile);
    return ierr;
}

int adcirc_station_output::readAscii(QString AdcircOutputFile, QString AdcircStationFile)
{
    QFile MyFile(AdcircOutputFile), StationFile(AdcircStationFile);
    QString header1, header2, TempLine;
    QStringList headerData, TempList;
    int nColumns;

    //Check if we can open the file
    if(!MyFile.open(QIODevice::ReadOnly|QIODevice::Text))
        return -1;

    if(!StationFile.open(QIODevice::ReadOnly|QIODevice::Text))
        return -1;

    //Read the 61/62 style file
    header1 = MyFile.readLine();
    header2 = MyFile.readLine().simplified();
    headerData = header2.split(" ");

    this->nSnaps = headerData.value(0).toInt();
    this->nStations = headerData.value(1).toInt();
    nColumns = headerData.value(4).toInt();

    this->time.resize(this->nSnaps);
    this->data.resize(this->nStations);

    for(int i=0;i<this->nStations;++i)
        this->data[i].resize(this->nSnaps);

    for(int i=0;i<this->nSnaps;++i)
    {
        TempLine = MyFile.readLine().simplified();
        TempList = TempLine.split(" ");
        this->time[i] = TempList.value(1).toDouble();
        for(int j=0;j<this->nStations;++j)
        {
            TempLine = MyFile.readLine().simplified();
            TempList = TempLine.split(" ");
            this->data[j][i] = TempList.value(1).toDouble();
            if(nColumns==2)
                this->data[j][i] = qPow(qPow(this->data[j][i],2) +
                                         qPow(TempList.value(2).toDouble(),2),2);
        }
    }
    MyFile.close();

    //Now read the station location file
    TempLine = StationFile.readLine().simplified();
    TempList = TempLine.split(" ");
    int TempStations = TempList.value(0).toInt();
    if(TempStations!=this->nStations)
        return -1;

    this->longitude.resize(this->nStations);
    this->latitude.resize(this->nStations);
    this->station_name.resize(this->nStations);

    for(int i=0;i<TempStations;++i)
    {
        TempLine = StationFile.readLine().simplified();
        TempList = TempLine.split(QRegExp(",| "));
        this->longitude[i] = TempList.value(0).toDouble();
        this->latitude[i] = TempList.value(1).toDouble();

        if(TempList.length()>2)
        {
            this->station_name[i] = "";
            for(int j=2;j<TempList.length();++j)
                this->station_name[i] = this->station_name[i]+" "+TempList.value(j);
        }
        else
            this->station_name[i] = "Station_"+QString::number(i);

    }
    StationFile.close();

    return 0;
}

int adcirc_station_output::readNetCDF(QString AdcircOutputFile)
{

    size_t station_size,time_size,startIndex;
    int i,j,time_size_int,station_size_int;
    int ierr, ncid, varid_zeta, varid_zeta2, varid_lat, varid_lon, varid_time;
    int dimid_time,dimid_station;
    bool isVector;
    double Temp;
    QVector<double> readData1;
    QVector<double> readData2;

    //Size the location array
    size_t start[2];

    QVector<QString> netcdf_types;
    netcdf_types.resize(6);
    netcdf_types[0] = "zeta";
    netcdf_types[1] = "u-vel";
    netcdf_types[2] = "v-vel";
    netcdf_types[3] = "pressure";
    netcdf_types[4] = "windx";
    netcdf_types[5] = "windy";

    //Open the file
    ierr = nc_open(AdcircOutputFile.toUtf8(),NC_NOWRITE,&ncid);
    if(ierr!=NC_NOERR)
        return -1;

    //Get the dimension ids
    ierr = nc_inq_dimid(ncid,"time",&dimid_time);
    if(ierr!=NC_NOERR)
        return -1;

    ierr = nc_inq_dimid(ncid,"station",&dimid_station);
    if(ierr!=NC_NOERR)
        return -1;

    //Find out the dimension size
    ierr = nc_inq_dimlen(ncid,dimid_time,&time_size);
    if(ierr!=NC_NOERR)
        return -1;

    ierr = nc_inq_dimlen(ncid,dimid_station,&station_size);
    if(ierr!=NC_NOERR)
        return -1;

    station_size_int = static_cast<unsigned int>(station_size);
    time_size_int = static_cast<unsigned int>(time_size);

    //Find the variable in the NetCDF file
    for(i=0;i<6;i++)
    {
        ierr = nc_inq_varid(ncid,netcdf_types[i].toUtf8(),&varid_zeta);

        //If we found the variable, we're done
        if(ierr==NC_NOERR)
        {
            if(i==1)
            {
                isVector = true;
                ierr = nc_inq_varid(ncid,netcdf_types[i+1].toUtf8(),&varid_zeta2);
                if(ierr!=NC_NOERR)
                    return -1;
            }
            else
                isVector = false;

            break;
        }

        //If we're at the end of the array
        //and haven't quit yet, that's a problem
        if(i==5)
            return -1;
    }

    //Size the output variables
    this->latitude.resize(station_size_int);
    this->longitude.resize(station_size_int);
    this->nStations = station_size_int;
    this->nSnaps = time_size_int;
    this->time.resize(time_size_int);
    this->data.resize(station_size_int);
    for(i=0;i<station_size_int;++i)
        this->data[i].resize(time_size_int);

    //Read the station locations and times
    ierr = nc_inq_varid(ncid,"time",&varid_time);
    if(ierr!=NC_NOERR)
        return -1;

    ierr = nc_inq_varid(ncid,"x",&varid_lon);
    if(ierr!=NC_NOERR)
        return -1;

    ierr = nc_inq_varid(ncid,"y",&varid_lat);
    if(ierr!=NC_NOERR)
        return -1;

    for(j=0;j<time_size_int;j++)
    {
        startIndex = static_cast<size_t>(j);
        ierr = nc_get_var1(ncid,varid_time,&startIndex,&Temp);
        if(ierr!=NC_NOERR)
            return -1;

        this->time[j] = Temp;
    }

    for(j=0;j<station_size_int;j++)
    {
        startIndex = static_cast<size_t>(j);
        ierr = nc_get_var1(ncid,varid_lon,&startIndex,&Temp);
        if(ierr!=NC_NOERR)
            return -1;

        this->longitude[j] = Temp;

        ierr = nc_get_var1(ncid,varid_lat,&startIndex,&Temp);
        if(ierr!=NC_NOERR)
            return -1;

        this->latitude[j] = Temp;
    }

    readData1.resize(time_size_int);
    readData2.resize(time_size_int);

    //Loop over the stations, reading the data into memory
    for(i=0;i<station_size_int;++i)
    {


        //Read from NetCDF
        for(j=0;j<time_size_int;j++)
        {
            start[0] = static_cast<size_t>(j);
            start[1] = static_cast<size_t>(i);
            ierr = nc_get_var1(ncid,varid_zeta,start,&Temp);
            if(ierr!=NC_NOERR)
                return -1;

            readData1[j] = Temp;
        }

        if(isVector)
        {
            for(j=0;j<time_size_int;j++)
            {
                start[0] = static_cast<size_t>(j);
                start[1] = static_cast<size_t>(i);
                ierr = nc_get_var1(ncid,varid_zeta2,start,&Temp);
                if(ierr!=NC_NOERR)
                    return -1;

                readData2[j] = Temp;
                this->data[i][j] = qSqrt(qPow(readData1[j],2.0)+qPow(readData2[j],2.0));
            }
        }
        else
        {
            //Place in the output variable
            for(j=0;j<time_size_int;++j)
                this->data[i][j] = readData1[j];
        }
    }
    ierr = nc_close(ncid);
    if(ierr!=NC_NOERR)
        return -1;

    //Finally, name the stations the default names for now. Later
    //we can get fancy and try to get the ADCIRC written names in
    //the NetCDF file
    this->station_name.resize(station_size_int);
    for(i=0;i<station_size_int;++i)
        this->station_name[i] = "Station "+QString::number(i);

    return 0;
}

imeds *adcirc_station_output::toIMEDS()
{
    imeds *outputIMEDS = new imeds(this);

    outputIMEDS->nstations = this->nStations;
    outputIMEDS->station.resize(outputIMEDS->nstations);

    for(int i=0;i<outputIMEDS->nstations;++i)
    {
        outputIMEDS->station[i] = new imeds_station(this);
        outputIMEDS->station[i]->data.resize(this->nSnaps);
        outputIMEDS->station[i]->date.resize(this->nSnaps);
        outputIMEDS->station[i]->StationName = this->station_name[i];
        outputIMEDS->station[i]->NumSnaps = this->nSnaps;
        outputIMEDS->station[i]->longitude = this->longitude[i];
        outputIMEDS->station[i]->latitude = this->latitude[i];
        outputIMEDS->station[i]->StationIndex = i;
        for(int j=0;j<this->nSnaps;++j)
        {
            outputIMEDS->station[i]->date[j] = this->coldStartTime.addSecs(this->time[j]);
            outputIMEDS->station[i]->data[j] = this->data[i][j];
        }
    }
    return outputIMEDS;
}
