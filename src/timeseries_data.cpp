//---- LGPL --------------------------------------------------------------------
//
// Copyright (C)  ARCADIS, 2011-2013.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation version 3.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// contact: Zachary Cobell, zachary.cobell@arcadis-us.com
// ARCADIS
// 4999 Pearl East Circle, Suite 200
// Boulder, CO 80301
//
// All indications and logos of, and references to, "ARCADIS" 
// are registered trademarks of ARCADIS, and remain the property of
// ARCADIS. All rights reserved.
//
//------------------------------------------------------------------------------
// $Author$
// $Date$
// $Id$
// $HeadURL$
//------------------------------------------------------------------------------
//  File: timeseries_data.cpp
//
//------------------------------------------------------------------------------

#include <timeseries.h>
#include <netcdf.h>
#include <qmath.h>

IMEDS readIMEDS(QString filename)
{

    //Variables
    IMEDS Output;
    QString year;
    QString month;
    QString day;
    QString hour;
    QString minute;
    QString second;
    QString DateString;
    QStringList TempList;
    QVector<QString> FileData;
    int nLine;
    int nStation;
    int i;
    int j;
    int k;
    double value;
    QFile MyFile(filename);

    //Default to an unsuccessful read
    Output.success = false;

    //Check if we can open the file
    if(!MyFile.open(QIODevice::ReadOnly|QIODevice::Text))
    {
        QMessageBox::information(NULL,"ERROR","ERROR:"+MyFile.errorString());
        return Output;
    }

    //Read the header to output variable
    try
    {
        Output.header1 = MyFile.readLine().simplified();
        Output.header2 = MyFile.readLine().simplified();
        Output.header3 = MyFile.readLine().simplified();

        //Read in the data portion of the file
        nLine = 0;
        while(!MyFile.atEnd())
        {
           nLine = nLine + 1;
           FileData.resize(nLine);
           FileData[nLine-1] = MyFile.readLine().simplified();
        }
        MyFile.close();

        //Count the number of stations in the file
        nStation = 0;
        for(i=0;i<nLine;i++)
        {
            TempList = FileData[i].split(" ");
            if(TempList.length()==3)
            {
                nStation = nStation + 1;
            }
        }

        //Size the station vector and read the datasets
        Output.station.resize(nStation);
        Output.nstations = nStation;
        j=-1;
        for(i=0;i<nLine;i++)
        {
            TempList = FileData[i].split(" ");
            if(TempList.length()==3)
            {
                j=j+1;
                Output.station[j].longitude = TempList[2].toDouble();
                Output.station[j].latitude = TempList[1].toDouble();
                Output.station[j].StationName = TempList[0];
                Output.station[j].StationIndex = j;
            }
            else
            {
                Output.station[j].NumSnaps = Output.station[j].NumSnaps + 1;
            }

        }
        //Preallocate arrays for data and dates

        for(i=0;i<nStation;i++)
        {
            Output.station[i].data.resize(Output.station[i].NumSnaps);
            Output.station[i].date.resize(Output.station[i].NumSnaps);
        }

        //Now, loop over the data section and save to vectors
        j=-1;k=-1;
        for(i=0;i<nLine;i++)
        {
            TempList = FileData[i].split(" ");
            if(TempList.length()==3)
            {
                j=j+1;
                k=-1;
            }
            else if(TempList.length()==6)
            {
                k=k+1;
                year = TempList.value(0);
                month = TempList.value(1);
                day = TempList.value(2);
                hour = TempList.value(3);
                minute = TempList.value(4);
                second = "0";
                value = TempList.value(5).toDouble();
                Output.station[j].date[k] =
                        QDateTime(QDate(year.toInt(),month.toInt(),day.toInt()),
                                  QTime(hour.toInt(),minute.toInt(),second.toInt()));
                Output.station[j].data[k] = value;
                Output.success = true;
            }
            else if(TempList.length()==7)
            {
                k=k+1;
                year = TempList.value(0);
                month = TempList.value(1);
                day = TempList.value(2);
                hour = TempList.value(3);
                minute = TempList.value(4);
                second = TempList.value(5);
                value = TempList.value(6).toDouble();
                Output.station[j].date[k] =
                        QDateTime(QDate(year.toInt(),month.toInt(),day.toInt()),
                                  QTime(hour.toInt(),minute.toInt(),second.toInt()));
                Output.station[j].data[k] = value;
                Output.success = true;
            }
            else
            {
                QMessageBox::information(NULL,"ERROR","Invalid dataset");
                Output.success = false;
                return Output;
            }

        }
        return Output;
    }
    catch(...)
    {
        Output.success = false;
        return Output;
    }

}

