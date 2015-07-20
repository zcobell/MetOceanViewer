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

#include <timeseries.h>
#include <netcdf.h>
#include <qmath.h>

//-------------------------------------------//
//Read an IMEDS file
//-------------------------------------------//
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
                Output.station[j].date[k].setTimeSpec(Qt::UTC);
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
                Output.station[j].date[k].setTimeSpec(Qt::UTC);
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
//-------------------------------------------//


//-------------------------------------------//
//Read an ADCIRC netCDF file
//-------------------------------------------//
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
//-------------------------------------------//


//-------------------------------------------//
//Convert the netCDF ADCIRC variable to an
//IMEDS style variable
//-------------------------------------------//
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
//-------------------------------------------//


//-------------------------------------------//
//Read an ADCIRC ASCII file
//-------------------------------------------//
ADCASCII readADCIRCascii(QString filename, QString stationfile)
{
    ADCASCII MyData;
    QFile MyFile(filename), StationFile(stationfile);
    QString header1, header2, TempLine;
    QStringList headerData, TempList, TempList2;
    int type;

    MyData.success = false;

    //Check if we can open the file
    if(!MyFile.open(QIODevice::ReadOnly|QIODevice::Text))
    {
        QMessageBox::information(NULL,"ERROR","ERROR:"+MyFile.errorString());
        MyFile.close();
        return MyData;
    }
    if(!StationFile.open(QIODevice::ReadOnly|QIODevice::Text))
    {
        QMessageBox::information(NULL,"ERROR","ERROR:"+StationFile.errorString());
        StationFile.close();
        return MyData;
    }


    //Read the 61/62 style file
    header1 = MyFile.readLine();
    header2 = MyFile.readLine().simplified();
    headerData = header2.split(" ");

    MyData.NumSnaps = headerData.value(0).toInt();
    MyData.nstations = headerData.value(1).toInt();
    MyData.OutputTimeFreq = headerData.value(2).toDouble();
    MyData.OutputTSFreq = headerData.value(3).toInt();
    MyData.NumColumns = headerData.value(4).toInt();

    MyData.time.resize(MyData.NumSnaps);
    MyData.data.resize(MyData.nstations);

    for(int i=0;i<MyData.nstations;++i)
        MyData.data[i].resize(MyData.NumSnaps);

    for(int i=0;i<MyData.NumSnaps;++i)
    {
        TempLine = MyFile.readLine().simplified();
        TempList = TempLine.split(" ");
        MyData.time[i] = TempList.value(1).toDouble();
        for(int j=0;j<MyData.nstations;++j)
        {
            TempLine = MyFile.readLine().simplified();
            TempList = TempLine.split(" ");
            MyData.data[j][i] = TempList.value(1).toDouble();
            if(MyData.NumColumns==2)
                MyData.data[j][i] = qPow(qPow(MyData.data[j][i],2) +
                                         qPow(TempList.value(2).toDouble(),2),2);
        }
    }
    MyFile.close();

    //Now read the station location file
    TempLine = StationFile.readLine().simplified();
    TempList = TempLine.split(" ");
    int TempStations = TempList.value(0).toInt();
    if(TempStations!=MyData.nstations)
    {
        QMessageBox::information(NULL,"ERROR","The number of stations do not match in both files");
        return MyData;
    }

    MyData.longitude.resize(MyData.nstations);
    MyData.latitude.resize(MyData.nstations);
    MyData.station_name.resize(MyData.nstations);
    type = -1;
    for(int i=0;i<TempStations;++i)
    {
        TempLine = StationFile.readLine().simplified();
        if(type==1)
            TempList = TempLine.split(" ");
        else if(type==2)
            TempList = TempLine.split(",");
        else
        {
            TempList = TempLine.split(" ");
            TempList2 = TempLine.split(",");
            if(TempList.length()>0)
                type = 1;
            else if(TempList2.length()>0)
            {
                type = 2;
                TempList = TempList2;
            }
        }
        MyData.longitude[i] = TempList.value(0).toDouble();
        MyData.latitude[i] = TempList.value(1).toDouble();

        if(TempList.length()>2)
        {
            MyData.station_name[i] = "";
            for(int j=2;j<TempList.length();++j)
                MyData.station_name[i] = MyData.station_name[i]+" "+TempList.value(j);
        }
        else
            MyData.station_name[i] = "Station_"+QString::number(i);

    }
    StationFile.close();

    MyData.success = true;
    return MyData;
}
//-------------------------------------------//


//-------------------------------------------//
//Convert an ADCIRC ASCII file to an IMEDS
//style file
//-------------------------------------------//
IMEDS ADCIRC_to_IMEDS(ADCASCII ASCII, QDateTime Cold)
{
    IMEDS MyOutput;

    MyOutput.nstations = ASCII.nstations;
    MyOutput.station.resize(MyOutput.nstations);

    for(int i=0;i<MyOutput.nstations;++i)
    {
        MyOutput.station[i].data.resize(ASCII.NumSnaps);
        MyOutput.station[i].date.resize(ASCII.NumSnaps);
        MyOutput.station[i].StationName = ASCII.station_name[i];
        MyOutput.station[i].NumSnaps = ASCII.NumSnaps;
        MyOutput.station[i].longitude = ASCII.longitude[i];
        MyOutput.station[i].latitude = ASCII.latitude[i];
        MyOutput.station[i].StationIndex = i;
        for(int j=0;j<ASCII.NumSnaps;++j)
        {
            MyOutput.station[i].date[j] = Cold.addSecs(ASCII.time[j]);
            MyOutput.station[i].data[j] = ASCII.data[i][j];
        }
    }
    MyOutput.success = true;
    return MyOutput;
}
//-------------------------------------------//


//-------------------------------------------//
//Generate a unique list of stations so that
//we can later build a complete list of stations
//and not show data where it doesn't exist for
//certain files
//-------------------------------------------//
int GetUniqueStationList(QVector<IMEDS> Data, QVector<double> &X, QVector<double> &Y)
{
    int i,j,k;
    bool found;
    for(i=0;i<Data.length();i++)
    {
        for(j=0;j<Data[i].nstations;j++)
        {
            found = false;
            for(k=0;k<X.length();k++)
            {
                if(Data[i].station[j].longitude == X[k] &&
                        Data[i].station[j].latitude == Y[k] )
                {
                    found = true;
                    break;
                }
            }
            if(!found)
            {
                X.resize(X.length()+1);
                Y.resize(Y.length()+1);
                X[X.length()-1] = Data[i].station[j].longitude;
                Y[Y.length()-1] = Data[i].station[j].latitude;
            }

        }
    }
    return 0;
}
//-------------------------------------------//


//-------------------------------------------//
//Build a revised set of IMEDS data series
//which will have null data where there was
//not data in the file
//-------------------------------------------//
int BuildRevisedIMEDS(QVector<IMEDS> Data,QVector<double> X, QVector<double> Y, QVector<IMEDS> &DataOut)
{
    int i,j,k;
    bool found;

    DataOut.resize(Data.length());
    for(i=0;i<Data.length();i++)
    {
        DataOut[i].nstations = X.length();
        DataOut[i].header1 = Data[i].header1;
        DataOut[i].header2 = Data[i].header2;
        DataOut[i].header3 = Data[i].header3;
        DataOut[i].station.resize(X.length());
        for(j=0;j<X.length();j++)
        {
            DataOut[i].station[j].longitude = X[j];
            DataOut[i].station[j].latitude = Y[j];
        }
    }

    for(i=0;i<Data.length();i++)
    {
        for(j=0;j<DataOut[i].nstations;j++)
        {
            found = false;
            for(k=0;k<Data[i].nstations;k++)
            {
                if(Data[i].station[k].longitude == DataOut[i].station[j].longitude &&
                   Data[i].station[k].latitude == DataOut[i].station[j].latitude)
                {
                    DataOut[i].station[j].data.resize(Data[i].station[k].data.length());
                    DataOut[i].station[j].date.resize(Data[i].station[k].date.length());
                    DataOut[i].station[j].NumSnaps = Data[i].station[k].NumSnaps;
                    DataOut[i].station[j].StationName = Data[i].station[k].StationName;
                    DataOut[i].station[j].data = Data[i].station[k].data;
                    DataOut[i].station[j].date = Data[i].station[k].date;
                    DataOut[i].station[j].isNull = false;
                    found = true;
                    break;
                }
            }
            if(!found)
            {
                //Build a station with a null dataset we can find later
                DataOut[i].station[j].data.resize(1);
                DataOut[i].station[j].date.resize(1);
                DataOut[i].station[j].StationName = "NONAME";
                DataOut[i].station[j].data[0] = FLAG_NULL_TS;
                DataOut[i].station[j].date[0] = FLAG_NULL_DATE;
                DataOut[i].station[j].isNull = true;
            }
        }
    }
    return 0;
}
//-------------------------------------------//