ADCNC readADCIRCnetCDF(QString filename)
{
    ADCNC MyData;
    size_t station_size,time_size;
    int i,j,time_size_int,station_size_int;
    int ierr, ncid, varid_zeta, varid_zeta2, varid_lat, varid_lon, varid_time;
    int dimid_time,dimid_station;
    bool isVector;

    QVector<QString> netcdf_types;
    netcdf_types.resize(6);
    netcdf_types[0] = "zeta";
    netcdf_types[1] = "u-vel";
    netcdf_types[2] = "v-vel";
    netcdf_types[3] = "pressure";
    netcdf_types[4] = "windx";
    netcdf_types[5] = "windy";

    //Open the file
    ierr = nc_open(filename.toStdString().c_str(),NC_NOWRITE,&ncid);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return MyData;
    }

    //Get the dimension ids
    ierr = nc_inq_dimid(ncid,"time",&dimid_time);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return MyData;
    }
    ierr = nc_inq_dimid(ncid,"station",&dimid_station);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return MyData;
    }

    //Find out the dimension size
    ierr = nc_inq_dimlen(ncid,dimid_time,&time_size);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return MyData;
    }
    ierr = nc_inq_dimlen(ncid,dimid_station,&station_size);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return MyData;
    }
    station_size_int = static_cast<unsigned int>(station_size);
    time_size_int = static_cast<unsigned int>(time_size);

    //Make some arrays
    double TempArray[time_size];
    double TempArray2[station_size];
    double TempArray3[station_size];

    //Size the location arrays
    size_t start[2];
    size_t length[2];

    //Set up the read sizes for NetCDF
    length[0] = time_size;
    length[1] = 1;

    //Find the variable in the NetCDF file
    for(i=0;i<6;i++)
    {
        ierr = nc_inq_varid(ncid,netcdf_types[i].toStdString().c_str(),&varid_zeta);

        //If we found the variable, we're done
        if(ierr==NC_NOERR)
        {
            if(i==1)
            {
                isVector = true;
                ierr = nc_inq_varid(ncid,netcdf_types[i+1].toStdString().c_str(),&varid_zeta2);
                if(ierr!=NC_NOERR)
                {
                    MyData.err = ierr;
                    MyData.success = false;
                    return MyData;
                }
            }
            else
                isVector = false;

            MyData.DataType=netcdf_types[i];
            break;
        }

        //If we're at the end of the array
        //and haven't quit yet, that's a problem
        if(i==5)
        {
            MyData.err = ierr;
            MyData.success = false;
            return MyData;
        }
    }

    //Size the output variables
    MyData.latitude.resize(station_size_int);
    MyData.longitude.resize(station_size_int);
    MyData.nstations = station_size_int;
    MyData.NumSnaps = time_size_int;
    MyData.time.resize(time_size_int);
    MyData.data.resize(station_size_int);
    for(i=0;i<station_size_int;++i)
        MyData.data[i].resize(time_size_int);

    //Read the station locations and times
    ierr = nc_inq_varid(ncid,"time",&varid_time);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return MyData;
    }
    ierr = nc_inq_varid(ncid,"x",&varid_lon);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return MyData;
    }
    ierr = nc_inq_varid(ncid,"y",&varid_lat);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return MyData;
    }

    ierr = nc_get_var(ncid,varid_time,&TempArray);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return MyData;
    }
    for(j=0;j<time_size_int;++j)
        MyData.time[j] = TempArray[j];

    ierr = nc_get_var(ncid,varid_lon,&TempArray2);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return MyData;
    }
    for(j=0;j<station_size_int;++j)
        MyData.longitude[j] = TempArray2[j];


    ierr = nc_get_var(ncid,varid_lat,&TempArray2);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return MyData;
    }
    for(j=0;j<station_size_int;++j)
        MyData.latitude[j] = TempArray2[j];

    //Loop over the stations, reading the data into memory
    for(i=0;i<station_size_int;++i)
    {
        //Generate the read start position
        start[0] = 0;
        start[1] = static_cast<size_t>(i);

        //Read from NetCDF
        ierr = nc_get_vara(ncid,varid_zeta,start,length,&TempArray);
        if(ierr!=NC_NOERR)
        {
            MyData.success = false;
            MyData.err = ierr;
            return MyData;
        }
        if(isVector)
        {
            ierr = nc_get_vara(ncid,varid_zeta2,start,length,&TempArray3);
            if(ierr!=NC_NOERR)
            {
                MyData.success = false;
                MyData.err = ierr;
                return MyData;
            }
            for(j=0;j<time_size_int;++j)
            {
                MyData.data[i][j] = qSqrt(qPow(TempArray2[j],2.0) + qPow(TempArray3[j],2.0));
            }
        }
        else
        {
            //Place in the output variable
            for(j=0;j<time_size_int;++j)
                MyData.data[i][j] = TempArray[j];
        }
    }
    ierr = nc_close(ncid);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return MyData;
    }

    //Finally, name the stations the default names for now. Later
    //we can get fancy and try to get the ADCIRC written names in
    //the NetCDF file
    MyData.station_name.resize(station_size_int);
    for(i=0;i<station_size_int;++i)
        MyData.station_name[i] = "Station "+QString::number(i);

    MyData.success = true;

    return MyData;
}


IMEDS NetCDF_to_IMEDS(ADCNC netcdf, QDateTime Cold)
{
    IMEDS Output;

    Output.nstations = netcdf.nstations;
    Output.station.resize(netcdf.nstations);
    for(int i=0;i<Output.nstations;++i)
    {
        Output.station[i].latitude = netcdf.latitude[i];
        Output.station[i].longitude = netcdf.longitude[i];
        Output.station[i].NumSnaps = netcdf.NumSnaps;
        Output.station[i].StationIndex = i;
        Output.station[i].StationName = netcdf.station_name[i];
        Output.station[i].data.resize(Output.station[i].NumSnaps);
        Output.station[i].date.resize(Output.station[i].NumSnaps);
        for(int j=0;j<Output.station[i].NumSnaps;++j)
        {
            Output.station[i].data[j] = netcdf.data[i][j];
            Output.station[i].date[j] = Cold.addSecs(netcdf.time[j]);
        }
    }
    Output.success = true;
    return Output;

}
